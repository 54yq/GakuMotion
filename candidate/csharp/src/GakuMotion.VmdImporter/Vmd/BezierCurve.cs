namespace GakuMotion.VmdImporter.Vmd;

public static class BezierCurve
{
    private const float MachineEpsilon = 1.1920929e-7f; // np.finfo(np.float32).eps (2^-23)

    /// <summary>
    /// Unpacks 64-byte MMD Bézier curve payload into four channel curves (X, Y, Z, Rotation).
    /// </summary>
    public static (BezierControlPoints X, BezierControlPoints Y, BezierControlPoints Z, BezierControlPoints R)
        UnpackBoneBezier(ReadOnlySpan<byte> bytes, InterpolationLayout layout = InterpolationLayout.ChannelBlocks)
    {
        if (bytes.Length != 64)
            throw new ArgumentException($"Bézier curve byte buffer must be exactly 64 bytes, got {bytes.Length}", nameof(bytes));

        const float inv127 = 1.0f / 127.0f;

        switch (layout)
        {
            case InterpolationLayout.ChannelBlocks:
            {
                // Offsets 0, 16, 32, 48 with stride (0, 4, 8, 12)
                var x = new BezierControlPoints(
                    bytes[0] * inv127,
                    bytes[4] * inv127,
                    bytes[8] * inv127,
                    bytes[12] * inv127);
                var y = new BezierControlPoints(
                    bytes[16] * inv127,
                    bytes[20] * inv127,
                    bytes[24] * inv127,
                    bytes[28] * inv127);
                var z = new BezierControlPoints(
                    bytes[32] * inv127,
                    bytes[36] * inv127,
                    bytes[40] * inv127,
                    bytes[44] * inv127);
                var r = new BezierControlPoints(
                    bytes[48] * inv127,
                    bytes[52] * inv127,
                    bytes[56] * inv127,
                    bytes[60] * inv127);
                return (x, y, z, r);
            }
            case InterpolationLayout.Legacy:
            {
                // Legacy MMD curve byte layout across first 16 bytes:
                // 0..3:   X_x1, Y_x1, Z_x1, R_x1
                // 4..7:   X_y1, Y_y1, Z_y1, R_y1
                // 8..11:  X_x2, Y_x2, Z_x2, R_x2
                // 12..15: X_y2, Y_y2, Z_y2, R_y2
                var x = new BezierControlPoints(
                    bytes[0] * inv127,
                    bytes[4] * inv127,
                    bytes[8] * inv127,
                    bytes[12] * inv127);
                var y = new BezierControlPoints(
                    bytes[1] * inv127,
                    bytes[5] * inv127,
                    bytes[9] * inv127,
                    bytes[13] * inv127);
                var z = new BezierControlPoints(
                    bytes[2] * inv127,
                    bytes[6] * inv127,
                    bytes[10] * inv127,
                    bytes[14] * inv127);
                var r = new BezierControlPoints(
                    bytes[3] * inv127,
                    bytes[7] * inv127,
                    bytes[11] * inv127,
                    bytes[15] * inv127);
                return (x, y, z, r);
            }
            default:
                throw new ArgumentOutOfRangeException(nameof(layout), layout, "Unknown VMD interpolation layout");
        }
    }

    /// <summary>
    /// Solves parameter s in [0, 1] such that B_x(s) == x using a 20-step bisection solver in double precision.
    /// Strictly replicates solve_bezier_s from bezier.py.
    /// </summary>
    public static double SolveBezierS(double p1x, double p2x, double x, int iterations = 20)
    {
        double low = 0.0, high = 1.0;
        for (int i = 0; i < iterations; i++)
        {
            double mid = (low + high) * 0.5;
            double invS = 1.0 - mid;
            double bx = 3.0 * invS * invS * mid * p1x + 3.0 * invS * mid * mid * p2x + mid * mid * mid;
            if (bx < x)
                low = mid;
            else
                high = mid;
        }
        return (low + high) * 0.5;
    }

    /// <summary>
    /// Evaluates the cubic Bézier curve at normalized time x in [0, 1].
    /// Returns the normalized interpolated value y in [0, 1].
    /// Strictly replicates eval_bezier from bezier.py.
    /// </summary>
    public static double EvalBezier(double p1x, double p1y, double p2x, double p2y, double x)
    {
        if (x <= 0.0)
            return 0.0;
        if (x >= 1.0)
            return 1.0;

        double s = SolveBezierS(p1x, p2x, x, 20);
        double invS = 1.0 - s;
        double by = 3.0 * invS * invS * s * p1y + 3.0 * invS * s * s * p2y + s * s * s;
        return by;
    }

    /// <summary>
    /// Evaluates the cubic Bézier curve from BezierControlPoints at normalized time x.
    /// </summary>
    public static float EvalBezier(BezierControlPoints cp, float x)
    {
        return (float)EvalBezier(cp.P1X, cp.P1Y, cp.P2X, cp.P2Y, x);
    }

