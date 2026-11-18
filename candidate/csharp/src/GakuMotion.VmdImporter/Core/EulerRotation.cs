namespace GakuMotion.VmdImporter.Core;

using System.Numerics;

/// <summary>
/// Euler XYZ rotation utilities and MMD waist cancellation grant semantics.
/// Strictly replicates SciPy Rotation and virtual_rig.py:474-482.
/// </summary>
public static class EulerRotation
{
    private const float FloatEpsilon = 1.1920929e-7f;

    /// <summary>
    /// Converts a quaternion to a rotation vector (angle * unit_axis).
    /// Matches SciPy Rotation.as_rotvec().
    /// </summary>
    public static Vector3 ToRotVec(Quaternion q)
    {
        // Enforce w >= 0 for consistent principal angle
        float x = q.X, y = q.Y, z = q.Z, w = q.W;
        if (w < 0.0f)
        {
            x = -x; y = -y; z = -z; w = -w;
        }

        double normV = Math.Sqrt((double)x * x + (double)y * y + (double)z * z);
        if (normV < 1e-12)
            return Vector3.Zero;

        double halfAngle = Math.Atan2(normV, (double)w);
        double angle = 2.0 * halfAngle;
        double scale = angle / normV;

        return new Vector3(
            (float)(x * scale),
            (float)(y * scale),
            (float)(z * scale));
    }

    /// <summary>
    /// Constructs a quaternion from a rotation vector (angle * unit_axis).
    /// Matches SciPy Rotation.from_rotvec().
    /// </summary>
    public static Quaternion FromRotVec(Vector3 rotVec)
    {
        double angle = Math.Sqrt((double)rotVec.X * rotVec.X + (double)rotVec.Y * rotVec.Y + (double)rotVec.Z * rotVec.Z);
        if (angle < 1e-12)
            return Quaternion.Identity;

        double half = 0.5 * angle;
        double sinHalf = Math.Sin(half);
        double cosHalf = Math.Cos(half);
        double factor = sinHalf / angle;

        return Math32.NormalizeQuaternionWxyz(new Quaternion(
            (float)(rotVec.X * factor),
            (float)(rotVec.Y * factor),
            (float)(rotVec.Z * factor),
            (float)cosHalf));
    }

    /// <summary>
    /// Computes the minimal rotation that transforms vector vFrom into vTo.
    /// Strictly replicates virtual_rig.py from_to_rotation(v_from, v_to).
    /// </summary>
    public static Quaternion FromToRotation(Vector3 vFrom, Vector3 vTo)
    {
        double nFrom = Math.Sqrt((double)vFrom.X * vFrom.X + (double)vFrom.Y * vFrom.Y + (double)vFrom.Z * vFrom.Z);
        double nTo = Math.Sqrt((double)vTo.X * vTo.X + (double)vTo.Y * vTo.Y + (double)vTo.Z * vTo.Z);
        if (nFrom < 1e-6 || nTo < 1e-6)
            return Quaternion.Identity;

        Vector3 uFrom = new((float)(vFrom.X / nFrom), (float)(vFrom.Y / nFrom), (float)(vFrom.Z / nFrom));
        Vector3 uTo = new((float)(vTo.X / nTo), (float)(vTo.Y / nTo), (float)(vTo.Z / nTo));

        double dot = Math.Clamp((double)uFrom.X * uTo.X + (double)uFrom.Y * uTo.Y + (double)uFrom.Z * uTo.Z, -1.0, 1.0);
        Vector3 cross = Vector3.Cross(uFrom, uTo);
        double crossNorm = Math.Sqrt((double)cross.X * cross.X + (double)cross.Y * cross.Y + (double)cross.Z * cross.Z);

        if (crossNorm < 1e-6)
        {
            if (dot > 0.0)
                return Quaternion.Identity;

            // Opposite vectors: choose orthogonal axis and rotate 180 degrees
            Vector3 axis = new(0.0f, 0.0f, 1.0f);
            if (MathF.Abs(uFrom.Z) > 0.9f)
                axis = new Vector3(1.0f, 0.0f, 0.0f);

            axis = Vector3.Cross(uFrom, axis);
            float axisLen = axis.Length();
            axis /= axisLen;
            return FromRotVec(axis * MathF.PI);
        }

        Vector3 rotAxis = new((float)(cross.X / crossNorm), (float)(cross.Y / crossNorm), (float)(cross.Z / crossNorm));
        float angle = (float)Math.Acos(dot);
        return FromRotVec(rotAxis * angle);
    }

