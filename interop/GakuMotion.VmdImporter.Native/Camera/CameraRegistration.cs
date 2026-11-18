namespace GakuMotion.VmdImporter.Native.Camera;

using System.Globalization;
using System.Numerics;
using System.Security.Cryptography;
using System.Text.Json;
using GakuMotion.VmdImporter.Output;
using GakuMotion.VmdImporter.Vmd;

/// <summary>A legacy/unpaired track lacks the inputs this registration supports.</summary>
internal sealed class RegistrationUnavailableException : Exception
{
    public RegistrationUnavailableException(string reason, string message) : base(message) => Reason = reason;

    /// <summary>Stable machine token recorded in info.json (never localized).</summary>
    public string Reason { get; }
}

/// <summary>Which root mapping, if any, was actually applied to the camera.</summary>
internal enum CameraRegistrationStatus
{
    /// <summary>The authenticated body/source pair agreed; corrections were applied.</summary>
    Applied,

    /// <summary>No reliable root model; raw source coordinates were kept.</summary>
    NotApplicable,

    /// <summary>The caller asked for raw source coordinates.</summary>
    Off
}

/// <summary>Registration outcome plus the diagnostics written into info.json.</summary>
internal sealed class CameraRegistrationResult
{
    public required CameraSample[] Samples { get; init; }
    public required CameraRegistrationStatus Status { get; init; }
    public required string Mode { get; init; }

    /// <summary>Stable token: off_by_request, root_trajectory, root_trajectory_mismatch, …</summary>
    public required string Reason { get; init; }

    /// <summary>Max body-clock deviation between the source controllers and the clip root.</summary>
    public double? ResidualMeters { get; init; }

    /// <summary>Human-readable log line (Chinese/English mixed is fine; not parsed).</summary>
    public required string Description { get; init; }

    public string StatusText => Status switch
    {
        CameraRegistrationStatus.Applied => "applied",
        CameraRegistrationStatus.Off => "off",
        _ => "not_applicable"
    };
}

/// <summary>
/// Port of tools/vmd2gkmotion/camera_registration.py.
///
/// This corrects the controller origin and translation scale only: it pairs a
/// camera VMD's coordinates with the authenticated body root of the song it is
/// imported into. The source controllers are read with the SAME interpolation
/// layout the body profile declares (`channel_blocks`); reading them as
/// `legacy` moved every controller position off the trajectory the body
/// actually consumed and manufactured false mismatches.
///
/// `auto` applies the mapping when the pair can be authenticated and falls
/// back to raw source coordinates otherwise — a residual above tolerance is
/// NOT proof that the camera belongs to another take (the baked root and the
/// raw controller proxy are not guaranteed to be equal), so it must not reject
/// the user's camera. `required` keeps the strict contract and errors instead;
/// `off` skips the mapping entirely.
/// </summary>
internal static class CameraRegistration
{
    public const string ModeAuto = "auto";
    public const string ModeOff = "off";
    public const string ModeRequired = "required";

    private const double SourceFps = 30.0;
    private const double RootMatchToleranceM = 1e-5;
    private const string RootProfileName = "root_retarget_profile.json";
    private const string CatalogResource = "GakuMotion.VmdImporter.Native.Resources.root_retarget_profiles.json";

    /// <summary>
    /// Must track `general.interpolation_layout` in tools/vmd2gkmotion/
    /// gakumas_bone_profile.json — the layout the body evaluation consumes.
    /// </summary>
    private const InterpolationLayout BodyInterpolationLayout = InterpolationLayout.ChannelBlocks;

    private static readonly string[] Controllers = { "全ての親", "センター", "グルーブ" };

    // ---------------------------------------------------------------- helpers

