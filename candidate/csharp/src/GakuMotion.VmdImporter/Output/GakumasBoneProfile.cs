namespace GakuMotion.VmdImporter.Output;

using System.Numerics;
using System.Text.Json;

/// <summary>
/// Metadata for one humanoid bone in the Gakumas target skeleton.
/// </summary>
public sealed class GakumasBoneEntry
{
    public required string Name { get; init; }
    public required int HumanoidId { get; init; }
    public required string VmdName { get; init; }
    public string? FallbackVmdName { get; init; }
    public Vector3 OffsetEuler { get; init; }
    public Vector3 PostOffsetEuler { get; init; }
    public float RotScale { get; init; } = 1.0f;
    public bool HeadSpaceEyeDelta { get; init; }

    public override string ToString() => $"GakumasBoneEntry({Name}, HID={HumanoidId}, VMD={VmdName})";
}

/// <summary>
/// Gakumas humanoid bone profile configuration.
/// Loaded from gakumas_bone_profile.json resource.
/// </summary>
public sealed class GakumasBoneProfile
{
    public string ProfileName { get; init; } = string.Empty;
    public string Version { get; init; } = "1.2";
    public float RootDisplacementScale { get; init; } = 0.07795f;
    public float Fps { get; init; } = 30.0f;
    public IReadOnlyList<GakumasBoneEntry> Bones { get; init; } = Array.Empty<GakumasBoneEntry>();

    private static GakumasBoneProfile? _defaultProfile;
    public static GakumasBoneProfile Default => _defaultProfile ??= LoadDefault();

    public static GakumasBoneProfile LoadDefault()
    {
        var assembly = typeof(GakumasBoneProfile).Assembly;
        using var stream = assembly.GetManifestResourceStream("GakuMotion.VmdImporter.Resources.gakumas_bone_profile.json")
            ?? throw new InvalidOperationException("Embedded resource gakumas_bone_profile.json not found.");
        return Load(stream);
    }

    public static GakumasBoneProfile Load(Stream stream)
    {
        using var doc = JsonDocument.Parse(stream);
        var root = doc.RootElement;
        string profileName = root.TryGetProperty("profile_name", out var pn) ? pn.GetString() ?? "" : "";
        string version = root.TryGetProperty("version", out var ver) ? ver.GetString() ?? "1.2" : "1.2";
        float rootScale = 0.07795f;
        float fps = 30.0f;
        if (root.TryGetProperty("general", out var gen))
        {
            if (gen.TryGetProperty("root_displacement_scale", out var rds))
                rootScale = rds.GetSingle();
            else if (gen.TryGetProperty("axis_scale", out var asc))
                rootScale = asc.GetSingle();
            if (gen.TryGetProperty("fps", out var fpsProp))
                fps = fpsProp.GetSingle();
        }

        var bonesList = new List<GakumasBoneEntry>();
        if (root.TryGetProperty("bones", out var bonesElem))
        {
            foreach (var b in bonesElem.EnumerateArray())
            {
                string name = b.GetProperty("name").GetString()!;
                int hid = b.GetProperty("humanoid_id").GetInt32();
                string vmdName = b.GetProperty("vmd_name").GetString()!;
                string? fallback = b.TryGetProperty("fallback_vmd_name", out var fb) ? fb.GetString() : null;
                Vector3 offEuler = Vector3.Zero;
                if (b.TryGetProperty("offset_euler", out var oe) && oe.GetArrayLength() == 3)
                {
                    offEuler = new Vector3(oe[0].GetSingle(), oe[1].GetSingle(), oe[2].GetSingle());
                }
                Vector3 postOffEuler = Vector3.Zero;
                if (b.TryGetProperty("post_offset_euler", out var poe) && poe.GetArrayLength() == 3)
                {
                    postOffEuler = new Vector3(poe[0].GetSingle(), poe[1].GetSingle(), poe[2].GetSingle());
                }
                float rotScale = b.TryGetProperty("rot_scale", out var rs) ? rs.GetSingle() : 1.0f;
                bool eyeDelta = b.TryGetProperty("head_space_eye_delta", out var hsed) && hsed.GetBoolean();

                bonesList.Add(new GakumasBoneEntry
                {
                    Name = name,
                    HumanoidId = hid,
                    VmdName = vmdName,
                    FallbackVmdName = fallback,
                    OffsetEuler = offEuler,
                    PostOffsetEuler = postOffEuler,
                    RotScale = rotScale,
                    HeadSpaceEyeDelta = eyeDelta
                });
            }
        }

        return new GakumasBoneProfile
        {
            ProfileName = profileName,
            Version = version,
            RootDisplacementScale = rootScale,
            Fps = fps,
            Bones = bonesList
        };
    }
}
