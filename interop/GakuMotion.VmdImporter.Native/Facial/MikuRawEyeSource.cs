namespace GakuMotion.VmdImporter.Native.Facial;

using System.Text.Json;

/// <summary>
/// The fitted eye/brow key set, loaded from the packer's projection of the
/// published table (tools/vmd2gkmotion/miku_raw_eye_keys.json).
///
/// Why an embedded table instead of the morph map: fifteen of the thirty krnh
/// keys have no entry in gakumas_morph_map.json at all, and eight of those are
/// accepted keys. Resolving "which source morphs are fitted eye inputs" from
/// the mapping table is therefore impossible; the packer projects it straight
/// out of the .krf it just wrote, so the importer cannot carry a key set that
/// drifted from the numbers the runtime actually evaluates.
/// </summary>
internal static class MikuRawEyeKeys
{
    private const string ResourceName = "GakuMotion.VmdImporter.Native.Resources.miku_raw_eye_keys.json";

    internal const string Prefix = "miku.raweye.v1.";
    internal static readonly string[] Markers = { Prefix + "present", Prefix + "unsupported", Prefix + "native_authored" };

    private static IReadOnlyList<string>? _names;
    private static IReadOnlyDictionary<string, int>? _index;

    /// <summary>Key names in packed-table order; never sorted here.</summary>
    internal static IReadOnlyList<string> Names => _names ??= Load();

    internal static int IndexOf(string name)
    {
        _index ??= Names
            .Select((key, index) => (key, index))
            .ToDictionary(pair => pair.key, pair => pair.index, StringComparer.Ordinal);
        return _index.TryGetValue(GakumasMorphMap.Normalize(name), out int index) ? index : -1;
    }

    private static IReadOnlyList<string> Load()
    {
        var assembly = typeof(MikuRawEyeKeys).Assembly;
        using Stream stream = assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException("embedded resource missing: " + ResourceName);
        using var doc = JsonDocument.Parse(stream);
        JsonElement root = doc.RootElement;

        if (!root.TryGetProperty("prefix", out JsonElement prefix) || prefix.GetString() != Prefix)
            throw new InvalidDataException("miku_raw_eye_keys.json carries an unexpected prefix");
        if (!root.TryGetProperty("keys", out JsonElement keys) || keys.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("miku_raw_eye_keys.json has no key array");

        var names = new List<string>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (JsonElement item in keys.EnumerateArray())
        {
            string? name = item.GetString();
            if (string.IsNullOrEmpty(name)) throw new InvalidDataException("empty eye key");
            string normalized = GakumasMorphMap.Normalize(name);
            if (!seen.Add(normalized)) throw new InvalidDataException("duplicate eye key: " + normalized);
            names.Add(normalized);
        }
        if (names.Count == 0) throw new InvalidDataException("miku_raw_eye_keys.json has no keys");
        return names;
    }
}

/// <summary>
/// Reserved, versioned raw eye/brow intent. Emitted verbatim next to the mapped
/// curves so the published fit can be evaluated without guessing which of the
/// thirty keys a clip actually drove.
/// </summary>
internal static class MikuRawEyeSource
{
    internal const string Prefix = MikuRawEyeKeys.Prefix;

    internal static bool IsNativeEyeChannel(string name)
    {
        string normalized = GakumasMorphMap.Normalize(name);
        return normalized.StartsWith("b_eye.", StringComparison.Ordinal) ||
               normalized.StartsWith("b_brow.", StringComparison.Ordinal);
    }

    /// <summary>
    /// Adds `miku.raweye.v1.&lt;key&gt;` for every fitted key the clip drives, plus
    /// the three markers. Returns false (and writes nothing) when the clip has
    /// no fitted eye source at all, so a legacy clip keeps its native mapping.
    /// </summary>
    internal static bool Append(Dictionary<string, float[]> channels,
        Dictionary<string, float[]> sourceSamples, GakumasMorphMap map, int frames,
        out bool nativeAuthored, out List<string> unsupportedNames, out int collapsedKeys)
    {
        nativeAuthored = false;
        unsupportedNames = new();
        collapsedKeys = 0;

        IReadOnlyList<string> keys = MikuRawEyeKeys.Names;
        // Key -> the source tracks that landed on it. More than one is an alias
        // pair the fit cannot represent without losing authored intent.
        var groups = new List<string>?[keys.Count];
        var unsupported = new float[frames];

        foreach (var pair in sourceSamples)
        {
            if (IsNativeEyeChannel(pair.Key)) nativeAuthored = true;
            int index = MikuRawEyeKeys.IndexOf(pair.Key);
            if (index >= 0)
            {
                (groups[index] ??= new()).Add(pair.Key);
                continue;
            }
            // An authored eye/brow source the table cannot host would be dropped
            // by the wholesale family replacement, so refuse the clip instead.
            if (!map.TryResolve(pair.Key, out IReadOnlyList<MorphTarget> targets)) continue;
            bool eyeFamily = targets.Any(target =>
                target.Name.StartsWith("b_eye.", StringComparison.Ordinal) ||
                target.Name.StartsWith("b_brow.", StringComparison.Ordinal) ||
                target.Name == "mmd2.smile_eyes");
            if (!eyeFamily) continue;
            unsupportedNames.Add(pair.Key);
            for (int f = 0; f < frames; f++) unsupported[f] = Math.Max(unsupported[f], Math.Abs(pair.Value[f]));
        }

        var driven = new float[keys.Count][];
        bool any = false;
        for (int i = 0; i < keys.Count; i++)
        {
            if (groups[i] is not { } group || group.Count == 0) { driven[i] = new float[frames]; continue; }
            any = true;
            if (group.Count == 1) { driven[i] = (float[])sourceSamples[group[0]].Clone(); continue; }
            collapsedKeys++;
            for (int f = 0; f < frames; f++) unsupported[f] = Math.Max(unsupported[f], Math.Abs(sourceSamples[group[0]][f]));
            driven[i] = (float[])sourceSamples[group[0]].Clone();
        }
        if (!any && unsupportedNames.Count == 0) return false;

        for (int f = 0; f < frames; f++) unsupported[f] = Math.Clamp(unsupported[f], 0, 1);
        for (int i = 0; i < keys.Count; i++)
        {
            float[] values = driven[i];
            for (int f = 0; f < frames; f++) values[f] = Math.Clamp(values[f], 0, 1);
            channels[Prefix + keys[i]] = values;
        }
        channels[MikuRawEyeKeys.Markers[0]] = Enumerable.Repeat(1f, frames).ToArray();
        channels[MikuRawEyeKeys.Markers[1]] = unsupported;
        channels[MikuRawEyeKeys.Markers[2]] = Enumerable.Repeat(nativeAuthored ? 1f : 0f, frames).ToArray();
        return true;
    }
}