    /// <summary>
    /// Evaluates a scalar source F-curve with absolute float32 handles using a 52-step bisection root solver in double precision.
    /// Strictly replicates _native_component_values from source_native_fk.py.
    /// </summary>
    public static float EvaluateNativeComponentValue(
        float begin,
        float end,
        float startFrame,
        float endFrame,
        BezierControlPoints curve,
        float time)
    {
        Span<float> result = stackalloc float[1];
        Span<float> times = stackalloc float[1] { time };
        EvaluateNativeComponentValues(begin, end, startFrame, endFrame, curve, times, result);
        return result[0];
    }

    /// <summary>
    /// Evaluates a scalar source F-curve across multiple frame times using the 52-step bisection root solver.
    /// Strictly replicates _native_component_values from source_native_fk.py.
    /// </summary>
    public static void EvaluateNativeComponentValues(
        float begin,
        float end,
        float startFrame,
        float endFrame,
        BezierControlPoints curve,
        ReadOnlySpan<float> times,
        Span<float> results)
    {
        if (results.Length < times.Length)
            throw new ArgumentException("Results buffer is smaller than times count", nameof(results));

        float duration = endFrame - startFrame;
        if (duration <= 0.0f)
        {
            results[..times.Length].Fill(end);
            return;
        }

        float raw0 = (float)Math.Round(curve.P1X * 127.0, MidpointRounding.ToEven);
        float raw1 = (float)Math.Round(curve.P1Y * 127.0, MidpointRounding.ToEven);
        float raw2 = (float)Math.Round(curve.P2X * 127.0, MidpointRounding.ToEven);
        float raw3 = (float)Math.Round(curve.P2Y * 127.0, MidpointRounding.ToEven);

        if (!float.IsFinite(raw0) || !float.IsFinite(raw1) || !float.IsFinite(raw2) || !float.IsFinite(raw3))
            throw new ArgumentException("Native interpolation curves require four finite controls", nameof(curve));

        if (raw0 == raw1 && raw2 == raw3)
        {
            for (int i = 0; i < times.Length; i++)
            {
                results[i] = begin + (end - begin) * ((times[i] - startFrame) / duration);
            }
            return;
        }

        float dx = duration / 127.0f;
        float dy = (end - begin) / 127.0f;
        float x1 = startFrame + dx * raw0;
        float x2 = startFrame + dx * raw2;
        float y1 = begin + dy * raw1;
        float y2 = begin + dy * raw3;

        // A single handle extending outside its adjacent key's span is shortened in both coordinates.
        float distance1 = MathF.Abs(x1 - startFrame);
        if (distance1 > duration)
        {
            float factor = duration / distance1;
            y1 = begin + factor * (y1 - begin);
            x1 = startFrame + factor * (x1 - startFrame);
        }

        float distance2 = MathF.Abs(endFrame - x2);
        if (distance2 > duration)
        {
            float factor = duration / distance2;
            y2 = end + factor * (y2 - end);
            x2 = endFrame + factor * (x2 - endFrame);
        }

        if (MathF.Abs(begin - end) < MachineEpsilon &&
            MathF.Abs(y1 - y2) < MachineEpsilon &&
            MathF.Abs(y2 - end) < MachineEpsilon)
        {
            results[..times.Length].Fill(begin);
            return;
        }

        double c1 = (double)(3.0f * (x1 - startFrame));
        double c2 = (double)(3.0f * ((startFrame - 2.0f * x1) + x2));
        double c3 = (double)((endFrame - startFrame) + 3.0f * (x1 - x2));

        float y0 = begin;
        float yC1 = 3.0f * (y1 - begin);
        float yC2 = 3.0f * ((begin - 2.0f * y1) + y2);
        float yC3 = (end - begin) + 3.0f * (y1 - y2);

        for (int i = 0; i < times.Length; i++)
        {
            double c0 = (double)startFrame - (double)times[i];
            double low = 0.0;
            double high = 1.0;
            for (int iter = 0; iter < 52; iter++)
            {
                double middle = (low + high) * 0.5;
                double value = ((c3 * middle + c2) * middle + c1) * middle + c0;
                if (value < 0.0)
                    low = middle;
                else
                    high = middle;
            }
            float parameter = (float)((low + high) * 0.5);

            results[i] = ((y0 + parameter * yC1) + (parameter * parameter) * yC2)
                         + ((parameter * parameter) * parameter) * yC3;
        }
    }

    /// <summary>
    /// Evaluates a scalar source F-curve and returns a newly allocated array.
    /// </summary>
    public static float[] EvaluateNativeComponentValues(
        float begin,
        float end,
        float startFrame,
        float endFrame,
        BezierControlPoints curve,
        ReadOnlySpan<float> times)
    {
        float[] result = new float[times.Length];
        EvaluateNativeComponentValues(begin, end, startFrame, endFrame, curve, times, result);
        return result;
    }
}
