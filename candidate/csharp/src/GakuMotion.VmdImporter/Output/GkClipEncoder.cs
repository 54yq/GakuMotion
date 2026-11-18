namespace GakuMotion.VmdImporter.Output;

using System.Buffers.Binary;
using System.Numerics;
using System.Text;
using GakuMotion.VmdImporter.Core;
using GakuMotion.VmdImporter.Rig;

/// <summary>
/// Serializes SourceEvaluationResult and GkClipData into canonical .gkclip binary format.
/// Strictly replicates gkclip_format.py and vmd2gkclip.py.
/// </summary>
public static class GkClipEncoder
{
    /// <summary>
    /// Serializes GkClipData into a byte array.
    /// </summary>
    public static byte[] Encode(GkClipData clip)
    {
        using var ms = new MemoryStream();
        Encode(clip, ms);
        return ms.ToArray();
    }

    /// <summary>
    /// Serializes GkClipData into an output stream.
    /// </summary>
    public static void Encode(GkClipData clip, Stream stream)
    {
        if (clip.Version is < 1 or > 5)
            throw new ArgumentException($"unsupported gkclip version: {clip.Version}", nameof(clip));

        if (clip.Version < 4 && clip.BlendshapeNames.Count > 0)
            throw new InvalidOperationException("blendshape channels require gkclip v4+");

        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);

        // 1. Magic: 8 bytes "GKCLIPCL"
        writer.Write(GkClipFormat.MagicBytes);

        // 2. Header fields (24 bytes at offset 8):
        // version (u32), fps (f32), total_frames (u32), duration (f32), bone_count (u32), root_scale (f32)
        writer.Write((uint)clip.Version);
        writer.Write(clip.Fps);
        writer.Write((uint)clip.TotalFrames);
        writer.Write(clip.DurationSeconds);
        writer.Write((uint)clip.Bones.Count);
        writer.Write(clip.RootScale);

        // 3. Bone table
        foreach (var b in clip.Bones)
        {
            if (b.HeadSpaceEyeDelta && (clip.Version < 5 || (b.HumanoidId != 21 && b.HumanoidId != 22) || !b.HasRotation))
                throw new InvalidOperationException("Head-space eye deltas require v5, an eye HID and rotation data");

            byte[] rawName = Encoding.UTF8.GetBytes(b.Name);
            if (rawName.Length > 255)
                throw new InvalidOperationException($"bone name too long for u8 length: {b.Name}");

            writer.Write(b.HumanoidId);
            writer.Write((byte)rawName.Length);
            writer.Write(b.Flags);
            writer.Write(rawName);
        }

        int nf = clip.TotalFrames;
        int nb = clip.Bones.Count;

        // 4. Interleaved payload: per frame [root 3f][deltas nb*4f]
        for (int f = 0; f < nf; f++)
        {
            Vector3 root = (clip.RootPositions != null && f < clip.RootPositions.Length)
                ? clip.RootPositions[f]
                : Vector3.Zero;

            writer.Write(root.X);
            writer.Write(root.Y);
            writer.Write(root.Z);

            for (int b = 0; b < nb; b++)
            {
                Quaternion q = (clip.BoneDeltas != null)
                    ? clip.BoneDeltas[f, b]
                    : Quaternion.Identity;

                // Defensive re-normalization in float32 matching gkclip_format.py:198-200
                float norm = MathF.Sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
                if (norm < 1e-9f) norm = 1.0f;
                float inv = 1.0f / norm;

                writer.Write(q.X * inv);
                writer.Write(q.Y * inv);
                writer.Write(q.Z * inv);
                writer.Write(q.W * inv);
            }
        }

        // 5. Camera section
        if (clip.CameraFrames != null && clip.CameraFrames.GetLength(0) == nf)
        {
            writer.Write((byte)1);
            for (int f = 0; f < nf; f++)
            {
                for (int c = 0; c < 7; c++)
                {
                    writer.Write(clip.CameraFrames[f, c]);
                }
            }
        }
        else
        {
            writer.Write((byte)0);
        }

