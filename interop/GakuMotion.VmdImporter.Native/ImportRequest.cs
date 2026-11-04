namespace GakuMotion.VmdImporter.Native;

using System.Text.Json;

/// <summary>
/// One in-game import job, serialized as UTF-8 JSON across the native boundary.
/// Field names mirror the Python CLI (tools/import_motion.py) so both backends
/// receive the same information.
/// </summary>
internal sealed class ImportRequest
{
    public required string MotionVmd { get; init; }
    public string EditDir { get; init; } = string.Empty;
    public string MorphVmd { get; init; } = string.Empty;
    public bool DisableMorph { get; init; }
    public required string MotionsRoot { get; init; }
    public string Title { get; init; } = string.Empty;
    public float Fps { get; init; }
    public string Audio { get; init; } = string.Empty;
    public bool ClearAudio { get; init; }
    public string CameraVmd { get; init; } = string.Empty;
    public bool ClearCamera { get; init; }
    public float CameraFps { get; init; }
    public float CameraScale { get; init; }
    public string CameraRootRegistration { get; init; } = string.Empty;
    public string ToothChannel { get; init; } = string.Empty;
    public string LogPath { get; init; } = string.Empty;
    public string TimingJsonl { get; init; } = string.Empty;
    public string TransactionRoot { get; init; } = string.Empty;
    public PerformanceTimingSettings? TrackTiming { get; init; }

    public static ImportRequest Parse(ReadOnlySpan<byte> utf8Json)
    {
        using JsonDocument doc = JsonDocument.Parse(utf8Json.ToArray());
        JsonElement root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException("import request must be a JSON object");

        string Require(string name)
        {
            if (!root.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.String)
                throw new InvalidDataException($"import request is missing string field '{name}'");
            return value.GetString() ?? string.Empty;
        }

        string Optional(string name)
        {
            return root.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
                ? value.GetString() ?? string.Empty
                : string.Empty;
        }

        bool OptionalBool(string name)
        {
            return root.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.True;
        }

        float OptionalFloat(string name)
        {
            return root.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.Number
                ? value.GetSingle()
                : 0.0f;
        }

        string editDir = Optional("edit_dir");
        return new ImportRequest
        {
            MotionVmd = editDir.Length == 0 ? Require("motion_vmd") : Optional("motion_vmd"),
            EditDir = editDir,
            MorphVmd = Optional("morph_vmd"),
            DisableMorph = OptionalBool("disable_morph"),
            MotionsRoot = editDir.Length == 0 ? Require("motions_root") : Optional("motions_root"),
            Title = Optional("title"),
            Fps = OptionalFloat("fps"),
            Audio = Optional("audio"),
            ClearAudio = OptionalBool("clear_audio"),
            CameraVmd = Optional("camera_vmd"),
            ClearCamera = OptionalBool("clear_camera"),
            CameraFps = OptionalFloat("camera_fps"),
            CameraScale = OptionalFloat("camera_scale"),
            CameraRootRegistration = Optional("camera_root_registration"),
            ToothChannel = Optional("tooth_channel"),
            LogPath = Optional("log_path"),
            TimingJsonl = Optional("timing_jsonl"),
            TransactionRoot = Optional("transaction_root"),
            TrackTiming = PerformanceTimingSettings.ParseOptional(root)
        };
    }
}
