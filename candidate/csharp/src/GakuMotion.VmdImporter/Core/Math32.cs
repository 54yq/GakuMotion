namespace GakuMotion.VmdImporter.Core;

using System.Numerics;

/// <summary>
/// 3x3 single-precision matrix used for native source coordinate frames and axis mapping.
/// </summary>
public readonly struct Matrix3x3 : IEquatable<Matrix3x3>
{
    public float M00 { get; }
    public float M01 { get; }
    public float M02 { get; }

    public float M10 { get; }
    public float M11 { get; }
    public float M12 { get; }

    public float M20 { get; }
    public float M21 { get; }
    public float M22 { get; }

    public Matrix3x3(
        float m00, float m01, float m02,
        float m10, float m11, float m12,
        float m20, float m21, float m22)
    {
        M00 = m00; M01 = m01; M02 = m02;
        M10 = m10; M11 = m11; M12 = m12;
        M20 = m20; M21 = m21; M22 = m22;
    }

    public static Matrix3x3 Identity => new(
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f);

    public static Matrix3x3 Zero => new(
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f);

    public float Determinant()
    {
        double m00 = M00, m01 = M01, m02 = M02;
        double m10 = M10, m11 = M11, m12 = M12;
        double m20 = M20, m21 = M21, m22 = M22;

        return (float)(
            m00 * (m11 * m22 - m12 * m21) -
            m01 * (m10 * m22 - m12 * m20) +
            m02 * (m10 * m21 - m11 * m20));
    }

    public Matrix3x3 Transpose()
    {
        return new Matrix3x3(
            M00, M10, M20,
            M01, M11, M21,
            M02, M12, M22);
    }

    public Matrix3x3 Invert()
    {
        double m00 = M00, m01 = M01, m02 = M02;
        double m10 = M10, m11 = M11, m12 = M12;
        double m20 = M20, m21 = M21, m22 = M22;

        double c00 = m11 * m22 - m12 * m21;
        double c01 = -(m10 * m22 - m12 * m20);
        double c02 = m10 * m21 - m11 * m20;

        double det = m00 * c00 + m01 * c01 + m02 * c02;
        if (Math.Abs(det) <= 1e-15)
            throw new InvalidOperationException("Matrix3x3 is singular and cannot be inverted.");

        double invDet = 1.0 / det;

        double c10 = -(m01 * m22 - m02 * m21);
        double c11 = m00 * m22 - m02 * m20;
        double c12 = -(m00 * m21 - m01 * m20);

        double c20 = m01 * m12 - m02 * m11;
        double c21 = -(m00 * m12 - m02 * m10);
        double c22 = m00 * m11 - m01 * m10;

        return new Matrix3x3(
            (float)(c00 * invDet), (float)(c10 * invDet), (float)(c20 * invDet),
            (float)(c01 * invDet), (float)(c11 * invDet), (float)(c21 * invDet),
            (float)(c02 * invDet), (float)(c12 * invDet), (float)(c22 * invDet));
    }

    public static Matrix3x3 operator *(in Matrix3x3 a, in Matrix3x3 b)
    {
        return new Matrix3x3(
            a.M00 * b.M00 + a.M01 * b.M10 + a.M02 * b.M20,
            a.M00 * b.M01 + a.M01 * b.M11 + a.M02 * b.M21,
            a.M00 * b.M02 + a.M01 * b.M12 + a.M02 * b.M22,

            a.M10 * b.M00 + a.M11 * b.M10 + a.M12 * b.M20,
            a.M10 * b.M01 + a.M11 * b.M11 + a.M12 * b.M21,
            a.M10 * b.M02 + a.M11 * b.M12 + a.M12 * b.M22,

            a.M20 * b.M00 + a.M21 * b.M10 + a.M22 * b.M20,
            a.M20 * b.M01 + a.M21 * b.M11 + a.M22 * b.M21,
            a.M20 * b.M02 + a.M21 * b.M12 + a.M22 * b.M22);
    }

    public static Matrix3x3 operator -(in Matrix3x3 a)
    {
        return new Matrix3x3(
            -a.M00, -a.M01, -a.M02,
            -a.M10, -a.M11, -a.M12,
            -a.M20, -a.M21, -a.M22);
    }

    public bool Equals(Matrix3x3 other)
    {
        return M00.Equals(other.M00) && M01.Equals(other.M01) && M02.Equals(other.M02) &&
               M10.Equals(other.M10) && M11.Equals(other.M11) && M12.Equals(other.M12) &&
               M20.Equals(other.M20) && M21.Equals(other.M21) && M22.Equals(other.M22);
    }

    public override bool Equals(object? obj) => obj is Matrix3x3 other && Equals(other);

    public override int GetHashCode()
    {
        HashCode hash = new();
        hash.Add(M00); hash.Add(M01); hash.Add(M02);
        hash.Add(M10); hash.Add(M11); hash.Add(M12);
        hash.Add(M20); hash.Add(M21); hash.Add(M22);
        return hash.ToHashCode();
    }

    public static bool operator ==(in Matrix3x3 left, in Matrix3x3 right) => left.Equals(right);
    public static bool operator !=(in Matrix3x3 left, in Matrix3x3 right) => !left.Equals(right);
}

