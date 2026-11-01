namespace GakuMotion.VmdImporter.Rig;

using System.Numerics;
using GakuMotion.VmdImporter.Core;
using GakuMotion.VmdImporter.Output;
using GakuMotion.VmdImporter.Vmd;

/// <summary>
/// In-memory virtual MMD skeleton replicating bone hierarchy, FK traversal, append/grant transforms,
/// analytical leg IK with positive knee bend convention, CCD toe IK, and SVD native IK.
/// Strictly replicates virtual_rig.py and CONTRACTS S1.
/// </summary>
public sealed class VirtualMMDRig
{
    public static readonly Vector3 DefaultToeOffset = new(0.0f, -0.8675f, 1.7151f);
    public static readonly Vector3 DefaultToeExOffset = new(0.0f, -0.8675f, 0.9257f);

    public const float DefaultToeIkStepDeg = 229.1831f;
    public const int DefaultToeIkLoop = 3;

    public float AxisScale { get; }
    public Dictionary<string, VirtualBone> Bones { get; } = new();

    public float CenterY { get; private set; }
    public float ThighLen { get; private set; }
    public float ShinLen { get; private set; }
    public float ThighY { get; private set; }
    public float FootY { get; private set; }
    public float KneeMinDeg { get; private set; }
    public float KneeMaxDeg { get; private set; }

    public int SourceStructureVersion { get; private set; } = 1;
    public MmdSourceSkeleton? SourceSkeleton { get; private set; }
    public Vector3 SourceHipsRestPosition { get; private set; }
    public IReadOnlyList<string> MovableBones { get; private set; }

    public VirtualBone Root { get; private set; }
    public VirtualBone Center { get; private set; }
    public VirtualBone Groove { get; private set; }
    public VirtualBone Waist { get; private set; }
    public VirtualBone LowerBody { get; private set; }
    public VirtualBone UpperBody { get; private set; }
    public VirtualBone UpperBody2 { get; private set; }
    public VirtualBone Neck { get; private set; }
    public VirtualBone Head { get; private set; }
    public VirtualBone Eyes { get; private set; }
    public VirtualBone LeftEye { get; private set; }
    public VirtualBone RightEye { get; private set; }

    public VirtualBone LeftShoulderP { get; private set; }
    public VirtualBone LeftShoulder { get; private set; }
    public VirtualBone LeftShoulderC { get; private set; }
    public VirtualBone LeftArm { get; private set; }
    public VirtualBone LeftArmTwist { get; private set; }
    public VirtualBone LeftElbow { get; private set; }
    public VirtualBone LeftHandTwist { get; private set; }
    public VirtualBone LeftWrist { get; private set; }

    public VirtualBone RightShoulderP { get; private set; }
    public VirtualBone RightShoulder { get; private set; }
    public VirtualBone RightShoulderC { get; private set; }
    public VirtualBone RightArm { get; private set; }
    public VirtualBone RightArmTwist { get; private set; }
    public VirtualBone RightElbow { get; private set; }
    public VirtualBone RightHandTwist { get; private set; }
    public VirtualBone RightWrist { get; private set; }

    public VirtualBone LeftWaistCancel { get; private set; }
    public VirtualBone LeftThigh { get; private set; }
    public VirtualBone LeftKnee { get; private set; }
    public VirtualBone LeftAnkle { get; private set; }
    public VirtualBone LeftToe { get; private set; }

    public VirtualBone RightWaistCancel { get; private set; }
    public VirtualBone RightThigh { get; private set; }
    public VirtualBone RightKnee { get; private set; }
    public VirtualBone RightAnkle { get; private set; }
    public VirtualBone RightToe { get; private set; }

    public VirtualBone LeftLegIk { get; private set; }
    public VirtualBone LeftToeIk { get; private set; }
    public VirtualBone RightLegIk { get; private set; }
    public VirtualBone RightToeIk { get; private set; }

    public VirtualBone LeftThighD { get; private set; }
    public VirtualBone LeftKneeD { get; private set; }
    public VirtualBone LeftAnkleD { get; private set; }
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1711:Identifiers should not have incorrect suffix", Justification = "Matches MMD bone naming convention 足先EX")]
    public VirtualBone LeftToeEx { get; private set; }

