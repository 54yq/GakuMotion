namespace GakuMotion.VmdImporter.Native.Camera;

using System.Buffers.Binary;
using System.Text;
using GakuMotion.VmdImporter.Vmd;

/// <summary>One baked camera sample, matching camera.gkcamera's 9f + u32 payload.</summary>
internal readonly record struct CameraSample(
    double X, double Y, double Z,
    double Qx, double Qy, double Qz, double Qw,
    double Fov, double Size, uint Flags);

/// <summary>
/// Port of tools/vmd2gkmotion/camera.py: VMD camera keys -> camera.gkcamera.
///
/// Coordinate convention, curve channel order, cut detection and the (fov ->
/// frustum size) mapping are the golden implementation's. This is a migration,
/// not a re-derivation: nothing here is re-tuned.
/// </summary>
internal static class CameraBaker
{
    private static ReadOnlySpan<byte> Magic => new byte[] { 0x47, 0x4B, 0x43, 0x41, 0x4D, 0x00, 0x00, 0x00 }; // "GKCAM\0\0\0"
    private const uint Version = 1;
    private const int HeaderSize = 20;   // <8sIfI
    private const int SampleSize = 40;   // <9fI
    private const int MaxSamples = 1_000_000;

    /// <summary>
    /// camera_curve: each of the six channels packs [x1, x2, y1, y2] as four
    /// consecutive bytes out of the 24-byte camera interpolation block.
    /// </summary>
    private static (double P1X, double P1Y, double P2X, double P2Y) Curve(byte[] data, int channel)
    {
        int offset = 4 * channel;
        return (
            data[offset + 0] / 127.0,   // x1
            data[offset + 2] / 127.0,   // y1
            data[offset + 1] / 127.0,   // x2
            data[offset + 3] / 127.0);  // y2
    }

    /// <summary>Quaternion (x,y,z,w) product, in the golden implementation's order.</summary>
    private static (double X, double Y, double Z, double W) Multiply(
        (double X, double Y, double Z, double W) a, (double X, double Y, double Z, double W) b)
    {
        return (
            a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
            a.W * b.Y + a.Y * b.W + a.Z * b.X - a.X * b.Z,
            a.W * b.Z + a.Z * b.W + a.X * b.Y - a.Y * b.X,
            a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);
    }

    private static (double X, double Y, double Z) Rotate((double X, double Y, double Z, double W) q,
        (double X, double Y, double Z) p)
    {
        // p + 2 * cross(q.xyz, cross(q.xyz, p) + q.w * p)
        double cx = q.Y * p.Z - q.Z * p.Y;
        double cy = q.Z * p.X - q.X * p.Z;
        double cz = q.X * p.Y - q.Y * p.X;
        double ix = cx + q.W * p.X;
        double iy = cy + q.W * p.Y;
        double iz = cz + q.W * p.Z;
        double tx = q.Y * iz - q.Z * iy;
        double ty = q.Z * ix - q.X * iz;
        double tz = q.X * iy - q.Y * ix;
        return (p.X + 2.0 * tx, p.Y + 2.0 * ty, p.Z + 2.0 * tz);
    }

    /// <summary>Quaternion.Euler(-rx, 180deg-ry, -rz) as Ry * Rx * Rz.</summary>
    private static ((double X, double Y, double Z) Position, (double X, double Y, double Z, double W) Rotation)
        Pose(IReadOnlyList<double> target, IReadOnlyList<double> angles, double distance, double scale)
    {
        double rx = angles[0], ry = angles[1], rz = angles[2];
        double x = -rx / 2.0, y = (Math.PI - ry) / 2.0, z = -rz / 2.0;
        var qy = (X: 0.0, Y: Math.Sin(y), Z: 0.0, W: Math.Cos(y));
        var qx = (X: Math.Sin(x), Y: 0.0, Z: 0.0, W: Math.Cos(x));
        var qz = (X: 0.0, Y: 0.0, Z: Math.Sin(z), W: Math.Cos(z));
        var q = Multiply(Multiply(qy, qx), qz);

        var scaled = (X: target[0] * -scale, Y: target[1] * scale, Z: target[2] * -scale);
        var offset = Rotate(q, (0.0, 0.0, distance * scale));
        return ((scaled.X + offset.X, scaled.Y + offset.Y, scaled.Z + offset.Z), q);
    }

    /// <summary>Linear interpolation helper with the golden endpoint hold.</summary>
    private static double Interp(double[] xs, double[] ys, double x)
    {
        int n = xs.Length;
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
        double slope = (ys[j + 1] - ys[j]) / (xs[j + 1] - xs[j]);
        return slope * (x - xs[j]) + ys[j];
    }

