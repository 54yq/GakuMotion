namespace GakuMotion.VmdImporter.Native.Facial;

using System.Text.Json;

/// <summary>
/// Fixed schema-2 global RBF fitted offline from source/target tooth geometry.
/// Every center contributes continuously, without nearest-neighbor or exact-hit
/// support changes. This appearance model does not recover absent authored tracks.
/// </summary>
internal sealed class ToothAppearanceLut
{
    private const string ResourceName = "GakuMotion.VmdImporter.Native.Resources.tooth_appearance_lut.json";
    private const int Dimensions = 5;
    private const string Channel026 = "b_mouth.mouth_026";
    private const string Channel028 = "b_mouth.mouth_028";
    private readonly double[] _centers;
    private readonly double[] _coefficients;
    private readonly double[] _polynomial;
    private readonly double _epsilon;
    private readonly double _envelopeLow;
    private readonly double _envelopeHigh;
    private static ToothAppearanceLut? _default;

    public string DefaultChannel { get; }
    public int KnotCount { get; }
    public int CenterCount => _centers.Length / Dimensions;
    public int SchemaVersion => 2;
    public string Kernel { get; }
    public static ToothAppearanceLut Default => _default ??= Load();

    private ToothAppearanceLut(string channel, int knots, string kernel, double epsilon,
        double[] centers, double[] coefficients, double[] polynomial, double low, double high)
    {
        DefaultChannel = channel;
        KnotCount = knots;
        Kernel = kernel;
        _epsilon = epsilon;
        _centers = centers;
        _coefficients = coefficients;
        _polynomial = polynomial;
        _envelopeLow = low;
        _envelopeHigh = high;
    }

    public static ToothAppearanceLut Load()
    {
        using Stream stream = typeof(ToothAppearanceLut).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException("embedded resource missing: " + ResourceName);
        using var doc = JsonDocument.Parse(stream);
        JsonElement root = doc.RootElement;
        if (root.GetProperty("schema_version").GetInt32() != 2)
            throw new InvalidOperationException("Tooth appearance LUT requires schema_version 2; legacy interpolation is unsupported.");
        string channel = root.GetProperty("default_channel").GetString() ?? string.Empty;
        if (channel != Channel026 && channel != Channel028)
            throw new InvalidOperationException("Invalid default tooth channel.");
        JsonElement knots = root.GetProperty("knots");
        if (knots.ValueKind != JsonValueKind.Array)
            throw new InvalidOperationException("Tooth LUT knots must retain the original anchor array.");
        JsonElement interpolation = root.GetProperty("interpolation");
        if (interpolation.GetProperty("method").GetString() != "global_rbf" ||
            interpolation.GetProperty("scale").GetString() != "raw")
            throw new InvalidOperationException("Tooth LUT requires global_rbf with raw coordinates.");
        RequireOrder(interpolation.GetProperty("input_order"), new[] { "A", "I", "U", "E", "O" });
        RequireOrder(interpolation.GetProperty("output_order"), new[] { Channel026, Channel028 });
        JsonElement clip = interpolation.GetProperty("input_clip");
        if (clip.ValueKind != JsonValueKind.Array || clip.GetArrayLength() != 2 ||
            Finite(clip[0]) != 0.0 || Finite(clip[1]) != 1.0)
            throw new InvalidOperationException("Tooth LUT input_clip must be [0,1].");
        string kernel = interpolation.GetProperty("kernel").GetString() ?? string.Empty;
        if (kernel is not ("linear" or "thin_plate_spline" or "cubic" or "gaussian"))
            throw new InvalidOperationException("Unsupported tooth RBF kernel: " + kernel);
        double epsilon = Finite(interpolation.GetProperty("epsilon"));
        if (epsilon <= 0.0) throw new InvalidOperationException("Tooth RBF epsilon must be positive.");
        if (interpolation.TryGetProperty("smoothing", out JsonElement smoothing) && Finite(smoothing) < 0.0)
            throw new InvalidOperationException("Tooth RBF smoothing must be nonnegative.");
        JsonElement centerRows = interpolation.GetProperty("centers");
        if (centerRows.ValueKind != JsonValueKind.Array || centerRows.GetArrayLength() == 0)
            throw new InvalidOperationException("Tooth RBF requires at least one center.");
        int count = centerRows.GetArrayLength();
        double[] centers = ReadMatrix(centerRows, count, Dimensions, "centers");
        foreach (double coordinate in centers)
            if (coordinate < 0.0 || coordinate > 1.0)
                throw new InvalidOperationException("Tooth RBF centers must lie in the normalized vowel cube.");
        double[] coefficients = ReadMatrix(interpolation.GetProperty("coefficients"), count, 2, "coefficients");
        double[] polynomial = ReadMatrix(interpolation.GetProperty("polynomial"), Dimensions + 1, 2, "polynomial");
        JsonElement envelope = interpolation.GetProperty("envelope");
        double low = Finite(envelope.GetProperty("low"));
        double high = Finite(envelope.GetProperty("high"));
        if (low < 0.0 || high > 1.0 || low >= high)
            throw new InvalidOperationException("Tooth envelope requires 0 <= low < high <= 1.");
        return new ToothAppearanceLut(channel, knots.GetArrayLength(), kernel, epsilon,
            centers, coefficients, polynomial, low, high);
    }