        // 6. Blendshape section (v4+)
        if (clip.Version >= 4)
        {
            var names = clip.BlendshapeNames;
            if (names.Count > GkClipFormat.MaxBlendshapes)
                throw new InvalidOperationException("too many blendshape channels");

            writer.Write((uint)names.Count);
            foreach (var name in names)
            {
                byte[] raw = Encoding.UTF8.GetBytes(name);
                if (raw.Length == 0 || raw.Length > 65535)
                    throw new InvalidOperationException("invalid blendshape name length");

                writer.Write((ushort)raw.Length);
                writer.Write(raw);
            }

            if (names.Count > 0)
            {
                if (clip.BlendshapeWeights == null ||
                    clip.BlendshapeWeights.GetLength(0) != nf ||
                    clip.BlendshapeWeights.GetLength(1) != names.Count)
                {
                    throw new InvalidOperationException("blendshape frame/channel count mismatch");
                }

                for (int f = 0; f < nf; f++)
                {
                    for (int c = 0; c < names.Count; c++)
                    {
                        float w = clip.BlendshapeWeights[f, c];
                        if (!float.IsFinite(w) || w < 0.0f || w > 1.0f)
                            throw new InvalidOperationException("blendshape weights must be finite in [0, 1]");

                        writer.Write(w);
                    }
                }
            }
        }
    }

    /// <summary>
    /// Constructs and serializes a candidate GkClipData from SourceEvaluationResult.
    /// Replicates vmd2gkclip.py full conversion pipeline.
    /// </summary>
    public static GkClipData FromEvaluationResult(
        SourceEvaluationResult result,
        GakumasBoneProfile? profile = null,
        VirtualMMDRig? rig = null,
        int version = GkClipFormat.DefaultVersion)
    {
        profile ??= GakumasBoneProfile.Default;
        int totalFrames = result.TotalFrames;
        var bones = profile.Bones;
        int boneCount = bones.Count;

        // 1. Build bone name index map for source evaluation result
        var sourceIndexMap = new Dictionary<string, int>(result.BoneNames.Count);
        for (int i = 0; i < result.BoneNames.Count; i++)
        {
            sourceIndexMap[result.BoneNames[i]] = i;
        }

        // 2. Build bone metadata list
        var metaList = new List<GkClipBoneMeta>(boneCount);
        for (int i = 0; i < boneCount; i++)
        {
            var b = bones[i];
            metaList.Add(new GkClipBoneMeta((ushort)b.HumanoidId, b.Name, hasRotation: true, headSpaceEyeDelta: b.HeadSpaceEyeDelta));
        }

        // 3. Frame alignment K matrix calculation on reference frame 0
        Dictionary<int, Quaternion> K;
        if (rig != null && totalFrames > 0)
        {
            var worldRef = new Dictionary<string, Quaternion>(result.BoneNames.Count);
            var posRef = new Dictionary<string, Vector3>(result.BoneNames.Count);
            for (int i = 0; i < result.BoneNames.Count; i++)
            {
                string bName = result.BoneNames[i];
                worldRef[bName] = result.DenseRotations[0, i];
                posRef[bName] = result.DensePositions[0, i];
            }

            if (totalFrames > 1)
            {
                int lateFrame = totalFrames - 1;
                var worldLate = new Dictionary<string, Quaternion>(result.BoneNames.Count);
                var posLate = new Dictionary<string, Vector3>(result.BoneNames.Count);
                for (int i = 0; i < result.BoneNames.Count; i++)
                {
                    string bName = result.BoneNames[i];
                    worldLate[bName] = result.DenseRotations[lateFrame, i];
                    posLate[bName] = result.DensePositions[lateFrame, i];
                }

                float drift = FrameAlignment.MeasureOffsetDrift(rig, worldRef, posRef, worldLate, posLate);
                FrameAlignment.AssertOffsetDrift(drift, 1.0f);
            }

            K = FrameAlignment.BuildFrameAlignment(rig, worldRef, posRef);
        }
        else
        {
            // Evaluate reference rig K
            var refRig = new VirtualMMDRig(useSourceSkeleton: true);
            var worldRef = new Dictionary<string, Quaternion>(result.BoneNames.Count);
            var posRef = new Dictionary<string, Vector3>(result.BoneNames.Count);
            if (totalFrames > 0)
            {
                for (int i = 0; i < result.BoneNames.Count; i++)
                {
                    string bName = result.BoneNames[i];
                    worldRef[bName] = result.DenseRotations[0, i];
                    posRef[bName] = result.DensePositions[0, i];
                }
            }
            K = FrameAlignment.BuildFrameAlignment(refRig, worldRef, posRef);
        }

        // 4. Build topological chain order and parent map
        var order = new List<string>(boneCount);
        for (int i = 0; i < boneCount; i++)
            order.Add(bones[i].Name);

        var presentBones = new HashSet<string>(order);
        var parents = new Dictionary<string, string?>(boneCount);
        foreach (string name in order)
        {
            if (FrameAlignment.GakumasParent.TryGetValue(name, out var p) && presentBones.Contains(p))
                parents[name] = p;
            else
                parents[name] = null;
        }

        // 5. Evaluate dense canonical local deltas per frame
        var boneDeltas = new Quaternion[totalFrames, boneCount];
        int headIdx = sourceIndexMap.GetValueOrDefault("頭", -1);

        for (int f = 0; f < totalFrames; f++)
        {
            var worldBuf = new Quaternion[boneCount];
            for (int b = 0; b < boneCount; b++)
            {
                var bone = bones[b];
                string vmdName = bone.VmdName;
                if (!sourceIndexMap.TryGetValue(vmdName, out int srcIdx) && bone.FallbackVmdName != null)
                {
                    sourceIndexMap.TryGetValue(bone.FallbackVmdName, out srcIdx);
                }

                Quaternion w = (srcIdx >= 0 && srcIdx < result.DenseRotations.GetLength(1))
                    ? result.DenseRotations[f, srcIdx]
                    : Quaternion.Identity;

                // Re-base world rotation with K alignment
                if (K.TryGetValue(bone.HumanoidId, out var k))
                {
                    w = Math32.NormalizeQuaternionWxyz(w * k);
                }

                worldBuf[b] = w;
            }

            // Decompose along Gakumas parent chain
            var deltas = FrameAlignment.WorldToCanonicalDeltas(order, worldBuf, parents);
            for (int b = 0; b < boneCount; b++)
            {
                boneDeltas[f, b] = deltas[b];
            }

            // Eye payload: Head-space delta (without target frame K)
            for (int b = 0; b < boneCount; b++)
            {
                var bone = bones[b];
                if (bone.HeadSpaceEyeDelta && headIdx >= 0)
                {
                    string vmdName = bone.VmdName;
                    if (sourceIndexMap.TryGetValue(vmdName, out int eyeSrcIdx))
                    {
                        Quaternion headWorld = result.DenseRotations[f, headIdx];
                        Quaternion eyeWorld = result.DenseRotations[f, eyeSrcIdx];
                        boneDeltas[f, b] = Math32.NormalizeQuaternionWxyz(Quaternion.Inverse(headWorld) * eyeWorld);
                    }
                }
            }
        }

        // 6. Blendshape weights
        var blendshapeNames = result.MorphWeights.Keys.OrderBy(x => x, StringComparer.Ordinal).ToList();
        float[,]? blendshapeWeights = null;
        if (blendshapeNames.Count > 0)
        {
            blendshapeWeights = new float[totalFrames, blendshapeNames.Count];
            for (int c = 0; c < blendshapeNames.Count; c++)
            {
                float[] weights = result.MorphWeights[blendshapeNames[c]];
                for (int f = 0; f < totalFrames; f++)
                {
                    float w = f < weights.Length ? weights[f] : 0.0f;
                    blendshapeWeights[f, c] = Math.Clamp(w, 0.0f, 1.0f);
                }
            }
        }

        return new GkClipData(result.Fps)
        {
            Version = version,
            TotalFrames = totalFrames,
            DurationSeconds = result.DurationSeconds,
            RootScale = profile.RootDisplacementScale,
            Bones = metaList,
            RootPositions = (Vector3[])result.RootTrajectory.Clone(),
            BoneDeltas = boneDeltas,
            BlendshapeNames = blendshapeNames,
            BlendshapeWeights = blendshapeWeights
        };
    }

    /// <summary>
    /// Serializes SourceEvaluationResult directly to binary .gkclip bytes.
    /// </summary>
    public static byte[] Encode(
        SourceEvaluationResult result,
        GakumasBoneProfile? profile = null,
        VirtualMMDRig? rig = null,
        int version = GkClipFormat.DefaultVersion)
    {
        var clip = FromEvaluationResult(result, profile, rig, version);
        return Encode(clip);
    }
}