    public VirtualBone RightThighD { get; private set; }
    public VirtualBone RightKneeD { get; private set; }
    public VirtualBone RightAnkleD { get; private set; }
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1711:Identifiers should not have incorrect suffix", Justification = "Matches MMD bone naming convention 足先EX")]
    public VirtualBone RightToeEx { get; private set; }

    public List<VirtualBone> AppendOrder0 { get; } = new();
    public List<(VirtualBone GroupRoot, List<VirtualBone> Chain)> AppendOrder1Groups { get; } = new();

    public Dictionary<string, (AffineMatrix4x4 Matrix, float Length)>? SourceNativePoses { get; private set; }
    public (Dictionary<string, Vector3[]> Positions, Dictionary<string, Quaternion[]> Rotations)? SourceNativeAnimation { get; private set; }

    public VirtualMMDRig(
        float axisScale = 0.083f,
        bool useSourceSkeleton = false,
        MmdSourceSkeleton? skeleton = null,
        float? centerY = null,
        float? thighLen = null,
        float? shinLen = null,
        float? thighY = null,
        float? footY = null,
        float? kneeMinDeg = null,
        float? kneeMaxDeg = null)
    {
        AxisScale = axisScale;

        // Default dimensions
        CenterY = centerY ?? 12.0f;
        ThighLen = thighLen ?? 5.3f;
        ShinLen = shinLen ?? 5.1f;
        ThighY = thighY ?? 11.8f;
        FootY = footY ?? 1.4f;
        KneeMinDeg = kneeMinDeg ?? 0.0f;
        KneeMaxDeg = kneeMaxDeg ?? 165.0f;

        float s = 1.0f;
        float thighX = 1.0f * s;

        // 1. Root & Torso Chain
        Root = new VirtualBone("全ての親", null, Vector3.Zero);
        Center = new VirtualBone("センター", Root, new Vector3(0.0f, CenterY, 0.0f));
        Groove = new VirtualBone("グルーブ", Center, Vector3.Zero);
        Waist = new VirtualBone("腰", Groove, Vector3.Zero);
        LowerBody = new VirtualBone("下半身", Waist, Vector3.Zero);
        UpperBody = new VirtualBone("上半身", Waist, Vector3.Zero);
        UpperBody2 = new VirtualBone("上半身2", UpperBody, new Vector3(0.0f, 2.0f * s, 0.0f));
        Neck = new VirtualBone("首", UpperBody2, new Vector3(0.0f, 3.0f * s, 0.0f));
        Head = new VirtualBone("頭", Neck, new Vector3(0.0f, 1.0f * s, 0.0f));

        Eyes = new VirtualBone("両目", Head, Vector3.Zero);
        LeftEye = new VirtualBone("左目", Head, Vector3.Zero, appendSource: Eyes, appendRate: 1.0f);
        RightEye = new VirtualBone("右目", Head, Vector3.Zero, appendSource: Eyes, appendRate: 1.0f);

        // 2. Arm Chains
        LeftShoulderP = new VirtualBone("左肩P", UpperBody2, new Vector3(-0.8f * s, 2.5f * s, 0.0f));
        LeftShoulder = new VirtualBone("左肩", LeftShoulderP, Vector3.Zero);
        LeftShoulderC = new VirtualBone("左肩C", LeftShoulder, new Vector3(-1.2f * s, -0.5f * s, 0.0f), appendSource: LeftShoulderP, appendRate: -1.0f);
        LeftArm = new VirtualBone("左腕", LeftShoulderC, Vector3.Zero);
        LeftArmTwist = new VirtualBone("左腕捩", LeftArm, new Vector3(-1.25f * s, 0.0f, 0.0f));
        LeftElbow = new VirtualBone("左ひじ", LeftArmTwist, new Vector3(-1.25f * s, 0.0f, 0.0f));
        LeftHandTwist = new VirtualBone("左手捩", LeftElbow, new Vector3(-1.25f * s, 0.0f, 0.0f));
        LeftWrist = new VirtualBone("左手首", LeftHandTwist, new Vector3(-1.25f * s, 0.0f, 0.0f));

        RightShoulderP = new VirtualBone("右肩P", UpperBody2, new Vector3(0.8f * s, 2.5f * s, 0.0f));
        RightShoulder = new VirtualBone("右肩", RightShoulderP, Vector3.Zero);
        RightShoulderC = new VirtualBone("右肩C", RightShoulder, new Vector3(1.2f * s, -0.5f * s, 0.0f), appendSource: RightShoulderP, appendRate: -1.0f);
        RightArm = new VirtualBone("右腕", RightShoulderC, Vector3.Zero);
        RightArmTwist = new VirtualBone("右腕捩", RightArm, new Vector3(1.25f * s, 0.0f, 0.0f));
        RightElbow = new VirtualBone("右ひじ", RightArmTwist, new Vector3(1.25f * s, 0.0f, 0.0f));
        RightHandTwist = new VirtualBone("右手捩", RightElbow, new Vector3(1.25f * s, 0.0f, 0.0f));
        RightWrist = new VirtualBone("右手首", RightHandTwist, new Vector3(1.25f * s, 0.0f, 0.0f));

        // 3. Finger Chains (thumb starts at 親指０)
        (string Side, VirtualBone Wrist)[] sides = [("左", LeftWrist), ("右", RightWrist)];
        (string FName, string[] Parts)[] fingerDefs =
        [
            ("親指", ["０", "１", "２", "先"]),
            ("人指", ["１", "２", "３"]),
            ("中指", ["１", "２", "３"]),
            ("薬指", ["１", "２", "３"]),
            ("小指", ["１", "２", "３"])
        ];

        foreach (var (sideName, wristBone) in sides)
        {
            foreach (var (fName, parts) in fingerDefs)
            {
                VirtualBone pBone = wristBone;
                foreach (string part in parts)
                {
                    string bName = $"{sideName}{fName}{part}";
                    pBone = new VirtualBone(bName, pBone, Vector3.Zero);
                }
            }
        }

        // 4. Leg Chains
        LeftWaistCancel = new VirtualBone("腰キャンセル左", LowerBody, new Vector3(-thighX, ThighY - CenterY, 0.0f), appendSource: Waist, appendRate: -1.0f);
        LeftThigh = new VirtualBone("左足", LeftWaistCancel, Vector3.Zero);
        LeftKnee = new VirtualBone("左ひざ", LeftThigh, new Vector3(0.0f, -ThighLen, 0.0f));
        LeftAnkle = new VirtualBone("左足首", LeftKnee, new Vector3(0.0f, -ShinLen, 0.0f));
        LeftToe = new VirtualBone("左つま先", LeftAnkle, DefaultToeOffset);

        RightWaistCancel = new VirtualBone("腰キャンセル右", LowerBody, new Vector3(thighX, ThighY - CenterY, 0.0f), appendSource: Waist, appendRate: -1.0f);
        RightThigh = new VirtualBone("右足", RightWaistCancel, Vector3.Zero);
        RightKnee = new VirtualBone("右ひざ", RightThigh, new Vector3(0.0f, -ThighLen, 0.0f));
        RightAnkle = new VirtualBone("右足首", RightKnee, new Vector3(0.0f, -ShinLen, 0.0f));
        RightToe = new VirtualBone("右つま先", RightAnkle, DefaultToeOffset);

        // 5. Leg IK Bones & Toe IK
        LeftLegIk = new VirtualBone("左足ＩＫ", Root, new Vector3(-thighX, FootY, 0.0f), order: 1);
        LeftToeIk = new VirtualBone("左つま先ＩＫ", LeftLegIk, DefaultToeOffset, order: 1);

        RightLegIk = new VirtualBone("右足ＩＫ", Root, new Vector3(thighX, FootY, 0.0f), order: 1);
        RightToeIk = new VirtualBone("右つま先ＩＫ", RightLegIk, DefaultToeOffset, order: 1);

        // 6. D-mirror Append Chains
        LeftThighD = new VirtualBone("左足D", LeftWaistCancel, Vector3.Zero, order: 1, appendSource: LeftThigh, appendRate: 1.0f);
        LeftKneeD = new VirtualBone("左ひざD", LeftThighD, new Vector3(0.0f, -ThighLen, 0.0f), order: 1, appendSource: LeftKnee, appendRate: 1.0f);
        LeftAnkleD = new VirtualBone("左足首D", LeftKneeD, new Vector3(0.0f, -ShinLen, 0.0f), order: 1, appendSource: LeftAnkle, appendRate: 1.0f);
        LeftToeEx = new VirtualBone("左足先EX", LeftAnkleD, DefaultToeExOffset, order: 1);

        RightThighD = new VirtualBone("右足D", RightWaistCancel, Vector3.Zero, order: 1, appendSource: RightThigh, appendRate: 1.0f);
        RightKneeD = new VirtualBone("右ひざD", RightThighD, new Vector3(0.0f, -ThighLen, 0.0f), order: 1, appendSource: RightKnee, appendRate: 1.0f);
        RightAnkleD = new VirtualBone("右足首D", RightKneeD, new Vector3(0.0f, -ShinLen, 0.0f), order: 1, appendSource: RightAnkle, appendRate: 1.0f);
        RightToeEx = new VirtualBone("右足先EX", RightAnkleD, DefaultToeExOffset, order: 1);

        // Index all 82 bones
        IndexBone(Root);

        // Append order 0
        AppendOrder0.Add(RightShoulderC);
        AppendOrder0.Add(LeftShoulderC);
        AppendOrder0.Add(RightWaistCancel);
        AppendOrder0.Add(LeftWaistCancel);
        AppendOrder0.Add(LeftEye);
        AppendOrder0.Add(RightEye);

        // Append order 1 groups
        AppendOrder1Groups.Add((RightThighD, [RightThighD, RightKneeD, RightAnkleD]));
        AppendOrder1Groups.Add((LeftThighD, [LeftThighD, LeftKneeD, LeftAnkleD]));

        // Default movable bones
        MovableBones = ["全ての親", "センター", "グルーブ", "左足ＩＫ", "右足ＩＫ", "左つま先ＩＫ", "右つま先ＩＫ"];
        SourceHipsRestPosition = Vector3.Zero;

        if (useSourceSkeleton || skeleton != null)
        {
            ConfigureSourceStructure(skeleton ?? MmdSourceSkeleton.Instance);
        }
    }