    public static CameraSample[] Bake(IReadOnlyList<CameraKeyframe> keysIn, double fps, double scale, bool detectCuts)
    {
        if (keysIn == null || keysIn.Count == 0) throw new InvalidDataException("VMD contains no camera keys");
        if (!double.IsFinite(fps) || fps < 30.0 || fps > 240.0 || !double.IsFinite(scale) || scale <= 0.0)
            throw new InvalidDataException("fps must be in [30,240]; scale must be positive and finite");
        double multiple = fps / 30.0;
        if (Math.Abs(multiple - Math.Round(multiple)) > 1e-6)
            throw new InvalidDataException("camera fps must be an integer multiple of 30");

        // Duplicate frame keys use the last occurrence, consistently.
        var unique = new SortedDictionary<uint, CameraKeyframe>();
        foreach (CameraKeyframe key in keysIn) unique[key.Frame] = key;
        var keys = new List<CameraKeyframe>(unique.Values);

        foreach (CameraKeyframe key in keys)
        {
            if (!float.IsFinite(key.Distance) || !float.IsFinite(key.TargetPosition.X) ||
                !float.IsFinite(key.TargetPosition.Y) || !float.IsFinite(key.TargetPosition.Z) ||
                !float.IsFinite(key.RotationEulerRad.X) || !float.IsFinite(key.RotationEulerRad.Y) ||
                !float.IsFinite(key.RotationEulerRad.Z))
                throw new InvalidDataException("non-finite or negative camera key");
            if (key.Fov < 1 || key.Fov > 179) throw new InvalidDataException("camera FOV must be in [1,179]");
            if (key.CurveBytes.Length != 24 || Array.Exists(key.CurveBytes, value => value > 127))
                throw new InvalidDataException("camera interpolation must contain 24 bytes in [0,127]");
            for (int channel = 0; channel < 6; channel++) _ = Curve(key.CurveBytes, channel);
        }

        uint lastFrame = keys[^1].Frame;
        int count = (int)Math.Round(lastFrame * fps / 30.0, MidpointRounding.ToEven) + 1;
        if (count > MaxSamples) throw new InvalidDataException("camera track exceeds 1,000,000 samples");

        var cutFrames = new HashSet<double>();
        if (detectCuts)
        {
            for (int i = 1; i < keys.Count; i++)
            {
                CameraKeyframe a = keys[i - 1];
                CameraKeyframe b = keys[i];
                if (b.Frame - a.Frame != 1) continue;
                double rotationDelta = Math.Max(
                    Math.Max(Math.Abs(a.RotationEulerRad.X - b.RotationEulerRad.X),
                             Math.Abs(a.RotationEulerRad.Y - b.RotationEulerRad.Y)),
                    Math.Abs(a.RotationEulerRad.Z - b.RotationEulerRad.Z));
                double positionDelta = Math.Sqrt(
                    Math.Pow(a.TargetPosition.X - b.TargetPosition.X, 2) +
                    Math.Pow(a.TargetPosition.Y - b.TargetPosition.Y, 2) +
                    Math.Pow(a.TargetPosition.Z - b.TargetPosition.Z, 2)) * scale;
                bool isCut = rotationDelta > Math.PI / 18.0
                             || positionDelta > 1.0
                             || Math.Abs(a.Distance - b.Distance) * scale > 1.0
                             || Math.Abs(a.Fov - b.Fov) > 10.0;
                if (isCut) cutFrames.Add(b.Frame);
            }
        }

        var frameNumbers = new double[keys.Count];
        for (int i = 0; i < keys.Count; i++) frameNumbers[i] = keys[i].Frame;

        var samples = new CameraSample[count];
        var target = new double[3];
        var angles = new double[3];
        for (int i = 0; i < count; i++)
        {
            double frame = i * 30.0 / fps;
            int a = Math.Max(0, UpperBound(frameNumbers, frame) - 1);
            int b = Math.Min(a + 1, keys.Count - 1);
            CameraKeyframe start = keys[a];
            CameraKeyframe end = keys[b];
            double span = end.Frame - (double)start.Frame;
            double t = span > 0.0 ? Math.Clamp((frame - start.Frame) / span, 0.0, 1.0) : 0.0;
            if (cutFrames.Contains(end.Frame)) t = 0.0;

            var curves = new double[6];
            for (int channel = 0; channel < 6; channel++)
            {
                (double p1x, double p1y, double p2x, double p2y) = Curve(end.CurveBytes, channel);
                curves[channel] = BezierCurve.EvalBezier(p1x, p1y, p2x, p2y, t);
            }

            target[0] = start.TargetPosition.X + (end.TargetPosition.X - start.TargetPosition.X) * curves[0];
            target[1] = start.TargetPosition.Y + (end.TargetPosition.Y - start.TargetPosition.Y) * curves[1];
            target[2] = start.TargetPosition.Z + (end.TargetPosition.Z - start.TargetPosition.Z) * curves[2];
            // MMD interpolates the three Euler components with one rotation curve.
            angles[0] = start.RotationEulerRad.X + (end.RotationEulerRad.X - start.RotationEulerRad.X) * curves[3];
            angles[1] = start.RotationEulerRad.Y + (end.RotationEulerRad.Y - start.RotationEulerRad.Y) * curves[3];
            angles[2] = start.RotationEulerRad.Z + (end.RotationEulerRad.Z - start.RotationEulerRad.Z) * curves[3];
            double distance = start.Distance + (end.Distance - start.Distance) * curves[4];
            // Fov is an unsigned VMD field; the golden implementation interpolates
            // it in signed float64, so narrow before subtracting.
            double fov = start.Fov + ((double)end.Fov - start.Fov) * curves[5];

            ((double X, double Y, double Z) Position, (double X, double Y, double Z, double W) Rotation) pose =
                Pose(target, angles, distance, scale);
            double size = Math.Max(0.001, Math.Abs(distance * scale) * Math.Tan(fov * Math.PI / 180.0 / 2.0));
            uint flags = start.Perspective ? 0u : 1u;
            if (cutFrames.Contains(frame)) flags |= 2u;

            samples[i] = new CameraSample(
                pose.Position.X, pose.Position.Y, pose.Position.Z,
                pose.Rotation.X, pose.Rotation.Y, pose.Rotation.Z, pose.Rotation.W,
                fov, size, flags);
        }

        return samples;
    }

