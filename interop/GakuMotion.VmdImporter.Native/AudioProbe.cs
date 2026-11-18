namespace GakuMotion.VmdImporter.Native;

using System.Buffers.Binary;
using System.Text;
using System.Text.Json.Nodes;

internal sealed class AudioProbeResult
{
    public bool Valid { get; init; }
    public string Codec { get; init; } = string.Empty;
    public int SampleRate { get; init; }
    public int Channels { get; init; }
    public double DurationSeconds { get; init; }
    public string Title { get; init; } = string.Empty;
    public string Artist { get; init; } = string.Empty;
    public string Album { get; init; } = string.Empty;
    public string CoverMime { get; init; } = string.Empty;
    public byte[]? CoverBytes { get; init; }
    public string Error { get; init; } = string.Empty;
}

internal static class AudioProbe
{
    public static readonly string[] SupportedExtensions =
    {
        ".wav", ".mp3", ".flac", ".ogg", ".oga", ".m4a", ".mp4", ".aac", ".opus", ".wma"
    };

    public static bool IsNetworkPath(string? path)
    {
        if (string.IsNullOrWhiteSpace(path)) return false;
        return path.Contains("://", StringComparison.Ordinal) ||
               path.StartsWith(@"\\", StringComparison.Ordinal) ||
               path.StartsWith("//", StringComparison.Ordinal);
    }

    public static bool IsSupportedExtension(string? pathOrExt)
    {
        if (string.IsNullOrWhiteSpace(pathOrExt)) return false;
        string ext = Path.GetExtension(pathOrExt);
        if (string.IsNullOrEmpty(ext)) ext = pathOrExt;
        return SupportedExtensions.Contains(ext, StringComparer.OrdinalIgnoreCase);
    }

    public static string? FindExistingAudioFile(string directory, JsonObject? info = null)
    {
        if (string.IsNullOrEmpty(directory) || !Directory.Exists(directory)) return null;
        string? hinted = info?["audio_file"]?.GetValue<string>();
        if (!string.IsNullOrEmpty(hinted) && IsSafeSingleSegment(hinted) && IsSupportedExtension(hinted))
        {
            string fullHint = Path.Combine(directory, hinted);
            if (File.Exists(fullHint)) return fullHint;
        }
        foreach (string ext in SupportedExtensions)
        {
            string candidate = Path.Combine(directory, "song" + ext);
            if (File.Exists(candidate)) return candidate;
        }
        return null;
    }

    public static void DeleteExistingAudioFiles(string stageDirectory)
    {
        foreach (string ext in SupportedExtensions)
        {
            string path = Path.Combine(stageDirectory, "song" + ext);
            if (File.Exists(path)) File.Delete(path);
        }
        foreach (string file in Directory.EnumerateFiles(stageDirectory, "song.*"))
        {
            if (IsSupportedExtension(file)) File.Delete(file);
        }
    }

    public static void DeleteExistingCoverFiles(string stageDirectory)
    {
        foreach (string ext in new[] { ".jpg", ".jpeg", ".png", ".webp", ".bmp" })
        {
            string path = Path.Combine(stageDirectory, "cover" + ext);
            if (File.Exists(path)) File.Delete(path);
        }
    }

    public static string? SaveCoverToDirectory(byte[]? bytes, string? mimeHint, string stageDirectory)
    {
        if (bytes is null || bytes.Length < 8) return null;
        bool isPng = (bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47) ||
                     string.Equals(mimeHint, "image/png", StringComparison.OrdinalIgnoreCase);
        bool isJpg = (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF);
        bool isWebp = bytes.Length >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
                      bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P';
        string fileName = isPng ? "cover.png" : isWebp ? "cover.webp" : (isJpg || string.Equals(mimeHint, "image/jpeg", StringComparison.OrdinalIgnoreCase) ? "cover.jpg" : "cover.jpg");
        DeleteExistingCoverFiles(stageDirectory);
        File.WriteAllBytes(Path.Combine(stageDirectory, fileName), bytes);
        return fileName;
    }

    public static string? ResolveInputAudio(string vmdPath, string audioPath, bool clearAudio)
    {
        if (clearAudio) return null;
        if (!string.IsNullOrWhiteSpace(audioPath))
        {
            if (IsNetworkPath(audioPath))
                throw new InvalidDataException("不支持网络或流媒体路径，请选择本地音频文件。");
            string full = Path.GetFullPath(audioPath);
            if (!File.Exists(full))
                throw new InvalidDataException("找不到音频文件: " + full);
            return full;
        }
        string stem = Path.Combine(Path.GetDirectoryName(vmdPath)!, Path.GetFileNameWithoutExtension(vmdPath));
        foreach (string ext in SupportedExtensions)
        {
            string sibling = stem + ext;
            if (File.Exists(sibling)) return sibling;
        }
        string dir = Path.GetDirectoryName(vmdPath)!;
        foreach (string ext in SupportedExtensions)
        {
            string song = Path.Combine(dir, "song" + ext);
            if (File.Exists(song)) return song;
        }
        return null;
    }