    private void IndexBone(VirtualBone bone)
    {
        Bones[bone.Name] = bone;
        foreach (var child in bone.Children)
        {
            IndexBone(child);
        }
    }

    private void ConfigureSourceStructure(MmdSourceSkeleton structure)
    {
        SourceSkeleton = structure;
        SourceStructureVersion = structure.NativeEvaluation ? 3 : 2;

        // Ensure all bones in structure exist in Bones
        foreach (var (name, rec) in structure.Bones)
        {
            if (!Bones.TryGetValue(name, out var bone))
            {
                bone = new VirtualBone(name);
                Bones[name] = bone;
            }

            bone.RestOffset = rec.RestOffset;
            bone.Order = rec.Order;
            bone.Children.Clear();

            if (rec.AppendSource != null && Bones.TryGetValue(rec.AppendSource, out var appSrc))
            {
                bone.AppendSource = appSrc;
                bone.AppendRate = rec.AppendRate;
            }
            else
            {
                bone.AppendSource = null;
                bone.AppendRate = 0.0f;
            }
        }

        // Wire parents and children
        foreach (var (name, rec) in structure.Bones)
        {
            var bone = Bones[name];
            if (rec.Parent != null && Bones.TryGetValue(rec.Parent, out var parentBone))
            {
                bone.Parent = parentBone;
                parentBone.Children.Add(bone);
            }
            else
            {
                bone.Parent = null;
            }
        }

        MovableBones = structure.MovableBones;
        SourceHipsRestPosition = structure.HipsRestPosition;

        ThighLen = LeftKnee.RestOffset.Length();
        ShinLen = LeftAnkle.RestOffset.Length();
        CenterY = structure.RestWorldPositions.TryGetValue(Center.Name, out var cPos) ? cPos.Y : CenterY;
        ThighY = structure.RestWorldPositions.TryGetValue(LeftThigh.Name, out var tPos) ? tPos.Y : ThighY;
        FootY = structure.RestWorldPositions.TryGetValue(LeftAnkle.Name, out var fPos) ? fPos.Y : FootY;
    }

