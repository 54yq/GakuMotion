namespace GakuMotion.VmdImporter.Rig;

using System.Numerics;
using GakuMotion.VmdImporter.Core;

/// <summary>
/// 4x4 single-precision affine transformation matrix in standard column-vector convention (M * v).
/// Matches NumPy (4, 4) layout where M[0..2, 3] is position and M[0..2, 0..2] is rotation/scale basis.
/// </summary>
public struct AffineMatrix4x4 : IEquatable<AffineMatrix4x4>
{
    public float M00 { get; set; }
    public float M01 { get; set; }
    public float M02 { get; set; }
    public float M03 { get; set; }

    public float M10 { get; set; }
    public float M11 { get; set; }
    public float M12 { get; set; }
    public float M13 { get; set; }

    public float M20 { get; set; }
    public float M21 { get; set; }
    public float M22 { get; set; }
    public float M23 { get; set; }

    public float M30 { get; set; }
    public float M31 { get; set; }
    public float M32 { get; set; }
    public float M33 { get; set; }

    public AffineMatrix4x4(
        float m00, float m01, float m02, float m03,
        float m10, float m11, float m12, float m13,
        float m20, float m21, float m22, float m23,
        float m30, float m31, float m32, float m33)
    {
        M00 = m00; M01 = m01; M02 = m02; M03 = m03;
        M10 = m10; M11 = m11; M12 = m12; M13 = m13;
        M20 = m20; M21 = m21; M22 = m22; M23 = m23;
        M30 = m30; M31 = m31; M32 = m32; M33 = m33;
    }

    public static AffineMatrix4x4 Identity => new(
        1f, 0f, 0f, 0f,
        0f, 1f, 0f, 0f,
        0f, 0f, 1f, 0f,
        0f, 0f, 0f, 1f);

    public float this[int row, int col]
    {
        readonly get => row switch
        {
            0 => col switch { 0 => M00, 1 => M01, 2 => M02, 3 => M03, _ => throw new ArgumentOutOfRangeException(nameof(col)) },
            1 => col switch { 0 => M10, 1 => M11, 2 => M12, 3 => M13, _ => throw new ArgumentOutOfRangeException(nameof(col)) },
            2 => col switch { 0 => M20, 1 => M21, 2 => M22, 3 => M23, _ => throw new ArgumentOutOfRangeException(nameof(col)) },
            3 => col switch { 0 => M30, 1 => M31, 2 => M32, 3 => M33, _ => throw new ArgumentOutOfRangeException(nameof(col)) },
            _ => throw new ArgumentOutOfRangeException(nameof(row))
        };
        set
        {
            switch (row)
            {
                case 0: switch (col) { case 0: M00 = value; break; case 1: M01 = value; break; case 2: M02 = value; break; case 3: M03 = value; break; default: throw new ArgumentOutOfRangeException(nameof(col)); } break;
                case 1: switch (col) { case 0: M10 = value; break; case 1: M11 = value; break; case 2: M12 = value; break; case 3: M13 = value; break; default: throw new ArgumentOutOfRangeException(nameof(col)); } break;
                case 2: switch (col) { case 0: M20 = value; break; case 1: M21 = value; break; case 2: M22 = value; break; case 3: M23 = value; break; default: throw new ArgumentOutOfRangeException(nameof(col)); } break;
                case 3: switch (col) { case 0: M30 = value; break; case 1: M31 = value; break; case 2: M32 = value; break; case 3: M33 = value; break; default: throw new ArgumentOutOfRangeException(nameof(col)); } break;
                default: throw new ArgumentOutOfRangeException(nameof(row));
            }
        }
    }

    public Vector3 Translation
    {
        readonly get => new(M03, M13, M23);
        set { M03 = value.X; M13 = value.Y; M23 = value.Z; }
    }

    public Matrix3x3 Basis3x3
    {
        readonly get => new(M00, M01, M02, M10, M11, M12, M20, M21, M22);
        set
        {
            M00 = value.M00; M01 = value.M01; M02 = value.M02;
            M10 = value.M10; M11 = value.M11; M12 = value.M12;
            M20 = value.M20; M21 = value.M21; M22 = value.M22;
        }
    }

