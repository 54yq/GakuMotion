namespace GakuMotion.VmdImporter.Output;

using System.Buffers.Binary;
using System.Numerics;
using System.Text;

/// <summary>
/// Readback decoder that reconstructs poses, metadata, and trajectories from binary .gkclip files.
/// Strictly replicates gkclip_format.py deserialize and verify_roundtrip.
/// </summary>
public static class GkClipDecoder
{
    /// <summary>
    /// Deserializes a .gkclip binary byte buffer into GkClipData.
    /// </summary>
    public static GkClipData Decode(ReadOnlySpan<byte> data)
    {
        if (data.Length < GkClipFormat.BoneTableOffset)
            throw new InvalidDataException("not a gkclip file (too short for header)");

        // Validate 8-byte magic: "GKCLIPCL"
        if (!data[..8].SequenceEqual(GkClipFormat.MagicBytes))
            throw new InvalidDataException("not a gkclip file (magic mismatch: expected GKCLIPCL)");

        // Header fields start at offset 8 (NOT offset 4)
        uint version = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(8, 4));
        float fps = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(12, 4));
        uint totalFrames = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(16, 4));
        float duration = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(20, 4));
        uint boneCount = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(24, 4));
        float rootScale = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(28, 4));

        if (version is < 1 or > 5)
            throw new InvalidDataException($"unsupported gkclip version: {version}");

        if (!float.IsFinite(fps) || fps <= 0.0f || totalFrames == 0 || totalFrames > 1_000_000 || boneCount == 0 || boneCount > 256)
            throw new InvalidDataException("invalid gkclip frame/bone/fps header");

        var clip = new GkClipData(fps)
        {
            Version = (int)version,
            TotalFrames = (int)totalFrames,
            DurationSeconds = duration,
            RootScale = rootScale
        };

        int off = GkClipFormat.BoneTableOffset; // 32

        // Bone table unpack
        for (int b = 0; b < (int)boneCount; b++)
        {
            if (off + 4 > data.Length)
                throw new InvalidDataException("truncated bone table header");

            ushort hid = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(off, 2));
            byte nlen = data[off + 2];
            byte flags = data[off + 3];
            off += 4;

            bool hasRot = (flags & 1) != 0;
            bool eyeDelta = (flags & 2) != 0;

            if (version < 5 && eyeDelta)
                throw new InvalidDataException("invalid eye representation flags (eye delta requires v5)");
            if (version >= 5 && ((flags & ~3) != 0 || (eyeDelta && (hid != 21 && hid != 22 || !hasRot))))
                throw new InvalidDataException("invalid eye representation flags");

            if (off + nlen > data.Length)
                throw new InvalidDataException("truncated bone name bytes");

            string name = Encoding.UTF8.GetString(data.Slice(off, nlen));
            off += nlen;

            clip.Bones.Add(new GkClipBoneMeta(hid, name, hasRot, eyeDelta));
        }

        int nf = (int)totalFrames;
        int nb = (int)boneCount;
        int payloadBytes = nf * (12 + 16 * nb);
        if (off + payloadBytes > data.Length)
            throw new InvalidDataException("truncated bone/root payload");

        clip.RootPositions = new Vector3[nf];
        clip.BoneDeltas = new Quaternion[nf, nb];

        for (int f = 0; f < nf; f++)
        {
            float rx = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(off, 4));
            float ry = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(off + 4, 4));
            float rz = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(off + 8, 4));
            clip.RootPositions[f] = new Vector3(rx, ry, rz);
            off += 12;

            for (int b = 0; b < nb; b++)
            {
                float qx = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(off, 4));
                float qy = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(off + 4, 4));
                float qz = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(off + 8, 4));
                float qw = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(off + 12, 4));
                clip.BoneDeltas[f, b] = new Quaternion(qx, qy, qz, qw);
                off += 16;
            }
        }

        // Camera section
        if (off < data.Length)
        {
            byte hasCam = data[off++];
            if (hasCam > 1)
                throw new InvalidDataException("invalid camera-present flag");

            if (hasCam == 1)
            {
                int camBytes = nf * 7 * 4;
                if (off + camBytes > data.Length)
                    throw new InvalidDataException("truncated camera payload");

                clip.CameraFrames = new float[nf, 7];
                for (int f = 0; f < nf; f++)
                {
                    for (int c = 0; c < 7; c++)
                    {
                        clip.CameraFrames[f, c] = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(off, 4));
                        off += 4;
                    }
                }
            }
        }

        // Blendshape section (v4+)
        if (clip.Version >= 4 && off < data.Length)
        {
            if (off + 4 > data.Length)
                throw new InvalidDataException("missing v4 blendshape table");

            uint channelCount = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(off, 4));
            off += 4;

            if (channelCount > GkClipFormat.MaxBlendshapes)
                throw new InvalidDataException("too many blendshape channels");

            clip.BlendshapeNames = new List<string>((int)channelCount);
            for (int c = 0; c < (int)channelCount; c++)
            {
                if (off + 2 > data.Length)
                    throw new InvalidDataException("truncated blendshape name length");

                ushort n = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(off, 2));
                off += 2;

                if (n == 0 || off + n > data.Length)
                    throw new InvalidDataException("truncated/empty blendshape name");

                string bName = Encoding.UTF8.GetString(data.Slice(off, n));
                off += n;
                clip.BlendshapeNames.Add(bName);
            }

            if (new HashSet<string>(clip.BlendshapeNames).Count != (int)channelCount)
                throw new InvalidDataException("duplicate blendshape names");

            int weightBytes = nf * (int)channelCount * 4;
            if (off + weightBytes != data.Length)
                throw new InvalidDataException("blendshape payload size mismatch");

            clip.BlendshapeWeights = new float[nf, (int)channelCount];
            for (int f = 0; f < nf; f++)
            {
                for (int c = 0; c < (int)channelCount; c++)
                {
                    float w = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(off, 4));
                    off += 4;

                    if (!float.IsFinite(w) || w < 0.0f || w > 1.0f)
                        throw new InvalidDataException("blendshape weights must be finite in [0, 1]");

                    clip.BlendshapeWeights[f, c] = w;
                }
            }
        }

        return clip;
    }

    /// <summary>
    /// Deserializes a stream containing .gkclip data into GkClipData.
    /// </summary>
    public static GkClipData Decode(Stream stream)
    {
        if (stream is MemoryStream ms && ms.TryGetBuffer(out ArraySegment<byte> seg))
        {
            return Decode(seg.AsSpan());
        }

        using var memory = new MemoryStream();
        stream.CopyTo(memory);
        return Decode(memory.ToArray());
    }

    /// <summary>
    /// Verifies round-trip accuracy between original and decoded clips.
    /// Returns maximum angular error in degrees across all frames and bones.
    /// Replicates Python gkclip_format.py verify_roundtrip.
    /// </summary>
    public static float VerifyRoundtrip(GkClipData original, GkClipData decoded)
    {
        if (decoded.TotalFrames != original.TotalFrames)
            throw new InvalidOperationException($"frame count mismatch: {decoded.TotalFrames} != {original.TotalFrames}");

        if (decoded.Bones.Count != original.Bones.Count)
            throw new InvalidOperationException($"bone count mismatch: {decoded.Bones.Count} != {original.Bones.Count}");

        for (int i = 0; i < original.Bones.Count; i++)
        {
            if (decoded.Bones[i].Name != original.Bones[i].Name)
                throw new InvalidOperationException($"bone table corrupted at {i}: {decoded.Bones[i].Name} != {original.Bones[i].Name}");

            if (decoded.Bones[i].HeadSpaceEyeDelta != original.Bones[i].HeadSpaceEyeDelta)
                throw new InvalidOperationException($"eye representation flag corrupted at {i}");
        }

        if (original.BlendshapeNames.Count != decoded.BlendshapeNames.Count)
            throw new InvalidOperationException("blendshape channel count mismatch");

        for (int i = 0; i < original.BlendshapeNames.Count; i++)
        {
            if (original.BlendshapeNames[i] != decoded.BlendshapeNames[i])
                throw new InvalidOperationException($"blendshape name mismatch at {i}: {decoded.BlendshapeNames[i]} != {original.BlendshapeNames[i]}");
        }

        int nf = original.TotalFrames;
        int nb = original.Bones.Count;
        double maxAngularErrorDeg = 0.0;

        for (int f = 0; f < nf; f++)
        {
            for (int b = 0; b < nb; b++)
            {
                Quaternion qa = original.BoneDeltas[f, b];
                Quaternion qb = decoded.BoneDeltas[f, b];

                // Normalize in double precision before angular error computation
                double aX = qa.X, aY = qa.Y, aZ = qa.Z, aW = qa.W;
                double aNorm = Math.Sqrt(aX * aX + aY * aY + aZ * aZ + aW * aW);
                if (aNorm > 1e-12) { aX /= aNorm; aY /= aNorm; aZ /= aNorm; aW /= aNorm; }

                double bX = qb.X, bY = qb.Y, bZ = qb.Z, bW = qb.W;
                double bNorm = Math.Sqrt(bX * bX + bY * bY + bZ * bZ + bW * bW);
                if (bNorm > 1e-12) { bX /= bNorm; bY /= bNorm; bZ /= bNorm; bW /= bNorm; }

                double dot = Math.Abs(aX * bX + aY * bY + aZ * bZ + aW * bW);
                dot = Math.Clamp(dot, -1.0, 1.0);

                double angleDeg = 2.0 * Math.Acos(dot) * (180.0 / Math.PI);
                if (angleDeg > maxAngularErrorDeg)
                    maxAngularErrorDeg = angleDeg;
            }
        }

        return (float)maxAngularErrorDeg;
    }
}
