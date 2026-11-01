namespace GakuMotion.VmdImporter.Core;

using System.Numerics;

/// <summary>
/// Double-precision scalar rotation operations used at Python SciPy Rotation
/// boundaries. Native float32 key and matrix operations remain in Math32.
/// </summary>
internal readonly record struct DoubleRotation(double X, double Y, double Z, double W)
{
    public static DoubleRotation Identity => new(0.0, 0.0, 0.0, 1.0);

    public static DoubleRotation FromComponents(double x, double y, double z, double w) => Normalize(new(x, y, z, w));

    public static DoubleRotation FromQuaternion(Quaternion quaternion) =>
        Normalize(new DoubleRotation(quaternion.X, quaternion.Y, quaternion.Z, quaternion.W));

    public Quaternion ToQuaternion() => new((float)X, (float)Y, (float)Z, (float)W);

    public DoubleRotation Inverse() => new(-X, -Y, -Z, W);

    public static DoubleRotation operator *(DoubleRotation left, DoubleRotation right) => Normalize(new DoubleRotation(
        left.W * right.X + left.X * right.W + left.Y * right.Z - left.Z * right.Y,
        left.W * right.Y - left.X * right.Z + left.Y * right.W + left.Z * right.X,
        left.W * right.Z + left.X * right.Y - left.Y * right.X + left.Z * right.W,
        left.W * right.W - left.X * right.X - left.Y * right.Y - left.Z * right.Z));

    private static DoubleRotation Normalize(DoubleRotation quaternion)
    {
        double norm = Math.Sqrt(quaternion.X * quaternion.X + quaternion.Y * quaternion.Y
            + quaternion.Z * quaternion.Z + quaternion.W * quaternion.W);
        if (!(norm > 0.0) || !double.IsFinite(norm))
            throw new InvalidOperationException("A source rotation must be finite and nonzero.");
        return new DoubleRotation(quaternion.X / norm, quaternion.Y / norm, quaternion.Z / norm, quaternion.W / norm);
    }

    public static DoubleRotation FromRotationVector(double[] vector)
    {
        double angle = Math.Sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
        double square = angle * angle;
        double scale = angle <= 1e-3
            ? 0.5 - square / 48.0 + square * square / 3840.0
            : Math.Sin(angle / 2.0) / angle;
        return new DoubleRotation(vector[0] * scale, vector[1] * scale, vector[2] * scale, Math.Cos(angle / 2.0));
    }

    public double[] ToRotationVector()
    {
        double x = X, y = Y, z = Z, w = W;
        if (w < 0.0 || (w == 0.0 && (x < 0.0 || (x == 0.0 && (y < 0.0 || (y == 0.0 && z < 0.0))))))
        {
            x = -x; y = -y; z = -z; w = -w;
        }
        double angle = 2.0 * Math.Atan2(Math.Sqrt(x * x + y * y + z * z), w);
        double square = angle * angle;
        double scale = angle <= 1e-3
            ? 2.0 + square / 12.0 + 7.0 * square * square / 2880.0
            : angle / Math.Sin(angle / 2.0);
        return [x * scale, y * scale, z * scale];
    }

    public static DoubleRotation FromMatrix(Matrix3x3 matrix) => FromMatrix(new double[,]
    {
        { matrix.M00, matrix.M01, matrix.M02 },
        { matrix.M10, matrix.M11, matrix.M12 },
        { matrix.M20, matrix.M21, matrix.M22 }
    });

    /// <summary>Matches SciPy 1.15 Rotation.from_matrix's Markley selection and normalization.</summary>
    public static DoubleRotation FromMatrix(double[,] matrix)
    {
        double trace = matrix[0, 0] + matrix[1, 1] + matrix[2, 2];
        double[] decision = [matrix[0, 0], matrix[1, 1], matrix[2, 2], trace];
        int choice = 0;
        for (int i = 1; i < 4; i++)
            if (decision[i] > decision[choice]) choice = i;
        double[] quaternion = new double[4];
        if (choice == 3)
        {
            quaternion[0] = matrix[2, 1] - matrix[1, 2];
            quaternion[1] = matrix[0, 2] - matrix[2, 0];
            quaternion[2] = matrix[1, 0] - matrix[0, 1];
            quaternion[3] = 1.0 + trace;
        }
        else
        {
            int i = choice, j = (i + 1) % 3, k = (j + 1) % 3;
            quaternion[i] = 1.0 - trace + 2.0 * matrix[i, i];
            quaternion[j] = matrix[j, i] + matrix[i, j];
            quaternion[k] = matrix[k, i] + matrix[i, k];
            quaternion[3] = matrix[k, j] - matrix[j, k];
        }
        return Normalize(new DoubleRotation(quaternion[0], quaternion[1], quaternion[2], quaternion[3]));
    }

    public double[,] ToMatrix()
    {
        double x2 = X * X, y2 = Y * Y, z2 = Z * Z, w2 = W * W;
        double xy = X * Y, zw = Z * W, xz = X * Z, yw = Y * W, yz = Y * Z, xw = X * W;
        return new double[,]
        {
            { x2 - y2 - z2 + w2, 2.0 * (xy - zw), 2.0 * (xz + yw) },
            { 2.0 * (xy + zw), -x2 + y2 - z2 + w2, 2.0 * (yz - xw) },
            { 2.0 * (xz - yw), 2.0 * (yz + xw), -x2 - y2 + z2 + w2 }
        };
    }

    public double[] Apply(double[] vector)
    {
        double[,] matrix = ToMatrix();
        return [
            matrix[0, 0] * vector[0] + matrix[0, 1] * vector[1] + matrix[0, 2] * vector[2],
            matrix[1, 0] * vector[0] + matrix[1, 1] * vector[1] + matrix[1, 2] * vector[2],
            matrix[2, 0] * vector[0] + matrix[2, 1] * vector[1] + matrix[2, 2] * vector[2]
        ];
    }

    public static DoubleRotation FromEulerXyz(double[] xyz)
    {
        DoubleRotation x = FromRotationVector([xyz[0], 0.0, 0.0]);
        DoubleRotation y = FromRotationVector([0.0, xyz[1], 0.0]);
        DoubleRotation z = FromRotationVector([0.0, 0.0, xyz[2]]);
        return z * y * x;
    }

    public double[] ToEulerXyz()
    {
        double[,] matrix = ToMatrix();
        double cy = Math.Sqrt(matrix[0, 0] * matrix[0, 0] + matrix[1, 0] * matrix[1, 0]);
        if (cy > 1e-7)
            return [Math.Atan2(matrix[2, 1], matrix[2, 2]), Math.Atan2(-matrix[2, 0], cy), Math.Atan2(matrix[1, 0], matrix[0, 0])];
        return [Math.Atan2(-matrix[1, 2], matrix[1, 1]), Math.Atan2(-matrix[2, 0], cy), 0.0];
    }
}
