namespace GakuMotion.VmdImporter.Rig;

using System.Numerics;
using GakuMotion.VmdImporter.Core;

/// <summary>
/// In-memory bone representation for the virtual MMD rig.
/// </summary>
public sealed class VirtualBone
{
    public string Name { get; }
    public VirtualBone? Parent { get; set; }
    public List<VirtualBone> Children { get; } = new();
    public Vector3 RestOffset { get; set; }
    public int Order { get; set; }               // PMX deformation order (0 or 1)
    public VirtualBone? AppendSource { get; set; }
    public float AppendRate { get; set; }

    // Evaluated per-frame state:
    public Vector3 LocalPosition { get; set; }
    public Quaternion LocalRotation { get; set; } = Quaternion.Identity;
    public Vector3 WorldPosition { get; set; }
    public Quaternion WorldRotation { get; set; } = Quaternion.Identity;

    public VirtualBone(
        string name,
        VirtualBone? parent = null,
        Vector3 restOffset = default,
        int order = 0,
        VirtualBone? appendSource = null,
        float appendRate = 0.0f)
    {
        Name = name;
        Parent = parent;
        RestOffset = restOffset;
        Order = order;
        AppendSource = appendSource;
        AppendRate = appendRate;

        parent?.Children.Add(this);
    }

    public override string ToString() => $"VirtualBone({Name}, parent={(Parent != null ? Parent.Name : "null")}, order={Order})";
}

/// <summary>
/// Rotation limit constraint descriptor for native pose limits.
/// </summary>
public readonly record struct PoseRotationLimit(
    string Type,
    bool Mute,
    float Influence,
    string OwnerSpace,
    bool UseLimitX,
    bool UseLimitY,
    bool UseLimitZ,
    float MinX,
    float MaxX,
    float MinY,
    float MaxY,
    float MinZ,
    float MaxZ);

/// <summary>
/// Native affine record for source FK evaluation.
/// </summary>
public sealed class NativeBoneRecord
{
    public required string Name { get; init; }
    public string? Parent { get; init; }
    public required float Length { get; init; }
    public required Matrix3x3 RelativeMatrix { get; init; }
    public required Matrix3x3 SourceToNativeAxis { get; init; }
    public required Vector3 Head { get; init; }
    public required Matrix4x4 MatrixLocal { get; init; }
    public required Matrix4x4 MatrixLocalInverse { get; init; }
    public IReadOnlyList<PoseRotationLimit> Constraints { get; init; } = Array.Empty<PoseRotationLimit>();
}

/// <summary>
/// IK solver limits for left or right leg from mmd_source_skeleton.json.
/// </summary>
public sealed class LegIkLimits
{
    public required string Controller { get; init; }
    public required string Thigh { get; init; }
    public required string Knee { get; init; }
    public required string Ankle { get; init; }
    public required string Toe { get; init; }
    public required string ToeController { get; init; }

    public float KneeMinDegrees { get; init; } = 0.5f;
    public float KneeMaxDegrees { get; init; } = 180.0f;
    public int LoopCount { get; init; } = 40;
    public float StepRadians { get; init; } = 2.0f;
    public int ToeLoopCount { get; init; } = 3;
    public float ToeStepRadians { get; init; } = 4.0f;
    public Vector3 KneeAxis { get; init; } = Vector3.UnitX;
    public float SolverKneeMinDegrees { get; init; }
    public float SolverKneeMaxDegrees { get; init; } = 180.0f;
}

/// <summary>
/// Static bone configuration record loaded from skeleton definition.
/// </summary>
public sealed class BoneRecord
{
    public required string Name { get; init; }
    public string? Parent { get; init; }
    public Vector3 RestOffset { get; init; }
    public bool Movable { get; init; }
    public int Order { get; init; }
    public string? AppendSource { get; init; }
    public float AppendRate { get; init; }
    public Quaternion? RestRotationXyzw { get; init; }
    public NativeBoneRecord? Native { get; init; }
}
