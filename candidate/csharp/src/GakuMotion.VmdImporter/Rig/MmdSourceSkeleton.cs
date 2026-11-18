namespace GakuMotion.VmdImporter.Rig;

using System.IO;
using System.Numerics;
using System.Reflection;
using System.Text.Json;
using GakuMotion.VmdImporter.Core;

/// <summary>
/// Authoritative 82/92-bone MMD source skeleton topology, rest geometry, and native affine records.
/// Embedded from mmd_source_skeleton.json.
/// </summary>
public sealed class MmdSourceSkeleton
{
    private static readonly Lazy<MmdSourceSkeleton> DefaultInstance = new(LoadFromEmbedded);

    public static MmdSourceSkeleton Instance => DefaultInstance.Value;

    public int Version { get; }
    public string Preset { get; }
    public string CoordinateSpace { get; }
    public bool NativeEvaluation { get; }

    public IReadOnlyDictionary<string, BoneRecord> Bones { get; }
    public IReadOnlyList<string> TraversalOrder { get; }
    public IReadOnlyList<string> NativeOrder { get; }
    public IReadOnlyList<string> MovableBones { get; }

    public LegIkLimits LeftLegIk { get; }
    public LegIkLimits RightLegIk { get; }

    public Vector3 HipsRestPosition { get; }
    public IReadOnlyDictionary<string, Vector3> RestWorldPositions { get; }
    public IReadOnlyDictionary<string, Quaternion> RestRotations { get; }

    public IReadOnlyDictionary<string, Matrix3x3> AxisMaps { get; }
    public IReadOnlyDictionary<string, Matrix3x3> PositionMaps { get; }
    public IReadOnlyDictionary<string, Matrix3x3> SourceAxisMaps { get; }
    public IReadOnlyDictionary<string, (int X, int Y, int Z)> LocationCurveOrders { get; }
    public IReadOnlyDictionary<string, Matrix4x4> RestOffsets4x4 { get; }
    public IReadOnlyDictionary<string, Matrix4x4> RestInverseLocalMatrices { get; }

    private static readonly Matrix3x3 UnityToMmd = new(
        -1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, -1.0f);

