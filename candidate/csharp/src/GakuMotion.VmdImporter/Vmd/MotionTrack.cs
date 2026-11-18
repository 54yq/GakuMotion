namespace GakuMotion.VmdImporter.Vmd;

using System.Numerics;

public readonly record struct BonePoseSample(Vector3 Position, Quaternion Rotation);

/// <summary>
/// Continuous animation track for a single bone across timeline frames.
/// Supports deduplication, boundary clamping, and sub-frame continuous sampling.
/// </summary>
public sealed class VmdBoneTrack
{
    private readonly List<BoneKeyframe> _keyframes;

    public string BoneName { get; }
    public IReadOnlyList<BoneKeyframe> Keyframes => _keyframes;
    public uint MinFrame => _keyframes.Count > 0 ? _keyframes[0].Frame : 0;
    public uint MaxFrame => _keyframes.Count > 0 ? _keyframes[^1].Frame : 0;

    public VmdBoneTrack(string boneName, IEnumerable<BoneKeyframe> keyframes, bool deduplicate = true)
    {
        ArgumentNullException.ThrowIfNull(boneName);
        ArgumentNullException.ThrowIfNull(keyframes);
        BoneName = boneName;

        if (deduplicate)
        {
            var dict = new Dictionary<uint, BoneKeyframe>();
            foreach (var kf in keyframes)
            {
                dict[kf.Frame] = kf;
            }
            _keyframes = dict.Values.OrderBy(k => k.Frame).ToList();
        }
        else
        {
            _keyframes = keyframes.OrderBy(k => k.Frame).ToList();
        }
    }

    /// <summary>
    /// Evaluates continuous bone position and rotation at any floating-point frame time.
    /// </summary>
    public BonePoseSample Sample(float frame, RotationInterpolationMode mode = RotationInterpolationMode.Slerp)
    {
        if (_keyframes.Count == 0)
            return new BonePoseSample(Vector3.Zero, Quaternion.Identity);

        if (_keyframes.Count == 1 || frame <= _keyframes[0].Frame)
            return new BonePoseSample(_keyframes[0].Position, _keyframes[0].Rotation);

        if (frame >= _keyframes[^1].Frame)
            return new BonePoseSample(_keyframes[^1].Position, _keyframes[^1].Rotation);

        // Binary search for keyframe segment [k1, k2] such that k1.Frame <= frame < k2.Frame
        int low = 0;
        int high = _keyframes.Count - 1;
        while (low <= high)
        {
            int mid = (low + high) / 2;
            if (_keyframes[mid].Frame <= frame)
                low = mid + 1;
            else
                high = mid - 1;
        }

        int idx1 = Math.Clamp(high, 0, _keyframes.Count - 2);
        int idx2 = idx1 + 1;

        var k1 = _keyframes[idx1];
        var k2 = _keyframes[idx2];

        float span = (float)(k2.Frame - k1.Frame);
        if (span <= 0.0f)
            return new BonePoseSample(k1.Position, k1.Rotation);

        double rawT = (frame - (float)k1.Frame) / span;

        double tx = BezierCurve.EvalBezier(k2.CurveX.P1X, k2.CurveX.P1Y, k2.CurveX.P2X, k2.CurveX.P2Y, rawT);
        double ty = BezierCurve.EvalBezier(k2.CurveY.P1X, k2.CurveY.P1Y, k2.CurveY.P2X, k2.CurveY.P2Y, rawT);
        double tz = BezierCurve.EvalBezier(k2.CurveZ.P1X, k2.CurveZ.P1Y, k2.CurveZ.P2X, k2.CurveZ.P2Y, rawT);

        float px = k1.Position.X + (k2.Position.X - k1.Position.X) * (float)tx;
        float py = k1.Position.Y + (k2.Position.Y - k1.Position.Y) * (float)ty;
        float pz = k1.Position.Z + (k2.Position.Z - k1.Position.Z) * (float)tz;
        var pos = new Vector3(px, py, pz);

        double tr = BezierCurve.EvalBezier(k2.CurveR.P1X, k2.CurveR.P1Y, k2.CurveR.P2X, k2.CurveR.P2Y, rawT);

        var q1 = k1.Rotation;
        var q2 = k2.Rotation;

        Quaternion rot;
        if (mode == RotationInterpolationMode.NormalizedComponent)
        {
            if (Vector4.Dot(new Vector4(q1.X, q1.Y, q1.Z, q1.W), new Vector4(q2.X, q2.Y, q2.Z, q2.W)) < 0.0f)
            {
                q2 = new Quaternion(-q2.X, -q2.Y, -q2.Z, -q2.W);
            }
            float t = (float)tr;
            float vx = q1.X + (q2.X - q1.X) * t;
            float vy = q1.Y + (q2.Y - q1.Y) * t;
            float vz = q1.Z + (q2.Z - q1.Z) * t;
            float vw = q1.W + (q2.W - q1.W) * t;
            double norm = Math.Sqrt((double)vx * vx + (double)vy * vy + (double)vz * vz + (double)vw * vw);
            rot = norm > 0.0
                ? new Quaternion((float)(vx / norm), (float)(vy / norm), (float)(vz / norm), (float)(vw / norm))
                : Quaternion.Identity;
        }
        else
        {
            if (Quaternion.Dot(q1, q2) < 0.0f)
            {
                q2 = -q2;
            }
            rot = Quaternion.Slerp(q1, q2, (float)tr);
        }

        return new BonePoseSample(pos, rot);
    }