/// <summary>
/// Strict IEEE 754 float32 parenthesized accumulation and native source arithmetic helpers.
/// Replicates Python source_native_fk.py and convert.py.
/// </summary>
public static class Math32
{
    /// <summary>
    /// NumPy float32 matrix @ accumulation, verified against the frozen runtime.
    /// This differs from the explicitly serial _matvec32 source helper below.
    /// Fused operations here preserve source rounding; they are not an optimization.
    /// </summary>
    internal static Matrix3x3 NativeMatrixProduct(in Matrix3x3 a, in Matrix3x3 b)
    {
        static float Dot(float a0, float a1, float a2, float b0, float b1, float b2) =>
            MathF.FusedMultiplyAdd(a2, b2, MathF.FusedMultiplyAdd(a1, b1, a0 * b0));
        return new Matrix3x3(
            Dot(a.M00, a.M01, a.M02, b.M00, b.M10, b.M20), Dot(a.M00, a.M01, a.M02, b.M01, b.M11, b.M21), Dot(a.M00, a.M01, a.M02, b.M02, b.M12, b.M22),
            Dot(a.M10, a.M11, a.M12, b.M00, b.M10, b.M20), Dot(a.M10, a.M11, a.M12, b.M01, b.M11, b.M21), Dot(a.M10, a.M11, a.M12, b.M02, b.M12, b.M22),
            Dot(a.M20, a.M21, a.M22, b.M00, b.M10, b.M20), Dot(a.M20, a.M21, a.M22, b.M01, b.M11, b.M21), Dot(a.M20, a.M21, a.M22, b.M02, b.M12, b.M22));
    }

    /// <summary>
    /// Strict float32 parenthesized accumulation quaternion normalization.
    /// Formula: square = ((w*w + x*x) + y*y) + z*z; inv = 1.0f / sqrt(square).
    /// STRICTLY PROHIBITS Quaternion.Normalize().
    /// </summary>
    public static Quaternion NormalizeQuaternionWxyz(Quaternion q)
    {
        float x = q.X;
        float y = q.Y;
        float z = q.Z;
        float w = q.W;

        float square = ((w * w + x * x) + y * y) + z * z;
        if (!float.IsFinite(square) || square <= 0.0f)
            throw new InvalidOperationException("Native quaternion samples must be finite and nonzero");

        float inv = 1.0f / MathF.Sqrt(square);
        return new Quaternion(x * inv, y * inv, z * inv, w * inv);
    }

    /// <summary>
    /// Serial float additions for matrix-vector multiplication: ((m0*v0 + m1*v1) + m2*v2).
    /// Replicates Python _matvec32(matrix, vector).
    /// </summary>
    public static Vector3 MatVec32(in Matrix3x3 m, in Vector3 v)
    {
        float r0 = ((m.M00 * v.X + m.M01 * v.Y) + m.M02 * v.Z);
        float r1 = ((m.M10 * v.X + m.M11 * v.Y) + m.M12 * v.Z);
        float r2 = ((m.M20 * v.X + m.M21 * v.Y) + m.M22 * v.Z);
        return new Vector3(r0, r1, r2);
    }

