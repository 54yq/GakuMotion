namespace GakuMotion.VmdImporter.Native.Facial;

using System.Text.Json;
using GakuMotion.VmdImporter.Output;
using GakuMotion.VmdImporter.Vmd;

/// <summary>
/// Port of vmd2gkclip's eye handling: the two extra v5 channels declared by
/// tools/vmd2gkmotion/vmd_eye_profile.json are appended to the frozen 52-bone
/// table ONLY when the source actually carries an eye track, and they carry a
/// Head-space delta rather than a canonical local delta.
///
/// The bone table itself is a frozen resource, so it is extended here instead
/// of being edited: the base profile is embedded in the frozen assembly and
/// stays byte-identical.
/// </summary>
internal static class EyeProfile
{
    private const string ResourceName = "GakuMotion.VmdImporter.Native.Resources.vmd_eye_profile.json";

    /// <summary>両目 grants both eyes; 左目/右目 rotate them individually.</summary>
    public static readonly string[] SourceTracks = { "両目", "左目", "右目" };

    private static IReadOnlyList<GakumasBoneEntry>? _bones;

    public static IReadOnlyList<GakumasBoneEntry> Bones => _bones ??= LoadBones();

    private static IReadOnlyList<GakumasBoneEntry> LoadBones()
    {
        var assembly = typeof(EyeProfile).Assembly;
        using Stream stream = assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException("embedded resource missing: " + ResourceName);
        using var doc = JsonDocument.Parse(stream);
        var list = new List<GakumasBoneEntry>(2);
        foreach (JsonElement bone in doc.RootElement.GetProperty("bones").EnumerateArray())
        {
            list.Add(new GakumasBoneEntry
            {
                Name = bone.GetProperty("name").GetString() ?? string.Empty,
                HumanoidId = bone.GetProperty("humanoid_id").GetInt32(),
                VmdName = bone.GetProperty("vmd_name").GetString() ?? string.Empty,
                FallbackVmdName = bone.TryGetProperty("fallback_vmd_name", out JsonElement fallback)
                    ? fallback.GetString()
                    : null,
                HeadSpaceEyeDelta = bone.TryGetProperty("head_space_eye_delta", out JsonElement flag) &&
                                    flag.ValueKind == JsonValueKind.True
            });
        }
        return list;
    }

    public static bool HasEyeTracks(VmdMotion motion)
    {
        foreach (string name in SourceTracks)
            if (motion.BoneKeyframes.ContainsKey(name)) return true;
        return false;
    }

    /// <summary>
    /// profile_bones = base + [eye bones whose humanoid id is not present],
    /// exactly like vmd2gkclip.convert.
    /// </summary>
    public static GakumasBoneProfile Extend(GakumasBoneProfile baseProfile)
    {
        var presentIds = new HashSet<int>(baseProfile.Bones.Count);
        foreach (GakumasBoneEntry bone in baseProfile.Bones) presentIds.Add(bone.HumanoidId);

        var merged = new List<GakumasBoneEntry>(baseProfile.Bones.Count + Bones.Count);
        merged.AddRange(baseProfile.Bones);
        foreach (GakumasBoneEntry bone in Bones)
            if (!presentIds.Contains(bone.HumanoidId)) merged.Add(bone);

        if (merged.Count == baseProfile.Bones.Count) return baseProfile;

        return new GakumasBoneProfile
        {
            ProfileName = baseProfile.ProfileName,
            Version = baseProfile.Version,
            RootDisplacementScale = baseProfile.RootDisplacementScale,
            Fps = baseProfile.Fps,
            Bones = merged
        };
    }
}
