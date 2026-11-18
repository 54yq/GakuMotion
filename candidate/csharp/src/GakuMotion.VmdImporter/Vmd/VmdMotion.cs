namespace GakuMotion.VmdImporter.Vmd;

using System.Numerics;

public sealed class VmdMotion
{
    private readonly Lazy<IReadOnlyDictionary<string, VmdBoneTrack>> _boneTracks;
    private readonly Lazy<IReadOnlyDictionary<string, VmdMorphTrack>> _morphTracks;

    public string Version { get; init; } = string.Empty;
    public string ModelName { get; init; } = string.Empty;
    public uint MaxFrame { get; init; }
    public float DurationSeconds => MaxFrame / 30.0f;

    public IReadOnlyDictionary<string, IReadOnlyList<BoneKeyframe>> BoneKeyframes { get; init; }
        = new Dictionary<string, IReadOnlyList<BoneKeyframe>>();

    public IReadOnlyDictionary<string, IReadOnlyList<MorphKeyframe>> MorphKeyframes { get; init; }
        = new Dictionary<string, IReadOnlyList<MorphKeyframe>>();

    public IReadOnlyList<CameraKeyframe> CameraKeyframes { get; init; }
        = Array.Empty<CameraKeyframe>();

    public IReadOnlyList<PropertyKeyframe> PropertyKeyframes { get; init; }
        = Array.Empty<PropertyKeyframe>();

    public IReadOnlyDictionary<string, VmdBoneTrack> BoneTracks => _boneTracks.Value;
    public IReadOnlyDictionary<string, VmdMorphTrack> MorphTracks => _morphTracks.Value;

    public VmdMotion()
    {
        _boneTracks = new Lazy<IReadOnlyDictionary<string, VmdBoneTrack>>(() =>
        {
            var dict = new Dictionary<string, VmdBoneTrack>(BoneKeyframes.Count);
            foreach (var kvp in BoneKeyframes)
            {
                dict[kvp.Key] = new VmdBoneTrack(kvp.Key, kvp.Value);
            }
            return dict;
        });

        _morphTracks = new Lazy<IReadOnlyDictionary<string, VmdMorphTrack>>(() =>
        {
            var dict = new Dictionary<string, VmdMorphTrack>(MorphKeyframes.Count);
            foreach (var kvp in MorphKeyframes)
            {
                dict[kvp.Key] = new VmdMorphTrack(kvp.Key, kvp.Value);
            }
            return dict;
        });
    }

    /// <summary>
    /// Samples a single bone's continuous transform at the given frame float.
    /// Returns (0, 0, 0) and Identity if the bone is not present in the motion.
    /// </summary>
    public BonePoseSample SampleBone(string boneName, float frame, RotationInterpolationMode mode = RotationInterpolationMode.Slerp)
    {
        if (BoneTracks.TryGetValue(boneName, out var track))
        {
            return track.Sample(frame, mode);
        }
        return new BonePoseSample(Vector3.Zero, Quaternion.Identity);
    }

    /// <summary>
    /// Samples a single morph's continuous weight at the given frame float.
    /// Returns 0.0f if the morph is not present in the motion.
    /// </summary>
    public float SampleMorph(string morphName, float frame)
    {
        if (MorphTracks.TryGetValue(morphName, out var track))
        {
            return track.Sample(frame);
        }
        return 0.0f;
    }

    /// <summary>
    /// Samples all authored bones at the given frame float.
    /// </summary>
    public IReadOnlyDictionary<string, BonePoseSample> Sample(float frame, RotationInterpolationMode mode = RotationInterpolationMode.Slerp)
    {
        var result = new Dictionary<string, BonePoseSample>(BoneTracks.Count);
        foreach (var kvp in BoneTracks)
        {
            result[kvp.Key] = kvp.Value.Sample(frame, mode);
        }
        return result;
    }
}