    public static AffineMatrix4x4 operator *(in AffineMatrix4x4 a, in AffineMatrix4x4 b)
    {
        AffineMatrix4x4 r = default;
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                float sum = 0;
                for (int k = 0; k < 4; k++) sum = MathF.FusedMultiplyAdd(a[i, k], b[k, j], sum);
                r[i, j] = sum;
            }
        }
        return r;
    }

    public static Vector3 operator *(in AffineMatrix4x4 m, in Vector3 v)
    {
        return new Vector3(
            m.M00 * v.X + m.M01 * v.Y + m.M02 * v.Z + m.M03,
            m.M10 * v.X + m.M11 * v.Y + m.M12 * v.Z + m.M13,
            m.M20 * v.X + m.M21 * v.Y + m.M22 * v.Z + m.M23);
    }

    public readonly AffineMatrix4x4 Invert()
    {
        // Convert to System.Numerics.Matrix4x4 (transposed because Numerics is row-vector)
        Matrix4x4 sysM = new(
            M00, M10, M20, M30,
            M01, M11, M21, M31,
            M02, M12, M22, M32,
            M03, M13, M23, M33);

        if (!Matrix4x4.Invert(sysM, out var invSys))
            throw new InvalidOperationException("AffineMatrix4x4 is not invertible.");

        return new AffineMatrix4x4(
            invSys.M11, invSys.M21, invSys.M31, invSys.M41,
            invSys.M12, invSys.M22, invSys.M32, invSys.M42,
            invSys.M13, invSys.M23, invSys.M33, invSys.M43,
            invSys.M14, invSys.M24, invSys.M34, invSys.M44);
    }

    public readonly bool Equals(AffineMatrix4x4 other)
    {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                if (!this[i, j].Equals(other[i, j]))
                    return false;
        return true;
    }

    public readonly override bool Equals(object? obj) => obj is AffineMatrix4x4 other && Equals(other);

    public readonly override int GetHashCode()
    {
        HashCode hash = new();
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                hash.Add(this[i, j]);
        return hash.ToHashCode();
    }

    public static bool operator ==(in AffineMatrix4x4 left, in AffineMatrix4x4 right) => left.Equals(right);
    public static bool operator !=(in AffineMatrix4x4 left, in AffineMatrix4x4 right) => !left.Equals(right);
}

/// <summary>
/// Source IK Solvers:
/// 1. Analytical Leg IK with positive knee bend convention R_x(+bend) (Ruling #7 / legacy YYB).
/// 2. CCD Toe IK (3 loops, 229.1831° max step, compensation to ankle).
/// 3. Native Leg Jacobian SVD solver with selective damping and knee swing-twist projection (source_native_ik.py).
/// 4. Source Toe SVD solver (source_leg_ik.py).
/// </summary>
public static class SourceNativeIK
{
    private static readonly Matrix3x3 PoseToUnity = new(
        -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, -1.0f, 0.0f);

    private static readonly Matrix3x3 AxialToUnity = -PoseToUnity;

    /// <summary>
    /// Solves analytical 2-bone leg IK matching MMD MMDIKSolverLeg and Ruling #7 positive knee convention.
    /// Strictly replicates virtual_rig.py:544-596.
    /// </summary>
    public static void SolveAnalyticalLeg(
        VirtualBone thigh,
        VirtualBone knee,
        VirtualBone ankle,
        VirtualBone ikBone,
        float thighLen,
        float shinLen,
        float kneeMinDeg = 0.0f,
        float kneeMaxDeg = 165.0f)
    {
        Vector3 thighPos = thigh.WorldPosition;
        Vector3 tgtPos = ikBone.WorldPosition;
        if (tgtPos.Y < 0.1f)
            tgtPos.Y = 0.1f;

        float dist = Vector3.Distance(tgtPos, thighPos);
        float legSum = thighLen + shinLen;

        float kneeAngle;
        if (dist < legSum)
        {
            float cosKnee = (thighLen * thighLen + shinLen * shinLen - dist * dist) / (2.0f * thighLen * shinLen);
            kneeAngle = MathF.PI - MathF.Acos(Math.Clamp(cosKnee, -1.0f, 1.0f));
        }
        else
        {
            kneeAngle = 0.0f;
        }

        float kneeAngleDeg = Math.Clamp(kneeAngle * (180.0f / MathF.PI), kneeMinDeg, kneeMaxDeg);

        // Ruling #7: Positive-sign knee local rotation R_x(+bend)
        Quaternion kneeLocalRot = EulerRotation.FromRotVec(new Vector3(kneeAngleDeg * (MathF.PI / 180.0f), 0.0f, 0.0f));

        Vector3 lThigh = new(0.0f, -thighLen, 0.0f);
        Vector3 lShin = new(0.0f, -shinLen, 0.0f);

        Quaternion curThighWorld = thigh.WorldRotation;
        Quaternion curKneeWorld = curThighWorld * kneeLocalRot;
        Vector3 curAnklePos = thighPos + Vector3.Transform(lThigh, curThighWorld) + Vector3.Transform(lShin, curKneeWorld);

        Vector3 vFrom = curAnklePos - thighPos;
        Vector3 vTo = tgtPos - thighPos;
        Quaternion deltaQ = EulerRotation.FromToRotation(vFrom, vTo);

        thigh.WorldRotation = deltaQ * curThighWorld;
        knee.WorldRotation = thigh.WorldRotation * kneeLocalRot;
        knee.WorldPosition = thighPos + Vector3.Transform(lThigh, thigh.WorldRotation);

        // D1 fix: Ankle FK passthrough: ankle_world = knee_world * ankle_local
        ankle.WorldRotation = knee.WorldRotation * ankle.LocalRotation;
        ankle.WorldPosition = tgtPos;
    }