    public void ForwardKinematics(VirtualBone? bone = null)
    {
        bone ??= Root;
        if (bone.Parent == null)
        {
            bone.WorldPosition = bone.RestOffset + bone.LocalPosition;
            bone.WorldRotation = bone.LocalRotation;
        }
        else
        {
            bone.WorldPosition = bone.Parent.WorldPosition + Vector3.Transform(bone.RestOffset + bone.LocalPosition, bone.Parent.WorldRotation);
            bone.WorldRotation = bone.Parent.WorldRotation * bone.LocalRotation;
        }

        foreach (var child in bone.Children)
        {
            ForwardKinematics(child);
        }
    }

    public static Quaternion EffectiveLocalRotation(VirtualBone bone)
    {
        if (bone.Parent == null)
            return bone.WorldRotation;
        // SciPy Rotation stores normalized double quaternions and uses their
        // conjugate inverse. A float norm/divide can inject a spurious local
        // rotation even when parent and child world rotations are identical.
        return (DoubleRotation.FromQuaternion(bone.Parent.WorldRotation).Inverse()
            * DoubleRotation.FromQuaternion(bone.WorldRotation)).ToQuaternion();
    }

    public void ApplyAppend(VirtualBone bone)
    {
        VirtualBone? src = bone.AppendSource;
        if (src == null || bone.AppendRate == 0.0f)
            return;

        Quaternion eff = EffectiveLocalRotation(src);
        Quaternion grant;

        // Waist cancellation grant semantics: Euler XYZ axis-by-axis negation
        if ((bone.Name == "腰キャンセル左" || bone.Name == "腰キャンセル右") && SourceSkeleton != null)
        {
            grant = EulerRotation.WaistEulerCancelGrant(eff, SourceSkeleton.AxisMaps[bone.Name]);
        }
        else
        {
            grant = EulerRotation.FromRotVec(EulerRotation.ToRotVec(eff) * bone.AppendRate);
        }

        bone.LocalRotation = grant * bone.LocalRotation;
    }

