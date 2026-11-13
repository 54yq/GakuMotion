namespace GakuMotion.VmdImporter.Native;

using System.Numerics;
using GakuMotion.VmdImporter.Vmd;

/// <summary>
/// Adapt baked MMD D-leg tracks without changing the frozen source evaluator.
/// A side is adapted only when all three D tracks exist, at least one contains
/// rotation, the corresponding ordinary and IK tracks are neutral, and the VMD
/// does not explicitly control that side's IK. Ambiguous motions stay unchanged.
/// </summary>
internal static class DeformLegTracks
{
    private static readonly string[] Sides = ["左", "右"];
    private static readonly string[] Parts = ["足", "ひざ", "足首"];
    private const float MaxNeutralPositionSquared = 0.000001f; // 0.001 MMD units
    private const float MaxNeutralRotationSinSquared = 0.0000007615f; // 0.1 degrees
    private const float MinAnimatedRotationSinSquared = 0.0000190385f; // 0.5 degrees

    internal static VmdMotion Prepare(VmdMotion motion, out string[] adaptedSides)
    {
        var selected = new List<string>();
        foreach (string side in Sides)
        {
            if (motion.PropertyKeyframes.Any(p =>
                p.IkSwitches.ContainsKey(side + "足ＩＫ") || p.IkSwitches.ContainsKey(side + "つま先ＩＫ")))
                continue;

            if (Parts.Any(part => !motion.BoneKeyframes.TryGetValue(side + part + "D", out var keys) ||
                keys.Count == 0 || keys.Any(k => !Finite(k.Position) || RotationSinSquared(k.Rotation) is null)))
                continue;
            if (Parts.Any(part => !NeutralTrack(motion, side + part)) ||
                !NeutralTrack(motion, side + "足ＩＫ") || !NeutralTrack(motion, side + "つま先ＩＫ"))
                continue;
            if (!Parts.Any(part => AnimatedRotation(motion.BoneKeyframes[side + part + "D"])))
                continue;
            selected.Add(side);
        }

        adaptedSides = selected.ToArray();
        if (selected.Count == 0) return motion;

        var bones = new Dictionary<string, IReadOnlyList<BoneKeyframe>>(motion.BoneKeyframes, StringComparer.Ordinal);
        foreach (string side in selected)
        foreach (string part in Parts)
        {
            string name = side + part;
            bones[name] = motion.BoneKeyframes[name + "D"]
                .Select(k => k with { Name = name }).ToArray();
        }

        Dictionary<string, bool> DisableSelected(IReadOnlyDictionary<string, bool> original)
        {
            var switches = original.ToDictionary(kv => kv.Key, kv => kv.Value, StringComparer.Ordinal);
            foreach (string side in selected)
            {
                switches[side + "足ＩＫ"] = false;
                switches[side + "つま先ＩＫ"] = false;
            }
            return switches;
        }

        var properties = new List<PropertyKeyframe>(motion.PropertyKeyframes.Count + 1)
        {
            new(0, true, DisableSelected(new Dictionary<string, bool>()))
        };
        properties.AddRange(motion.PropertyKeyframes.Select(p =>
            new PropertyKeyframe(p.Frame, p.Visible, DisableSelected(p.IkSwitches))));
        return new VmdMotion
        {
            Version = motion.Version, ModelName = motion.ModelName, MaxFrame = motion.MaxFrame,
            BoneKeyframes = bones, MorphKeyframes = motion.MorphKeyframes,
            CameraKeyframes = motion.CameraKeyframes, PropertyKeyframes = properties
        };
    }

    private static bool NeutralTrack(VmdMotion motion, string name) =>
        !motion.BoneKeyframes.TryGetValue(name, out var keys) || keys.All(IsNeutral);

    private static bool IsNeutral(BoneKeyframe key) =>
        Finite(key.Position) && key.Position.LengthSquared() <= MaxNeutralPositionSquared &&
        RotationSinSquared(key.Rotation) is float value && value <= MaxNeutralRotationSinSquared;

    private static bool AnimatedRotation(IReadOnlyList<BoneKeyframe> keys) =>
        keys.Any(k => RotationSinSquared(k.Rotation) is float value && value > MinAnimatedRotationSinSquared);

    private static float? RotationSinSquared(Quaternion q)
    {
        float norm = q.LengthSquared();
        if (!float.IsFinite(norm) || norm < 1e-12f) return null;
        return (q.X * q.X + q.Y * q.Y + q.Z * q.Z) / norm;
    }

    private static bool Finite(Vector3 p) => float.IsFinite(p.X) && float.IsFinite(p.Y) && float.IsFinite(p.Z);
}
