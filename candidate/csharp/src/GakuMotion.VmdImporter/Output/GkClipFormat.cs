namespace GakuMotion.VmdImporter.Output;

using System.Numerics;
using System.Text;

/// <summary>
/// Constants and layout descriptors for the .gkclip canonical distribution format.
/// Strictly replicates DevAgents/architecture/ACTION_FORMATS.md and gkclip_format.py.
/// </summary>
public static class GkClipFormat
{
    /// <summary>
    /// Magic string prefix: "GKCLIP" (6 bytes ASCII).
    /// </summary>
    public const string MagicPrefix = "GKCLIP";

    /// <summary>
    /// Format identifier: "CL" (2 bytes ASCII).
    /// </summary>
    public const string FormatId = "CL";

    /// <summary>
    /// Complete 8-byte magic: "GKCLIPCL".
    /// </summary>
    public const string MagicFull = "GKCLIPCL";

    /// <summary>
    /// Byte representation of the 8-byte magic.
    /// </summary>
    public static ReadOnlySpan<byte> MagicBytes => "GKCLIPCL"u8;

    /// <summary>
    /// Default format version emitted (v4: bone frames + named blendshape weights).
    /// </summary>
    public const int DefaultVersion = 4;

    /// <summary>
    /// Header payload start offset in bytes (immediately after the 8-byte magic).
    /// NOTE: Must be 8, NEVER 4 (avoids legacy gkmotion alignment bug).
    /// </summary>
    public const int HeaderOffset = 8;

    /// <summary>
    /// Length of the 6 fixed header fields in bytes (Version, Fps, TotalFrames, Duration, BoneCount, RootScale = 24 bytes).
    /// </summary>
    public const int HeaderFieldsLength = 24;

    /// <summary>
    /// Offset where the bone table begins (8 + 24 = 32 bytes).
    /// </summary>
    public const int BoneTableOffset = 32;

    /// <summary>
    /// Maximum allowed blendshape channels (1024).
    /// </summary>
    public const int MaxBlendshapes = 1024;
}

/// <summary>
/// One entry of the .gkclip bone table.
/// </summary>
public sealed class GkClipBoneMeta
{
    public ushort HumanoidId { get; init; }
    public string Name { get; init; } = string.Empty;
    public bool HasRotation { get; init; } = true;
    public bool HeadSpaceEyeDelta { get; init; }

    public byte Flags => (byte)((HasRotation ? 1 : 0) | (HeadSpaceEyeDelta ? 2 : 0));

    public GkClipBoneMeta() { }

    public GkClipBoneMeta(ushort humanoidId, string name, bool hasRotation = true, bool headSpaceEyeDelta = false)
    {
        HumanoidId = humanoidId;
        Name = name;
        HasRotation = hasRotation;
        HeadSpaceEyeDelta = headSpaceEyeDelta;
    }

    public override string ToString() => $"GkClipBoneMeta(HID={HumanoidId}, Name='{Name}', Rot={HasRotation}, EyeDelta={HeadSpaceEyeDelta})";
}

/// <summary>
/// In-memory representation of a .gkclip canonical local deltas motion file.
/// Replicates Python gkclip_format.py GkClipData.
/// </summary>
public sealed class GkClipData
{
    public int Version { get; set; } = GkClipFormat.DefaultVersion;
    public float Fps { get; set; } = 30.0f;
    public int TotalFrames { get; set; }
    public float DurationSeconds { get; set; }
    public float RootScale { get; set; } = RootDisplacement.DefaultScale;

    public List<GkClipBoneMeta> Bones { get; set; } = new();

    /// <summary>
    /// Root displacement in meters from the clip's own frame 0: [frame].
    /// Shape: (TotalFrames, 3).
    /// </summary>
    public Vector3[] RootPositions { get; set; } = Array.Empty<Vector3>();

    /// <summary>
    /// Canonical local delta quaternions: [frame, boneIndex].
    /// Shape: (TotalFrames, BoneCount).
    /// </summary>
    public Quaternion[,] BoneDeltas { get; set; } = new Quaternion[0, 0];

    /// <summary>
    /// Optional camera frame data: [frame, 7].
    /// </summary>
    public float[,]? CameraFrames { get; set; }

    /// <summary>
    /// Names of animated blendshapes (v4+).
    /// </summary>
    public List<string> BlendshapeNames { get; set; } = new();

    /// <summary>
    /// Normalized blendshape weights in [0.0, 1.0]: [frame, blendshapeIndex] (v4+).
    /// </summary>
    public float[,]? BlendshapeWeights { get; set; }

    public GkClipData(float fps = 30.0f)
    {
        Fps = fps;
    }

    /// <summary>
    /// Verifies round-trip encoding and decoding, returning maximum angular error in degrees.
    /// Replicates Python GkClipData.verify_roundtrip().
    /// </summary>
    public float VerifyRoundtrip()
    {
        byte[] bytes = GkClipEncoder.Encode(this);
        GkClipData back = GkClipDecoder.Decode(bytes);
        return GkClipDecoder.VerifyRoundtrip(this, back);
    }
}