    private void PropagateChildren(VirtualBone bone)
    {
        foreach (var child in bone.Children)
        {
            child.WorldPosition = bone.WorldPosition + Vector3.Transform(child.RestOffset + child.LocalPosition, bone.WorldRotation);
            child.WorldRotation = bone.WorldRotation * child.LocalRotation;
            PropagateChildren(child);
        }
    }

    public void SolveLegIk(bool isLeft)
    {
        VirtualBone thigh = isLeft ? LeftThigh : RightThigh;
        VirtualBone knee = isLeft ? LeftKnee : RightKnee;
        VirtualBone ankle = isLeft ? LeftAnkle : RightAnkle;
        VirtualBone ikBone = isLeft ? LeftLegIk : RightLegIk;

        if (SourceStructureVersion >= 2 && SourceSkeleton != null)
        {
            var limits = isLeft ? SourceSkeleton.LeftLegIk : SourceSkeleton.RightLegIk;

            if (SourceStructureVersion >= 3 && SourceNativePoses != null && thigh.Parent != null)
            {
                var parentPose = SourceNativePoses[thigh.Parent.Name].Matrix;
                var thighPose = SourceNativePoses[thigh.Name].Matrix;
                var kneePose = SourceNativePoses[knee.Name].Matrix;
                Vector3 goalNative = SourceNativePoses[ikBone.Name].Matrix.Translation;

                var thighRest = SourceSkeleton.Bones[thigh.Name].Native!.RelativeMatrix;
                var kneeRest = SourceSkeleton.Bones[knee.Name].Native!.RelativeMatrix;
                float thighL = SourceSkeleton.Bones[thigh.Name].Native!.Length;
                float kneeL = SourceSkeleton.Bones[knee.Name].Native!.Length;

                var (upper, lower) = SourceNativeIK.SolveNativeLeg(
                    parentPose, thighPose, kneePose, goalNative,
                    thighRest, kneeRest, thighL, kneeL,
                    limits.LoopCount,
                    limits.SolverKneeMinDegrees,
                    limits.SolverKneeMaxDegrees);

                Matrix3x3 poseToUnity = new(-1f, 0f, 0f, 0f, 0f, 1f, 0f, -1f, 0f);
                Matrix3x3 poseToUnityT = poseToUnity.Transpose();

                (VirtualBone Bone, AffineMatrix4x4 Pose)[] pairs = [(thigh, upper), (knee, lower)];
                foreach (var (b, pose) in pairs)
                {
                    Matrix4x4 restInv = SourceSkeleton.RestInverseLocalMatrices[b.Name];
                    AffineMatrix4x4 restInvAff = default;
                    restInvAff.M00 = restInv.M11; restInvAff.M01 = restInv.M12; restInvAff.M02 = restInv.M13; restInvAff.M03 = restInv.M41;
                    restInvAff.M10 = restInv.M21; restInvAff.M11 = restInv.M22; restInvAff.M12 = restInv.M23; restInvAff.M13 = restInv.M42;
                    restInvAff.M20 = restInv.M31; restInvAff.M21 = restInv.M32; restInvAff.M22 = restInv.M33; restInvAff.M23 = restInv.M43;

                    AffineMatrix4x4 def = pose * restInvAff;
                    Matrix3x3 rotMat = poseToUnity * def.Basis3x3 * poseToUnityT;
                    b.WorldRotation = MatrixToQuat(rotMat);
                }

                knee.WorldPosition = thigh.WorldPosition + Vector3.Transform(knee.RestOffset + knee.LocalPosition, thigh.WorldRotation);
                ankle.WorldPosition = knee.WorldPosition + Vector3.Transform(ankle.RestOffset + ankle.LocalPosition, knee.WorldRotation);
                ankle.WorldRotation = knee.WorldRotation * ankle.LocalRotation;
                return;
            }
        }

        // Standard Analytical Leg IK
        SourceNativeIK.SolveAnalyticalLeg(
            thigh, knee, ankle, ikBone,
            ThighLen, ShinLen,
            KneeMinDeg, KneeMaxDeg);
    }

