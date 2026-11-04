namespace GakuMotion.EditTrackTests;

using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using GakuMotion.TimebaseTests;
using GakuMotion.VmdImporter.Native;
using GakuMotion.VmdImporter.Output;

internal static unsafe class Program
{
    private static delegate* unmanaged<byte*, byte*, int, int> _run;
    private static string _root = string.Empty;
    private static int _cases, _failed;

    private static int Main(string[] args)
    {
        nint native = 0;
        if (args.Length == 2 && args[0] == "--native-dll")
        {
            native = NativeLibrary.Load(Path.GetFullPath(args[1]));
            _run = (delegate* unmanaged<byte*, byte*, int, int>)NativeLibrary.GetExport(native, "GkVmdImport_Run");
        }
        else _run = &NativeExports.Run;
        _root = Fixtures.NewTempRoot("edit-track");
        try
        {
            Case("edit-only JSON permits omitted body/library fields", () =>
            {
                string track = Track();
                Result result = Edit(track, ("title", "  新标题  "));
                Require(result.Ok && result.Directory == track, result.Error);
                Require(Info(track)["title"]!.GetValue<string>() == "新标题", "title was not trimmed");
            });
            Case("metadata edit preserves body/source/camera/audio and custom metadata", () =>
            {
                string track = Track();
                var before = Snapshot(track);
                Require(Edit(track, ("title", "Changed"), ("fps", 60)).Ok, "metadata edit failed");
                var after = Snapshot(track);
                foreach (string path in before.Keys.Where(p => p != "info.json")) Require(before[path] == after[path], path);
                Require(Info(track)["custom"]!["value"]!.GetValue<int>() == 17, "custom metadata lost");
                Require(Info(track)["fps"]!.GetValue<float>() == 30, "fps-only edit must not rebake the body");
            });
            Case("clear flags take precedence and remove both camera files", () =>
            {
                string track = Track();
                string body = Digest(Path.Combine(track, "motion.gkclip"));
                Require(Edit(track, ("clear_audio", true), ("audio", "missing.wav"),
                    ("clear_camera", true), ("camera_vmd", "missing.vmd")).Ok, "clear edit failed");
                Require(!File.Exists(Path.Combine(track, "song.wav")), "audio not cleared");
                Require(!File.Exists(Path.Combine(track, "camera.gkcamera")), "camera not cleared");
                Require(!File.Exists(Path.Combine(track, "source_camera.vmd")), "camera source not cleared");
                Require(!Info(track)["has_camera"]!.GetValue<bool>(), "camera badge stale");
                Require(body == Digest(Path.Combine(track, "motion.gkclip")), "body changed");
            });
            Case("per-file timing edits preserve source payload without requiring VMD backups", () =>
            {
                string track = Track();
                File.Delete(Path.Combine(track, "source.vmd"));
                var before = Snapshot(track);
                var settings = new JsonObject
                {
                    ["motion"] = new JsonObject { ["speed"] = 0.5f, ["offset_seconds"] = 2.0f },
                    ["facial"] = new JsonObject { ["speed"] = 1.5f, ["offset_seconds"] = -1.0f },
                    ["camera"] = new JsonObject { ["speed"] = 0.0f, ["offset_seconds"] = 0.25f },
                    ["audio"] = new JsonObject { ["speed"] = 2.0f, ["offset_seconds"] = -0.5f }
                };
                Require(RunRequest(new JsonObject { ["edit_dir"] = track, ["track_timing"] = settings }).Ok,
                    "timing-only edit failed");
                Require(JsonNode.DeepEquals(Info(track)["track_timing"], settings), "timing values differ after edit");
                Require(Info(track)["custom"]!["value"]!.GetValue<int>() == 17, "custom metadata lost");
                var after = Snapshot(track);
                foreach (string path in before.Keys.Where(p => p != "info.json")) Require(before[path] == after[path], path);
                Require(Edit(track, ("title", "Legacy metadata caller")).Ok, "later title edit failed");
                Require(JsonNode.DeepEquals(Info(track)["track_timing"], settings), "legacy edit erased timing metadata");
            });
            Case("invalid timing fails atomically before changing source or metadata", () =>
            {
                string track = Track();
                var before = Snapshot(track);
                foreach (JsonNode? invalid in new JsonNode?[]
                {
                    JsonValue.Create("bad"),
                    new JsonObject { ["audio"] = new JsonObject { ["speed"] = -0.1f } },
                    new JsonObject { ["motion"] = new JsonObject { ["offset_seconds"] = "bad" } }
                })
                {
                    Require(!RunRequest(new JsonObject { ["edit_dir"] = track, ["track_timing"] = invalid }).Ok,
                        "invalid timing accepted");
                    Same(before, Snapshot(track));
                }
            });
            Case("new import persists independent timing settings inside its transaction", () =>
            {
                string source = Fixtures.WriteSyntheticVmd(Path.Combine(_root, "timed-new-source"), frames: 11);
                var settings = new JsonObject
                {
                    ["motion"] = new JsonObject { ["speed"] = 0.0f, ["offset_seconds"] = -2.0f },
                    ["audio"] = new JsonObject { ["speed"] = 1.2f, ["offset_seconds"] = 3.0f }
                };
                Result result = RunRequest(new JsonObject
                {
                    ["motion_vmd"] = source, ["motions_root"] = Path.Combine(_root, "timed-library"),
                    ["title"] = "Timing", ["clear_audio"] = true, ["track_timing"] = settings
                });
                Require(result.Ok, result.Error);
                JsonObject info = Info(result.Directory);
                Require(info["track_timing"]!["motion"]!["speed"]!.GetValue<float>() == 0,
                    "zero speed was not preserved");
                Require(info["track_timing"]!["audio"]!["offset_seconds"]!.GetValue<float>() == 3,
                    "positive offset was not preserved");
                Require(info["track_timing"]!["facial"]!["speed"]!.GetValue<float>() == 1,
                    "missing track did not get default speed");
            });
            Case("WAV replacement changes only audio and metadata", () =>
            {
                string track = Track(), audio = Path.Combine(_root, "replacement.wav");
                Wav(audio, sample: 123);
                string body = Digest(Path.Combine(track, "motion.gkclip"));
                Require(Edit(track, ("audio", audio)).Ok, "audio edit failed");
                Require(Digest(audio) == Digest(Path.Combine(track, "song.wav")), "audio differs");
                Require(body == Digest(Path.Combine(track, "motion.gkclip")), "body changed");
            });
            Case("invalid audio fails atomically", () =>
            {
                string track = Track(), audio = Path.Combine(_root, "bad.wav");
                File.WriteAllText(audio, "not wave");
                var before = Snapshot(track);
                Require(!Edit(track, ("title", "Must not commit"), ("audio", audio)).Ok, "bad audio accepted");
                Same(before, Snapshot(track));
            });
            Case("prebaked camera replaces camera and clears old VMD provenance", () =>
            {
                string track = Track(), source = Path.Combine(_root, "replacement.gkcamera");
                Camera(source, x: 4);
                string body = Digest(Path.Combine(track, "motion.gkclip"));
                Require(Edit(track, ("camera_vmd", source)).Ok, "camera edit failed");
                Require(Digest(source) == Digest(Path.Combine(track, "camera.gkcamera")), "camera differs");
                Require(!File.Exists(Path.Combine(track, "source_camera.vmd")), "old camera VMD kept");
                Require(Info(track)["source_camera_vmd"]!.GetValue<string>() == source, "camera source metadata wrong");
                Require(body == Digest(Path.Combine(track, "motion.gkclip")), "body changed");
            });
            Case("invalid camera and body inputs fail atomically", () =>
            {
                string track = Track(), bad = Path.Combine(_root, "bad.vmd");
                File.WriteAllText(bad, "invalid");
                var before = Snapshot(track);
                Require(!Edit(track, ("camera_vmd", bad)).Ok, "invalid camera accepted");
                Same(before, Snapshot(track));
                Require(!Edit(track, ("motion_vmd", bad)).Ok, "invalid body accepted");
                Same(before, Snapshot(track));
            });
            Case("body replacement preserves unrelated payload and retimes at requested fps", () =>
            {
                string track = Track(), input = Path.Combine(_root, "new-body");
                string vmd = Fixtures.WriteSyntheticVmd(input, frames: 11);
                File.Delete(Path.Combine(track, "song.wav"));
                Require(Edit(track, ("motion_vmd", vmd), ("fps", 60), ("title", "New body")).Ok, "body edit failed");
                GkClipData clip = GkClipDecoder.Decode(File.ReadAllBytes(Path.Combine(track, "motion.gkclip")));
                Require(clip.Fps == 60 && clip.TotalFrames == 22, "body clock mismatch");
                Require(File.ReadAllText(Path.Combine(track, "custom", "keep.bin")) == "keep", "custom file lost");
                Require(Info(track)["custom"]!["value"]!.GetValue<int>() == 17, "custom metadata lost");
                Require(Info(track)["source_vmd"]!.GetValue<string>() == vmd, "source provenance wrong");
            });
            Case("standalone eye/morph input is backed up and kept on a later body edit", () =>
            {
                string track = Track();
                string morph = Fixtures.WriteSyntheticVmd(Path.Combine(_root, "morph"), frames: 5);
                Require(Edit(track, ("morph_vmd", morph)).Ok, "morph replacement failed");
                string hash = Digest(Path.Combine(track, "source_morph.vmd"));
                Require(hash == Digest(morph), "morph backup differs");
                Require(Info(track)["source_morph_vmd"]!.GetValue<string>() == morph, "morph provenance wrong");
                string body = Fixtures.WriteSyntheticVmd(Path.Combine(_root, "body-after-morph"), frames: 7);
                Require(Edit(track, ("motion_vmd", body)).Ok, "body replacement failed");
                Require(hash == Digest(Path.Combine(track, "source_morph.vmd")), "existing standalone morph discarded");
                Require(Info(track)["source_morph_vmd"]!.GetValue<string>() == morph, "morph provenance changed to staging path");
            });
            Case("disable morph rebakes source backup and preserves original source identity", () =>
            {
                string track = Track(), source = Info(track)["source_vmd"]!.GetValue<string>();
                Require(Edit(track, ("disable_morph", true)).Ok, "disable morph failed");
                Require(!File.Exists(Path.Combine(track, "source_morph.vmd")), "morph backup not removed");
                Require(!Info(track)["has_facial"]!.GetValue<bool>(), "facial badge still set");
                Require(Info(track)["source_morph_vmd"]!.GetValue<string>() == "", "morph source metadata stale");
                Require(Info(track)["source_vmd"]!.GetValue<string>() == source, "source identity replaced by temporary path");
            });
            Case("missing source backup and geometry-dependent rebake fail atomically", () =>
            {
                string track = Track();
                File.Delete(Path.Combine(track, "source.vmd"));
                var before = Snapshot(track);
                Require(!Edit(track, ("disable_morph", true)).Ok, "missing backup accepted");
                Same(before, Snapshot(track));
                string geo = Track();
                File.WriteAllText(Path.Combine(geo, "target_leg_geometry.json"), "{}");
                var original = Snapshot(geo);
                Require(!Edit(geo, ("disable_morph", true)).Ok, "geometry silently discarded");
                Same(original, Snapshot(geo));
                Require(Edit(geo, ("title", "Geometry metadata edit")).Ok, "geometry must not block metadata-only edit");
            });
            Case("directory move failure keeps original track intact", () =>
            {
                string track = Track();
                var before = Snapshot(track);
                using (var held = new FileStream(Path.Combine(track, "motion.gkclip"), FileMode.Open, FileAccess.Read, FileShare.Read))
                    Require(!Edit(track, ("title", "Must not commit")).Ok, "locked track unexpectedly committed");
                Same(before, Snapshot(track));
            });
            Case("add rejects existing normalized names before parsing and preserves track", () =>
            {
                string track = Track(), invalid = Path.Combine(_root, "not-parsed.vmd");
                File.WriteAllText(invalid, "invalid VMD: collision must be reported before parsing");
                var before = Snapshot(track);
                foreach (string title in new[] { "Existing", " Existing... ", "existing" })
                {
                    Result result = RunRequest(new JsonObject
                    {
                        ["motion_vmd"] = invalid, ["motions_root"] = Path.GetDirectoryName(track), ["title"] = title
                    });
                    Require(!result.Ok && result.Error.Contains("管理编辑"), "missing useful name-conflict error: " + result.Error);
                    Same(before, Snapshot(track));
                }
            });
            Case("add rejects illegal-character and truncated-name collisions", () =>
            {
                string seed = Path.Combine(_root, "name-collisions"), library = Path.Combine(seed, "motions");
                string source = Fixtures.WriteSyntheticVmd(seed, frames: 3);
                foreach (var (title, folder) in new[] { ("Collision?", "Collision_"), (new string('X', 101), new string('X', 100)) })
                {
                    string existing = Path.Combine(library, folder);
                    Directory.CreateDirectory(existing);
                    File.WriteAllText(Path.Combine(existing, "keep.txt"), "external data");
                    var before = Snapshot(existing);
                    Result result = RunRequest(new JsonObject
                    {
                        ["motion_vmd"] = source, ["motions_root"] = library, ["title"] = title
                    });
                    Require(!result.Ok && result.Error.Contains("管理编辑"), result.Error);
                    Same(before, Snapshot(existing));
                }
            });
            Case("new-track publication refuses a destination created during preparation", () =>
            {
                string parent = Path.Combine(_root, "late-add"), stage = Path.Combine(parent, "stage"), destination = Path.Combine(parent, "track");
                Directory.CreateDirectory(stage);
                File.WriteAllText(Path.Combine(stage, "new.txt"), "new payload");
                Directory.CreateDirectory(destination);
                File.WriteAllText(Path.Combine(destination, "keep.txt"), "external payload");
                var before = Snapshot(destination);
                ExpectConflict(() => ImportPipeline.PublishNewTrack(stage, destination), "管理编辑");
                Same(before, Snapshot(destination));
                Require(File.Exists(Path.Combine(stage, "new.txt")), "staging was moved over existing track");
                string fileDestination = Path.Combine(parent, "file-track");
                File.WriteAllText(fileDestination, "keep file");
                ExpectConflict(() => ImportPipeline.PublishNewTrack(stage, fileDestination), "管理编辑");
                Require(File.ReadAllText(fileDestination) == "keep file", "existing file overwritten");
            });
            Case("edit publication rejects external content and membership changes", () =>
            {
                Action<string>[] changes =
                {
                    track =>
                    {
                        string path = Path.Combine(track, "custom", "keep.bin");
                        DateTime written = File.GetLastWriteTimeUtc(path);
                        File.WriteAllText(path, "KEEP"); // same byte length, restored timestamp
                        File.SetLastWriteTimeUtc(path, written);
                    },
                    track => File.WriteAllText(Path.Combine(track, "external.txt"), "external add"),
                    track => File.Delete(Path.Combine(track, "custom", "keep.bin")),
                    track => File.Move(Path.Combine(track, "custom", "keep.bin"), Path.Combine(track, "custom", "renamed.bin")),
                    track => Directory.CreateDirectory(Path.Combine(track, "external-empty-directory")),
                    track => File.SetLastWriteTimeUtc(Path.Combine(track, "custom", "keep.bin"), DateTime.UtcNow.AddMinutes(1))
                };
                foreach (Action<string> change in changes)
                {
                    string track = Track(), parent = Path.GetDirectoryName(track)!;
                    var original = ImportPipeline.CaptureEditSignature(track);
                    string stage = Path.Combine(parent, "private-stage"), backup = Path.Combine(parent, "private-backup");
                    Directory.CreateDirectory(stage);
                    File.WriteAllText(Path.Combine(stage, "edited.txt"), "staged edit");
                    change(track);
                    var external = Snapshot(track);
                    ExpectConflict(() => ImportPipeline.PublishEditedTrack(stage, track, backup, original), "外部修改");
                    Same(external, Snapshot(track));
                    Require(Directory.Exists(stage) && !Directory.Exists(backup), "conflict moved original or staging directory");
                }
            });
            Case("edit publication does not recreate an externally removed track", () =>
            {
                string track = Track(), parent = Path.GetDirectoryName(track)!;
                var original = ImportPipeline.CaptureEditSignature(track);
                string stage = Path.Combine(parent, "removed-stage"), backup = Path.Combine(parent, "removed-backup");
                Directory.CreateDirectory(stage);
                Directory.Delete(track, recursive: true);
                ExpectConflict(() => ImportPipeline.PublishEditedTrack(stage, track, backup, original), "外部修改");
                Require(!Directory.Exists(track) && Directory.Exists(stage), "removed track was recreated");
            });
            Case("no private edit stages remain in the scanned library", () =>
            {
                Require(!Directory.EnumerateDirectories(_root, "gkmotion-cs-edit-*", SearchOption.AllDirectories).Any(), "edit staging leaked");
            });
            Case("managed scratch edits preserve full payload and clean completed transaction", () =>
            {
                string track = Track(), scratch = Path.Combine(_root, "managed-temp", "transactions");
                Require(Edit(track, ("title", "Managed scratch"), ("transaction_root", scratch)).Ok, "managed scratch edit failed");
                Require(File.ReadAllText(Path.Combine(track, "custom", "keep.bin")) == "keep", "custom payload changed");
                Require(Directory.Exists(scratch) && !Directory.EnumerateFileSystemEntries(scratch).Any(), "completed transaction was not cleaned");
            });
            Case("managed scratch rejects the scanned library before changing original", () =>
            {
                string track = Track(), library = Path.GetDirectoryName(track)!;
                var before = Snapshot(track);
                Require(!Edit(track, ("title", "Must not apply"), ("transaction_root", library)).Ok, "scratch accepted library root");
                Same(before, Snapshot(track));
            });
        }
        finally
        {
            Fixtures.TryDelete(_root);
            if (native != 0) NativeLibrary.Free(native);
        }
        Console.WriteLine($"{_cases - _failed} PASS / {_failed} FAIL");
        return _failed == 0 ? 0 : 1;
    }

