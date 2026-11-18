namespace GakuMotion.VmdImporter.Native.Facial;

internal static class MikuRawMouthSource
{
    internal const string Prefix = "miku.rawmouth.v1.";
    internal static readonly string[] InputNames = { "a", "i", "u", "e", "o", "∧", "口角上げ" };
    internal static readonly string[] Names = InputNames.Select(name => Prefix + name)
        .Concat(new[] { Prefix + "present", Prefix + "unsupported", Prefix + "native_authored" }).ToArray();

    internal static int CanonicalInput(string name)
    {
        int vowel = MikuRawSource.CanonicalVowel(name);
        if (vowel >= 0) return vowel;
        return GakumasMorphMap.Normalize(name) switch { "∧" => 5, "口角上げ" => 6, _ => -1 };
    }

    internal static bool Append(Dictionary<string, float[]> channels,
        Dictionary<string, float[]> sourceSamples, GakumasMorphMap map, int frames)
    {
        var groups = Enumerable.Range(0, InputNames.Length).Select(_ => new List<string>()).ToArray();
        foreach (string name in sourceSamples.Keys)
        {
            int index = CanonicalInput(name);
            if (index >= 0) groups[index].Add(name);
        }
        if (!groups.Any(group => group.Count != 0)) return false;

        var raw = groups.Select(group => group.Count == 0 ? new float[frames]
            : (float[])sourceSamples[group[0]].Clone()).ToArray();
        var unsupported = new float[frames];
        bool nativeAuthored = false;
        foreach (var pair in sourceSamples)
        {
            string normalized = GakumasMorphMap.Normalize(pair.Key).ToLowerInvariant();
            nativeAuthored |= normalized.StartsWith("b_mouth.", StringComparison.Ordinal) || normalized == "tooth";
            int input = CanonicalInput(pair.Key);
            if (input >= 0)
            {
                for (int frame = 0; frame < frames; frame++)
                {
                    float value = pair.Value[frame];
                    if (!float.IsFinite(value) || value < 0 || value > 1) unsupported[frame] = 1;
                    else if (groups[input].Count > 1) unsupported[frame] = Math.Max(unsupported[frame], value);
                }
                continue;
            }
            if (MikuRawEyeKeys.IndexOf(pair.Key) >= 0 || MikuRawEyeSource.IsNativeEyeChannel(pair.Key)) continue;
            bool resolved = map.TryResolve(pair.Key, out var targets);
            bool blocked = !resolved || targets.Count == 0 || targets.Any(target =>
                target.Name.StartsWith("b_mouth.", StringComparison.Ordinal) ||
                target.Name is "mmd2.a" or "mmd2.i" or "mmd2.u" or "mmd2.e" or "mmd2.o");
            if (!blocked) continue;
            for (int frame = 0; frame < frames; frame++)
                unsupported[frame] = Math.Max(unsupported[frame], float.IsFinite(pair.Value[frame])
                    ? Math.Clamp(Math.Abs(pair.Value[frame]), 0, 1) : 1);
        }
        for (int channel = 0; channel < InputNames.Length; channel++)
        {
            for (int frame = 0; frame < frames; frame++)
                raw[channel][frame] = float.IsFinite(raw[channel][frame]) ? Math.Clamp(raw[channel][frame], 0, 1) : 0;
            channels[Names[channel]] = raw[channel];
        }
        channels[Names[7]] = Enumerable.Repeat(1f, frames).ToArray();
        channels[Names[8]] = unsupported;
        channels[Names[9]] = Enumerable.Repeat(nativeAuthored ? 1f : 0f, frames).ToArray();
        return true;
    }
}