    /// <summary>
    /// Evaluates finite-loop CCD rotating ankle to aim toe at toe-IK target.
    /// Strictly replicates virtual_rig.py:598-650.
    /// </summary>
    public static void SolveToeCcd(
        VirtualBone ankle,
        VirtualBone toe,
        VirtualBone toeIk,
        int loopCount = 3,
        float stepAngleDeg = 229.1831f,
        Action<VirtualBone>? propagateChildrenAction = null)
    {
        Vector3 tgtPos = toeIk.WorldPosition;
        float stepRad = stepAngleDeg * (MathF.PI / 180.0f);

        for (int iter = 0; iter < loopCount; iter++)
        {
            Vector3 endPos = toe.WorldPosition;
            Vector3 bonePos = ankle.WorldPosition;
            Vector3 fromV = endPos - bonePos;
            Vector3 toV = tgtPos - bonePos;

            double nFrom = fromV.Length();
            double nTo = toV.Length();
            if (nFrom < 1e-9 || nTo < 1e-9)
                break;

            double cosAng = Math.Clamp(Vector3.Dot(fromV, toV) / (nFrom * nTo), -1.0, 1.0);
            double angle = Math.Acos(cosAng);
            if (angle < 1e-9)
                break; // Exact match, terminate

            Vector3 axis = Vector3.Cross(fromV, toV);
            double nAxis = axis.Length();
            if (nAxis < 1e-9)
            {
                Vector3 refAxis = MathF.Abs(fromV.X) > 0.9f * (float)nFrom ? new Vector3(0, 1, 0) : new Vector3(1, 0, 0);
                axis = Vector3.Cross(fromV, refAxis);
                nAxis = axis.Length();
            }

            if (angle > stepRad)
                angle = stepRad;

            Quaternion deltaQ = EulerRotation.FromRotVec((axis / (float)nAxis) * (float)angle);
            ankle.WorldRotation = deltaQ * ankle.WorldRotation;
            propagateChildrenAction?.Invoke(ankle);
        }
    }

    /// <summary>
    /// Limit pose-space X before IK, using the Euler branch nearest zero.
    /// Strictly replicates source_leg_ik.py:36-51.
    /// </summary>
    public static Quaternion LimitSourceKneePose(
        Quaternion world,
        Quaternion rest,
        float minimumDegrees,
        float maximumDegrees)
    {
        Matrix3x3 poseToUnityT = PoseToUnity.Transpose();
        Matrix3x3 worldRestMat = Math32.NativeQuaternionMatrix(world * rest);
        Matrix3x3 poseMat = poseToUnityT * worldRestMat * PoseToUnity;

        Quaternion poseQuat = Math32.NormalizeQuaternionWxyz(EulerRotation.FromRotVec(EulerRotation.ToRotVec(
            EulerRotation.EulerXYZToQuaternion(EulerRotation.QuaternionToEulerXYZ(EulerRotation.FromRotVec(
                EulerRotation.ToRotVec(EulerRotation.EulerXYZToQuaternion(Vector3.Zero))))))));

        // Extract near-origin XYZ Euler angles from poseMat
        Vector3 xyz = NearOriginXyz(poseMat);
        float minRad = minimumDegrees * (MathF.PI / 180.0f);
        float maxRad = maximumDegrees * (MathF.PI / 180.0f);
        float limited = Math.Clamp(xyz.X, minRad, maxRad);

        if (limited == xyz.X)
            return world;

        xyz.X = limited;
        Quaternion limitedPoseQuat = EulerRotation.EulerXYZToQuaternion(xyz);
        Matrix3x3 limitedPoseMat = Math32.NativeQuaternionMatrix(limitedPoseQuat);
        Matrix3x3 worldMat = PoseToUnity * limitedPoseMat * poseToUnityT;

        Quaternion worldQuat = EulerRotation.FromRotVec(EulerRotation.ToRotVec(
            EulerRotation.EulerXYZToQuaternion(NearOriginXyz(worldMat))));

        return worldQuat * Quaternion.Inverse(rest);
    }