    /// <summary>
    /// Python np.remainder equivalent for float precision: (a % b + b) % b when b > 0.
    /// </summary>
    public static float Remainder(float a, float b)
    {
        float r = a % b;
        return (r < 0.0f && b > 0.0f) ? r + b : r;
    }

    /// <summary>
    /// Wraps authored quaternion key through axis/angle in float32 precision.
    /// Replicates Python _native_key_quaternion(raw_xyzw, source_axis).
    /// </summary>
    public static Quaternion NativeKeyQuaternion(Quaternion rawRot, in Matrix3x3 sourceAxis)
    {
        Quaternion q = NormalizeQuaternionWxyz(rawRot);
        float half = MathF.Acos(Math.Clamp(q.W, -1.0f, 1.0f));
        float sine = NativeFloatTrig.Sin(half);
        float factor = MathF.Abs(sine) >= 0.0005f ? sine : 1.0f;

        Vector3 srcDir = new(q.X / factor, q.Y / factor, q.Z / factor);
        if (srcDir == Vector3.Zero)
            srcDir = new Vector3(0.0f, 1.0f, 0.0f);

        Vector3 direction = MatVec32(sourceAxis, srcDir);
        float square = (direction.X * direction.X + direction.Y * direction.Y) + direction.Z * direction.Z;
        if (square <= 0.0f)
            return Quaternion.Identity;

        direction *= 1.0f / MathF.Sqrt(square);

        float pi = MathF.PI;
        float angle = Remainder(2.0f * half + pi, 2.0f * pi) - pi;
        half = 0.5f * angle;

        float sinHalf = NativeFloatTrig.Sin(half);
        return NormalizeQuaternionWxyz(new Quaternion(
            direction.X * sinHalf,
            direction.Y * sinHalf,
            direction.Z * sinHalf,
            NativeFloatTrig.Cos(half)));
    }

    /// <summary>
    /// Native float quaternion normalization followed by double products.
    /// Replicates Python native_quaternion_matrix(raw_xyzw).
    /// </summary>
    public static Matrix3x3 NativeQuaternionMatrix(Quaternion rawRot)
    {
        Quaternion q = NormalizeQuaternionWxyz(rawRot);
        double x = (double)q.X * Math.Sqrt(2.0);
        double y = (double)q.Y * Math.Sqrt(2.0);
        double z = (double)q.Z * Math.Sqrt(2.0);
        double w = (double)q.W * Math.Sqrt(2.0);

        return new Matrix3x3(
            (float)(1.0 - y * y - z * z), (float)(x * y - z * w), (float)(x * z + y * w),
            (float)(x * y + z * w), (float)(1.0 - x * x - z * z), (float)(y * z - x * w),
            (float)(x * z - y * w), (float)(y * z + x * w), (float)(1.0 - x * x - y * y));
    }

    /// <summary>
    /// Convert MMD quaternion (x, y, z, w) to Unity quaternion (-x, y, -z, w).
    /// </summary>
    public static Quaternion ToUnityRotation(Quaternion qMmd)
    {
        return new Quaternion(-qMmd.X, qMmd.Y, -qMmd.Z, qMmd.W);
    }

    /// <summary>
    /// Convert Unity quaternion (-x, y, -z, w) to MMD quaternion (x, y, z, w).
    /// </summary>
    public static Quaternion ToMmdRotation(Quaternion qUnity)
    {
        return new Quaternion(-qUnity.X, qUnity.Y, -qUnity.Z, qUnity.W);
    }

    /// <summary>
    /// Convert MMD translation (x, y, z) to Unity translation (-x, y, -z).
    /// </summary>
    public static Vector3 ToUnityPosition(Vector3 pMmd)
    {
        return new Vector3(-pMmd.X, pMmd.Y, -pMmd.Z);
    }

    /// <summary>
    /// Convert Unity translation (-x, y, -z) to MMD translation (x, y, z).
    /// </summary>
    public static Vector3 ToMmdPosition(Vector3 pUnity)
    {
        return new Vector3(-pUnity.X, pUnity.Y, -pUnity.Z);
    }
}