    /// <summary>
    /// Dense discrete frame sampling from frame 0 up to totalFrames - 1.
    /// </summary>
    public (Vector3[] Positions, Quaternion[] Rotations) SampleDense(int totalFrames, RotationInterpolationMode mode = RotationInterpolationMode.Slerp)
    {
        return MotionSampling.SampleBoneKeyframes(_keyframes, totalFrames, mode);
    }
}

/// <summary>
/// Continuous animation track for a single morph / blendshape channel.
/// Supports deduplication, boundary clamping, and sub-frame continuous sampling.
/// </summary>
public sealed class VmdMorphTrack
{
    private readonly List<MorphKeyframe> _keyframes;

    public string MorphName { get; }
    public IReadOnlyList<MorphKeyframe> Keyframes => _keyframes;
    public uint MinFrame => _keyframes.Count > 0 ? _keyframes[0].Frame : 0;
    public uint MaxFrame => _keyframes.Count > 0 ? _keyframes[^1].Frame : 0;

    public VmdMorphTrack(string morphName, IEnumerable<MorphKeyframe> keyframes, bool deduplicate = true)
    {
        ArgumentNullException.ThrowIfNull(morphName);
        ArgumentNullException.ThrowIfNull(keyframes);
        MorphName = morphName;

        if (deduplicate)
        {
            var dict = new Dictionary<uint, MorphKeyframe>();
            foreach (var kf in keyframes)
            {
                dict[kf.Frame] = kf;
            }
            _keyframes = dict.Values.OrderBy(k => k.Frame).ToList();
        }
        else
        {
            _keyframes = keyframes.OrderBy(k => k.Frame).ToList();
        }
    }

    /// <summary>
    /// Evaluates continuous morph weight in [0, 1] at any floating-point frame time.
    /// </summary>
    public float Sample(float frame)
    {
        if (_keyframes.Count == 0)
            return 0.0f;

        if (_keyframes.Count == 1 || frame <= _keyframes[0].Frame)
            return _keyframes[0].Weight;

        if (frame >= _keyframes[^1].Frame)
            return _keyframes[^1].Weight;

        int low = 0;
        int high = _keyframes.Count - 1;
        while (low <= high)
        {
            int mid = (low + high) / 2;
            if (_keyframes[mid].Frame <= frame)
                low = mid + 1;
            else
                high = mid - 1;
        }

        int idx1 = Math.Clamp(high, 0, _keyframes.Count - 2);
        int idx2 = idx1 + 1;

        var k1 = _keyframes[idx1];
        var k2 = _keyframes[idx2];

        float span = (float)(k2.Frame - k1.Frame);
        if (span <= 0.0f)
            return k1.Weight;

        float t = (frame - (float)k1.Frame) / span;
        return k1.Weight + (k2.Weight - k1.Weight) * t;
    }

    /// <summary>
    /// Dense discrete frame sampling from frame 0 up to totalFrames - 1.
    /// </summary>
    public float[] SampleDense(int totalFrames)
    {
        return MotionSampling.SampleMorphKeyframes(_keyframes, totalFrames);
    }
}