    private static Vector3 NearOriginXyz(in Matrix3x3 m)
    {
        float cy = MathF.Sqrt(m.M00 * m.M00 + m.M10 * m.M10);
        float a0, a1, a2;
        if (cy > 16.0f * 1.1920929e-7f)
        {
            a0 = MathF.Atan2(m.M21, m.M22);
            a1 = MathF.Atan2(-m.M20, cy);
            a2 = MathF.Atan2(m.M10, m.M00);
        }
        else
        {
            a0 = MathF.Atan2(-m.M12, m.M11);
            a1 = MathF.Atan2(-m.M20, cy);
            a2 = 0.0f;
        }

        float twoPi = 2.0f * MathF.PI;
        float alt0 = Math32.Remainder(a0 + MathF.PI + MathF.PI, twoPi) - MathF.PI;
        float alt1 = Math32.Remainder(MathF.PI - a1 + MathF.PI, twoPi) - MathF.PI;
        float alt2 = Math32.Remainder(a2 + MathF.PI + MathF.PI, twoPi) - MathF.PI;

        return (MathF.Abs(alt0) + MathF.Abs(alt1) + MathF.Abs(alt2) < MathF.Abs(a0) + MathF.Abs(a1) + MathF.Abs(a2))
            ? new Vector3(alt0, alt1, alt2)
            : new Vector3(a0, a1, a2);
    }

