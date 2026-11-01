namespace GakuMotion.VmdImporter.Native.Timebase;

using System.Numerics;
using GakuMotion.VmdImporter.Output;

/// <summary>
/// Timebase contract for the body clip.
///
/// A VMD carries no frame-rate field: frame indices are ticks on a timeline
/// whose rate has to be inferred from the data (hash-bound info.json, the
/// paired WAV duration, or a "60 fps" filename stem). That inferred rate is
/// the *source clock*. It fixes the physical length of the motion
/// (<c>sourceTicks / sourceFps</c>) and does not depend on the rate the clip is
/// stored at.
///
/// The gkclip header carries three numbers that must agree with each other:
/// <code>
///   duration_seconds = sourceTicks / sourceFps   (invariant to output rate)
///   fps              = rate at which stored frames advance
///   total_frames     = duration_seconds * fps
/// </code>
/// The frozen Candidate B evaluator always emits one sample per VMD tick, so a
/// non-standard output rate needs an explicit resample. This type is that
/// layer: it re-times already-evaluated samples and never touches evaluation
/// numerics, IK, or the retarget K matrix.
///
/// Sampling rule (matches tools/vmd2gkmotion/arm_hinge_bake.py <c>_resample</c>):
/// output frame i reads source position <c>i * sourceFps / outputFps</c>,
/// clamped to the last tick, so the terminal hold of the source is preserved
/// rather than stretched. Samples that land exactly on a tick are copied
/// bit-for-bit, which is what keeps 30 FPS output byte-identical to the
/// un-retimed clip.
/// </summary>
internal static class ClipTimebase
{
    /// <summary>Upper bound on emitted frames; a corrupt ratio must not allocate wildly.</summary>
    private const int MaxFrames = 4_000_000;

    private const double RateEpsilon = 1e-9;

    /// <summary>
    /// True when the stored frame rate already equals the source clock, i.e.
    /// one VMD tick is one stored frame and no resampling is allowed.
    /// </summary>
    public static bool IsIdentity(double sourceFps, double outputFps)
    {
        Validate("source fps", sourceFps);
        Validate("output fps", outputFps);
        double scale = Math.Max(1.0, Math.Abs(sourceFps));
        return Math.Abs(outputFps - sourceFps) <= RateEpsilon * scale;
    }

    /// <summary>
    /// Re-times <paramref name="clip"/> (sampled on the source clock) to
    /// <paramref name="outputFps"/>. Returns the same instance when the rates
    /// already match, so the 30 FPS path allocates and rounds nothing.
    /// </summary>
    public static GkClipData Retime(GkClipData clip, double sourceFps, double outputFps)
    {
        if (clip == null) throw new ArgumentNullException(nameof(clip));
        Validate("source fps", sourceFps);
        Validate("output fps", outputFps);
        if (IsIdentity(sourceFps, outputFps)) return clip;

        int sourceFrames = clip.TotalFrames;
        if (sourceFrames <= 0)
            throw new InvalidDataException("cannot retime a clip without frames");

        double ratio = outputFps / sourceFps;
        double scaled = sourceFrames * ratio;
        if (!double.IsFinite(scaled) || scaled < 1.0 || scaled > MaxFrames)
            throw new InvalidDataException(
                $"retimed frame count out of range: {sourceFrames} ticks at {sourceFps:g} -> {outputFps:g} fps");

        int outputFrames = (int)Math.Round(scaled, MidpointRounding.ToEven);
        if (outputFrames < 1) outputFrames = 1;

        int boneCount = clip.BoneDeltas.GetLength(1);
        var deltas = new Quaternion[outputFrames, boneCount];
        var roots = new Vector3[outputFrames];
        float[,]? weights = clip.BlendshapeWeights == null
            ? null
            : new float[outputFrames, clip.BlendshapeWeights.GetLength(1)];
        float[,]? camera = clip.CameraFrames == null || clip.CameraFrames.GetLength(0) != sourceFrames
            ? null
            : new float[outputFrames, clip.CameraFrames.GetLength(1)];

        int last = sourceFrames - 1;
        for (int f = 0; f < outputFrames; f++)
        {
            // Frame position on the source timeline, clamped to the terminal hold.
            double position = Math.Min(f / ratio, last);
            int low = (int)Math.Floor(position);
            if (low < 0) low = 0;
            if (low > last) low = last;
            int high = Math.Min(low + 1, last);
            double fraction = position - low;

            for (int b = 0; b < boneCount; b++)
            {
                Quaternion a = clip.BoneDeltas[low, b];
                deltas[f, b] = fraction <= 0.0 ? a : Slerp(a, clip.BoneDeltas[high, b], fraction);
            }

            Vector3 rootA = clip.RootPositions != null && low < clip.RootPositions.Length
                ? clip.RootPositions[low]
                : Vector3.Zero;
            Vector3 rootB = clip.RootPositions != null && high < clip.RootPositions.Length
                ? clip.RootPositions[high]
                : rootA;
            roots[f] = fraction <= 0.0 ? rootA : rootA + (rootB - rootA) * (float)fraction;

            if (weights != null)
            {
                int channels = weights.GetLength(1);
                for (int c = 0; c < channels; c++)
                {
                    float wa = clip.BlendshapeWeights![low, c];
                    weights[f, c] = fraction <= 0.0
                        ? wa
                        : wa + (clip.BlendshapeWeights[high, c] - wa) * (float)fraction;
                }
            }

            if (camera != null)
            {
                int fields = camera.GetLength(1);
                for (int c = 0; c < fields; c++)
                {
                    float ca = clip.CameraFrames![low, c];
                    camera[f, c] = fraction <= 0.0
                        ? ca
                        : ca + (clip.CameraFrames[high, c] - ca) * (float)fraction;
                }
            }
        }

        return new GkClipData((float)outputFps)
        {
            Version = clip.Version,
            TotalFrames = outputFrames,
            // Invariant: the same source timeline keeps the same wall-clock length.
            DurationSeconds = clip.DurationSeconds,
            RootScale = clip.RootScale,
            Bones = clip.Bones,
            RootPositions = roots,
            BoneDeltas = deltas,
            CameraFrames = camera,
            BlendshapeNames = clip.BlendshapeNames,
            BlendshapeWeights = weights
        };
    }

