namespace GakuMotion.VmdImporter.Core;

/// <summary>
/// Scalar transcription of the frozen NumPy 1.26.4 float32 sin/cos operation
/// sequence. Preserves its range reduction, polynomial coefficients and FMA
/// rounding without vectorization or a change in evaluation algorithm.
/// Source: numpy/core/src/umath/loops_trigonometric.dispatch.c.src (v1.26.4).
/// NumPy BSD license: third_party/numpy/LICENSE.txt.
/// </summary>
internal static class NativeFloatTrig
{
    internal static float Sin(float value) => Evaluate(value, cosine: false);
    internal static float Cos(float value) => Evaluate(value, cosine: true);

    private static float Bits(uint value) => BitConverter.UInt32BitsToSingle(value);

    private static float Evaluate(float value, bool cosine)
    {
        if (float.IsNaN(value)) return float.NaN;
        if (MathF.Abs(value) > (cosine ? 71476.0625f : 117435.992f))
            return cosine ? MathF.Cos(value) : MathF.Sin(value);

        float quadrant = value * Bits(0x3f22f983);
        quadrant += 12582912.0f;
        quadrant -= 12582912.0f;
        float reduced = MathF.FusedMultiplyAdd(quadrant, Bits(0xbfc90fd8), value);
        reduced = MathF.FusedMultiplyAdd(quadrant, Bits(0xb4a8885a), reduced);
        reduced = MathF.FusedMultiplyAdd(quadrant, Bits(0xa7c234c4), reduced);
        float square = reduced * reduced;

        float cos = MathF.FusedMultiplyAdd(Bits(0x37cc730b), square, Bits(0xbab6036e));
        cos = MathF.FusedMultiplyAdd(cos, square, Bits(0x3d2aaa9e));
        cos = MathF.FusedMultiplyAdd(cos, square, -.5f);
        cos = MathF.FusedMultiplyAdd(cos, square, 1.0f);

        float sin = MathF.FusedMultiplyAdd(Bits(0x363e9dde), square, Bits(0xb95035dd));
        sin = MathF.FusedMultiplyAdd(sin, square, Bits(0x3c0888cd));
        sin = MathF.FusedMultiplyAdd(sin, square, Bits(0xbe2aaaab));
        sin = MathF.FusedMultiplyAdd(sin, square, 0.0f);
        sin = MathF.FusedMultiplyAdd(sin, reduced, reduced);

        int index = (int)quadrant + (cosine ? 1 : 0);
        float result = (index & 1) == 0 ? sin : cos;
        return (index & 2) == 2 ? 0.0f - result : result;
    }
}