    /// <summary>
    /// Solves native affine leg poses via damped SVD iteration and knee hinge swing-twist projection.
    /// Strictly replicates source_native_ik.py solve_native_leg.
    /// </summary>
    public static (AffineMatrix4x4 SolvedThigh, AffineMatrix4x4 SolvedKnee) SolveNativeLeg(
        in AffineMatrix4x4 parentPose,
        in AffineMatrix4x4 thighPose,
        in AffineMatrix4x4 kneePose,
        in Vector3 goalNative,
        in Matrix3x3 thighRestRelative,
        in Matrix3x3 kneeRestRelative,
        float thighLength,
        float kneeLength,
        int iterations = 40,
        float kneeMinDeg = 0.0f,
        float kneeMaxDeg = 180.0f)
    {
        Matrix3x3 root = NormalizeColumns(parentPose.Basis3x3);
        Matrix3x3 thighOrient = NormalizeColumns(thighPose.Basis3x3);
        Matrix3x3 kneeOrient = NormalizeColumns(kneePose.Basis3x3);

        Matrix3x3 rootInverse = root.Invert();
        Matrix3x3 upperInverse = thighOrient.Invert();
        AffineMatrix4x4 kneeRelative = thighPose.Invert() * kneePose;

        Vector3 goalDiff = goalNative - thighPose.Translation;
        Vector3 target3 = Math32.MatVec32(rootInverse, goalDiff);
        double[] target = [(double)target3.X, (double)target3.Y, (double)target3.Z];

        Vector3 thighCol1 = new(thighPose.M01, thighPose.M11, thighPose.M21);
        Vector3 upperTail = thighPose.Translation + thighCol1 * thighLength;
        Vector3 kneeDiff = kneePose.Translation - upperTail;
        Vector3 start3 = Math32.MatVec32(upperInverse, kneeDiff);
        double[] start = [(double)start3.X, (double)start3.Y, (double)start3.Z];

        float thighCol1Len = MathF.Sqrt((thighCol1.X * thighCol1.X + thighCol1.Y * thighCol1.Y) + thighCol1.Z * thighCol1.Z);
        Vector3 kneeCol1 = new(kneePose.M01, kneePose.M11, kneePose.M21);
        float kneeCol1Len = MathF.Sqrt((kneeCol1.X * kneeCol1.X + kneeCol1.Y * kneeCol1.Y) + kneeCol1.Z * kneeCol1.Z);

        double[] actualLengths = [thighLength * thighCol1Len, kneeLength * kneeCol1Len];

        Matrix3x3 base0Mat = Math32.NativeMatrixProduct(thighRestRelative.Transpose(), Math32.NativeMatrixProduct(rootInverse, thighOrient));
        Matrix3x3 base1Mat = Math32.NativeMatrixProduct(kneeRestRelative.Transpose(), Math32.NativeMatrixProduct(upperInverse, kneeOrient));

        double[,] base0 = ToDouble3x3(base0Mat);
        double[,] base1 = ToDouble3x3(base1Mat);

        double[,] origBase0 = (double[,])base0.Clone();
        double[,] origBase1 = (double[,])base1.Clone();

        double[,] rest0 = ToDouble3x3(thighRestRelative);
        double[,] rest1 = ToDouble3x3(kneeRestRelative);

        double startNorm = Math.Sqrt(start[0] * start[0] + start[1] * start[1] + start[2] * start[2]);
        double scale = actualLengths[0] + actualLengths[1] + startNorm;

        start[0] /= scale; start[1] /= scale; start[2] /= scale;
        double[] lengthsNormalized = [actualLengths[0] / scale, actualLengths[1] / scale];
        target[0] /= scale; target[1] /= scale; target[2] /= scale;

        double clampLength = (lengthsNormalized[0] + lengthsNormalized[1] + (startNorm / scale)) / 4.0;
        double minimum = kneeMinDeg * (Math.PI / 180.0);
        double maximum = kneeMaxDeg * (Math.PI / 180.0);

        double[,] J = new double[3, 6];
        double[] error = new double[3];

        for (int iter = 0; iter < iterations; iter++)
        {
            double[,] upper = MatMul3x3(rest0, base0);
            double[] kneeStartArg = [start[0], lengthsNormalized[0] + start[1], start[2]];
            double[] kneeStart = MatVecMul3x3(upper, kneeStartArg);

            double[,] lowerRest = MatMul3x3(upper, rest1);
            double[,] lower = MatMul3x3(lowerRest, base1);

            double[] lowerVector = [lower[0, 1] * lengthsNormalized[1], lower[1, 1] * lengthsNormalized[1], lower[2, 1] * lengthsNormalized[1]];
            double[] endpoint = [kneeStart[0] + lowerVector[0], kneeStart[1] + lowerVector[1], kneeStart[2] + lowerVector[2]];

            error[0] = target[0] - endpoint[0];
            error[1] = target[1] - endpoint[1];
            error[2] = target[2] - endpoint[2];

            double dist = Math.Sqrt(error[0] * error[0] + error[1] * error[1] + error[2] * error[2]);
            if (dist > clampLength)
            {
                double factor = clampLength / dist;
                error[0] *= factor; error[1] *= factor; error[2] *= factor;
            }

            // Jacobian columns: J[:, 0..2] = cross(upper.T, endpoint).T
            // J[:, 3..5] = cross(lower.T, lower_vector).T
            for (int col = 0; col < 3; col++)
            {
                double ux = upper[0, col], uy = upper[1, col], uz = upper[2, col];
                // cross(u_col, endpoint)
                J[0, col] = uy * endpoint[2] - uz * endpoint[1];
                J[1, col] = uz * endpoint[0] - ux * endpoint[2];
                J[2, col] = ux * endpoint[1] - uy * endpoint[0];

                double lx = lower[0, col], ly = lower[1, col], lz = lower[2, col];
                // cross(l_col, lower_vector)
                J[0, 3 + col] = ly * lowerVector[2] - lz * lowerVector[1];
                J[1, 3 + col] = lz * lowerVector[0] - lx * lowerVector[2];
                J[2, 3 + col] = lx * lowerVector[1] - ly * lowerVector[0];
            }

            bool[] locked = new bool[3];
            double[] values = new double[3];
            double constrainedStep = 0.0;
            DoubleRotation previousKnee = DoubleRotation.FromMatrix(base1);
            DoubleRotation projectedKnee = previousKnee;
            double[] update = new double[6];

            for (int inner = 0; inner < 5; inner++)
            {
                update = SelectiveStep(J, error);
                double[,] updateKneeMat = DoubleRotation.FromRotationVector([update[3], update[4], update[5]]).ToMatrix();
                double[,] proposedMat = MatMul3x3(base1, updateKneeMat);
                DoubleRotation proposed = DoubleRotation.FromMatrix(proposedMat);

                var (proj, parameters, violated) = ProjectKnee(proposed, locked, values, minimum, maximum);
                projectedKnee = proj;

                double[] fixedArr = (previousKnee.Inverse() * proj).ToRotationVector();

                bool anyViolated = violated[0] || violated[1] || violated[2];
                if (!anyViolated)
                    break;

                int chosen = -1;
                double minAbsFixed = double.PositiveInfinity;
                for (int vIdx = 0; vIdx < 3; vIdx++)
                {
                    if (violated[vIdx] && Math.Abs(fixedArr[vIdx]) < minAbsFixed)
                    {
                        minAbsFixed = Math.Abs(fixedArr[vIdx]);
                        chosen = vIdx;
                    }
                }

                int[] lockIndices = chosen == 1 ? [1] : [0, 2];
                foreach (int idx in lockIndices)
                {
                    if (locked[idx]) continue;
                    locked[idx] = true;
                    values[idx] = parameters[idx];

                    error[0] -= J[0, 3 + idx] * fixedArr[idx];
                    error[1] -= J[1, 3 + idx] * fixedArr[idx];
                    error[2] -= J[2, 3 + idx] * fixedArr[idx];

                    J[0, 3 + idx] = 0.0; J[1, 3 + idx] = 0.0; J[2, 3 + idx] = 0.0;
                    constrainedStep = Math.Max(constrainedStep, Math.Abs(fixedArr[idx]));
                }
            }

            double[,] thighUpdateMat = DoubleRotation.FromRotationVector([update[0], update[1], update[2]]).ToMatrix();
            base0 = MatMul3x3(base0, thighUpdateMat);
            base1 = projectedKnee.ToMatrix();

            double maxAbsUpdate = 0.0;
            for (int u = 0; u < 6; u++)
                maxAbsUpdate = Math.Max(maxAbsUpdate, Math.Abs(update[u]));

            if (iter > 10 && Math.Max(constrainedStep, maxAbsUpdate) < 1e-3)
                break;
        }

        Matrix3x3 changeThigh3 = ToSingle3x3(MatMul3x3(Transpose3x3(origBase0), base0));
        Matrix3x3 changeKnee3 = ToSingle3x3(MatMul3x3(Transpose3x3(origBase1), base1));

        AffineMatrix4x4 changeThigh = AffineMatrix4x4.Identity;
        changeThigh.Basis3x3 = changeThigh3;

        AffineMatrix4x4 changeKnee = AffineMatrix4x4.Identity;
        changeKnee.Basis3x3 = changeKnee3;

        AffineMatrix4x4 solvedThigh = thighPose * changeThigh;
        AffineMatrix4x4 solvedKnee = (solvedThigh * kneeRelative) * changeKnee;

        return (solvedThigh, solvedKnee);
    }

