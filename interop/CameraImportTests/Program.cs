namespace GakuMotion.CameraImportTests;

using System.Buffers.Binary;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using GakuMotion.VmdImporter.Native;

/// <summary>
/// Camera product/ABI tests, with independent hand-written GKCAM/VMD fixtures.
/// Pass --native-dll to drive the exact NativeAOT exports loaded by the plugin.
/// </summary>
internal static unsafe class Program
{
    private static delegate* unmanaged<byte*, byte*, int, int> _run;
    private static delegate* unmanaged<byte*, byte*, int, int> _bodyRun;
    private static int _passed;
    private static int _failed;
    private static string _root = string.Empty;
    private static int _case;

    private static int Main(string[] args)
    {
        nint native = 0;
        if (args.Length == 2 && args[0] == "--native-dll")
        {
            native = NativeLibrary.Load(Path.GetFullPath(args[1]));
            _run = (delegate* unmanaged<byte*, byte*, int, int>)NativeLibrary.GetExport(native, "GkCameraImport_Run");
            _bodyRun = (delegate* unmanaged<byte*, byte*, int, int>)NativeLibrary.GetExport(native, "GkVmdImport_Run");
            var cameraAbi = (delegate* unmanaged<int>)NativeLibrary.GetExport(native, "GkCameraImporter_AbiVersion");
            var bodyAbi = (delegate* unmanaged<int>)NativeLibrary.GetExport(native, "GkVmdImporter_AbiVersion");
            Require(cameraAbi() == 1 && bodyAbi() == 3, "camera v1 / body v3 ABI versions");
            Console.WriteLine("Testing published NativeAOT camera exports");
        }
        else
        {
            _run = &NativeExports.RunCamera;
            _bodyRun = &NativeExports.Run;
            Console.WriteLine("Testing managed camera exports through the unmanaged ABI");
        }

        _root = Path.Combine(Path.GetTempPath(), "gakumotion-camera-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_root);
        try
        {
            Case("baked camera preserves bytes and body; publishes DONE progress", () =>
            {
                string song = Song();
                byte[] body = File.ReadAllBytes(Path.Combine(song, "motion.gkclip"));
                string source = Path.Combine(_root, "镜头 source.gkcamera");
                byte[] camera = Baked(7);
                File.WriteAllBytes(source, camera);
                Result result = Import(source, song);
                Require(result.Ok && result.Code == 0, result.Error);
                Same(camera, File.ReadAllBytes(Path.Combine(song, "camera.gkcamera")), "baked camera bytes");
                Same(body, File.ReadAllBytes(Path.Combine(song, "motion.gkclip")), "body must stay unchanged");
                Require(File.ReadAllText(Path.Combine(song, "import.log")).Contains("@PROGRESS:100:DONE:"), "DONE progress missing");
                Clean(song);
            });
            Case("camera may import itself without truncation", () =>
            {
                string song = Song();
                string source = Path.Combine(song, "camera.gkcamera");
                byte[] bytes = File.ReadAllBytes(source);
                Require(Import(source, song).Ok, "self import failed");
                Same(bytes, File.ReadAllBytes(source), "self import bytes");
                Clean(song);
            });
            Case("invalid baked input leaves previous camera and no staging file", () =>
            {
                string song = Song();
                byte[] before = File.ReadAllBytes(Path.Combine(song, "camera.gkcamera"));
                string source = Path.Combine(_root, "bad.gkcamera");
                File.WriteAllBytes(source, new byte[20]);
                Require(!Import(source, song).Ok, "invalid camera accepted");
                Same(before, File.ReadAllBytes(Path.Combine(song, "camera.gkcamera")), "old camera changed");
                Clean(song);
            });
            Case("VMD bake matches analytic first/middle/last pose and source clock", () =>
            {
                string song = Song(); // Invalid placeholder body is deliberately unauthenticated.
                string source = Vmd(30);
                Result result = Import(source, song);
                Require(result.Ok, result.Error);
                byte[] bytes = File.ReadAllBytes(Path.Combine(song, "camera.gkcamera"));
                Require(BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(16)) == 61, "expected 61 camera samples");
                Near(60, Float(bytes, 12), "camera fps");
                Pose(bytes, 0, -1, 2, 7);
                Pose(bytes, 30, -2, 3, 11);
                Pose(bytes, 60, -3, 4, 15);
                Require(File.ReadAllText(Path.Combine(song, "import.log")).Contains("unauthenticated_source"), "auto registration should explain its raw-coordinate result");
                Clean(song);
            });
            Case("detect-cuts request controls adjacent-key hold", () =>
            {
                string source = Vmd(1);
                string cut = Song(), smooth = Song();
                Require(Import(source, cut, detectCuts: true).Ok, "cut import failed");
                Require(Import(source, smooth, detectCuts: false).Ok, "smooth import failed");
                byte[] held = File.ReadAllBytes(Path.Combine(cut, "camera.gkcamera"));
                byte[] interpolated = File.ReadAllBytes(Path.Combine(smooth, "camera.gkcamera"));
                Pose(held, 1, -1, 2, 7);
                Pose(interpolated, 1, -2, 3, 11);
                Require((BinaryPrimitives.ReadUInt32LittleEndian(held.AsSpan(20 + 2 * 40 + 36)) & 2) != 0, "cut frame flag missing");
            });
            Case("authenticated existing body is decoded for required root registration", () =>
            {
                string song = Song();
                string source = Vmd(30, body: true);
                File.Copy(source, Path.Combine(song, "source.vmd"));
                File.WriteAllBytes(Path.Combine(song, "motion.gkclip"), StationaryBody(31));
                File.WriteAllText(Path.Combine(song, "info.json"), JsonSerializer.Serialize(new
                {
                    source_vmd_sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(source))).ToLowerInvariant(),
                    source_fps = 30
                }));
                Result result = Import(Vmd(30), song, mode: "required");
                Require(result.Ok, result.Error);
                Require(File.ReadAllText(Path.Combine(song, "import.log")).Contains("reason=root_trajectory"), "authenticated root registration was not applied");
                Pose(File.ReadAllBytes(Path.Combine(song, "camera.gkcamera")), 30, -2, 3, 11);
                Clean(song);
            });
            Case("required registration failure preserves old camera and cleans staging", () =>
            {
                string song = Song();
                byte[] before = File.ReadAllBytes(Path.Combine(song, "camera.gkcamera"));
                Result result = Import(Vmd(30), song, mode: "required");
                Require(!result.Ok && result.Code != 0, "required registration unexpectedly passed");
                Same(before, File.ReadAllBytes(Path.Combine(song, "camera.gkcamera")), "failed registration changed camera");
                Clean(song);
            });
            Case("prebaked camera bypasses required body registration", () =>
            {
                string song = Song();
                Require(Import(Path.Combine(song, "camera.gkcamera"), song, mode: "required").Ok, "prebaked camera should bypass registration");
            });
            Case("destination requires an existing motion file", () =>
            {
                string directory = Path.Combine(_root, "empty-song");
                Directory.CreateDirectory(directory);
                Require(!Import(Vmd(30), directory).Ok, "empty destination accepted");
                Require(!File.Exists(Path.Combine(directory, "camera.gkcamera")), "empty destination was modified");
                Clean(directory);
            });
            Case("VMD with no camera keys preserves existing sidecar", () =>
            {
                string song = Song();
                byte[] before = File.ReadAllBytes(Path.Combine(song, "camera.gkcamera"));
                Result result = Import(Vmd(0, empty: true), song);
                Require(!result.Ok && result.Error.Contains("no camera keys"), "missing camera keys must fail clearly");
                Same(before, File.ReadAllBytes(Path.Combine(song, "camera.gkcamera")), "empty VMD changed camera");
                Clean(song);
            });
            Case("invalid sampling settings fail before publishing", () =>
            {
                string song = Song();
                byte[] before = File.ReadAllBytes(Path.Combine(song, "camera.gkcamera"));
                Require(!Import(Vmd(30), song, fps: 59).Ok, "non-multiple camera FPS accepted");
                Require(!Import(Vmd(30), song, scale: -1).Ok, "negative scale accepted");
                Same(before, File.ReadAllBytes(Path.Combine(song, "camera.gkcamera")), "settings failure changed camera");
                Clean(song);
            });
            Case("locked destination keeps previous camera and removes temporary bake", () =>
            {
                string song = Song();
                string target = Path.Combine(song, "camera.gkcamera");
                byte[] before = File.ReadAllBytes(target);
                using (var held = new FileStream(target, FileMode.Open, FileAccess.Read, FileShare.Read))
                    Require(!Import(Vmd(30), song).Ok, "locked destination should fail");
                Same(before, File.ReadAllBytes(target), "locked destination camera changed");
                Clean(song);
            });
            Case("malformed request returns a structured failure across ABI", () =>
            {
                Result result = Call("{}");
                Require(!result.Ok && result.Code != 0 && result.Error.Contains("source"), "bad request was not rejected");
            });
            Case("body failure returns nonzero even when its JSON response fits", () =>
            {
                Result result = Call(JsonSerializer.Serialize(new Dictionary<string, object>
                {
                    ["motion_vmd"] = Path.Combine(_root, "missing.vmd"),
                    ["motions_root"] = _root
                }), body: true);
                Require(!result.Ok && result.Code == 1, "body failure must return failure code 1");
            });
        }
        finally
        {
            Directory.Delete(_root, recursive: true);
            if (native != 0) NativeLibrary.Free(native);
        }
        Console.WriteLine($"{_passed} passed, {_failed} failed");
        return _failed == 0 ? 0 : 1;
    }

    private sealed record Result(bool Ok, int Code, string Error);

    private static Result Import(string source, string song, bool detectCuts = true,
        string mode = "auto", double fps = 60, double scale = 1)
        => Call(JsonSerializer.Serialize(new Dictionary<string, object>
        {
            ["source"] = source, ["motion_dir"] = song, ["detect_cuts"] = detectCuts,
            ["root_registration"] = mode, ["fps"] = fps, ["scale"] = scale,
            ["log_path"] = Path.Combine(song, "import.log")
        }));

    private static Result Call(string json, bool body = false)
    {
        byte[] request = Encoding.UTF8.GetBytes(json + '\0');
        byte[] response = new byte[16384];
        int code;
        delegate* unmanaged<byte*, byte*, int, int> run = body ? _bodyRun : _run;
        fixed (byte* input = request, output = response) code = run(input, output, response.Length);
        int length = Array.IndexOf(response, (byte)0);
        Require(length > 0, "native export returned no response");
        using JsonDocument doc = JsonDocument.Parse(response.AsMemory(0, length));
        return new Result(doc.RootElement.GetProperty("ok").GetBoolean(), code,
            doc.RootElement.GetProperty("error").GetString() ?? string.Empty);
    }

    private static string Song()
    {
        string directory = Path.Combine(_root, "曲目 " + ++_case);
        Directory.CreateDirectory(directory);
        File.WriteAllBytes(Path.Combine(directory, "motion.gkclip"), new byte[] { 10, 20, 30 });
        File.WriteAllText(Path.Combine(directory, "info.json"), "{}");
        File.WriteAllBytes(Path.Combine(directory, "camera.gkcamera"), Baked(1));
        return directory;
    }

    private static byte[] Baked(float x)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        writer.Write("GKCAM\0\0\0"u8);
        writer.Write(1u);
        writer.Write(60f);
        writer.Write(1u);
        foreach (float value in new[] { x, 2, 3, 0, 0, 0, 1, 45, 10 }) writer.Write(value);
        writer.Write(0u);
        return stream.ToArray();
    }

    private static byte[] StationaryBody(int frames)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        writer.Write("GKCLIPCL"u8);
        writer.Write(4u); writer.Write(30f); writer.Write((uint)frames);
        writer.Write(frames / 30f); writer.Write(1u); writer.Write(0.07795f);
        writer.Write((ushort)0); writer.Write((byte)4); writer.Write((byte)1);
        writer.Write("Hips"u8);
        for (int frame = 0; frame < frames; frame++)
            foreach (float value in new[] { 0f, 0, 0, 0, 0, 0, 1 }) writer.Write(value);
        writer.Write((byte)0); // embedded camera absent
        writer.Write(0u); // blendshape channels absent
        return stream.ToArray();
    }

    private static string Vmd(uint lastFrame, bool empty = false, bool body = false)
    {
        string path = Path.Combine(_root, "camera-" + Guid.NewGuid().ToString("N") + ".vmd");
        using var stream = File.Create(path);
        using var writer = new BinaryWriter(stream);
        byte[] header = new byte[30];
        Encoding.ASCII.GetBytes("Vocaloid Motion Data 0002", header);
        writer.Write(header);
        writer.Write(new byte[20]);
        writer.Write(body ? 1u : 0u);
        if (body)
        {
            byte[] name = new byte[15];
            Encoding.ASCII.GetBytes("root", name);
            writer.Write(name); writer.Write(lastFrame);
            foreach (float value in new[] { 0f, 0, 0, 0, 0, 0, 1 }) writer.Write(value);
            writer.Write(new byte[64]);
        }
        writer.Write(0u); // morphs
        writer.Write(empty ? 0u : 2u);
        if (!empty)
            for (int key = 0; key < 2; key++)
            {
                writer.Write(key == 0 ? 0u : lastFrame);
                writer.Write(key == 0 ? -10f : -20f);
                writer.Write(key == 0 ? 1f : 3f);
                writer.Write(key == 0 ? 2f : 4f);
                writer.Write(key == 0 ? 3f : 5f);
                writer.Write(0f); writer.Write(0f); writer.Write(0f);
                for (int curve = 0; curve < 6; curve++) writer.Write(new byte[] { 0, 127, 0, 127 });
                writer.Write(45u);
                writer.Write((byte)0);
            }
        writer.Write(0u); writer.Write(0u); writer.Write(0u);
        return path;
    }

    private static float Float(byte[] bytes, int offset) => BinaryPrimitives.ReadSingleLittleEndian(bytes.AsSpan(offset, 4));
    private static void Pose(byte[] bytes, int frame, double x, double y, double z)
    {
        int offset = 20 + 40 * frame;
        Near(x, Float(bytes, offset), "camera x");
        Near(y, Float(bytes, offset + 4), "camera y");
        Near(z, Float(bytes, offset + 8), "camera z");
        Near(1, Math.Abs(Float(bytes, offset + 16)), "camera quaternion y");
    }
    private static void Clean(string directory) => Require(!Directory.EnumerateFiles(directory, ".camera-import-*").Any(), "staging file leaked");
    private static void Same(byte[] expected, byte[] actual, string message) => Require(expected.SequenceEqual(actual), message);
    private static void Near(double expected, double actual, string message) => Require(double.IsFinite(actual) && Math.Abs(expected - actual) < 1e-5, $"{message}: expected {expected}, got {actual}");
    private static void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static void Case(string name, Action test)
    {
        try { test(); _passed++; Console.WriteLine("PASS " + name); }
        catch (Exception ex) { _failed++; Console.WriteLine("FAIL " + name + ": " + ex.Message); }
    }
}