    private static (double X, double Y, double Z) Rotate((double X, double Y, double Z, double W) q,
        (double X, double Y, double Z) p)
    {
        double cx = q.Y * p.Z - q.Z * p.Y;
        double cy = q.Z * p.X - q.X * p.Z;
        double cz = q.X * p.Y - q.Y * p.X;
        double ix = cx + q.W * p.X;
        double iy = cy + q.W * p.Y;
        double iz = cz + q.W * p.Z;
        return (p.X + 2.0 * (q.Y * iz - q.Z * iy),
                p.Y + 2.0 * (q.Z * ix - q.X * iz),
                p.Z + 2.0 * (q.X * iy - q.Y * ix));
    }

    private static double Interp(double[] xs, double[] ys, double x)
    {
        int n = xs.Length;
        if (n == 0) return 0.0;
        if (n == 1 || x <= xs[0]) return ys[0];
        if (x >= xs[n - 1]) return ys[n - 1];
        int low = 0, high = n - 1;
        while (low <= high)
        {
            int mid = (low + high) >> 1;
            if (xs[mid] <= x) low = mid + 1;
            else high = mid - 1;
        }
        int j = Math.Clamp(high, 0, n - 2);
        return (ys[j + 1] - ys[j]) / (xs[j + 1] - xs[j]) * (x - xs[j]) + ys[j];
    }