    /// <summary>
    /// Solves source toe rotation in native coordinate frame.
    /// Strictly replicates source_leg_ik.py solve_source_toe.
    /// </summary>
    public static Quaternion SolveSourceToe(
        Vector3 anklePosition,
        Vector3 goalPosition,
        Vector3 toeOffset,
        Quaternion ankleWorldHint,
        Quaternion ankleRestRotation,
        int iterations = 3)
    {
        double[] offset = [toeOffset.X, toeOffset.Y, toeOffset.Z];
        double length = Math.Sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
        if (length < 1e-12 || iterations < 1)
            throw new ArgumentException("Source toe segment and iteration count must be positive");
        double[,] basis = MatMul3x3(DoubleRotation.FromQuaternion(ankleRestRotation).ToMatrix(), ToDouble3x3(AxialToUnity));
        double[] local = [offset[0] / length, offset[1] / length, offset[2] / length];
        double[] target = [((double)goalPosition.X - anklePosition.X) / length,
            ((double)goalPosition.Y - anklePosition.Y) / length, ((double)goalPosition.Z - anklePosition.Z) / length];
        DoubleRotation rotation = DoubleRotation.FromQuaternion(ankleWorldHint);
        double[,] jacobian = new double[3, 3];
        for (int iter = 0; iter < iterations; iter++)
        {
            double[] endpoint = rotation.Apply(local);
            double[] error = [target[0] - endpoint[0], target[1] - endpoint[1], target[2] - endpoint[2]];
            double distance = Math.Sqrt(error[0] * error[0] + error[1] * error[1] + error[2] * error[2]);
            if (distance > .5) for (int i = 0; i < 3; i++) error[i] *= .5 / distance;
            double[,] axes = MatMul3x3(rotation.ToMatrix(), basis);
            for (int col = 0; col < 3; col++)
            {
                double x = axes[0, col], y = axes[1, col], z = axes[2, col];
                jacobian[0, col] = y * endpoint[2] - z * endpoint[1];
                jacobian[1, col] = z * endpoint[0] - x * endpoint[2];
                jacobian[2, col] = x * endpoint[1] - y * endpoint[0];
            }
            double[] update = SelectiveStep(jacobian, error);
            rotation *= DoubleRotation.FromRotationVector(MatVecMul3x3(basis, update));
        }
        return rotation.ToQuaternion();
    }