    public static AudioProbeResult Probe(string path, bool extractCover = false)
    {
        if (string.IsNullOrWhiteSpace(path))
            return Fail("音频路径为空。");
        if (IsNetworkPath(path))
            return Fail("不支持纯网络流媒体或 UNC 路径，请选择本地音频文件。");
        if (!File.Exists(path))
            return Fail("找不到音频文件: " + path);

        string ext = Path.GetExtension(path).ToLowerInvariant();
        if (!IsSupportedExtension(ext))
            return Fail($"不支持的音频格式 {ext}（支持 .wav / .mp3 / .flac / .ogg / .m4a / .aac / .opus）。");

        long len = new FileInfo(path).Length;
        if (len < 12)
            return Fail($"音频文件过小或已损坏：{path}");
        if (len > 512L * 1024L * 1024L)
            return Fail($"音频文件超过 512 MiB 上限：{path}");

        byte[] data = File.ReadAllBytes(path);
        return ext switch
        {
            ".wav" => ParseWav(data, path),
            ".flac" => ParseFlac(data, path, extractCover),
            ".mp3" => ParseMp3(data, path, extractCover),
            ".ogg" or ".oga" or ".opus" => ParseOgg(data, path, extractCover),
            ".m4a" or ".mp4" => ParseMp4(data, path, extractCover),
            ".aac" => ParseAdtsAac(data, path, extractCover),
            ".wma" => ParseWma(data, path),
            _ => Fail($"不支持的音频格式: {ext}")
        };
    }

    private static bool IsSafeSingleSegment(string s)
    {
        return !string.IsNullOrEmpty(s) && s != "." && s != ".." &&
               !s.Contains('/') && !s.Contains('\\') && !s.Contains(':');
    }

    private static AudioProbeResult Fail(string error) => new() { Valid = false, Error = error };