    private static string Sha256File(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    // ----------------------------------------------------- controller sampling

    /// <summary>
    /// _sample_controller: Bezier position + normalized slerp, stored float32
    /// like the golden implementation (it is NOT the body sampler: the body
    /// uses the profile's declared component interpolation).
    /// </summary>
    private static (float[] Positions, float[] Rotations) SampleController(
        IReadOnlyList<BoneKeyframe>? keys, int count)
    {
        var positions = new float[count * 3];
        var rotations = new float[count * 4];
        for (int f = 0; f < count; f++) rotations[f * 4 + 3] = 1.0f;
        if (keys == null || keys.Count == 0) return (positions, rotations);

        int index = 0;
        for (int frame = 0; frame < count; frame++)
        {
            while (index + 1 < keys.Count && keys[index + 1].Frame <= frame) index++;
            BoneKeyframe a = keys[index];
            if (index + 1 == keys.Count || frame <= a.Frame)
            {
                Write(positions, rotations, frame, a.Position.X, a.Position.Y, a.Position.Z,
                    a.Rotation.X, a.Rotation.Y, a.Rotation.Z, a.Rotation.W);
                continue;
            }

            BoneKeyframe b = keys[index + 1];
            double t = (frame - a.Frame) / (double)(b.Frame - a.Frame);
            double cx = BezierCurve.EvalBezier(b.CurveX.P1X, b.CurveX.P1Y, b.CurveX.P2X, b.CurveX.P2Y, t);
            double cy = BezierCurve.EvalBezier(b.CurveY.P1X, b.CurveY.P1Y, b.CurveY.P2X, b.CurveY.P2Y, t);
            double cz = BezierCurve.EvalBezier(b.CurveZ.P1X, b.CurveZ.P1Y, b.CurveZ.P2X, b.CurveZ.P2Y, t);
            double cr = BezierCurve.EvalBezier(b.CurveR.P1X, b.CurveR.P1Y, b.CurveR.P2X, b.CurveR.P2Y, t);

            double px = a.Position.X + (b.Position.X - a.Position.X) * cx;
            double py = a.Position.Y + (b.Position.Y - a.Position.Y) * cy;
            double pz = a.Position.Z + (b.Position.Z - a.Position.Z) * cz;

            double qx = a.Rotation.X, qy = a.Rotation.Y, qz = a.Rotation.Z, qw = a.Rotation.W;
            double rx = b.Rotation.X, ry = b.Rotation.Y, rz = b.Rotation.Z, rw = b.Rotation.W;
            double qn = Math.Sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
            double rn = Math.Sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
            if (qn > 0.0) { qx /= qn; qy /= qn; qz /= qn; qw /= qn; }
            if (rn > 0.0) { rx /= rn; ry /= rn; rz /= rn; rw /= rn; }

            double dot = qx * rx + qy * ry + qz * rz + qw * rw;
            if (dot < 0.0) { rx = -rx; ry = -ry; rz = -rz; rw = -rw; dot = -dot; }
            double angle = Math.Acos(Math.Min(1.0, dot));
            double u = cr;
            double ox, oy, oz, ow;
            if (angle < 1e-10)
            {
                ox = (1.0 - u) * qx + u * rx;
                oy = (1.0 - u) * qy + u * ry;
                oz = (1.0 - u) * qz + u * rz;
                ow = (1.0 - u) * qw + u * rw;
            }
            else
            {
                double sa = Math.Sin(angle);
                ox = (Math.Sin((1.0 - u) * angle) * qx + Math.Sin(u * angle) * rx) / sa;
                oy = (Math.Sin((1.0 - u) * angle) * qy + Math.Sin(u * angle) * ry) / sa;
                oz = (Math.Sin((1.0 - u) * angle) * qz + Math.Sin(u * angle) * rz) / sa;
                ow = (Math.Sin((1.0 - u) * angle) * qw + Math.Sin(u * angle) * rw) / sa;
            }

            Write(positions, rotations, frame, px, py, pz, ox, oy, oz, ow);
        }

        return (positions, rotations);
    }

    private static void Write(float[] positions, float[] rotations, int frame,
        double px, double py, double pz, double qx, double qy, double qz, double qw)
    {
        positions[frame * 3 + 0] = (float)px;
        positions[frame * 3 + 1] = (float)py;
        positions[frame * 3 + 2] = (float)pz;
        rotations[frame * 4 + 0] = (float)qx;
        rotations[frame * 4 + 1] = (float)qy;
        rotations[frame * 4 + 2] = (float)qz;
        rotations[frame * 4 + 3] = (float)qw;
    }

    /// <summary>
    /// sample_source_controllers: controller displacement in axis-mapped MMD
    /// units, no rest height. A rotating 全ての親 is refused because the static
    /// center height would manufacture a model-dependent origin correction.
    /// </summary>
    public static double[] SampleSourceControllers(VmdMotion motion)
    {
        int count = (int)motion.MaxFrame + 1;
        if (motion.BoneKeyframes.Count == 0 || count <= 0 || count > 1_000_000)
            throw new InvalidDataException("motion VMD must contain bone keys and at most 1,000,000 frames");

        var tracks = new Dictionary<string, (float[] Positions, float[] Rotations)>(StringComparer.Ordinal);
        foreach (string name in Controllers)
        {
            motion.BoneKeyframes.TryGetValue(name, out IReadOnlyList<BoneKeyframe>? keys);
            if (keys != null)
            {
                foreach (BoneKeyframe key in keys)
                {
                    if (!float.IsFinite(key.Position.X) || !float.IsFinite(key.Position.Y) ||
                        !float.IsFinite(key.Position.Z) || !float.IsFinite(key.Rotation.X) ||
                        !float.IsFinite(key.Rotation.Y) || !float.IsFinite(key.Rotation.Z) ||
                        !float.IsFinite(key.Rotation.W))
                        throw new InvalidDataException($"invalid source controller key: {name}");
                    double norm = Math.Sqrt(
                        (double)key.Rotation.X * key.Rotation.X + (double)key.Rotation.Y * key.Rotation.Y +
                        (double)key.Rotation.Z * key.Rotation.Z + (double)key.Rotation.W * key.Rotation.W);
                    if (norm < 1e-8) throw new InvalidDataException($"invalid source controller key: {name}");
                    if (name == "全ての親")
                    {
                        double axis = Math.Sqrt((double)key.Rotation.X * key.Rotation.X +
                                                (double)key.Rotation.Y * key.Rotation.Y +
                                                (double)key.Rotation.Z * key.Rotation.Z);
                        if (axis / norm > 1e-6)
                            throw new RegistrationUnavailableException("rotating_all_parent",
                                "nonidentity all-parent rotation is unsupported: source PMX pivots/rest offsets are required");
                    }
                }
            }
            tracks[name] = SampleController(keys, count);
        }

        var roots = new double[count * 3];
        for (int frame = 0; frame < count; frame++)
        {
            double parentX = tracks["全ての親"].Positions[frame * 3 + 0];
            double parentY = tracks["全ての親"].Positions[frame * 3 + 1];
            double parentZ = tracks["全ての親"].Positions[frame * 3 + 2];
            double centerX = tracks["センター"].Positions[frame * 3 + 0];
            double centerY = tracks["センター"].Positions[frame * 3 + 1];
            double centerZ = tracks["センター"].Positions[frame * 3 + 2];
            double grooveX = tracks["グルーブ"].Positions[frame * 3 + 0];
            double grooveY = tracks["グルーブ"].Positions[frame * 3 + 1];
            double grooveZ = tracks["グルーブ"].Positions[frame * 3 + 2];

            double qx = tracks["センター"].Rotations[frame * 4 + 0] * -1.0;
            double qy = tracks["センター"].Rotations[frame * 4 + 1];
            double qz = tracks["センター"].Rotations[frame * 4 + 2] * -1.0;
            double qw = tracks["センター"].Rotations[frame * 4 + 3];
            double norm = Math.Sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
            if (norm > 0.0) { qx /= norm; qy /= norm; qz /= norm; qw /= norm; }

            var groove = (-grooveX, grooveY, -grooveZ);
            (double X, double Y, double Z) spun = Rotate((qx, qy, qz, qw), groove);
            roots[frame * 3 + 0] = -(parentX + centerX) + spun.X;
            roots[frame * 3 + 1] = (parentY + centerY) + spun.Y;
            roots[frame * 3 + 2] = -(parentZ + centerZ) + spun.Z;
        }

        foreach (double value in roots)
            if (!double.IsFinite(value)) throw new InvalidDataException("source controller trajectory contains non-finite values");
        return roots;
    }

    // -------------------------------------------------------------- validation

    private static int BodySubsteps(float fps)
    {
        if (!float.IsFinite(fps) || fps < SourceFps || fps > 240.0f)
            throw new RegistrationUnavailableException("unsupported_body_clock",
                "body clock must be a 30 FPS multiple up to 240 FPS");
        int steps = (int)Math.Round(fps / SourceFps, MidpointRounding.ToEven);
        if (Math.Abs(fps - steps * SourceFps) > 1e-6)
            throw new RegistrationUnavailableException("unsupported_body_clock",
                "body clock must be a 30 FPS multiple up to 240 FPS");
        return steps;
    }

    private static void ValidateMotionClock(GkClipData clip, double[] sourceRoots)
    {
        int sourceFrames = sourceRoots.Length / 3;
        int steps = BodySubsteps(clip.Fps);
        if (clip.TotalFrames != sourceFrames * steps)
            throw new InvalidDataException("motion clip/source VMD frame count mismatch");
        // math.isclose(..., rel_tol=1e-6, abs_tol=1e-6): the clip stores its
        // duration as float32, so a long track legitimately differs from
        // frames/30 by ~1e-5 absolute; only the relative term catches it.
        double expectedDuration = sourceFrames / SourceFps;
        double durationTolerance = Math.Max(
            1e-6 * Math.Max(Math.Abs((double)clip.DurationSeconds), Math.Abs(expectedDuration)), 1e-6);
        if (!float.IsFinite(clip.DurationSeconds) ||
            Math.Abs(clip.DurationSeconds - expectedDuration) > durationTolerance)
            throw new InvalidDataException("motion clip/source VMD duration mismatch");
        if (clip.RootPositions == null || clip.RootPositions.Length != clip.TotalFrames)
            throw new InvalidDataException("motion clip root positions are invalid");
        foreach (Vector3 position in clip.RootPositions)
            if (!float.IsFinite(position.X) || !float.IsFinite(position.Y) || !float.IsFinite(position.Z))
                throw new InvalidDataException("motion clip root positions are invalid");
        if (!float.IsFinite(clip.RootScale) || clip.RootScale <= 0.0f)
            throw new InvalidDataException("motion clip root scale must be positive and finite");
    }

    /// <summary>
    /// Max deviation between the clip root and the source controller proxy, in
    /// metres at the body clock. This is a MEASUREMENT, not a verdict: the
    /// baked root and the raw controller proxy are not guaranteed to be equal,
    /// so the caller decides whether the result is close enough to apply.
    /// Clock/integrity violations still throw.
    /// </summary>
    private static double MeasureSourceRootMatch(GkClipData clip, double[] sourceRoots)
    {
        ValidateMotionClock(clip, sourceRoots);
        int sourceFrames = sourceRoots.Length / 3;
        var sourceTimes = new double[sourceFrames];
        for (int i = 0; i < sourceFrames; i++) sourceTimes[i] = i / SourceFps;
        var bodyTimes = new double[clip.TotalFrames];
        for (int i = 0; i < clip.TotalFrames; i++) bodyTimes[i] = i / (double)clip.Fps;

        var relativeAxis = new double[sourceFrames];
        var expected = new double[clip.TotalFrames * 3];
        double error = 0.0;
        for (int axis = 0; axis < 3; axis++)
        {
            double origin = sourceRoots[axis];
            for (int i = 0; i < sourceFrames; i++)
                relativeAxis[i] = (sourceRoots[i * 3 + axis] - origin) * clip.RootScale;

            for (int f = 0; f < clip.TotalFrames; f++)
                expected[f * 3 + axis] = Interp(sourceTimes, relativeAxis, bodyTimes[f]);
        }

        for (int f = 0; f < clip.TotalFrames; f++)
        {
            double dx = clip.RootPositions[f].X - expected[f * 3 + 0];
            double dy = clip.RootPositions[f].Y - expected[f * 3 + 1];
            double dz = clip.RootPositions[f].Z - expected[f * 3 + 2];
            double norm = Math.Sqrt(dx * dx + dy * dy + dz * dz);
            if (norm > error) error = norm;
        }

        return error;
    }

    private static string MismatchMessage(double error) =>
        // ":G9" mirrors Python's f"{error:.9g}" so both importers report the
        // same digits for the same mismatch (9 significant figures, not places).
        $"motion clip/source root trajectory mismatch: {error.ToString("G9", CultureInfo.InvariantCulture)} m " +
        $"exceeds {RootMatchToleranceM:g} m";

    // -------------------------------------------------------------- corrections

    private static double[] RegistrationCorrections(double[] sourceRoots, GkClipData clip,
        double[] sampleTimes, double scale)
    {
        if (!double.IsFinite(scale) || scale <= 0.0)
            throw new InvalidDataException("camera scale must be positive and finite");
        ValidateMotionClock(clip, sourceRoots);

        int sourceFrames = sourceRoots.Length / 3;
        var sourceTimes = new double[sourceFrames];
        for (int i = 0; i < sourceFrames; i++) sourceTimes[i] = i / SourceFps;
        var bodyTimes = new double[clip.TotalFrames];
        for (int i = 0; i < clip.TotalFrames; i++) bodyTimes[i] = i / (double)clip.Fps;

        var corrections = new double[sampleTimes.Length * 3];
        var column = new double[sourceFrames];
        var bodyColumn = new double[clip.TotalFrames];
        for (int axis = 0; axis < 3; axis++)
        {
            for (int i = 0; i < sourceFrames; i++) column[i] = sourceRoots[i * 3 + axis];
            for (int i = 0; i < clip.TotalFrames; i++)
                bodyColumn[i] = axis == 0 ? clip.RootPositions[i].X
                    : axis == 1 ? clip.RootPositions[i].Y
                    : clip.RootPositions[i].Z;

            for (int i = 0; i < sampleTimes.Length; i++)
            {
                double source = Interp(sourceTimes, column, sampleTimes[i]);
                double installed = Interp(bodyTimes, bodyColumn, sampleTimes[i]);
                corrections[i * 3 + axis] = installed - scale * source;
            }
        }
        return corrections;
    }

    private static bool HasRootRetargetProfile(string vmdPath)
    {
        string? directory = Path.GetDirectoryName(vmdPath);
        if (!string.IsNullOrEmpty(directory))
        {
            string adjacent = Path.Combine(directory, RootProfileName);
            if (File.Exists(adjacent))
            {
                try
                {
                    using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(adjacent));
                    if (doc.RootElement.TryGetProperty("source_vmd_sha256", out JsonElement bound) &&
                        bound.ValueKind == JsonValueKind.String &&
                        string.Equals(bound.GetString(), Sha256File(vmdPath), StringComparison.Ordinal))
                        return true;
                }
                catch
                {
                    // An unreadable adjacent profile is not this check's business.
                }
            }
        }

        try
        {
            var assembly = typeof(CameraRegistration).Assembly;
            using Stream? stream = assembly.GetManifestResourceStream(CatalogResource);
            if (stream == null) return false;
            using JsonDocument catalog = JsonDocument.Parse(stream);
            return catalog.RootElement.TryGetProperty("profiles", out JsonElement profiles) &&
                   profiles.TryGetProperty(Sha256File(vmdPath), out _);
        }
        catch
        {
            return false;
        }
    }