    public MmdSourceSkeleton(
        int version,
        string preset,
        string coordinateSpace,
        bool nativeEvaluation,
        Dictionary<string, BoneRecord> bones,
        LegIkLimits leftLegIk,
        LegIkLimits rightLegIk)
    {
        Version = version;
        Preset = preset;
        CoordinateSpace = coordinateSpace;
        NativeEvaluation = nativeEvaluation;
        Bones = bones;
        LeftLegIk = leftLegIk;
        RightLegIk = rightLegIk;

        // 1. Compute topological traversal order
        var order = new List<string>();
        var visiting = new HashSet<string>();
        var visited = new HashSet<string>();

        void Visit(string name)
        {
            if (visiting.Contains(name))
                throw new InvalidOperationException($"Cycle detected in source skeleton for bone '{name}'.");
            if (visited.Contains(name))
                return;

            visiting.Add(name);
            if (bones.TryGetValue(name, out var rec) && rec.Parent != null && bones.ContainsKey(rec.Parent))
            {
                Visit(rec.Parent);
            }
            visiting.Remove(name);
            visited.Add(name);
            order.Add(name);
        }

        foreach (var name in bones.Keys)
        {
            Visit(name);
        }
        TraversalOrder = order;

        // 2. Compute Rest World Positions
        var restWorld = new Dictionary<string, Vector3>();
        foreach (var name in TraversalOrder)
        {
            var bone = bones[name];
            if (bone.Parent != null && restWorld.TryGetValue(bone.Parent, out var parentWorld))
            {
                restWorld[name] = parentWorld + bone.RestOffset;
            }
            else
            {
                restWorld[name] = bone.RestOffset;
            }
        }
        RestWorldPositions = restWorld;
        HipsRestPosition = restWorld.TryGetValue("下半身", out var hipsPos) ? hipsPos : Vector3.Zero;

        // 3. Movable bones
        var movable = new List<string>();
        foreach (var (name, rec) in bones)
        {
            if (rec.Movable)
                movable.Add(name);
        }
        MovableBones = movable;

        // 4. Native records and maps
        var nativeOrderList = new List<string>();
        var axisMaps = new Dictionary<string, Matrix3x3>();
        var posMaps = new Dictionary<string, Matrix3x3>();
        var srcAxisMaps = new Dictionary<string, Matrix3x3>();
        var locCurveOrders = new Dictionary<string, (int, int, int)>();
        var restRotations = new Dictionary<string, Quaternion>();
        var restOffsets = new Dictionary<string, Matrix4x4>();
        var restInvLocals = new Dictionary<string, Matrix4x4>();

        // Native topological order
        var nativeVisiting = new HashSet<string>();
        var nativeVisited = new HashSet<string>();

        void VisitNative(string name)
        {
            if (nativeVisiting.Contains(name))
                throw new InvalidOperationException($"Cycle in native hierarchy for '{name}'.");
            if (nativeVisited.Contains(name))
                return;

            nativeVisiting.Add(name);
            if (bones.TryGetValue(name, out var rec) && rec.Native != null && rec.Native.Parent != null)
            {
                VisitNative(rec.Native.Parent);
            }
            nativeVisiting.Remove(name);
            nativeVisited.Add(name);
            nativeOrderList.Add(name);
        }

        foreach (var (name, rec) in bones)
        {
            if (rec.RestRotationXyzw.HasValue)
                restRotations[name] = rec.RestRotationXyzw.Value;

            if (rec.Native != null)
            {
                VisitNative(name);
                var nat = rec.Native;

                // axis_maps[name] = axis @ _UNITY_TO_MMD
                Matrix3x3 axis = nat.SourceToNativeAxis;
                Matrix3x3 mapped = axis * UnityToMmd;
                axisMaps[name] = mapped;
                posMaps[name] = -mapped;
                srcAxisMaps[name] = axis;
                locCurveOrders[name] = LocationCurveOrder(-axis);

                // Offset 4x4 matrix
                // offset = eye(4); offset[:3, :3] = relative; offset[:3, 3] = head
                // if parent != null: offset[1, 3] += parent.length
                Matrix4x4 offset4 = new(
                    nat.RelativeMatrix.M00, nat.RelativeMatrix.M01, nat.RelativeMatrix.M02, 0.0f,
                    nat.RelativeMatrix.M10, nat.RelativeMatrix.M11, nat.RelativeMatrix.M12, 0.0f,
                    nat.RelativeMatrix.M20, nat.RelativeMatrix.M21, nat.RelativeMatrix.M22, 0.0f,
                    nat.Head.X, nat.Head.Y, nat.Head.Z, 1.0f);

                if (nat.Parent != null && bones.TryGetValue(nat.Parent, out var parentRec) && parentRec.Native != null)
                {
                    // In column-vector convention offset[1, 3] += parent.length.
                    // In System.Numerics.Matrix4x4, M42 is translation Y!
                    offset4.M42 += parentRec.Native.Length;
                }

                restOffsets[name] = offset4;
                restInvLocals[name] = nat.MatrixLocalInverse;
            }
        }

        NativeOrder = nativeOrderList;
        AxisMaps = axisMaps;
        PositionMaps = posMaps;
        SourceAxisMaps = srcAxisMaps;
        LocationCurveOrders = locCurveOrders;
        RestRotations = restRotations;
        RestOffsets4x4 = restOffsets;
        RestInverseLocalMatrices = restInvLocals;
    }

    private static (int X, int Y, int Z) LocationCurveOrder(in Matrix3x3 matrix)
    {
        var choices = new List<(float AbsVal, int Row, int Col)>(9);
        for (int r = 0; r < 3; r++)
        {
            for (int c = 0; c < 3; c++)
            {
                float val = r switch
                {
                    0 => c == 0 ? matrix.M00 : (c == 1 ? matrix.M01 : matrix.M02),
                    1 => c == 0 ? matrix.M10 : (c == 1 ? matrix.M11 : matrix.M12),
                    _ => c == 0 ? matrix.M20 : (c == 1 ? matrix.M21 : matrix.M22),
                };
                choices.Add((MathF.Abs(val), r, c));
            }
        }

        // Sort by -AbsVal, Row, Col
        choices.Sort((a, b) =>
        {
            int cmp = b.AbsVal.CompareTo(a.AbsVal);
            if (cmp != 0) return cmp;
            cmp = a.Row.CompareTo(b.Row);
            if (cmp != 0) return cmp;
            return a.Col.CompareTo(b.Col);
        });

        var usedRows = new HashSet<int>();
        var usedCols = new HashSet<int>();
        int[] order = new int[3];

        foreach (var (_, row, col) in choices)
        {
            if (!usedRows.Contains(row) && !usedCols.Contains(col))
            {
                order[row] = col;
                usedRows.Add(row);
                usedCols.Add(col);
            }
        }

        return (order[0], order[1], order[2]);
    }