    private static Quaternion MatrixToQuat(in Matrix3x3 matrix) => DoubleRotation.FromMatrix(matrix).ToQuaternion();

    public void SolveToeIk(bool isLeft)
    {
        VirtualBone ankle = isLeft ? LeftAnkle : RightAnkle;
        VirtualBone toe = isLeft ? LeftToe : RightToe;
        VirtualBone toeIk = isLeft ? LeftToeIk : RightToeIk;

        if (SourceStructureVersion >= 2 && SourceSkeleton != null && SourceSkeleton.RestRotations.ContainsKey(ankle.Name))
        {
            var limits = isLeft ? SourceSkeleton.LeftLegIk : SourceSkeleton.RightLegIk;
            Quaternion restRot = SourceSkeleton.RestRotations[ankle.Name];
            ankle.WorldRotation = SourceNativeIK.SolveSourceToe(
                ankle.WorldPosition,
                toeIk.WorldPosition,
                toe.RestOffset + toe.LocalPosition,
                ankle.WorldRotation,
                restRot,
                limits.ToeLoopCount);
            PropagateChildren(ankle);
            return;
        }

        // Standard CCD Toe IK
        SourceNativeIK.SolveToeCcd(ankle, toe, toeIk, DefaultToeIkLoop, DefaultToeIkStepDeg, PropagateChildren);
    }