    private static int UpperBound(double[] values, double query)
    {
        int low = 0, high = values.Length;
        while (low < high)
        {
            int mid = (low + high) >> 1;
            if (values[mid] <= query) low = mid + 1;
            else high = mid;
        }
        return low;
    }

    public static void Write(string path, IReadOnlyList<CameraSample> samples, double fps)
    {
        string? directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);

        using var stream = new FileStream(path, FileMode.Create, FileAccess.Write);
        var header = new byte[HeaderSize];
        Magic.CopyTo(header.AsSpan(0, 8));
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(8, 4), Version);
        BinaryPrimitives.WriteSingleLittleEndian(header.AsSpan(12, 4), (float)fps);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(16, 4), (uint)samples.Count);
        stream.Write(header, 0, header.Length);

        var payload = new byte[SampleSize];
        foreach (CameraSample sample in samples)
        {
            // Python keeps float64 through baking/registration and narrows once
            // at struct.pack time; mirror that single rounding.
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(0, 4), (float)sample.X);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(4, 4), (float)sample.Y);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(8, 4), (float)sample.Z);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(12, 4), (float)sample.Qx);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(16, 4), (float)sample.Qy);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(20, 4), (float)sample.Qz);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(24, 4), (float)sample.Qw);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(28, 4), (float)sample.Fov);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(32, 4), (float)sample.Size);
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(36, 4), sample.Flags);
            stream.Write(payload, 0, payload.Length);
        }
    }

    /// <summary>Port of import_camera.validate_camera: the strict loader contract.</summary>
    public static (uint Count, float Fps) Validate(string path)
    {
        var info = new FileInfo(path);
        long length = info.Length;
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        var header = new byte[HeaderSize];
        if (length < HeaderSize) throw new InvalidDataException("Camera header is truncated");
        stream.ReadExactly(header, 0, HeaderSize);
        if (!header.AsSpan(0, 8).SequenceEqual(Magic))
            throw new InvalidDataException("Unsupported .gkcamera header or version");
        uint version = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(8, 4));
        float fps = BinaryPrimitives.ReadSingleLittleEndian(header.AsSpan(12, 4));
        uint count = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(16, 4));
        if (version != Version) throw new InvalidDataException("Unsupported .gkcamera header or version");
        if (!float.IsFinite(fps) || fps <= 0.0f || fps > 240.0f || count == 0 || count > MaxSamples)
            throw new InvalidDataException("Invalid camera sample rate or frame count");
        if (length != HeaderSize + (long)SampleSize * count)
            throw new InvalidDataException("Camera length does not match its frame count");

        var payload = new byte[SampleSize];
        for (uint frame = 0; frame < count; frame++)
        {
            try
            {
                stream.ReadExactly(payload, 0, SampleSize);
            }
            catch (EndOfStreamException)
            {
                throw new InvalidDataException($"Invalid camera sample at frame {frame}");
            }
            float[] values = new float[9];
            for (int i = 0; i < 9; i++)
                values[i] = BinaryPrimitives.ReadSingleLittleEndian(payload.AsSpan(i * 4, 4));
            uint flags = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(36, 4));
            double norm = 0.0;
            for (int i = 3; i < 7; i++) norm += (double)values[i] * values[i];
            if (Array.Exists(values, value => !float.IsFinite(value)) ||
                !double.IsFinite(norm) || norm < 1e-8 || norm > 3.4028234663852886e38 ||
                values[7] < 1.0f || values[7] > 179.0f || values[8] <= 0.0f || (flags & ~3u) != 0)
                throw new InvalidDataException($"Invalid camera sample at frame {frame}");
        }
        return (count, fps);
    }

    public static string Describe(IReadOnlyList<CameraSample> samples, double fps)
        => $"{samples.Count} frames at {fps:g} fps, duration {(samples.Count - 1) / fps:F3}s";
}
