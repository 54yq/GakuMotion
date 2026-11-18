namespace GakuMotion.VmdImporter.Native;

using System.Text.Json;
using System.Text.Json.Nodes;

/// <summary>Playback metadata only; source animation and WAV bytes stay unchanged.</summary>
internal sealed class PerformanceTimingSettings
{
    private sealed record Track(float Speed = 1.0f, float OffsetSeconds = 0.0f)
    {
        public JsonObject ToJson() => new() { ["speed"] = Speed, ["offset_seconds"] = OffsetSeconds };
    }

    private Track Motion { get; init; } = new();
    private Track Facial { get; init; } = new();
    private Track Camera { get; init; } = new();
    private Track Audio { get; init; } = new();

    public static PerformanceTimingSettings? ParseOptional(JsonElement request)
    {
        // Old API callers keep existing metadata on edits. New imports default
        // to unit speed and zero offset when the field is absent.
        if (!request.TryGetProperty("track_timing", out JsonElement root)) return null;
        if (root.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException("track_timing must be an object");
        Track Read(string key)
        {
            if (!root.TryGetProperty(key, out JsonElement track)) return new();
            if (track.ValueKind != JsonValueKind.Object)
                throw new InvalidDataException($"track_timing.{key} must be an object");
            float Number(string name, float fallback)
            {
                if (!track.TryGetProperty(name, out JsonElement number)) return fallback;
                if (number.ValueKind != JsonValueKind.Number || !number.TryGetSingle(out float value) || !float.IsFinite(value))
                    throw new InvalidDataException($"track_timing.{key}.{name} must be finite");
                return value;
            }
            float speed = Number("speed", 1.0f);
            if (speed < 0.0f) throw new InvalidDataException($"track_timing.{key}.speed must be at least zero");
            return new Track(speed, Number("offset_seconds", 0.0f));
        }
        return new PerformanceTimingSettings
        {
            Motion = Read("motion"), Facial = Read("facial"), Camera = Read("camera"), Audio = Read("audio")
        };
    }

    public JsonObject ToJson() => new()
    {
        ["motion"] = Motion.ToJson(), ["facial"] = Facial.ToJson(),
        ["camera"] = Camera.ToJson(), ["audio"] = Audio.ToJson()
    };
}
