namespace GakuMotion.VmdImporter.Native.Facial;

using System.Text;
using System.Text.Json;

/// <summary>One target blendshape a source morph feeds.</summary>
internal readonly record struct MorphTarget(string Name, float Scale);

/// <summary>
/// Port of morph_translation.translate_morphs' name resolution: the shipped
/// gakumas_morph_map.json, NFKC-normalized aliases, and the same validation
/// that refuses an unknown target or a conflicting alias.
///
/// The map is embedded from tools/vmd2gkmotion/gakumas_morph_map.json at build
/// time (see the csproj), so the DLL can never pair with a different map than
/// the one the Python product path uses.
/// </summary>
internal sealed class GakumasMorphMap
{
    private const string ResourceName = "GakuMotion.VmdImporter.Native.Resources.gakumas_morph_map.json";

    private readonly HashSet<string> _known;
    private readonly Dictionary<string, List<MorphTarget>> _aliases;

    public string? FacialAdapterVersion { get; }
    public string SourceLabel => "gakumas_morph_map.json (embedded from tools/vmd2gkmotion)";
    public int MappingCount => _aliases.Count;

    private GakumasMorphMap(HashSet<string> known, Dictionary<string, List<MorphTarget>> aliases, string? adapter)
    {
        _known = known;
        _aliases = aliases;
        FacialAdapterVersion = adapter;
    }

    private static GakumasMorphMap? _default;

    public static GakumasMorphMap Default => _default ??= Load();

    public static GakumasMorphMap Load()
    {
        var assembly = typeof(GakumasMorphMap).Assembly;
        using Stream stream = assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException("embedded resource missing: " + ResourceName);
        using var doc = JsonDocument.Parse(stream);
        JsonElement root = doc.RootElement;

        var known = new HashSet<string>(StringComparer.Ordinal);
        if (root.TryGetProperty("blendshapes", out JsonElement blendshapes) &&
            blendshapes.ValueKind == JsonValueKind.Array)
        {
            foreach (JsonElement item in blendshapes.EnumerateArray())
            {
                string? name = item.GetString();
                if (string.IsNullOrEmpty(name)) throw new InvalidDataException("blendshapes must contain nonempty target names");
                known.Add(name);
            }
        }
        if (root.TryGetProperty("semantic_channels", out JsonElement semantic) &&
            semantic.ValueKind == JsonValueKind.Array)
        {
            foreach (JsonElement item in semantic.EnumerateArray())
            {
                string? name = item.GetString();
                if (string.IsNullOrEmpty(name)) throw new InvalidDataException("blendshapes must contain nonempty target names");
                known.Add(name);
            }
        }
        if (known.Count == 0) throw new InvalidDataException("blendshapes must contain nonempty target names");

        var aliases = new Dictionary<string, List<MorphTarget>>(StringComparer.Ordinal);
        if (root.TryGetProperty("mappings", out JsonElement mappings) && mappings.ValueKind == JsonValueKind.Object)
        {
            foreach (JsonProperty entry in mappings.EnumerateObject())
            {
                var targets = new List<MorphTarget>(entry.Value.GetArrayLength());
                foreach (JsonElement target in entry.Value.EnumerateArray())
                {
                    string name = target.GetProperty("name").GetString() ?? string.Empty;
                    float scale = target.TryGetProperty("scale", out JsonElement scaleElement)
                        ? scaleElement.GetSingle()
                        : 1.0f;
                    if (!known.Contains(name) || !float.IsFinite(scale))
                        throw new InvalidDataException($"invalid blendshape mapping: {entry.Name}: {name}");
                    targets.Add(new MorphTarget(name, scale));
                }

                string alias = Normalize(entry.Name);
                if (aliases.TryGetValue(alias, out List<MorphTarget>? existing))
                {
                    if (!TargetsEqual(existing, targets))
                        throw new InvalidDataException($"conflicting normalized morph alias: {entry.Name}");
                }
                else
                {
                    aliases[alias] = targets;
                }
            }
        }

        string? adapter = root.TryGetProperty("facial_adapter_version", out JsonElement version) &&
                          version.ValueKind == JsonValueKind.Number
            ? version.GetInt32().ToString(System.Globalization.CultureInfo.InvariantCulture)
            : null;

        return new GakumasMorphMap(known, aliases, adapter);
    }

    private static bool TargetsEqual(List<MorphTarget> a, List<MorphTarget> b)
    {
        if (a.Count != b.Count) return false;
        for (int i = 0; i < a.Count; i++)
        {
            if (!string.Equals(a[i].Name, b[i].Name, StringComparison.Ordinal) || a[i].Scale != b[i].Scale)
                return false;
        }
        return true;
    }

    /// <summary>unicodedata.normalize('NFKC', name).strip()</summary>
    public static string Normalize(string name) => name.Normalize(NormalizationForm.FormKC).Trim();

    /// <summary>
    /// Resolution order of translate_morphs: a native target name wins over an
    /// alias, so a VMD that already authors b_mouth.* is passed through.
    /// </summary>
    public bool TryResolve(string sourceName, out IReadOnlyList<MorphTarget> targets)
    {
        if (_known.Contains(sourceName))
        {
            targets = new[] { new MorphTarget(sourceName, 1.0f) };
            return true;
        }
        if (_aliases.TryGetValue(Normalize(sourceName), out List<MorphTarget>? aliased))
        {
            targets = aliased;
            return true;
        }
        targets = Array.Empty<MorphTarget>();
        return false;
    }

    /// <summary>The map's native channel names, for the report.</summary>
    public bool IsKnownChannel(string name) => _known.Contains(name);
}
