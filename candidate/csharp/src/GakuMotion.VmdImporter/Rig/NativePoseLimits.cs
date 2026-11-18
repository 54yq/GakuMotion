namespace GakuMotion.VmdImporter.Rig;

using GakuMotion.VmdImporter.Core;

/// <summary>Float32 pose-space constraints from source_native_fk.limit_native_pose.</summary>
internal static class NativePoseLimits
{
    internal static AffineMatrix4x4 Apply(AffineMatrix4x4 pose, IReadOnlyList<PoseRotationLimit> constraints)
    {
        foreach (var constraint in constraints)
        {
            if (constraint.Type != "LIMIT_ROTATION" || constraint.Mute || constraint.Influence == 0) continue;
            if (constraint.Influence != 1 || constraint.OwnerSpace != "POSE")
                throw new InvalidOperationException("Numeric source FK supports full pose-space rotation limits");
            float[,] rotation = new float[3, 3];
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) rotation[r, c] = pose[r, c];
            RemoveYShear(rotation);
            bool[] enabled = [constraint.UseLimitX, constraint.UseLimitY, constraint.UseLimitZ];
            if (enabled.Any(value => value))
            {
                float[] lengths = new float[3];
                for (int c = 0; c < 3; c++)
                {
                    lengths[c] = Norm(rotation, c);
                    if (lengths[c] <= 1e-20f) throw new InvalidOperationException("A native pose limit cannot decompose a zero scale");
                    for (int r = 0; r < 3; r++) rotation[r, c] /= lengths[c];
                }
                float[] angles = NearOriginXyz(rotation);
                float[] minima = [constraint.MinX, constraint.MinY, constraint.MinZ];
                float[] maxima = [constraint.MaxX, constraint.MaxY, constraint.MaxZ];
                for (int axis = 0; axis < 3; axis++)
                {
                    if (!enabled[axis]) continue;
                    if (!float.IsFinite(minima[axis]) || !float.IsFinite(maxima[axis]) || minima[axis] > maxima[axis])
                        throw new InvalidOperationException("Invalid native pose rotation limits");
                    angles[axis] = Math.Clamp(angles[axis], minima[axis], maxima[axis]);
                }
                float sx = NativeFloatTrig.Sin(angles[0]), sy = NativeFloatTrig.Sin(angles[1]), sz = NativeFloatTrig.Sin(angles[2]);
                float cx = NativeFloatTrig.Cos(angles[0]), cy = NativeFloatTrig.Cos(angles[1]), cz = NativeFloatTrig.Cos(angles[2]);
                rotation = new float[,]
                {
                    {cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz},
                    {cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz},
                    {-sy, sx * cy, cx * cy},
                };
                for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) rotation[r, c] *= lengths[c];
            }
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) pose[r, c] = rotation[r, c];
        }
        return pose;
    }

    private static float Norm(float[,] matrix, int column) => MathF.Sqrt(
        (matrix[0, column] * matrix[0, column] + matrix[1, column] * matrix[1, column]) + matrix[2, column] * matrix[2, column]);

    private static float Dot(float[,] matrix, int a, int b) =>
        (matrix[0, a] * matrix[0, b] + matrix[1, a] * matrix[1, b]) + matrix[2, a] * matrix[2, b];

    private static void RemoveYShear(float[,] matrix)
    {
        double square = Dot(matrix, 1, 1);
        if (square <= 1e-20) throw new InvalidOperationException("A native pose limit requires a nonzero Y axis");
        foreach (int c in new[] { 0, 2 })
        {
            float projection = (float)(Dot(matrix, 1, c) / square);
            for (int r = 0; r < 3; r++) matrix[r, c] -= matrix[r, 1] * projection;
        }
        float l0 = Norm(matrix, 0), l2 = Norm(matrix, 2);
        if (Math.Min(l0, l2) <= 1e-20) throw new InvalidOperationException("Degenerate secondary axes in native pose limit");
        float[,] directions = new float[3, 2];
        for (int r = 0; r < 3; r++) { directions[r, 0] = matrix[r, 0] / l0; directions[r, 1] = matrix[r, 2] / l2; }
        double cosine = (directions[0, 0] * directions[0, 1] + directions[1, 0] * directions[1, 1]) + directions[2, 0] * directions[2, 1];
        if (1e-4 < Math.Abs(cosine) && Math.Abs(cosine) < 1 - 1.1920929e-7)
        {
            double[,] input = new double[3, 2];
            for (int r = 0; r < 3; r++) for (int c = 0; c < 2; c++) input[r, c] = directions[r, c];
            SourceNativeIK.Svd3xN(input, out var left, out _, out var right);
            float areaScale = (float)Math.Pow(Math.Max(0, 1 - cosine * cosine), .25);
            for (int r = 0; r < 3; r++) for (int c = 0; c < 2; c++)
            {
                double perpendicular = left[r, 0] * right[0, c] + left[r, 1] * right[1, c];
                matrix[r, c == 0 ? 0 : 2] = (float)(perpendicular * (c == 0 ? l0 : l2) * areaScale);
            }
        }
    }

    private static float[] NearOriginXyz(float[,] matrix)
    {
        float cy = (float)Math.Sqrt((double)matrix[0, 0] * matrix[0, 0] + (double)matrix[1, 0] * matrix[1, 0]);
        float[] angles = cy > 16 * 1.1920929e-7f
            ? [MathF.Atan2(matrix[2, 1], matrix[2, 2]), MathF.Atan2(-matrix[2, 0], cy), MathF.Atan2(matrix[1, 0], matrix[0, 0])]
            : [MathF.Atan2(-matrix[1, 2], matrix[1, 1]), MathF.Atan2(-matrix[2, 0], cy), 0];
        double[] alternate = [angles[0] + Math.PI, Math.PI - angles[1], angles[2] + Math.PI];
        float[] wrapped = alternate.Select(value => (float)(((value + Math.PI) % (2 * Math.PI) + 2 * Math.PI) % (2 * Math.PI) - Math.PI)).ToArray();
        return wrapped.Sum(value => MathF.Abs(value)) < angles.Sum(value => MathF.Abs(value)) ? wrapped : angles;
    }
}