    public static MmdSourceSkeleton LoadFromEmbedded()
    {
        var assembly = Assembly.GetExecutingAssembly();
        const string resourceName = "GakuMotion.VmdImporter.Resources.mmd_source_skeleton.json";

        using var stream = assembly.GetManifestResourceStream(resourceName)
            ?? throw new InvalidOperationException($"Embedded resource '{resourceName}' not found.");

        return Load(stream);
    }

    public static MmdSourceSkeleton Load(Stream stream)
    {
        using var doc = JsonDocument.Parse(stream);
        var root = doc.RootElement;

        int version = root.GetProperty("version").GetInt32();
        string preset = root.GetProperty("preset").GetString() ?? string.Empty;
        string coordSpace = root.GetProperty("coordinate_space").GetString() ?? string.Empty;
        bool nativeEval = root.TryGetProperty("native_evaluation", out var ne) && ne.GetBoolean();

        var bones = new Dictionary<string, BoneRecord>();
        var bonesElem = root.GetProperty("bones");

        foreach (var prop in bonesElem.EnumerateObject())
        {
            string name = prop.Name;
            var boneObj = prop.Value;

            string? parent = boneObj.GetProperty("parent").GetString();
            var restOffsetArr = boneObj.GetProperty("rest_offset");
            Vector3 restOffset = new(
                restOffsetArr[0].GetSingle(),
                restOffsetArr[1].GetSingle(),
                restOffsetArr[2].GetSingle());

            bool movable = boneObj.TryGetProperty("movable", out var movElem) && movElem.GetBoolean();
            int order = boneObj.TryGetProperty("order", out var ordElem) ? ordElem.GetInt32() : 0;

            string? appendSrc = null;
            float appendRate = 0.0f;
            if (boneObj.TryGetProperty("append", out var appElem) && appElem.ValueKind == JsonValueKind.Object)
            {
                appendSrc = appElem.GetProperty("source").GetString();
                appendRate = appElem.GetProperty("rate").GetSingle();
            }

            Quaternion? restRot = null;
            if (boneObj.TryGetProperty("rest_rotation_xyzw", out var rrElem) && rrElem.ValueKind == JsonValueKind.Array)
            {
                restRot = new Quaternion(
                    rrElem[0].GetSingle(),
                    rrElem[1].GetSingle(),
                    rrElem[2].GetSingle(),
                    rrElem[3].GetSingle());
            }

            NativeBoneRecord? natRecord = null;
            if (boneObj.TryGetProperty("native", out var natElem) && natElem.ValueKind == JsonValueKind.Object)
            {
                float length = natElem.GetProperty("length").GetSingle();

                var relMatElem = natElem.GetProperty("relative_matrix");
                Matrix3x3 relativeMat = new(
                    relMatElem[0][0].GetSingle(), relMatElem[0][1].GetSingle(), relMatElem[0][2].GetSingle(),
                    relMatElem[1][0].GetSingle(), relMatElem[1][1].GetSingle(), relMatElem[1][2].GetSingle(),
                    relMatElem[2][0].GetSingle(), relMatElem[2][1].GetSingle(), relMatElem[2][2].GetSingle());

                var axisMatElem = natElem.GetProperty("source_to_native_axis");
                Matrix3x3 axisMat = new(
                    axisMatElem[0][0].GetSingle(), axisMatElem[0][1].GetSingle(), axisMatElem[0][2].GetSingle(),
                    axisMatElem[1][0].GetSingle(), axisMatElem[1][1].GetSingle(), axisMatElem[1][2].GetSingle(),
                    axisMatElem[2][0].GetSingle(), axisMatElem[2][1].GetSingle(), axisMatElem[2][2].GetSingle());

                var headElem = natElem.GetProperty("head");
                Vector3 head = new(
                    headElem[0].GetSingle(),
                    headElem[1].GetSingle(),
                    headElem[2].GetSingle());

                var matLocalElem = natElem.GetProperty("matrix_local");
                Matrix4x4 matLocal = new(
                    matLocalElem[0][0].GetSingle(), matLocalElem[0][1].GetSingle(), matLocalElem[0][2].GetSingle(), matLocalElem[0][3].GetSingle(),
                    matLocalElem[1][0].GetSingle(), matLocalElem[1][1].GetSingle(), matLocalElem[1][2].GetSingle(), matLocalElem[1][3].GetSingle(),
                    matLocalElem[2][0].GetSingle(), matLocalElem[2][1].GetSingle(), matLocalElem[2][2].GetSingle(), matLocalElem[2][3].GetSingle(),
                    matLocalElem[3][0].GetSingle(), matLocalElem[3][1].GetSingle(), matLocalElem[3][2].GetSingle(), matLocalElem[3][3].GetSingle());

                if (!Matrix4x4.Invert(matLocal, out var matLocalInv))
                {
                    matLocalInv = Matrix4x4.Identity;
                }

                var constraints = new List<PoseRotationLimit>();
                if (natElem.TryGetProperty("constraints", out var conElem) && conElem.ValueKind == JsonValueKind.Array)
                {
                    foreach (var cItem in conElem.EnumerateArray())
                    {
                        constraints.Add(new PoseRotationLimit(
                            Type: cItem.GetProperty("type").GetString() ?? string.Empty,
                            Mute: cItem.TryGetProperty("mute", out var mElem) && mElem.GetBoolean(),
                            Influence: cItem.TryGetProperty("influence", out var infElem) ? infElem.GetSingle() : 1.0f,
                            OwnerSpace: cItem.TryGetProperty("owner_space", out var osElem) ? osElem.GetString() ?? "POSE" : "POSE",
                            UseLimitX: cItem.TryGetProperty("use_limit_x", out var ulx) && ulx.GetBoolean(),
                            UseLimitY: cItem.TryGetProperty("use_limit_y", out var uly) && uly.GetBoolean(),
                            UseLimitZ: cItem.TryGetProperty("use_limit_z", out var ulz) && ulz.GetBoolean(),
                            MinX: cItem.TryGetProperty("min_x", out var mix) ? mix.GetSingle() : -MathF.PI,
                            MaxX: cItem.TryGetProperty("max_x", out var max) ? max.GetSingle() : MathF.PI,
                            MinY: cItem.TryGetProperty("min_y", out var miy) ? miy.GetSingle() : -MathF.PI,
                            MaxY: cItem.TryGetProperty("max_y", out var may) ? may.GetSingle() : MathF.PI,
                            MinZ: cItem.TryGetProperty("min_z", out var miz) ? miz.GetSingle() : -MathF.PI,
                            MaxZ: cItem.TryGetProperty("max_z", out var maz) ? maz.GetSingle() : MathF.PI));
                    }
                }

                natRecord = new NativeBoneRecord
                {
                    Name = name,
                    Parent = parent,
                    Length = length,
                    RelativeMatrix = relativeMat,
                    SourceToNativeAxis = axisMat,
                    Head = head,
                    MatrixLocal = matLocal,
                    MatrixLocalInverse = matLocalInv,
                    Constraints = constraints
                };
            }

            bones[name] = new BoneRecord
            {
                Name = name,
                Parent = parent,
                RestOffset = restOffset,
                Movable = movable,
                Order = order,
                AppendSource = appendSrc,
                AppendRate = appendRate,
                RestRotationXyzw = restRot,
                Native = natRecord
            };
        }

        // Leg IK
        var legIkElem = root.GetProperty("leg_ik");
        var leftElem = legIkElem.GetProperty("left");
        var rightElem = legIkElem.GetProperty("right");

        LegIkLimits ParseLegLimits(JsonElement elem)
        {
            var axisArr = elem.GetProperty("knee_axis");
            Vector3 kneeAxis = new(axisArr[0].GetSingle(), axisArr[1].GetSingle(), axisArr[2].GetSingle());

            return new LegIkLimits
            {
                Controller = elem.GetProperty("controller").GetString()!,
                Thigh = elem.GetProperty("thigh").GetString()!,
                Knee = elem.GetProperty("knee").GetString()!,
                Ankle = elem.GetProperty("ankle").GetString()!,
                Toe = elem.GetProperty("toe").GetString()!,
                ToeController = elem.GetProperty("toe_controller").GetString()!,
                KneeMinDegrees = elem.GetProperty("knee_min_degrees").GetSingle(),
                KneeMaxDegrees = elem.GetProperty("knee_max_degrees").GetSingle(),
                LoopCount = elem.GetProperty("loop_count").GetInt32(),
                StepRadians = elem.GetProperty("step_radians").GetSingle(),
                ToeLoopCount = elem.GetProperty("toe_loop_count").GetInt32(),
                ToeStepRadians = elem.GetProperty("toe_step_radians").GetSingle(),
                KneeAxis = kneeAxis,
                SolverKneeMinDegrees = elem.GetProperty("solver_knee_min_degrees").GetSingle(),
                SolverKneeMaxDegrees = elem.GetProperty("solver_knee_max_degrees").GetSingle()
            };
        }

        return new MmdSourceSkeleton(
            version,
            preset,
            coordSpace,
            nativeEval,
            bones,
            ParseLegLimits(leftElem),
            ParseLegLimits(rightElem));
    }
}