    private static void Case(string name, Action run)
    {
        _cases++;
        try { run(); Console.WriteLine("PASS " + name); }
        catch (Exception ex) { _failed++; Console.WriteLine("FAIL " + name + ": " + ex.Message); }
    }

    private static string Track()
    {
        string seed = Path.Combine(_root, "case-" + _cases + "-" + Guid.NewGuid().ToString("N"));
        string vmd = Fixtures.WriteSyntheticVmd(seed, frames: 5);
        string track = Fixtures.ImportDirectory(vmd, seed, "Existing", requestFps: 0);
        var info = Info(track);
        info["custom"] = new JsonObject { ["value"] = 17 };
        File.WriteAllText(Path.Combine(track, "info.json"), info.ToJsonString());
        Directory.CreateDirectory(Path.Combine(track, "custom"));
        File.WriteAllText(Path.Combine(track, "custom", "keep.bin"), "keep");
        Wav(Path.Combine(track, "song.wav"), sample: 10);
        Camera(Path.Combine(track, "camera.gkcamera"), x: 1);
        File.WriteAllText(Path.Combine(track, "source_camera.vmd"), "old source backup");
        return track;
    }

    private sealed record Result(bool Ok, string Directory, string Error);
    private static Result Edit(string directory, params (string Name, object Value)[] fields)
    {
        var request = new JsonObject { ["edit_dir"] = directory };
        foreach (var (name, value) in fields) request[name] = value switch
        {
            string text => JsonValue.Create(text), bool flag => JsonValue.Create(flag),
            int number => JsonValue.Create(number), _ => throw new InvalidOperationException("test value")
        };
        return RunRequest(request);
    }
    private static Result RunRequest(JsonObject request)
    {
        byte[] input = Encoding.UTF8.GetBytes(request.ToJsonString() + '\0'), output = new byte[65536];
        int code;
        fixed (byte* p = input) fixed (byte* q = output) code = _run(p, q, output.Length);
        using JsonDocument response = JsonDocument.Parse(output.AsMemory(0, Array.IndexOf(output, (byte)0)));
        JsonElement root = response.RootElement;
        return new Result(code == 0 && root.GetProperty("ok").GetBoolean(),
            root.GetProperty("dir").GetString() ?? "", root.GetProperty("error").GetString() ?? "");
    }
    private static void ExpectConflict(Action action, string detail)
    {
        try { action(); }
        catch (InvalidDataException ex)
        {
            Require(ex.Message.Contains(detail), "unhelpful conflict message: " + ex.Message);
            return;
        }
        throw new Exception("external change did not prevent publication");
    }
    private static JsonObject Info(string track) => (JsonObject)JsonNode.Parse(File.ReadAllBytes(Path.Combine(track, "info.json")))!;
    private static string Digest(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)));
    private static Dictionary<string, string> Snapshot(string directory) => Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories)
        .ToDictionary(p => Path.GetRelativePath(directory, p), Digest);
    private static void Same(Dictionary<string, string> a, Dictionary<string, string> b) =>
        Require(a.Count == b.Count && a.All(p => b.TryGetValue(p.Key, out string? value) && value == p.Value), "track changed on failure");
    private static void Require(bool test, string message) { if (!test) throw new Exception(message); }
    private static void Wav(string path, short sample)
    {
        using var writer = new BinaryWriter(File.Create(path));
        writer.Write(Encoding.ASCII.GetBytes("RIFF")); writer.Write(36 + 200);
        writer.Write(Encoding.ASCII.GetBytes("WAVEfmt ")); writer.Write(16);
        writer.Write((short)1); writer.Write((short)1); writer.Write(3000); writer.Write(6000);
        writer.Write((short)2); writer.Write((short)16); writer.Write(Encoding.ASCII.GetBytes("data")); writer.Write(200);
        for (int i = 0; i < 100; i++) writer.Write(sample);
    }
    private static void Camera(string path, float x)
    {
        using var writer = new BinaryWriter(File.Create(path));
        writer.Write(Encoding.ASCII.GetBytes("GKCAM\0\0\0")); writer.Write(1u); writer.Write(60f); writer.Write(1u);
        foreach (float value in new[] { x, 2, 3, 0, 0, 0, 1, 45, 5 }) writer.Write(value);
        writer.Write(0u);
    }
}