    public void PrepareNativeAnimation(VmdMotion motion, int totalFrames)
    {
        if (SourceStructureVersion < 3 || SourceSkeleton == null)
            return;

        // Python samples the native ancestor closure, including unkeyed bones.
        // Granted/offset rotations stay on the finalized public-local path.
        var posMap = new Dictionary<string, Vector3[]>();
        var rotMap = new Dictionary<string, Quaternion[]>();
        var offsets = GakumasBoneProfile.Default.Bones
            .Where(bone => bone.OffsetEuler != Vector3.Zero)
            .Select(bone => bone.VmdName).ToHashSet(StringComparer.Ordinal);

        foreach (string name in SourceSkeleton.NativeOrder)
        {
            motion.BoneKeyframes.TryGetValue(name, out var keys);
            var (p, r) = NativeAnimationSampling.Sample(keys ?? Array.Empty<BoneKeyframe>(), totalFrames,
                SourceSkeleton.SourceAxisMaps[name], SourceSkeleton.LocationCurveOrders[name]);
            if (Bones.TryGetValue(name, out var bone))
            {
                posMap[name] = p;
                if (bone.AppendSource == null && !offsets.Contains(name)) rotMap[name] = r;
            }
        }

        SourceNativeAnimation = (posMap, rotMap);
    }

    public IReadOnlyDictionary<string, Quaternion> EvaluateFrame(
        int frameIdx,
        IReadOnlyDictionary<string, Quaternion> boneRotationsMap,
        IReadOnlyDictionary<string, Vector3> bonePositionsMap,
        IReadOnlyDictionary<string, Quaternion>? twistMap = null,
        bool leftLegIk = true,
        bool rightLegIk = true,
        bool? leftToeIk = null,
        bool? rightToeIk = null)
    {
        // 1. Reset all bones
        foreach (var b in Bones.Values)
        {
            b.LocalPosition = Vector3.Zero;
            b.LocalRotation = Quaternion.Identity;
        }

        // 2. Consume authored translations: ALL bones including 下半身!
        foreach (var (name, pMmd) in bonePositionsMap)
        {
            if (Bones.TryGetValue(name, out var bone))
            {
                bone.LocalPosition = Math32.ToUnityPosition(pMmd);
            }
        }

        // 3. Sampled rotations
        foreach (var (name, bone) in Bones)
        {
            if (boneRotationsMap.TryGetValue(name, out var rot))
            {
                bone.LocalRotation = rot;
            }
            else if (twistMap != null && twistMap.TryGetValue(name, out var tRot))
            {
                bone.LocalRotation = tRot;
            }
        }

        // 4. FK pass
        ForwardKinematics(Root);

        // 5. Order 0 Append pass
        foreach (var b in AppendOrder0)
        {
            ApplyAppend(b);
            ForwardKinematics(b);
        }

        // 6. Native Source FK (Version 3)
        if (SourceStructureVersion >= 3 && SourceSkeleton != null)
        {
            Dictionary<string, Vector3>? nativePositions = null;
            Dictionary<string, Quaternion>? nativeRotations = null;

            if (SourceNativeAnimation.HasValue)
            {
                var (pCache, rCache) = SourceNativeAnimation.Value;
                nativePositions = new Dictionary<string, Vector3>();
                nativeRotations = new Dictionary<string, Quaternion>();

                foreach (var (name, pArr) in pCache)
                {
                    if (frameIdx < pArr.Length)
                        nativePositions[name] = pArr[frameIdx];
                }

                foreach (var (name, rArr) in rCache)
                {
                    if (frameIdx < rArr.Length)
                        nativeRotations[name] = rArr[frameIdx];
                }
            }

            var localRots = new Dictionary<string, Quaternion>();
            var localPoss = new Dictionary<string, Vector3>();
            foreach (var (name, bone) in Bones)
            {
                localRots[name] = bone.LocalRotation;
                localPoss[name] = bone.LocalPosition;
            }

            SourceNativePoses = EvaluateNativeSourceFk(localRots, localPoss, nativeRotations, nativePositions);
        }

        // Pose limits (Version 2+)
        if (SourceStructureVersion >= 2 && SourceSkeleton != null)
        {
            (string Side, VirtualBone Knee)[] knees = [("left", LeftKnee), ("right", RightKnee)];
            foreach (var (side, knee) in knees)
            {
                var limits = side == "left" ? SourceSkeleton.LeftLegIk : SourceSkeleton.RightLegIk;
                if (SourceSkeleton.RestRotations.TryGetValue(knee.Name, out var restRot))
                {
                    knee.WorldRotation = SourceNativeIK.LimitSourceKneePose(
                        knee.WorldRotation,
                        restRot,
                        limits.KneeMinDegrees,
                        limits.KneeMaxDegrees);
                    PropagateChildren(knee);
                }
            }
        }

        // 7. Leg and Toe IK
        for (int sideIdx = 0; sideIdx < 2; sideIdx++)
        {
            bool isLeft = sideIdx == 1; // right then left
            bool ikEnabled = isLeft ? leftLegIk : rightLegIk;
            bool toeEnabled = isLeft ? (leftToeIk ?? leftLegIk) : (rightToeIk ?? rightLegIk);
            VirtualBone ankle = isLeft ? LeftAnkle : RightAnkle;

            if (ikEnabled)
            {
                SolveLegIk(isLeft);
                PropagateChildren(ankle);
            }

            if (toeEnabled)
            {
                SolveToeIk(isLeft);
                PropagateChildren(ankle);
            }
        }

        // 8. Order 1 Append pass (D mirrors)
        foreach (var (groupRoot, chain) in AppendOrder1Groups)
        {
            foreach (var b in chain)
            {
                ApplyAppend(b);
            }
            ForwardKinematics(groupRoot);
        }

        // 9. Collect world rotations
        var worldRots = new Dictionary<string, Quaternion>(Bones.Count);
        foreach (var (name, bone) in Bones)
        {
            worldRots[name] = bone.WorldRotation;
        }

        return worldRots;
    }