    /// <summary>
    /// Decomposes quaternion into extrinsic XYZ Euler angles (radians), selecting branch closest to origin.
    /// Strictly replicates SciPy Rotation.as_euler('xyz') and source_native_fk.py _near_origin_xyz.
    /// </summary>
    public static Vector3 QuaternionToEulerXYZ(Quaternion q)
    {
        Matrix3x3 m = Math32.NativeQuaternionMatrix(q);
        float cy = MathF.Sqrt(m.M00 * m.M00 + m.M10 * m.M10);

        float a0, a1, a2;
        if (cy > 16.0f * FloatEpsilon)
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

        float sumOriginal = MathF.Abs(a0) + MathF.Abs(a1) + MathF.Abs(a2);
        float sumAlternate = MathF.Abs(alt0) + MathF.Abs(alt1) + MathF.Abs(alt2);

        return (sumAlternate < sumOriginal)
            ? new Vector3(alt0, alt1, alt2)
            : new Vector3(a0, a1, a2);
    }

    /// <summary>
    /// Constructs a quaternion from extrinsic XYZ Euler angles in radians: Q = Qz * Qy * Qx.
    /// Matches SciPy Rotation.from_euler('xyz', ...).
    /// </summary>
    public static Quaternion EulerXYZToQuaternion(Vector3 euler)
    {
        Quaternion qx = FromRotVec(new Vector3(euler.X, 0.0f, 0.0f));
        Quaternion qy = FromRotVec(new Vector3(0.0f, euler.Y, 0.0f));
        Quaternion qz = FromRotVec(new Vector3(0.0f, 0.0f, euler.Z));

        // Extrinsic XYZ = Rz * Ry * Rx
        return Math32.NormalizeQuaternionWxyz(qz * qy * qx);
    }

    /// <summary>
    /// Constructs a quaternion from extrinsic XYZ Euler angles in degrees.
    /// </summary>
    public static Quaternion EulerXYZToQuaternionDegrees(Vector3 eulerDegrees)
    {
        float degToRad = MathF.PI / 180.0f;
        return EulerXYZToQuaternion(new Vector3(
            eulerDegrees.X * degToRad,
            eulerDegrees.Y * degToRad,
            eulerDegrees.Z * degToRad));
    }

    /// <summary>
    /// Applies MMD/PMX waist cancellation grant rotation using local Euler XYZ axis-by-axis negation.
    /// Strictly replicates virtual_rig.py:475-480. STRICTLY PROHIBITS quaternion inverse.
    /// </summary>
    public static Quaternion WaistEulerCancelGrant(Quaternion effectiveSourceLocalRot, in Matrix3x3 axisMap)
    {
        Vector3 effVec = ToRotVec(effectiveSourceLocalRot);
        Vector3 mappedVec = Math32.MatVec32(axisMap, effVec);
        Quaternion mappedRot = FromRotVec(mappedVec);

        Vector3 euler = QuaternionToEulerXYZ(mappedRot);
        Vector3 eulerNeg = new(-euler.X, -euler.Y, -euler.Z);
        Quaternion negMappedRot = EulerXYZToQuaternion(eulerNeg);

        Matrix3x3 invAxisMap = axisMap.Invert();
        Vector3 negVec = ToRotVec(negMappedRot);
        Vector3 grantVec = Math32.MatVec32(invAxisMap, negVec);

        return FromRotVec(grantVec);
    }

    /// <summary>
    /// Computes rotation magnitude in degrees: 2 * acos(clamp(|w|, -1, 1)) * (180 / pi).
    /// </summary>
    public static float AngleDegrees(Quaternion q)
    {
        Vector3 rv = ToRotVec(q);
        return rv.Length() * (180.0f / MathF.PI);
    }

    /// <summary>
    /// Computes angular difference between two rotations in degrees: (a^-1 * b).magnitude() in degrees.
    /// </summary>
    public static float AngularDifferenceDegrees(Quaternion a, Quaternion b)
    {
        Quaternion diff = Quaternion.Inverse(a) * b;
        return AngleDegrees(diff);
    }
}
