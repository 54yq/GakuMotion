namespace GakuMotion.VmdImporter.Native.Facial;

/// <summary>Reserved, versioned raw-vowel intent; never inferred from mapped curves.</summary>
internal static class MikuRawSource
{
    internal const string Prefix = "miku.raw.v1.";
    internal static readonly string[] Names = { Prefix+"a", Prefix+"i", Prefix+"u", Prefix+"e", Prefix+"o",
        Prefix+"present", Prefix+"unsupported", Prefix+"native_authored" };

    internal static int CanonicalVowel(string name) => GakumasMorphMap.Normalize(name).ToLowerInvariant() switch
    {
        "あ" or "a" => 0, "い" or "i" => 1, "う" or "u" => 2,
        "え" or "e" => 3, "お" or "o" => 4, _ => -1
    };

    internal static bool Append(Dictionary<string, float[]> channels,
        Dictionary<string, float[]> sourceSamples, GakumasMorphMap map, int frames,
        out bool nativeAuthored, out int invalidFrames, out List<string> unsupportedNames)
    {
        nativeAuthored = false; invalidFrames = 0; unsupportedNames = new();
        var groups = new List<string>[5];
        for (int i=0; i<5; i++) groups[i] = new();
        foreach (string name in sourceSamples.Keys)
        {
            int vowel = CanonicalVowel(name);
            if (vowel >= 0) groups[vowel].Add(name);
        }
        if (!groups.Any(group => group.Count != 0)) return false;
        var raw = new float[5][];
        var unsupported = new float[frames];
        var invalid = new bool[frames];
        for (int i=0; i<5; i++)
        {
            raw[i] = groups[i].Count == 0 ? new float[frames] : (float[])sourceSamples[groups[i][0]].Clone();
            if (groups[i].Count > 1)
                foreach (string name in groups[i])
                    for (int f=0; f<frames; f++) unsupported[f] = Math.Max(unsupported[f], Math.Abs(sourceSamples[name][f]));
        }
        foreach (var pair in sourceSamples)
        {
            string normalized = GakumasMorphMap.Normalize(pair.Key).ToLowerInvariant();
            if (normalized.StartsWith("b_mouth.", StringComparison.Ordinal) || normalized == "tooth") nativeAuthored = true;
            int vowel = CanonicalVowel(pair.Key);
            if (vowel >= 0)
            {
                for (int f=0; f<frames; f++) invalid[f] |= pair.Value[f] < 0 || pair.Value[f] > 1;
                continue;
            }
            bool resolved = map.TryResolve(pair.Key, out var targets);
            bool blocked = !resolved || targets.Count == 0 || targets.Any(target =>
                target.Name.StartsWith("b_mouth.", StringComparison.Ordinal) ||
                target.Name is "mmd2.a" or "mmd2.i" or "mmd2.u" or "mmd2.o");
            if (blocked)
            {
                unsupportedNames.Add(pair.Key);
                for (int f=0; f<frames; f++) unsupported[f] = Math.Max(unsupported[f], Math.Abs(pair.Value[f]));
            }
        }
        for (int f=0; f<frames; f++)
        {
            if (invalid[f]) { unsupported[f] = 1; invalidFrames++; }
            unsupported[f] = Math.Clamp(unsupported[f], 0, 1);
            for (int i=0; i<5; i++) raw[i][f] = Math.Clamp(raw[i][f], 0, 1);
        }
        for (int i=0; i<5; i++) channels[Names[i]] = raw[i];
        channels[Names[5]] = Enumerable.Repeat(1f, frames).ToArray();
        channels[Names[6]] = unsupported;
        channels[Names[7]] = Enumerable.Repeat(nativeAuthored ? 1f : 0f, frames).ToArray();
        return true;
    }
}