    private Dictionary<string, (AffineMatrix4x4 Matrix, float Length)> EvaluateNativeSourceFk(
        Dictionary<string, Quaternion> localRotationsUnity,
        Dictionary<string, Vector3> localPositionsUnity,
        Dictionary<string, Quaternion>? nativeRotations = null,
        Dictionary<string, Vector3>? nativePositions = null)
    {
        var result = new Dictionary<string, (AffineMatrix4x4 Matrix, float Length)>();
        var skeleton = SourceSkeleton!;

        foreach (string name in skeleton.NativeOrder)
        {
            var rec = skeleton.Bones[name].Native!;
            AffineMatrix4x4 basis = AffineMatrix4x4.Identity;

            if (nativeRotations != null && nativeRotations.TryGetValue(name, out var nRot))
            {
                basis.Basis3x3 = Math32.NativeQuaternionMatrix(nRot);
            }
            else if (localRotationsUnity.TryGetValue(name, out var uRot))
            {
                Vector3 vec = EulerRotation.ToRotVec(uRot);
                float angle = vec.Length();
                if (angle > 1e-15f)
                {
                    Matrix3x3 axisMap = skeleton.AxisMaps[name];
                    Vector3 axis = Math32.MatVec32(axisMap, vec / angle);
                    float size = axis.Length();
                    if (size > 1e-20f && float.IsFinite(size))
                    {
                        Quaternion rot = EulerRotation.FromRotVec(axis * (angle / size));
                        basis.Basis3x3 = Math32.NativeQuaternionMatrix(rot);
                    }
                }
            }

            if (nativePositions != null && nativePositions.TryGetValue(name, out var nPos))
            {
                basis.Translation = nPos;
            }
            else if (localPositionsUnity.TryGetValue(name, out var uPos))
            {
                basis.Translation = Math32.MatVec32(skeleton.PositionMaps[name], uPos);
            }

            AffineMatrix4x4 offset = AffineMatrix4x4.Identity;
            Matrix4x4 off4 = skeleton.RestOffsets4x4[name];
            offset.M00 = off4.M11; offset.M01 = off4.M12; offset.M02 = off4.M13; offset.M03 = off4.M41;
            offset.M10 = off4.M21; offset.M11 = off4.M22; offset.M12 = off4.M23; offset.M13 = off4.M42;
            offset.M20 = off4.M31; offset.M21 = off4.M32; offset.M22 = off4.M33; offset.M23 = off4.M43;

            string? parent = rec.Parent;
            AffineMatrix4x4 matrix = parent == null
                ? offset
                : result[parent].Matrix * offset;

            matrix *= basis;
            matrix = NativePoseLimits.Apply(matrix, rec.Constraints);

            result[name] = (matrix, rec.Length);
        }

        return result;
    }
}
