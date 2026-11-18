namespace GakuMotion.VmdImporter.Vmd;

using System.Buffers.Binary;
using System.Numerics;
using System.Text;

/// <summary>
/// Binary VMD (Vocaloid Motion Data) parser.
/// Parses standard VMD 0002 and legacy VMD files matching the MMD specification and Python vmd_parser.py.
/// </summary>
public static class VmdReader
{
    private static readonly Encoding ShiftJis;

    static VmdReader()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        ShiftJis = Encoding.GetEncoding(932, EncoderFallback.ReplacementFallback, DecoderFallback.ReplacementFallback);
    }

    /// <summary>
    /// Parses a VMD file from disk.
    /// </summary>
    public static VmdMotion Parse(string filePath, InterpolationLayout layout = InterpolationLayout.ChannelBlocks)
    {
        using var fs = File.OpenRead(filePath);
        return Parse(fs, layout);
    }

    /// <summary>
    /// Parses a VMD binary payload from a byte array.
    /// </summary>
    public static VmdMotion Parse(byte[] bytes, InterpolationLayout layout = InterpolationLayout.ChannelBlocks)
    {
        using var ms = new MemoryStream(bytes, writable: false);
        return Parse(ms, layout);
    }

    /// <summary>
    /// Parses a VMD binary payload from a ReadOnlySpan of bytes.
    /// </summary>
    public static VmdMotion Parse(ReadOnlySpan<byte> bytes, InterpolationLayout layout = InterpolationLayout.ChannelBlocks)
    {
        return Parse(bytes.ToArray(), layout);
    }

    /// <summary>
    /// Parses a binary VMD stream and returns a populated VmdMotion instance.
    /// </summary>
    public static VmdMotion Parse(Stream stream, InterpolationLayout layout = InterpolationLayout.ChannelBlocks)
    {
        using var reader = new BinaryReader(stream, Encoding.ASCII, leaveOpen: true);

        // Header: 30 bytes ASCII
        byte[] rawHeader = reader.ReadBytes(30);
        if (rawHeader.Length < 30)
            throw new InvalidDataException($"Unexpected end of stream while reading VMD header (got {rawHeader.Length} bytes, expected 30)");

        string version = DecodeNullTerminated(rawHeader, Encoding.ASCII);

        int modelLen = version.StartsWith("Vocaloid Motion Data 0002", StringComparison.Ordinal) ? 20 : 10;
        byte[] rawModel = reader.ReadBytes(modelLen);
        if (rawModel.Length < modelLen)
            throw new InvalidDataException($"Unexpected end of stream while reading VMD model name (got {rawModel.Length} bytes, expected {modelLen})");

        string modelName = DecodeNullTerminated(rawModel, ShiftJis);

        uint maxFrame = 0;

        // Bone keyframes chunk
        byte[] rawBoneCount = reader.ReadBytes(4);
        uint boneCount = rawBoneCount.Length == 4 ? BinaryPrimitives.ReadUInt32LittleEndian(rawBoneCount) : 0;

        var boneDict = new Dictionary<string, List<BoneKeyframe>>();
        byte[] curveBytes = new byte[64];

        for (uint i = 0; i < boneCount; i++)
        {
            byte[] rawBname = reader.ReadBytes(15);
            if (rawBname.Length < 15)
                throw new EndOfStreamException($"Unexpected end of stream while reading bone name at index {i}");

            string bname = DecodeNullTerminated(rawBname, ShiftJis);
            uint frame = reader.ReadUInt32();
            float px = reader.ReadSingle();
            float py = reader.ReadSingle();
            float pz = reader.ReadSingle();
            float rx = reader.ReadSingle();
            float ry = reader.ReadSingle();
            float rz = reader.ReadSingle();
            float rw = reader.ReadSingle();

            int curveRead = reader.Read(curveBytes, 0, 64);
            if (curveRead < 64)
                throw new EndOfStreamException($"Unexpected end of stream while reading Bézier curve at bone index {i}");

            var (cx, cy, cz, cr) = BezierCurve.UnpackBoneBezier(curveBytes, layout);

            var kf = new BoneKeyframe(
                bname,
                frame,
                new Vector3(px, py, pz),
                new Quaternion(rx, ry, rz, rw),
                cx, cy, cz, cr);

            if (!boneDict.TryGetValue(bname, out var list))
            {
                list = new List<BoneKeyframe>();
                boneDict[bname] = list;
            }
            list.Add(kf);

            if (frame > maxFrame)
                maxFrame = frame;
        }

        var boneKeyframes = new Dictionary<string, IReadOnlyList<BoneKeyframe>>(boneDict.Count);
        foreach (var kvp in boneDict)
        {
            kvp.Value.Sort((a, b) => a.Frame.CompareTo(b.Frame));
            boneKeyframes[kvp.Key] = kvp.Value;
        }

        // Morph keyframes chunk
        byte[] rawMorphCount = reader.ReadBytes(4);
        var morphDict = new Dictionary<string, List<MorphKeyframe>>();
        if (rawMorphCount.Length == 4)
        {
            uint morphCount = BinaryPrimitives.ReadUInt32LittleEndian(rawMorphCount);
            for (uint i = 0; i < morphCount; i++)
            {
                byte[] rawMname = reader.ReadBytes(15);
                if (rawMname.Length < 15)
                    throw new EndOfStreamException($"Unexpected end of stream while reading morph name at index {i}");

                string mname = DecodeNullTerminated(rawMname, ShiftJis);
                uint frame = reader.ReadUInt32();
                float weight = reader.ReadSingle();

                var mkf = new MorphKeyframe(mname, frame, weight);
                if (!morphDict.TryGetValue(mname, out var mlist))
                {
                    mlist = new List<MorphKeyframe>();
                    morphDict[mname] = mlist;
                }
                mlist.Add(mkf);

                if (frame > maxFrame)
                    maxFrame = frame;
            }
        }

        var morphKeyframes = new Dictionary<string, IReadOnlyList<MorphKeyframe>>(morphDict.Count);
        foreach (var kvp in morphDict)
        {
            kvp.Value.Sort((a, b) => a.Frame.CompareTo(b.Frame));
            morphKeyframes[kvp.Key] = kvp.Value;
        }

        // Camera keyframes chunk
        byte[] rawCamCount = reader.ReadBytes(4);
        var cameraKeyframes = new List<CameraKeyframe>();
        if (rawCamCount.Length == 4)
        {
            uint camCount = BinaryPrimitives.ReadUInt32LittleEndian(rawCamCount);
            for (uint i = 0; i < camCount; i++)
            {
                uint frame = reader.ReadUInt32();
                float dist = reader.ReadSingle();
                float tx = reader.ReadSingle();
                float ty = reader.ReadSingle();
                float tz = reader.ReadSingle();
                float rx = reader.ReadSingle();
                float ry = reader.ReadSingle();
                float rz = reader.ReadSingle();

                byte[] camCurves = reader.ReadBytes(24);
                if (camCurves.Length < 24)
                    throw new EndOfStreamException($"Unexpected end of stream while reading camera curves at index {i}");

                uint fov = reader.ReadUInt32();
                byte perspByte = reader.ReadByte();
                // 0 is perspective, 1 is orthographic
                bool perspective = perspByte == 0;

                var ckf = new CameraKeyframe(
                    frame,
                    dist,
                    new Vector3(tx, ty, tz),
                    new Vector3(rx, ry, rz),
                    camCurves,
                    fov,
                    perspective);
                cameraKeyframes.Add(ckf);

                if (frame > maxFrame)
                    maxFrame = frame;
            }
            cameraKeyframes.Sort((a, b) => a.Frame.CompareTo(b.Frame));
        }

        // Optional Light keyframes
        var propertyKeyframes = new List<PropertyKeyframe>();
        byte[] rawLightCount = reader.ReadBytes(4);
        if (rawLightCount.Length == 4)
        {
            uint lightCount = BinaryPrimitives.ReadUInt32LittleEndian(rawLightCount);
            SkipBytes(stream, 28L * lightCount);

            // Optional Shadow keyframes
            byte[] rawShadowCount = reader.ReadBytes(4);
            if (rawShadowCount.Length == 4)
            {
                uint shadowCount = BinaryPrimitives.ReadUInt32LittleEndian(rawShadowCount);
                SkipBytes(stream, 9L * shadowCount);

                // Optional Property / IK Switch keyframes
                byte[] rawIkCount = reader.ReadBytes(4);
                if (rawIkCount.Length == 4)
                {
                    uint ikCount = BinaryPrimitives.ReadUInt32LittleEndian(rawIkCount);
                    for (uint i = 0; i < ikCount; i++)
                    {
                        uint ikFrame = reader.ReadUInt32();
                        byte visibleByte = reader.ReadByte();
                        bool visible = visibleByte != 0;
                        uint numIk = reader.ReadUInt32();

                        var ikSwitches = new Dictionary<string, bool>((int)numIk);
                        for (uint j = 0; j < numIk; j++)
                        {
                            byte[] rawIkName = reader.ReadBytes(20);
                            if (rawIkName.Length < 20)
                                throw new EndOfStreamException($"Unexpected end of stream while reading IK bone name at IK index {i}, item {j}");

                            string ikName = DecodeNullTerminated(rawIkName, ShiftJis);
                            byte enabledByte = reader.ReadByte();
                            ikSwitches[ikName] = enabledByte != 0;
                        }

                        var pkf = new PropertyKeyframe(ikFrame, visible, ikSwitches);
                        propertyKeyframes.Add(pkf);

                        if (ikFrame > maxFrame)
                            maxFrame = ikFrame;
                    }
                    propertyKeyframes.Sort((a, b) => a.Frame.CompareTo(b.Frame));
                }
            }
        }

        return new VmdMotion
        {
            Version = version,
            ModelName = modelName,
            MaxFrame = maxFrame,
            BoneKeyframes = boneKeyframes,
            MorphKeyframes = morphKeyframes,
            CameraKeyframes = cameraKeyframes,
            PropertyKeyframes = propertyKeyframes
        };
    }

    private static string DecodeNullTerminated(ReadOnlySpan<byte> bytes, Encoding encoding)
    {
        int nullIdx = bytes.IndexOf((byte)0);
        ReadOnlySpan<byte> slice = nullIdx >= 0 ? bytes[..nullIdx] : bytes;
        return encoding.GetString(slice);
    }

    private static void SkipBytes(Stream stream, long count)
    {
        if (count <= 0)
            return;

        if (stream.CanSeek)
        {
            stream.Seek(count, SeekOrigin.Current);
        }
        else
        {
            byte[] buffer = new byte[Math.Min(count, 4096)];
            long remaining = count;
            while (remaining > 0)
            {
                int toRead = (int)Math.Min(remaining, buffer.Length);
                int read = stream.Read(buffer, 0, toRead);
                if (read == 0)
                    throw new EndOfStreamException("Unexpected end of stream while skipping bytes");
                remaining -= read;
            }
        }
    }
}