    private static AudioProbeResult ParseWav(byte[] data, string path)
    {
        if (data.Length < 44) return Fail($"音频文件不是有效的 WAV（长度不足）：{path}");
        if (!MatchAscii(data, 0, "RIFF") || !MatchAscii(data, 8, "WAVE"))
            return Fail($"音频文件不是有效的 RIFF/WAVE：{path}");

        bool hasFmt = false, hasData = false;
        int formatTag = 0, channels = 0, sampleRate = 0, bitsPerSample = 0;
        long dataBytes = 0;
        int byteRate = 0;

        int pos = 12;
        for (int i = 0; i < 4096 && pos + 8 <= data.Length; ++i)
        {
            string id = Encoding.ASCII.GetString(data, pos, 4);
            uint chunkSize = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(pos + 4, 4));
            pos += 8;
            if ((long)pos + chunkSize > data.Length)
                return Fail($"WAV 数据块越界（文件可能已截断）：{path}");

            if (id == "fmt " && chunkSize >= 16)
            {
                formatTag = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(pos, 2));
                channels = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(pos + 2, 2));
                sampleRate = (int)BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(pos + 4, 4));
                byteRate = (int)BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(pos + 8, 4));
                ushort blockAlign = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(pos + 12, 2));
                bitsPerSample = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(pos + 14, 2));

                if (formatTag == 0xFFFE && chunkSize >= 40)
                {
                    ushort subTag = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(pos + 24, 2));
                    if (subTag != 1 && subTag != 3)
                        return Fail($"WAV Extensible 子格式暂不支持（subFormat={subTag}）：{path}");
                }
                else if (formatTag != 1 && formatTag != 3)
                {
                    return Fail($"WAV 仅支持 PCM 或 IEEE Float 编码（format={formatTag}）：{path}");
                }

                if (channels <= 0 || sampleRate <= 0 || blockAlign == 0 ||
                    bitsPerSample is not (8 or 16 or 24 or 32) ||
                    channels * (bitsPerSample / 8) != blockAlign ||
                    (long)sampleRate * blockAlign != byteRate)
                {
                    return Fail($"WAV fmt 参数无效或已损坏：{path}");
                }
                hasFmt = true;
            }
            else if (id == "data")
            {
                dataBytes = chunkSize;
                hasData = true;
            }

            long advance = chunkSize + (chunkSize & 1u);
            if (pos + advance > data.Length) break;
            pos += (int)advance;
        }

        if (!hasFmt || !hasData || dataBytes <= 0 || byteRate <= 0)
            return Fail($"WAV 缺少 fmt 或 data 块：{path}");

        return new AudioProbeResult
        {
            Valid = true,
            Codec = formatTag == 3 ? "wav-float" : "wav",
            SampleRate = sampleRate,
            Channels = channels,
            DurationSeconds = (double)dataBytes / byteRate
        };
    }

    private sealed class TagAccumulator
    {
        public string Title = string.Empty;
        public string Artist = string.Empty;
        public string Album = string.Empty;
        public string CoverMime = string.Empty;
        public byte[]? CoverBytes;

        public void AssignVorbisComment(string key, string value)
        {
            if (string.IsNullOrWhiteSpace(value)) return;
            if (key.Equals("TITLE", StringComparison.OrdinalIgnoreCase) && Title.Length == 0) Title = value.Trim();
            else if ((key.Equals("ARTIST", StringComparison.OrdinalIgnoreCase) || key.Equals("ALBUMARTIST", StringComparison.OrdinalIgnoreCase)) && Artist.Length == 0)
                Artist = value.Trim();
            else if (key.Equals("ALBUM", StringComparison.OrdinalIgnoreCase) && Album.Length == 0) Album = value.Trim();
        }
    }

    private static int ParseId3v2IfPresent(byte[] data, TagAccumulator tags, bool extractCover)
    {
        if (data.Length < 10 || !MatchAscii(data, 0, "ID3")) return 0;
        byte ver = data[3];
        if (ver < 2 || ver > 4) return 0;
        uint tagSize = ReadSyncsafe(data.AsSpan(6, 4));
        int pos = 10;
        int end = (int)Math.Min((long)data.Length, 10L + tagSize);

        // Skip optional extended header
        if (ver >= 3 && (data[5] & 0x40) != 0 && pos + 4 <= end)
        {
            uint extLen = ver == 4 ? ReadSyncsafe(data.AsSpan(pos, 4)) : BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(pos, 4));
            pos += ver == 4 ? (int)extLen : (int)extLen + 4;
        }

        while (pos < end)
        {
            if (data[pos] == 0) break;
            string frameId;
            uint frameSize;
            int headerLen;

            if (ver == 2)
            {
                if (pos + 6 > end) break;
                frameId = Encoding.ASCII.GetString(data, pos, 3);
                frameSize = (uint)((data[pos + 3] << 16) | (data[pos + 4] << 8) | data[pos + 5]);
                headerLen = 6;
            }
            else
            {
                if (pos + 10 > end) break;
                frameId = Encoding.ASCII.GetString(data, pos, 4);
                frameSize = ver == 4 ? ReadSyncsafe(data.AsSpan(pos + 4, 4)) : BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(pos + 4, 4));
                headerLen = 10;
            }

            pos += headerLen;
            if (frameSize == 0 || pos + frameSize > end) break;
            ReadOnlySpan<byte> payload = data.AsSpan(pos, (int)frameSize);
            pos += (int)frameSize;

            if (frameId is "TIT2" or "TT2")
            {
                string val = DecodeId3Text(payload);
                if (val.Length > 0 && tags.Title.Length == 0) tags.Title = val;
            }
            else if (frameId is "TPE1" or "TPE2" or "TP1" or "TP2")
            {
                string val = DecodeId3Text(payload);
                if (val.Length > 0 && tags.Artist.Length == 0) tags.Artist = val;
            }
            else if (frameId is "TALB" or "TAL")
            {
                string val = DecodeId3Text(payload);
                if (val.Length > 0 && tags.Album.Length == 0) tags.Album = val;
            }
            else if (frameId == "APIC" && payload.Length >= 4)
            {
                ParseId3Apic(payload, tags, extractCover);
            }
            else if (frameId == "PIC" && payload.Length >= 6)
            {
                ParseId3Pic(payload, tags, extractCover);
            }
        }
        return end;
    }

    private static string DecodeId3Text(ReadOnlySpan<byte> payload)
    {
        if (payload.Length < 2) return string.Empty;
        byte enc = payload[0];
        ReadOnlySpan<byte> raw = payload.Slice(1);
        return DecodeEncodedString(enc, raw).Trim('\0', ' ', '\t', '\r', '\n');
    }

    private static string DecodeEncodedString(byte enc, ReadOnlySpan<byte> raw)
    {
        if (raw.Length == 0) return string.Empty;
        if (enc == 0)
            return Encoding.GetEncoding("latin1").GetString(raw).Split('\0')[0];
        if (enc == 1)
        {
            if (raw.Length >= 2 && raw[0] == 0xFE && raw[1] == 0xFF)
                return Encoding.BigEndianUnicode.GetString(raw.Slice(2)).Split('\0')[0];
            int start = (raw.Length >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) ? 2 : 0;
            return Encoding.Unicode.GetString(raw.Slice(start)).Split('\0')[0];
        }
        if (enc == 2)
            return Encoding.BigEndianUnicode.GetString(raw).Split('\0')[0];
        return Encoding.UTF8.GetString(raw).Split('\0')[0];
    }

    private static void ParseId3Apic(ReadOnlySpan<byte> payload, TagAccumulator tags, bool extractCover)
    {
        byte enc = payload[0];
        int idx = 1;
        while (idx < payload.Length && payload[idx] != 0) ++idx;
        if (idx >= payload.Length) return;
        string mime = Encoding.ASCII.GetString(payload.Slice(1, idx - 1)).ToLowerInvariant();
        if (mime == "jpg") mime = "image/jpeg";
        else if (mime == "png") mime = "image/png";
        ++idx;
        if (idx >= payload.Length) return;
        byte picType = payload[idx++];
        idx = SkipEncodedTerminator(enc, payload, idx);
        if (idx < 0 || idx >= payload.Length) return;
        if (tags.CoverBytes != null && picType != 3) return;
        ReadOnlySpan<byte> img = payload.Slice(idx);
        if (img.Length < 8) return;
        tags.CoverMime = NormalizeImageMime(mime, img);
        if (extractCover) tags.CoverBytes = img.ToArray();
        else if (tags.CoverBytes == null) tags.CoverBytes = Array.Empty<byte>();
    }

    private static void ParseId3Pic(ReadOnlySpan<byte> payload, TagAccumulator tags, bool extractCover)
    {
        byte enc = payload[0];
        string fmt = Encoding.ASCII.GetString(payload.Slice(1, 3)).ToLowerInvariant();
        string mime = fmt == "png" ? "image/png" : "image/jpeg";
        byte picType = payload[4];
        int idx = SkipEncodedTerminator(enc, payload, 5);
        if (idx < 0 || idx >= payload.Length) return;
        if (tags.CoverBytes != null && picType != 3) return;
        ReadOnlySpan<byte> img = payload.Slice(idx);
        if (img.Length < 8) return;
        tags.CoverMime = NormalizeImageMime(mime, img);
        if (extractCover) tags.CoverBytes = img.ToArray();
        else if (tags.CoverBytes == null) tags.CoverBytes = Array.Empty<byte>();
    }

    private static int SkipEncodedTerminator(byte enc, ReadOnlySpan<byte> data, int pos)
    {
        if (enc is 1 or 2)
        {
            while (pos + 1 < data.Length)
            {
                if (data[pos] == 0 && data[pos + 1] == 0) return pos + 2;
                pos += 2;
            }
            return -1;
        }
        while (pos < data.Length && data[pos] != 0) ++pos;
        return pos < data.Length ? pos + 1 : -1;
    }

    private static AudioProbeResult ParseMp3(byte[] data, string path, bool extractCover)
    {
        var tags = new TagAccumulator();
        int audioStart = ParseId3v2IfPresent(data, tags, extractCover);

        // Also check ID3v1 tail if metadata still missing
        int audioEnd = data.Length;
        if (data.Length >= 128 && MatchAscii(data, data.Length - 128, "TAG"))
        {
            audioEnd -= 128;
            if (tags.Title.Length == 0) tags.Title = Encoding.GetEncoding("latin1").GetString(data, data.Length - 125, 30).Trim('\0', ' ');
            if (tags.Artist.Length == 0) tags.Artist = Encoding.GetEncoding("latin1").GetString(data, data.Length - 95, 30).Trim('\0', ' ');
            if (tags.Album.Length == 0) tags.Album = Encoding.GetEncoding("latin1").GetString(data, data.Length - 65, 30).Trim('\0', ' ');
        }

        int[] srV1 = { 44100, 48000, 32000, 0 };
        int[] srV2 = { 22050, 24000, 16000, 0 };
        int[] srV25 = { 11025, 12000, 8000, 0 };
        int[] brV1L3 = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
        int[] brV2L3 = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
        int[] brV1L2 = { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0 };
        int[] brV1L1 = { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 };
        int[] brV2L1 = { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0 };

        int firstPos = -1, sampleRate = 0, channels = 0, firstLayer = 0, firstVer = 0, firstSpf = 0;
        uint? headerTotalFrames = null;
        long totalFrames = 0;
        double totalDuration = 0.0;

        for (int p = audioStart; p + 4 <= audioEnd;)
        {
            if (data[p] != 0xFF || (data[p + 1] & 0xE0) != 0xE0)
            {
                if (firstPos < 0 && p - audioStart < 65536) { ++p; continue; }
                break;
            }
            int verId = (data[p + 1] >> 3) & 3;
            int layerId = (data[p + 1] >> 1) & 3;
            int brIdx = (data[p + 2] >> 4) & 0xF;
            int srIdx = (data[p + 2] >> 2) & 3;
            int pad = (data[p + 2] >> 1) & 1;
            int chMode = (data[p + 3] >> 6) & 3;
            if (verId == 1 || layerId == 0 || brIdx == 0 || brIdx == 15 || srIdx == 3)
            {
                if (firstPos < 0 && p - audioStart < 65536) { ++p; continue; }
                break;
            }

            int sr = verId == 3 ? srV1[srIdx] : verId == 2 ? srV2[srIdx] : srV25[srIdx];
            int bitrateKbps = layerId == 3 ? (verId == 3 ? brV1L1[brIdx] : brV2L1[brIdx])
                            : layerId == 2 ? (verId == 3 ? brV1L2[brIdx] : brV2L3[brIdx])
                            : (verId == 3 ? brV1L3[brIdx] : brV2L3[brIdx]);
            if (sr <= 0 || bitrateKbps <= 0) break;

            int spf = layerId == 3 ? 384 : (layerId == 2 || verId == 3 ? 1152 : 576);
            int frameLen = layerId == 3
                ? ((12 * (bitrateKbps * 1000) / sr) + pad) * 4
                : ((spf / 8) * (bitrateKbps * 1000) / sr) + pad;
            if (frameLen < 4 || p + frameLen > audioEnd) break;

            if (firstPos < 0)
            {
                firstPos = p;
                sampleRate = sr;
                channels = chMode == 3 ? 1 : 2;
                firstVer = verId;
                firstLayer = layerId;
                firstSpf = spf;

                int xingOff = verId == 3 ? (chMode == 3 ? 21 : 36) : (chMode == 3 ? 13 : 21);
                if (p + xingOff + 12 <= audioEnd &&
                    (MatchAscii(data, p + xingOff, "Xing") || MatchAscii(data, p + xingOff, "Info")))
                {
                    uint flags = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(p + xingOff + 4, 4));
                    if ((flags & 1u) != 0)
                        headerTotalFrames = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(p + xingOff + 8, 4));
                }
                else if (p + 36 + 18 <= audioEnd && MatchAscii(data, p + 36, "VBRI"))
                {
                    headerTotalFrames = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(p + 36 + 14, 4));
                }
            }

            ++totalFrames;
            totalDuration += (double)spf / sr;
            p += frameLen;
            if (headerTotalFrames is > 0) break;
        }

        if (firstPos < 0 || sampleRate <= 0)
            return Fail($"未能识别有效的 MP3 帧数据：{path}");

        double duration = headerTotalFrames is > 0
            ? (double)headerTotalFrames.Value * firstSpf / sampleRate
            : totalDuration;

        return new AudioProbeResult
        {
            Valid = duration > 0.0,
            Codec = "mp3",
            SampleRate = sampleRate,
            Channels = channels,
            DurationSeconds = duration,
            Title = tags.Title,
            Artist = tags.Artist,
            Album = tags.Album,
            CoverMime = tags.CoverMime,
            CoverBytes = extractCover ? tags.CoverBytes : null
        };
    }

    private static AudioProbeResult ParseFlac(byte[] data, string path, bool extractCover)
    {
        var tags = new TagAccumulator();
        int pos = ParseId3v2IfPresent(data, tags, extractCover);
        if (pos + 4 > data.Length || !MatchAscii(data, pos, "fLaC"))
            return Fail($"音频文件不是有效的 FLAC：{path}");
        pos += 4;

        int sampleRate = 0, channels = 0;
        long totalSamples = 0;
        bool hasStreamInfo = false;

        bool last = false;
        while (!last && pos + 4 <= data.Length)
        {
            byte hdr = data[pos];
            last = (hdr & 0x80) != 0;
            int blockType = hdr & 0x7F;
            int len = (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
            pos += 4;
            if (pos + len > data.Length) break;
            ReadOnlySpan<byte> block = data.AsSpan(pos, len);

            if (blockType == 0 && len >= 34)
            {
                sampleRate = (block[10] << 12) | (block[11] << 4) | (block[12] >> 4);
                channels = ((block[12] >> 1) & 7) + 1;
                totalSamples = ((long)(block[13] & 0x0F) << 32) | BinaryPrimitives.ReadUInt32BigEndian(block.Slice(14, 4));
                hasStreamInfo = sampleRate > 0;
            }
            else if (blockType == 4)
            {
                ParseVorbisComments(block, tags, extractCover);
            }
            else if (blockType == 6)
            {
                ParseFlacPicture(block, tags, extractCover);
            }
            pos += len;
        }

        if (!hasStreamInfo) return Fail($"FLAC 缺少 STREAMINFO 头：{path}");
        double duration = totalSamples > 0 ? (double)totalSamples / sampleRate : 0.0;
        return new AudioProbeResult
        {
            Valid = duration > 0.0,
            Codec = "flac",
            SampleRate = sampleRate,
            Channels = channels,
            DurationSeconds = duration,
            Title = tags.Title,
            Artist = tags.Artist,
            Album = tags.Album,
            CoverMime = tags.CoverMime,
            CoverBytes = extractCover ? tags.CoverBytes : null
        };
    }

    private static void ParseVorbisComments(ReadOnlySpan<byte> block, TagAccumulator tags, bool extractCover)
    {
        int p = 0;
        if (p + 4 > block.Length) return;
        uint vendorLen = BinaryPrimitives.ReadUInt32LittleEndian(block.Slice(p, 4));
        p += 4;
        if (vendorLen > block.Length - p) return;
        p += (int)vendorLen;
        if (p + 4 > block.Length) return;
        uint count = BinaryPrimitives.ReadUInt32LittleEndian(block.Slice(p, 4));
        p += 4;
        for (uint i = 0; i < count && p + 4 <= block.Length; ++i)
        {
            uint len = BinaryPrimitives.ReadUInt32LittleEndian(block.Slice(p, 4));
            p += 4;
            if (len > block.Length - p) break;
            string kv = Encoding.UTF8.GetString(block.Slice(p, (int)len));
            p += (int)len;
            int eq = kv.IndexOf('=');
            if (eq <= 0) continue;
            string key = kv[..eq];
            string val = kv[(eq + 1)..];
            if (key.Equals("METADATA_BLOCK_PICTURE", StringComparison.OrdinalIgnoreCase))
            {
                try
                {
                    byte[] rawPic = Convert.FromBase64String(val);
                    ParseFlacPicture(rawPic, tags, extractCover);
                }
                catch { }
            }
            else
            {
                tags.AssignVorbisComment(key, val);
            }
        }
    }

    private static void ParseFlacPicture(ReadOnlySpan<byte> block, TagAccumulator tags, bool extractCover)
    {
        int p = 0;
        if (p + 8 > block.Length) return;
        uint picType = BinaryPrimitives.ReadUInt32BigEndian(block.Slice(p, 4)); p += 4;
        uint mimeLen = BinaryPrimitives.ReadUInt32BigEndian(block.Slice(p, 4)); p += 4;
        if (mimeLen > block.Length - p) return;
        string mime = Encoding.ASCII.GetString(block.Slice(p, (int)mimeLen)).ToLowerInvariant(); p += (int)mimeLen;
        if (p + 4 > block.Length) return;
        uint descLen = BinaryPrimitives.ReadUInt32BigEndian(block.Slice(p, 4)); p += 4;
        if (descLen > block.Length - p) return;
        p += (int)descLen;
        if (p + 20 > block.Length) return;
        p += 16; // w, h, depth, indexed
        uint dataLen = BinaryPrimitives.ReadUInt32BigEndian(block.Slice(p, 4)); p += 4;
        if (dataLen < 8 || dataLen > block.Length - p) return;
        if (tags.CoverBytes != null && picType != 3) return;
        ReadOnlySpan<byte> img = block.Slice(p, (int)dataLen);
        tags.CoverMime = NormalizeImageMime(mime, img);
        if (extractCover) tags.CoverBytes = img.ToArray();
        else if (tags.CoverBytes == null) tags.CoverBytes = Array.Empty<byte>();
    }

    private static AudioProbeResult ParseOgg(byte[] data, string path, bool extractCover)
    {
        var tags = new TagAccumulator();
        string codec = string.Empty;
        int sampleRate = 0, channels = 0;
        ushort opusPreSkip = 0;
        long maxGranule = 0;
        uint primarySerial = 0;
        bool hasSerial = false;

        var packetBuf = new MemoryStream();
        int packetCount = 0;

        for (int p = 0; p + 27 <= data.Length;)
        {
            if (!MatchAscii(data, p, "OggS")) break;
            byte segCount = data[p + 26];
            if (p + 27 + segCount > data.Length) break;

            long granule = BinaryPrimitives.ReadInt64LittleEndian(data.AsSpan(p + 6, 8));
            uint serial = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(p + 14, 4));
            if (!hasSerial) { primarySerial = serial; hasSerial = true; }

            int bodyStart = p + 27 + segCount;
            int bodyLen = 0;
            for (int s = 0; s < segCount; ++s) bodyLen += data[p + 27 + s];
            if (bodyStart + bodyLen > data.Length) break;

            if (serial == primarySerial)
            {
                if (granule > maxGranule) maxGranule = granule;
                if (packetCount < 2)
                {
                    int off = bodyStart;
                    for (int s = 0; s < segCount && packetCount < 2; ++s)
                    {
                        byte seg = data[p + 27 + s];
                        if (seg > 0) packetBuf.Write(data, off, seg);
                        off += seg;
                        if (seg < 255)
                        {
                            byte[] pkt = packetBuf.ToArray();
                            packetBuf.SetLength(0);
                            ++packetCount;
                            InspectOggHeaderPacket(pkt, tags, extractCover, ref codec, ref sampleRate, ref channels, ref opusPreSkip);
                        }
                    }
                }
            }
            p = bodyStart + bodyLen;
        }

        // Fast scan backward for tail granule position
        for (int back = Math.Max(0, data.Length - 65536); back + 27 <= data.Length; ++back)
        {
            if (!MatchAscii(data, back, "OggS")) continue;
            uint serial = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(back + 14, 4));
            if (serial == primarySerial)
            {
                long g = BinaryPrimitives.ReadInt64LittleEndian(data.AsSpan(back + 6, 8));
                if (g > maxGranule) maxGranule = g;
            }
        }

        if (sampleRate <= 0) return Fail($"未能解析 Ogg/Opus 音频流：{path}");
        double duration = codec == "opus"
            ? Math.Max(0L, maxGranule - opusPreSkip) / 48000.0
            : (double)maxGranule / sampleRate;
        if (duration <= 0.0) return Fail($"未能获取 Ogg/Opus 有效时长：{path}");

        return new AudioProbeResult
        {
            Valid = true,
            Codec = codec,
            SampleRate = sampleRate,
            Channels = channels,
            DurationSeconds = duration,
            Title = tags.Title,
            Artist = tags.Artist,
            Album = tags.Album,
            CoverMime = tags.CoverMime,
            CoverBytes = extractCover ? tags.CoverBytes : null
        };
    }

    private static void InspectOggHeaderPacket(byte[] pkt, TagAccumulator tags, bool extractCover,
        ref string codec, ref int sampleRate, ref int channels, ref ushort opusPreSkip)
    {
        if (pkt.Length >= 30 && pkt[0] == 1 && MatchAscii(pkt, 1, "vorbis"))
        {
            codec = "ogg-vorbis";
            channels = pkt[11];
            sampleRate = (int)BinaryPrimitives.ReadUInt32LittleEndian(pkt.AsSpan(12, 4));
        }
        else if (pkt.Length >= 19 && MatchAscii(pkt, 0, "OpusHead"))
        {
            codec = "opus";
            channels = pkt[9];
            opusPreSkip = BinaryPrimitives.ReadUInt16LittleEndian(pkt.AsSpan(10, 2));
            sampleRate = (int)BinaryPrimitives.ReadUInt32LittleEndian(pkt.AsSpan(12, 4));
            if (sampleRate <= 0) sampleRate = 48000;
        }
        else if (pkt.Length > 7 && pkt[0] == 3 && MatchAscii(pkt, 1, "vorbis"))
        {
            ParseVorbisComments(pkt.AsSpan(7), tags, extractCover);
        }
        else if (pkt.Length > 8 && MatchAscii(pkt, 0, "OpusTags"))
        {
            ParseVorbisComments(pkt.AsSpan(8), tags, extractCover);
        }
    }

    private static AudioProbeResult ParseMp4(byte[] data, string path, bool extractCover)
    {
        if (data.Length < 12 || !MatchAscii(data, 4, "ftyp"))
            return Fail($"音频文件不是有效的 MP4/M4A：{path}");
        var tags = new TagAccumulator();
        double duration = 0.0;
        int sampleRate = 44100, channels = 2;
        WalkMp4Boxes(data, 0, data.Length, tags, extractCover, ref duration, ref sampleRate, ref channels);
        if (duration <= 0.0) return Fail($"未能获取 M4A/MP4 时长：{path}");
        return new AudioProbeResult
        {
            Valid = true,
            Codec = "m4a",
            SampleRate = sampleRate,
            Channels = channels,
            DurationSeconds = duration,
            Title = tags.Title,
            Artist = tags.Artist,
            Album = tags.Album,
            CoverMime = tags.CoverMime,
            CoverBytes = extractCover ? tags.CoverBytes : null
        };
    }

    private static void WalkMp4Boxes(byte[] data, int start, int end, TagAccumulator tags, bool extractCover,
        ref double duration, ref int sampleRate, ref int channels)
    {
        for (int p = start; p + 8 <= end;)
        {
            uint size32 = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(p, 4));
            string type = Encoding.ASCII.GetString(data, p + 4, 4);
            int hdr = 8;
            long boxEnd;
            if (size32 == 1 && p + 16 <= end)
            {
                long size64 = (long)BinaryPrimitives.ReadUInt64BigEndian(data.AsSpan(p + 8, 8));
                hdr = 16;
                boxEnd = p + size64;
            }
            else if (size32 == 0)
            {
                boxEnd = end;
            }
            else
            {
                if (size32 < 8) break;
                boxEnd = (long)p + size32;
            }
            if (boxEnd > end || boxEnd <= p + hdr) break;

            int body = p + hdr;
            int bodyEnd = (int)boxEnd;

            if (type == "mvhd" && body + 20 <= bodyEnd)
            {
                byte ver = data[body];
                if (ver == 0 && body + 20 <= bodyEnd)
                {
                    uint ts = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(body + 12, 4));
                    uint dur = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(body + 16, 4));
                    if (ts > 0 && dur > 0) duration = (double)dur / ts;
                }
                else if (ver == 1 && body + 32 <= bodyEnd)
                {
                    uint ts = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(body + 20, 4));
                    ulong dur = BinaryPrimitives.ReadUInt64BigEndian(data.AsSpan(body + 24, 8));
                    if (ts > 0 && dur > 0) duration = (double)dur / ts;
                }
            }
            else if (type == "stsd" && body + 8 <= bodyEnd)
            {
                int entryPos = body + 8;
                if (entryPos + 36 <= bodyEnd)
                {
                    string fmt = Encoding.ASCII.GetString(data, entryPos + 4, 4);
                    if (fmt is "mp4a" or "alac")
                    {
                        ushort ch = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(entryPos + 24, 2));
                        ushort sr = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(entryPos + 32, 2));
                        if (ch > 0) channels = ch;
                        if (sr > 0) sampleRate = sr;
                    }
                }
            }
            else if (type == "ilst")
            {
                ParseMp4Ilst(data, body, bodyEnd, tags, extractCover);
            }
            else if (type is "moov" or "trak" or "mdia" or "minf" or "stbl" or "udta")
            {
                WalkMp4Boxes(data, body, bodyEnd, tags, extractCover, ref duration, ref sampleRate, ref channels);
            }
            else if (type == "meta" && body + 4 < bodyEnd)
            {
                WalkMp4Boxes(data, body + 4, bodyEnd, tags, extractCover, ref duration, ref sampleRate, ref channels);
            }

            p = bodyEnd;
        }
    }

    private static void ParseMp4Ilst(byte[] data, int start, int end, TagAccumulator tags, bool extractCover)
    {
        for (int p = start; p + 8 <= end;)
        {
            uint size = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(p, 4));
            if (size < 8 || (long)p + size > end) break;
            int bodyEnd = p + (int)size;
            byte t0 = data[p + 4], t1 = data[p + 5], t2 = data[p + 6], t3 = data[p + 7];

            for (int q = p + 8; q + 16 <= bodyEnd;)
            {
                uint ds = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(q, 4));
                if (ds < 16 || (long)q + ds > bodyEnd) break;
                if (MatchAscii(data, q + 4, "data"))
                {
                    uint dataType = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(q + 8, 4));
                    ReadOnlySpan<byte> val = data.AsSpan(q + 16, (int)ds - 16);
                    if (t0 == 0xA9 && t1 == 'n' && t2 == 'a' && t3 == 'm' && tags.Title.Length == 0)
                        tags.Title = Encoding.UTF8.GetString(val).Trim('\0', ' ');
                    else if (t0 == 0xA9 && t1 == 'A' && t2 == 'R' && t3 == 'T' && tags.Artist.Length == 0)
                        tags.Artist = Encoding.UTF8.GetString(val).Trim('\0', ' ');
                    else if (t0 == 0xA9 && t1 == 'a' && t2 == 'l' && t3 == 'b' && tags.Album.Length == 0)
                        tags.Album = Encoding.UTF8.GetString(val).Trim('\0', ' ');
                    else if (t0 == 'c' && t1 == 'o' && t2 == 'v' && t3 == 'r' && val.Length >= 8)
                    {
                        string hint = dataType == 14 ? "image/png" : "image/jpeg";
                        tags.CoverMime = NormalizeImageMime(hint, val);
                        if (extractCover) tags.CoverBytes = val.ToArray();
                        else if (tags.CoverBytes == null) tags.CoverBytes = Array.Empty<byte>();
                    }
                    break;
                }
                q += (int)ds;
            }
            p = bodyEnd;
        }
    }

    private static AudioProbeResult ParseAdtsAac(byte[] data, string path, bool extractCover)
    {
        var tags = new TagAccumulator();
        int p = ParseId3v2IfPresent(data, tags, extractCover);
        int[] srTable = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350, 0, 0, 0 };
        int sampleRate = 0, channels = 0;
        long totalFrames = 0;
        while (p + 7 <= data.Length)
        {
            if (data[p] != 0xFF || (data[p + 1] & 0xF6) != 0xF0) break;
            int srIdx = (data[p + 2] >> 2) & 0x0F;
            int ch = ((data[p + 2] & 1) << 2) | ((data[p + 3] >> 6) & 3);
            int frameLen = ((data[p + 3] & 3) << 11) | (data[p + 4] << 3) | ((data[p + 5] >> 5) & 7);
            int sr = srTable[srIdx];
            if (sr <= 0 || frameLen < 7 || p + frameLen > data.Length) break;
            if (sampleRate == 0) { sampleRate = sr; channels = ch > 0 ? ch : 2; }
            ++totalFrames;
            p += frameLen;
        }
        if (totalFrames == 0 || sampleRate <= 0)
            return Fail($"未能解析 AAC ADTS 帧：{path}");
        double duration = (double)totalFrames * 1024.0 / sampleRate;
        return new AudioProbeResult
        {
            Valid = duration > 0.0,
            Codec = "aac",
            SampleRate = sampleRate,
            Channels = channels,
            DurationSeconds = duration,
            Title = tags.Title,
            Artist = tags.Artist,
            Album = tags.Album,
            CoverMime = tags.CoverMime,
            CoverBytes = extractCover ? tags.CoverBytes : null
        };
    }

    private static AudioProbeResult ParseWma(byte[] data, string path)
    {
        ReadOnlySpan<byte> asfGuid = new byte[] { 0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
        ReadOnlySpan<byte> filePropGuid = new byte[] { 0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11, 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
        if (data.Length < 30 || !data.AsSpan(0, 16).SequenceEqual(asfGuid))
            return Fail($"音频文件不是有效的 WMA/ASF：{path}");

        double duration = 0.0;
        int p = 30;
        while (p + 24 <= data.Length)
        {
            ulong objSize = BinaryPrimitives.ReadUInt64LittleEndian(data.AsSpan(p + 16, 8));
            if (objSize < 24 || (ulong)p + objSize > (ulong)data.Length) break;
            if (data.AsSpan(p, 16).SequenceEqual(filePropGuid) && objSize >= 88)
            {
                ulong play100Ns = BinaryPrimitives.ReadUInt64LittleEndian(data.AsSpan(p + 64, 8));
                ulong prerollMs = BinaryPrimitives.ReadUInt64LittleEndian(data.AsSpan(p + 80, 8));
                duration = Math.Max(0.0, play100Ns / 1e7 - prerollMs / 1000.0);
                break;
            }
            p += (int)objSize;
        }

        if (duration <= 0.0) return Fail($"未能读取 WMA 有效时长：{path}");
        return new AudioProbeResult { Valid = true, Codec = "wma", SampleRate = 44100, Channels = 2, DurationSeconds = duration };
    }

    private static uint ReadSyncsafe(ReadOnlySpan<byte> b)
    {
        return ((uint)(b[0] & 0x7F) << 21) |
               ((uint)(b[1] & 0x7F) << 14) |
               ((uint)(b[2] & 0x7F) << 7) |
               (uint)(b[3] & 0x7F);
    }

    private static bool MatchAscii(byte[] data, int offset, string ascii)
    {
        if (offset < 0 || offset + ascii.Length > data.Length) return false;
        for (int i = 0; i < ascii.Length; ++i)
            if (data[offset + i] != (byte)ascii[i]) return false;
        return true;
    }

    private static string NormalizeImageMime(string mimeHint, ReadOnlySpan<byte> img)
    {
        if (img.Length >= 4 && img[0] == 0x89 && img[1] == 0x50 && img[2] == 0x4E && img[3] == 0x47)
            return "image/png";
        if (img.Length >= 3 && img[0] == 0xFF && img[1] == 0xD8 && img[2] == 0xFF)
            return "image/jpeg";
        if (img.Length >= 12 && img[0] == 'R' && img[1] == 'I' && img[2] == 'F' && img[3] == 'F')
            return "image/webp";
        return string.IsNullOrEmpty(mimeHint) ? "image/jpeg" : mimeHint;
    }
}