    // ------------------------------------------------------------------ entry

    public static CameraRegistrationResult Register(
        CameraSample[] samples, string motionDirectory, double fps, double scale, string mode, GkClipData? clip = null)
    {
        if (mode != ModeAuto && mode != ModeOff && mode != ModeRequired)
            throw new InvalidDataException("root registration must be auto, off or required");
        if (mode == ModeOff)
            return new CameraRegistrationResult
            {
                Samples = samples,
                Status = CameraRegistrationStatus.Off,
                Mode = mode,
                Reason = "off_by_request",
                Description = "off (raw source coordinates)"
            };

        try
        {
            string infoPath = Path.Combine(motionDirectory, "info.json");
            string clipPath = Path.Combine(motionDirectory, "motion.gkclip");
            if (!File.Exists(infoPath) || !File.Exists(clipPath))
                throw new RegistrationUnavailableException("no_paired_body", "no paired body metadata/clip");

            using JsonDocument info = JsonDocument.Parse(File.ReadAllBytes(infoPath));
            if (info.RootElement.ValueKind != JsonValueKind.Object)
                throw new InvalidDataException("song info.json must contain a JSON object");
            if (!info.RootElement.TryGetProperty("source_vmd_sha256", out JsonElement expectedElement) ||
                expectedElement.ValueKind != JsonValueKind.String)
                throw new RegistrationUnavailableException("unauthenticated_source", "no authenticated source VMD SHA256");
            string expected = expectedElement.GetString() ?? string.Empty;
            if (expected.Length != 64 || !expected.All(Uri.IsHexDigit))
                throw new InvalidDataException("invalid song source_vmd_sha256");
            expected = expected.ToLowerInvariant();

            var candidates = new List<string>();
            if (info.RootElement.TryGetProperty("vmd_source", out JsonElement vmdSource) &&
                vmdSource.ValueKind == JsonValueKind.String)
                candidates.Add(vmdSource.GetString() ?? string.Empty);
            candidates.Add("source.vmd");
            candidates.Add("Motion.vmd");
            if (info.RootElement.TryGetProperty("source_vmd", out JsonElement sourceVmd) &&
                sourceVmd.ValueKind == JsonValueKind.String)
                candidates.Add(sourceVmd.GetString() ?? string.Empty);

            string? source = null;
            bool existingSource = false;
            foreach (string candidate in candidates)
            {
                if (string.IsNullOrEmpty(candidate)) continue;
                string path = Path.IsPathRooted(candidate) ? candidate : Path.Combine(motionDirectory, candidate);
                if (!File.Exists(path)) continue;
                existingSource = true;
                if (string.Equals(Sha256File(path), expected, StringComparison.Ordinal))
                {
                    source = Path.GetFullPath(path);
                    break;
                }
            }
            if (source == null)
            {
                if (existingSource) throw new InvalidDataException("body source VMD SHA256 differs from song metadata");
                throw new RegistrationUnavailableException("source_vmd_unavailable", "matching source VMD is unavailable");
            }

            if (info.RootElement.TryGetProperty("source_fps", out JsonElement sourceFps) &&
                sourceFps.ValueKind == JsonValueKind.Number &&
                Math.Abs(sourceFps.GetDouble() - 30.0) > 1e-9)
                throw new RegistrationUnavailableException("nonstandard_source_clock",
                    "only standard 30 FPS source clocks are supported");

            // Existing-song imports load the body only after metadata/source
            // authentication, matching the Python registration decision order.
            clip ??= GkClipDecoder.Decode(File.ReadAllBytes(clipPath));
            BodySubsteps(clip.Fps);
            if (HasRootRetargetProfile(source))
                throw new RegistrationUnavailableException("pivot_aware_profile",
                    "pivot-aware root-retarget profiles are unsupported");

            // Read the source the way the body profile reads it. The Python
            // golden used parse_vmd's 'legacy' default here, which is NOT the
            // layout the body converter consumes; that mismatch alone pushed
            // Kick Drum Bass to 1.351e-3 m against a 1e-5 m gate even though
            // the correct layout reproduces its root to 4.09e-9 m.
            VmdMotion motion = VmdReader.Parse(source, BodyInterpolationLayout);
            double[] roots = SampleSourceControllers(motion);
            double residual = MeasureSourceRootMatch(clip, roots);

            // A residual above tolerance means the camera cannot be proven to
            // belong to this body take — not that the camera data is corrupt.
            // `auto` keeps the user's camera on raw coordinates; `required`
            // is the caller's explicit request for strict authentication.
            if (residual > RootMatchToleranceM)
            {
                string mismatch = MismatchMessage(residual);
                if (mode == ModeRequired) throw new InvalidDataException(mismatch);
                return new CameraRegistrationResult
                {
                    Samples = samples,
                    Status = CameraRegistrationStatus.NotApplicable,
                    Mode = mode,
                    Reason = "root_trajectory_mismatch",
                    ResidualMeters = residual,
                    Description = "legacy coordinates; " + mismatch
                };
            }

            var times = new double[samples.Length];
            for (int i = 0; i < samples.Length; i++) times[i] = i / fps;
            double[] corrections = RegistrationCorrections(roots, clip, times, scale);

            var corrected = new CameraSample[samples.Length];
            for (int i = 0; i < samples.Length; i++)
            {
                CameraSample sample = samples[i];
                corrected[i] = sample with
                {
                    X = sample.X + corrections[i * 3 + 0],
                    Y = sample.Y + corrections[i * 3 + 1],
                    Z = sample.Z + corrections[i * 3 + 2]
                };
            }

            return new CameraRegistrationResult
            {
                Samples = corrected,
                Status = CameraRegistrationStatus.Applied,
                Mode = mode,
                Reason = "root_trajectory",
                ResidualMeters = residual,
                Description =
                    $"root-trajectory (authenticated body/source pair; source={SourceFps:g} FPS, body={clip.Fps:g} FPS; " +
                    $"residual={residual.ToString("G3", CultureInfo.InvariantCulture)} m)"
            };
        }
        catch (RegistrationUnavailableException error)
        {
            if (mode == ModeRequired) throw;
            return new CameraRegistrationResult
            {
                Samples = samples,
                Status = CameraRegistrationStatus.NotApplicable,
                Mode = mode,
                Reason = error.Reason,
                Description = "legacy coordinates; " + error.Message
            };
        }
    }
}