    private static double Finite(JsonElement value)
    {
        double number = value.GetDouble();
        if (!double.IsFinite(number)) throw new InvalidOperationException("Tooth LUT contains a non-finite number.");
        return number;
    }

    private static void RequireOrder(JsonElement values, string[] expected)
    {
        if (values.ValueKind != JsonValueKind.Array || values.GetArrayLength() != expected.Length)
            throw new InvalidOperationException("Invalid tooth LUT channel ordering.");
        for (int i = 0; i < expected.Length; i++)
            if (values[i].GetString() != expected[i])
                throw new InvalidOperationException("Invalid tooth LUT channel ordering.");
    }

    private static double[] ReadMatrix(JsonElement rows, int rowCount, int columns, string name)
    {
        if (rows.ValueKind != JsonValueKind.Array || rows.GetArrayLength() != rowCount)
            throw new InvalidOperationException("Invalid tooth LUT " + name + " row count.");
        var result = new double[checked(rowCount * columns)];
        for (int row = 0; row < rowCount; row++)
        {
            if (rows[row].ValueKind != JsonValueKind.Array || rows[row].GetArrayLength() != columns)
                throw new InvalidOperationException("Invalid tooth LUT " + name + " column count.");
            for (int column = 0; column < columns; column++)
                result[row * columns + column] = Finite(rows[row][column]);
        }
        return result;
    }

    /// <summary>Inputs are float32 [frames,5] in A,I,U,E,O order; RBF arithmetic is double.</summary>
    public float[] Evaluate(float[] vowels, int frames, string? channel)
    {
        ArgumentNullException.ThrowIfNull(vowels);
        if (frames < 0 || (long)frames * Dimensions != vowels.Length)
            throw new ArgumentException("Tooth inputs must have shape [frames,5].", nameof(vowels));
        string target = string.IsNullOrEmpty(channel) ? DefaultChannel : channel;
        int outputIndex = target == Channel026 ? 0 : target == Channel028 ? 1
            : throw new ArgumentException("Unsupported tooth output channel.", nameof(channel));
        var output = new float[frames];
        Span<double> x = stackalloc double[Dimensions];
        for (int frame = 0; frame < frames; frame++)
        {
            double maxVowel = 0.0;
            for (int d = 0; d < Dimensions; d++)
            {
                float value = vowels[frame * Dimensions + d];
                if (!float.IsFinite(value))
                    throw new ArgumentException("Tooth inputs must be finite.", nameof(vowels));
                x[d] = Math.Clamp((double)value, 0.0, 1.0);
                maxVowel = Math.Max(maxVowel, x[d]);
            }
            if (maxVowel <= _envelopeLow) continue;
            double raw = _polynomial[outputIndex];
            for (int d = 0; d < Dimensions; d++)
                raw += x[d] * _polynomial[(d + 1) * 2 + outputIndex];
            for (int center = 0; center < CenterCount; center++)
            {
                double distanceSquared = 0.0;
                for (int d = 0; d < Dimensions; d++)
                {
                    double difference = x[d] - _centers[center * Dimensions + d];
                    distanceSquared += difference * difference;
                }
                double basis = Kernel switch
                {
                    "linear" => -Math.Sqrt(distanceSquared),
                    "thin_plate_spline" => distanceSquared > 0.0 ? 0.5 * distanceSquared * Math.Log(distanceSquared) : 0.0,
                    "cubic" => distanceSquared * Math.Sqrt(distanceSquared),
                    _ => Math.Exp(-_epsilon * _epsilon * distanceSquared)
                };
                raw += basis * _coefficients[center * 2 + outputIndex];
            }
            double valueOut = Math.Clamp(raw, 0.0, 1.0);
            if (maxVowel < _envelopeHigh)
            {
                double u = (maxVowel - _envelopeLow) / (_envelopeHigh - _envelopeLow);
                valueOut *= u * u * (3.0 - 2.0 * u);
            }
            output[frame] = (float)valueOut;
        }
        return output;
    }
}