    private static (DoubleRotation Result, double[] Parameters, bool[] Violated) ProjectKnee(
        DoubleRotation proposal,
        bool[] locked,
        double[] values,
        double minimum,
        double maximum)
    {
        double y = proposal.Y, w = proposal.W;
        if (w < 0.0f)
        {
            y = -y; w = -w;
        }

        double twistHypot = Math.Sqrt((double)y * y + (double)w * w);
        DoubleRotation twist;
        if (twistHypot > 1e-20)
        {
            double inv = 1.0 / twistHypot;
            twist = new DoubleRotation(0.0, y * inv, 0.0, w * inv);
        }
        else
        {
            twist = DoubleRotation.Identity;
        }

        DoubleRotation swing = proposal * twist.Inverse();
        double sign = swing.W < 0 ? -1 : 1;

        double[] parameters = [sign * swing.X, twist.ToRotationVector()[1], sign * swing.Z];

        for (int i = 0; i < 3; i++)
        {
            if (locked[i])
                parameters[i] = values[i];
        }

        double[] projected = (double[])parameters.Clone();
        projected[0] = Math.Clamp(projected[0], Math.Sin(minimum / 2.0), Math.Sin(maximum / 2.0));
        projected[1] = 0.0;
        projected[2] = 0.0;

        bool[] violated = new bool[3];
        for (int i = 0; i < 3; i++)
        {
            violated[i] = (Math.Abs(projected[i] - parameters[i]) > 1e-20) && !locked[i];
        }

        double p0 = projected[0];
        DoubleRotation result = DoubleRotation.FromComponents(p0, 0, 0, Math.Sqrt(Math.Max(0, 1 - p0 * p0)));

        return (result, projected, violated);
    }

    /// <summary>
    /// SVD update with selective damping and bounded angular step.
    /// Strictly replicates source_leg_ik.py _selective_step.
    /// </summary>
    public static double[] SelectiveStep(double[,] J, double[] error)
    {
        int cols = J.GetLength(1);
        Svd3xN(J, out double[,] left, out double[] singular, out double[,] right);

        double[] colLengths = new double[cols];
        for (int j = 0; j < cols; j++)
        {
            double sumSq = 0.0;
            for (int i = 0; i < 3; i++)
                sumSq += J[i, j] * J[i, j];
            colLengths[j] = Math.Sqrt(sumSq);
        }

        double[] update = new double[cols];
        double maximum = Math.PI / 4.0;

        for (int idx = 0; idx < 3; idx++)
        {
            double val = singular[idx];
            if (val <= 1e-10) continue;

            double dot = 0.0;
            for (int i = 0; i < 3; i++)
                dot += left[i, idx] * error[i];
            dot /= val;

            double part = 0.0;
            double maxContrib = 0.0;
            for (int j = 0; j < cols; j++)
            {
                part += Math.Abs(right[idx, j]) * colLengths[j];
                double contrib = Math.Abs(right[idx, j] * dot);
                if (contrib > maxContrib) maxContrib = contrib;
            }
            part /= val;

            double allowed = maximum / Math.Max(1.0, part);
            double damping = Math.Min(1.0, allowed / Math.Max(1e-30, maxContrib));

            for (int j = 0; j < cols; j++)
            {
                update[j] += 0.8 * damping * (right[idx, j] * dot);
            }
        }

        double peak = 0.0;
        for (int j = 0; j < cols; j++)
            if (Math.Abs(update[j]) > peak) peak = Math.Abs(update[j]);

        if (peak > maximum)
        {
            double factor = maximum / (maximum + peak);
            for (int j = 0; j < cols; j++)
                update[j] *= factor;
        }

        return update;
    }

