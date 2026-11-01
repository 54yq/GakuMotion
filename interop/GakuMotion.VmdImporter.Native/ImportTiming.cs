namespace GakuMotion.VmdImporter.Native;

using System.Diagnostics;
using System.Globalization;
using System.Text;

/// <summary>
/// Minimal timing record for one import. Fields are exactly the ones the test
/// build must report: VMD parse, source evaluation, retarget/encode, total,
/// frame count and clip size.
/// </summary>
internal sealed class ImportTiming
{
    public double ParseMs { get; set; }
    public double EvaluateMs { get; set; }
    public double RetargetEncodeMs { get; set; }
    public double FacialMs { get; set; }
    public double CameraMs { get; set; }
    public double PackMs { get; set; }
    public double TotalMs { get; set; }
    public int Frames { get; set; }
    public int Bones { get; set; }
    public long ClipBytes { get; set; }
    public float Fps { get; set; }
    public long VmdBytes { get; set; }

    /// <summary>Single-line machine-readable form used in the import log.</summary>
    public string ToLogLine(string candidateTreeSha256)
    {
        var sb = new StringBuilder(256);
        sb.Append("@TIMING:");
        sb.Append("candidate_b_tree=").Append(candidateTreeSha256).Append(';');
        sb.Append("parse_ms=").Append(F(ParseMs)).Append(';');
        sb.Append("eval_ms=").Append(F(EvaluateMs)).Append(';');
        sb.Append("retarget_encode_ms=").Append(F(RetargetEncodeMs)).Append(';');
        sb.Append("facial_ms=").Append(F(FacialMs)).Append(';');
        sb.Append("camera_ms=").Append(F(CameraMs)).Append(';');
        sb.Append("pack_ms=").Append(F(PackMs)).Append(';');
        sb.Append("total_ms=").Append(F(TotalMs)).Append(';');
        sb.Append("frames=").Append(Frames.ToString(CultureInfo.InvariantCulture)).Append(';');
        sb.Append("bones=").Append(Bones.ToString(CultureInfo.InvariantCulture)).Append(';');
        sb.Append("clip_bytes=").Append(ClipBytes.ToString(CultureInfo.InvariantCulture)).Append(';');
        sb.Append("fps=").Append(F(Fps)).Append(';');
        sb.Append("vmd_bytes=").Append(VmdBytes.ToString(CultureInfo.InvariantCulture));
        return sb.ToString();
    }

    public string ToUserLine()
    {
        return string.Format(CultureInfo.InvariantCulture,
            "导入计时: VMD 解析 {0:F1} ms | 源求值 {1:F1} ms | 表情 {2:F1} ms | 重定向+编码 {3:F1} ms | 运镜 {4:F1} ms | 落盘 {5:F1} ms | 合计 {6:F1} ms | {7} 帧 / {8} 骨骼 | clip {9} 字节",
            ParseMs, EvaluateMs, FacialMs, RetargetEncodeMs, CameraMs, PackMs, TotalMs, Frames, Bones, ClipBytes);
    }

    /// <summary>Append-only JSONL record for collecting real-device import speed.</summary>
    public string ToJsonlLine(string candidateTreeSha256, string vmdPath, string backend, string outcome)
    {
        var sb = new StringBuilder(384);
        sb.Append('{');
        sb.Append("\"utc\":\"").Append(DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture)).Append("\",");
        sb.Append("\"backend\":\"").Append(JsonEscape(backend)).Append("\",");
        sb.Append("\"outcome\":\"").Append(JsonEscape(outcome)).Append("\",");
        sb.Append("\"candidate_b_tree\":\"").Append(candidateTreeSha256).Append("\",");
        sb.Append("\"vmd\":\"").Append(JsonEscape(vmdPath)).Append("\",");
        sb.Append("\"vmd_bytes\":").Append(VmdBytes.ToString(CultureInfo.InvariantCulture)).Append(',');
        sb.Append("\"parse_ms\":").Append(F(ParseMs)).Append(',');
        sb.Append("\"eval_ms\":").Append(F(EvaluateMs)).Append(',');
        sb.Append("\"retarget_encode_ms\":").Append(F(RetargetEncodeMs)).Append(',');
        sb.Append("\"facial_ms\":").Append(F(FacialMs)).Append(',');
        sb.Append("\"camera_ms\":").Append(F(CameraMs)).Append(',');
        sb.Append("\"pack_ms\":").Append(F(PackMs)).Append(',');
        sb.Append("\"total_ms\":").Append(F(TotalMs)).Append(',');
        sb.Append("\"frames\":").Append(Frames.ToString(CultureInfo.InvariantCulture)).Append(',');
        sb.Append("\"bones\":").Append(Bones.ToString(CultureInfo.InvariantCulture)).Append(',');
        sb.Append("\"clip_bytes\":").Append(ClipBytes.ToString(CultureInfo.InvariantCulture)).Append(',');
        sb.Append("\"fps\":").Append(F(Fps));
        sb.Append('}');
        return sb.ToString();
    }

    private static string F(double value) => value.ToString("F3", CultureInfo.InvariantCulture);

    private static string JsonEscape(string value)
    {
        var sb = new StringBuilder(value.Length + 8);
        foreach (char c in value)
        {
            switch (c)
            {
                case '"': sb.Append("\\\""); break;
                case '\\': sb.Append("\\\\"); break;
                case '\n': sb.Append("\\n"); break;
                case '\r': sb.Append("\\r"); break;
                case '\t': sb.Append("\\t"); break;
                default:
                    if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                    else sb.Append(c);
                    break;
            }
        }
        return sb.ToString();
    }
}

/// <summary>
/// Append-only writer for the log file the native panel already drains. Lines
/// are flushed immediately: the panel polls this file for progress.
/// </summary>
internal sealed class ImportLog : IDisposable
{
    private readonly object _gate = new();
    private readonly string _path;
    private FileStream? _stream;
    private StreamWriter? _writer;

    public ImportLog(string path)
    {
        _path = path;
        if (string.IsNullOrEmpty(path)) return;
        try
        {
            string? dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            _stream = new FileStream(path, FileMode.Create, FileAccess.Write,
                FileShare.ReadWrite | FileShare.Delete, 4096, FileOptions.WriteThrough);
            _writer = new StreamWriter(_stream, new UTF8Encoding(false)) { AutoFlush = true };
        }
        catch
        {
            _stream = null;
            _writer = null;
        }
    }

    public bool Active => _writer != null;

    public void Progress(int percent, string stage, string message)
    {
        Write($"@PROGRESS:{Math.Clamp(percent, 0, 100)}:{stage}:{message}");
    }

    public void Line(string message) => Write(message);

    public void Error(string message) => Write("ERROR: " + message);

    private void Write(string line)
    {
        if (_writer == null) return;
        lock (_gate)
        {
            try
            {
                _writer.WriteLine(line);
                _writer.Flush();
            }
            catch
            {
                // Logging must never break an import.
            }
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            try { _writer?.Dispose(); } catch { }
            try { _stream?.Dispose(); } catch { }
            _writer = null;
            _stream = null;
        }
    }
}
