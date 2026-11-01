namespace GakuMotion.VmdImporter.Native.Camera;

using System.Diagnostics;
using System.Text.Json;
using GakuMotion.VmdImporter.Vmd;

/// <summary>Camera-only product request; independent of the body import ABI.</summary>
internal sealed class CameraImportRequest
{
    public required string Source { get; init; }
    public required string MotionDirectory { get; init; }
    public double Fps { get; init; } = 60;
    public double Scale { get; init; } = 0.083;
    public bool DetectCuts { get; init; } = true;
    public string RootRegistration { get; init; } = CameraRegistration.ModeAuto;
    public string LogPath { get; init; } = string.Empty;

    public static CameraImportRequest Parse(ReadOnlySpan<byte> utf8Json)
    {
        using JsonDocument doc = JsonDocument.Parse(utf8Json.ToArray());
        JsonElement root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException("camera import request must be a JSON object");

        string Text(string name, bool required = false)
        {
            if (!root.TryGetProperty(name, out JsonElement value))
            {
                if (required) throw new InvalidDataException($"camera import request is missing '{name}'");
                return string.Empty;
            }
            if (value.ValueKind != JsonValueKind.String)
                throw new InvalidDataException($"camera import field '{name}' must be a string");
            return value.GetString() ?? string.Empty;
        }

        double Number(string name, double fallback) => root.TryGetProperty(name, out JsonElement value)
            ? value.GetDouble() : fallback;

        return new CameraImportRequest
        {
            Source = Text("source", required: true),
            MotionDirectory = Text("motion_dir", required: true),
            Fps = Number("fps", 60),
            Scale = Number("scale", 0.083),
            DetectCuts = !root.TryGetProperty("detect_cuts", out JsonElement cuts) || cuts.GetBoolean(),
            RootRegistration = root.TryGetProperty("root_registration", out _) ? Text("root_registration") : CameraRegistration.ModeAuto,
            LogPath = Text("log_path")
        };
    }
}

/// <summary>
/// Camera-only equivalent of import_camera.py. The validated staging file lives
/// beside its destination so publishing replaces the sidecar in one filesystem
/// operation. A failed bake never removes an existing camera or body asset.
/// </summary>
internal static class CameraImportPipeline
{
    public static ImportOutcome Run(CameraImportRequest request, ImportLog log)
    {
        var watch = Stopwatch.StartNew();
        var timing = new ImportTiming();
        string? stage = null;
        try
        {
            log.Progress(3, "INIT", "正在校验镜头与目标曲目...");
            string source = Path.GetFullPath(request.Source);
            string directory = Path.GetFullPath(request.MotionDirectory);
            string extension = Path.GetExtension(source);
            bool prebaked = extension.Equals(".gkcamera", StringComparison.OrdinalIgnoreCase);
            if (!File.Exists(source) || (!prebaked && !extension.Equals(".vmd", StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("请选择存在的 .vmd 或 .gkcamera 镜头文件");
            if (!Directory.Exists(directory) || !new[] { "motion.gkclip", "motion.gk_world_dump", "motion.gkmotion" }
                    .Any(name => File.Exists(Path.Combine(directory, name))))
                throw new InvalidDataException("目标必须是包含动作文件的已有曲目目录");
            if (!double.IsFinite(request.Fps) || request.Fps < 30 || request.Fps > 240 ||
                !double.IsFinite(request.Scale) || request.Scale <= 0)
                throw new InvalidDataException("镜头采样率须在 30–240 fps，缩放须为正有限值");
            if (request.RootRegistration is not (CameraRegistration.ModeAuto or CameraRegistration.ModeOff or CameraRegistration.ModeRequired))
                throw new InvalidDataException("root registration must be auto, off or required");

            stage = Path.Combine(directory, ".camera-import-" + Guid.NewGuid().ToString("N") + ".tmp");
            if (prebaked)
            {
                log.Progress(20, "CAMERA", "正在校验已烘焙镜头...");
                CameraBaker.Validate(source);
                File.Copy(source, stage, overwrite: false);
                log.Line("Prebaked camera: root registration bypassed (copied as-is).");
            }
            else
            {
                log.Progress(15, "PARSE", "正在读取 VMD 镜头关键帧...");
                var samples = CameraBaker.Bake(VmdReader.Parse(source).CameraKeyframes,
                    request.Fps, request.Scale, request.DetectCuts);
                log.Progress(65, "CAMERA", "正在配对动作根轨迹...");
                CameraRegistrationResult registration = CameraRegistration.Register(samples, directory,
                    request.Fps, request.Scale, request.RootRegistration);
                log.Line($"Camera root registration: {registration.StatusText} " +
                         $"(mode={registration.Mode}, reason={registration.Reason}) — {registration.Description}");
                CameraBaker.Write(stage, registration.Samples, request.Fps);
            }

            log.Progress(90, "PACK", "正在校验并发布镜头轨道...");
            (uint count, float fps) = CameraBaker.Validate(stage);
            long bytes = new FileInfo(stage).Length;
            string target = Path.Combine(directory, "camera.gkcamera");
            File.Move(stage, target, overwrite: true);
            stage = null;
            timing.Frames = checked((int)count);
            timing.ClipBytes = bytes;
            timing.CameraMs = watch.Elapsed.TotalMilliseconds;
            timing.TotalMs = watch.Elapsed.TotalMilliseconds;
            log.Line($"Camera imported: {target}\n{count} frames at {fps:g} fps, duration {(count - 1) / fps:F3}s");
            log.Progress(100, "DONE", "镜头导入完成");
            return new ImportOutcome { Ok = true, Directory = directory, Timing = timing };
        }
        catch (Exception ex)
        {
            timing.TotalMs = watch.Elapsed.TotalMilliseconds;
            string message = ex is InvalidDataException ? ex.Message : ex.GetType().Name + ": " + ex.Message;
            log.Error(message);
            return new ImportOutcome { Ok = false, Error = message, ErrorCode = "camera_import_failed", Timing = timing };
        }
        finally
        {
            if (stage != null)
            {
                try { File.Delete(stage); }
                catch (IOException ex) { log.Error("无法清理镜头暂存文件: " + ex.Message); }
                catch (UnauthorizedAccessException ex) { log.Error("无法清理镜头暂存文件: " + ex.Message); }
            }
        }
    }
}