    /// <summary>
    /// Shortest-arc spherical interpolation mirroring
    /// arm_hinge_bake.native_slerp: a normalised linear blend for nearly
    /// coincident quaternions, spherical otherwise.
    /// </summary>
    private static Quaternion Slerp(Quaternion a, Quaternion b, double t)
    {
        double ax = a.X, ay = a.Y, az = a.Z, aw = a.W;
        double bx = b.X, by = b.Y, bz = b.Z, bw = b.W;

        double na = Math.Sqrt(ax * ax + ay * ay + az * az + aw * aw);
        if (na <= 0.0) return b;
        ax /= na; ay /= na; az /= na; aw /= na;

        double nb = Math.Sqrt(bx * bx + by * by + bz * bz + bw * bw);
        if (nb <= 0.0) return a;
        bx /= nb; by /= nb; bz /= nb; bw /= nb;

        double dot = ax * bx + ay * by + az * bz + aw * bw;
        if (dot < 0.0)
        {
            bx = -bx; by = -by; bz = -bz; bw = -bw;
            dot = -dot;
        }

        double x, y, z, w;
        if (dot > 0.9995)
        {
            x = ax + (bx - ax) * t;
            y = ay + (by - ay) * t;
            z = az + (bz - az) * t;
            w = aw + (bw - aw) * t;
        }
        else
        {
            double theta = Math.Acos(Math.Min(dot, 1.0));
            double sin = Math.Sin(theta);
            if (sin <= 0.0)
            {
                x = ax; y = ay; z = az; w = aw;
            }
            else
            {
                double ka = Math.Sin((1.0 - t) * theta) / sin;
                double kb = Math.Sin(t * theta) / sin;
                x = ax * ka + bx * kb;
                y = ay * ka + by * kb;
                z = az * ka + bz * kb;
                w = aw * ka + bw * kb;
            }
        }

        double norm = Math.Sqrt(x * x + y * y + z * z + w * w);
        if (norm <= 0.0) return a;
        return new Quaternion((float)(x / norm), (float)(y / norm), (float)(z / norm), (float)(w / norm));
    }

    private static void Validate(string name, double value)
    {
        if (!double.IsFinite(value) || value <= 0.0 || value > 1000.0)
            throw new InvalidDataException($"{name} must be finite in (0, 1000], got {value}");
    }
}
