namespace GakuMotion.VmdImporter.Native.Facial;

using GakuMotion.VmdImporter.Vmd;

/// <summary>
/// Port of morph_translation.sample_morph_keyframes.
///
/// MMD morph keys carry no Bezier payload, so the golden sampler is a plain
/// np.interp over integer frame ticks: endpoint hold, linear inside, and
/// duplicate authoring frames resolved to the LAST key in file order (the
/// Python dict assignment). Evaluating in float64 and narrowing at the end
/// keeps the C# result inside the float32 contract of the emitted channel.
/// </summary>
internal static class MorphCurve
{
    public static float[] Sample(IReadOnlyList<MorphKeyframe>? keyframes, int totalFrames)
    {
        var result = new float[Math.Max(0, totalFrames)];
        if (keyframes == null || keyframes.Count == 0 || totalFrames <= 0) return result;

        // Frame -> weight, last occurrence wins, then ascending frames.
        var byFrame = new SortedDictionary<uint, double>();
        foreach (MorphKeyframe key in keyframes)
        {
            if (!float.IsFinite(key.Weight))
                throw new InvalidDataException($"invalid morph key: {key.Name}@{key.Frame}");
            byFrame[key.Frame] = key.Weight;
        }

        int n = byFrame.Count;
        var ticks = new double[n];
        var values = new double[n];
        int index = 0;
        foreach (KeyValuePair<uint, double> pair in byFrame)
        {
            ticks[index] = pair.Key;
            values[index] = pair.Value;
            index++;
        }

        for (int f = 0; f < totalFrames; f++)
        {
            double query = f;
            if (query <= ticks[0])
            {
                result[f] = (float)values[0];
                continue;
            }
            if (query >= ticks[n - 1])
            {
                result[f] = (float)values[n - 1];
                continue;
            }

            // last tick <= query
            int low = 0, high = n - 1;
            while (low <= high)
            {
                int mid = (low + high) >> 1;
                if (ticks[mid] <= query) low = mid + 1;
                else high = mid - 1;
            }
            int j = Math.Clamp(high, 0, n - 2);
            double slope = (values[j + 1] - values[j]) / (ticks[j + 1] - ticks[j]);
            result[f] = (float)(slope * (query - ticks[j]) + values[j]);
        }

        return result;
    }
}