    /// <summary>
    /// Thin SVD of the original 3xN Jacobian. Orthogonalizing its transposed
    /// columns avoids squaring the condition number through J*J^T, which turns
    /// exact null modes into spurious singular values above the source cutoff.
    /// </summary>
    public static void Svd3xN(double[,] J, out double[,] left, out double[] singular, out double[,] right)
    {
        const double epsilon = 2.2204460492503131e-16;
        int cols = J.GetLength(1);
        double[,] work = new double[cols, 3];
        double[,] vectors = new double[3, 3];
        for (int i = 0; i < 3; i++)
        {
            vectors[i, i] = 1;
            for (int k = 0; k < cols; k++) work[k, i] = J[i, k];
        }
        for (int sweep = 0; sweep < 40; sweep++)
        {
            bool changed = false;
            for (int p = 0; p < 2; p++) for (int q = p + 1; q < 3; q++)
            {
                double alpha = 0, beta = 0, gamma = 0;
                for (int k = 0; k < cols; k++)
                {
                    alpha += work[k, p] * work[k, p];
                    beta += work[k, q] * work[k, q];
                    gamma += work[k, p] * work[k, q];
                }
                if (Math.Abs(gamma) <= epsilon * Math.Sqrt(alpha) * Math.Sqrt(beta)) continue;
                double tau = (beta - alpha) / (2 * gamma);
                double t = Math.CopySign(1, tau) / (Math.Abs(tau) + Math.Sqrt(1 + tau * tau));
                double c = 1 / Math.Sqrt(1 + t * t), sin = t * c;
                for (int k = 0; k < cols; k++)
                {
                    double a = work[k, p], b = work[k, q];
                    work[k, p] = c * a - sin * b;
                    work[k, q] = sin * a + c * b;
                }
                for (int k = 0; k < 3; k++)
                {
                    double a = vectors[k, p], b = vectors[k, q];
                    vectors[k, p] = c * a - sin * b;
                    vectors[k, q] = sin * a + c * b;
                }
                changed = true;
            }
            if (!changed) break;
        }
        double[] sizes = new double[3];
        for (int i = 0; i < 3; i++)
        {
            double square = 0;
            for (int k = 0; k < cols; k++) square += work[k, i] * work[k, i];
            sizes[i] = Math.Sqrt(square);
        }
        int[] indices = [0, 1, 2];
        Array.Sort(indices, (a, b) => sizes[b].CompareTo(sizes[a]));
        singular = new double[3];
        left = new double[3, 3];
        right = new double[3, cols];
        for (int i = 0; i < 3; i++)
        {
            int index = indices[i];
            singular[i] = sizes[index];
            for (int k = 0; k < 3; k++) left[k, i] = vectors[k, index];
            if (singular[i] > 0)
                for (int k = 0; k < cols; k++) right[i, k] = work[k, index] / singular[i];
        }
    }

    private static Matrix3x3 NormalizeColumns(in Matrix3x3 m)
    {
        float l0 = MathF.Sqrt(m.M00 * m.M00 + m.M10 * m.M10 + m.M20 * m.M20);
        float l1 = MathF.Sqrt(m.M01 * m.M01 + m.M11 * m.M11 + m.M21 * m.M21);
        float l2 = MathF.Sqrt(m.M02 * m.M02 + m.M12 * m.M12 + m.M22 * m.M22);

        return new Matrix3x3(
            m.M00 / l0, m.M01 / l1, m.M02 / l2,
            m.M10 / l0, m.M11 / l1, m.M12 / l2,
            m.M20 / l0, m.M21 / l1, m.M22 / l2);
    }

    private static double[,] ToDouble3x3(in Matrix3x3 m) => new double[,]
    {
        { m.M00, m.M01, m.M02 },
        { m.M10, m.M11, m.M12 },
        { m.M20, m.M21, m.M22 }
    };

    private static Matrix3x3 ToSingle3x3(double[,] d) => new(
        (float)d[0, 0], (float)d[0, 1], (float)d[0, 2],
        (float)d[1, 0], (float)d[1, 1], (float)d[1, 2],
        (float)d[2, 0], (float)d[2, 1], (float)d[2, 2]);

    private static double[,] MatMul3x3(double[,] a, double[,] b)
    {
        double[,] r = new double[3, 3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                r[i, j] = a[i, 0] * b[0, j] + a[i, 1] * b[1, j] + a[i, 2] * b[2, j];
        return r;
    }

    private static double[] MatVecMul3x3(double[,] m, double[] v) =>
    [
        m[0, 0] * v[0] + m[0, 1] * v[1] + m[0, 2] * v[2],
        m[1, 0] * v[0] + m[1, 1] * v[1] + m[1, 2] * v[2],
        m[2, 0] * v[0] + m[2, 1] * v[1] + m[2, 2] * v[2]
    ];

    private static double[,] Transpose3x3(double[,] m) => new double[,]
    {
        { m[0, 0], m[1, 0], m[2, 0] },
        { m[0, 1], m[1, 1], m[2, 1] },
        { m[0, 2], m[1, 2], m[2, 2] }
    };

    private static Quaternion MatrixToQuaternion(double[,] m)
    {
        Matrix3x3 s = ToSingle3x3(m);
        return EulerRotation.EulerXYZToQuaternion(NearOriginXyz(s));
    }
}
