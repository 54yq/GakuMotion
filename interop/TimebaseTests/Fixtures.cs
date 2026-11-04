namespace GakuMotion.TimebaseTests;

using System.Numerics;
using System.Text;
using System.Text.Json;
using GakuMotion.VmdImporter.Native;
using GakuMotion.VmdImporter.Output;

/// <summary>Synthetic inputs for the timebase cases: an in-memory clip and a real VMD file.</summary>
internal static class Fixtures
{
    public const string BodyBoneName = "Spine";     // profile bone driven by VMD 上半身
    public const string EyeBoneName = "LeftEye";    // v5 head-space eye channel, VMD 左目
    public const int EyeBoneIndex = 2;

    private const float DegreesPerTick = 3.0f;

    /// <summary>
    /// A clip laid out exactly like one the importer emits: one sample per VMD
    /// tick, a moving root, two blendshape channels and one head-space eye bone.
    /// </summary>
    public static GkClipData BuildClip(int frames, float fps)
    {
        var bones = new List<GkClipBoneMeta>
        {
            new(humanoidId: 0, name: "Hips"),
            new(humanoidId: 7, name: BodyBoneName),
            new(humanoidId: 21, name: EyeBoneName, hasRotation: true, headSpaceEyeDelta: true)
        };

        var deltas = new Quaternion[frames, bones.Count];
        var roots = new Vector3[frames];
        var weights = new float[frames, 2];
        for (int f = 0; f < frames; f++)
        {
            deltas[f, 0] = Quaternion.CreateFromAxisAngle(Vector3.UnitX, Radians(DegreesPerTick * f));
            deltas[f, 1] = Quaternion.CreateFromAxisAngle(Vector3.UnitX, Radians(-2.0f * f));
            deltas[f, 2] = Quaternion.CreateFromAxisAngle(Vector3.UnitY, Radians(2.0f * f));
            roots[f] = new Vector3(0.01f * f, 0.0f, -0.02f * f);
            float ramp = frames > 1 ? f / (float)(frames - 1) : 1.0f;
            weights[f, 0] = ramp;
            weights[f, 1] = 1.0f - ramp;
        }

        return new GkClipData(fps)
        {
            Version = 5,
            TotalFrames = frames,
            DurationSeconds = frames / fps,
            RootScale = 0.07795f,
            Bones = bones,
            RootPositions = roots,
            BoneDeltas = deltas,
            BlendshapeNames = new List<string> { "b_face.a", "b_face.i" },
            BlendshapeWeights = weights
        };
    }

    /// <summary>Writes a 121-tick (or N-tick) VMD with body, head and left-eye tracks.</summary>
    public static string WriteSyntheticVmd(string root, int frames)
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        Encoding shiftJis = Encoding.GetEncoding(932);
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "probe.vmd");

        var tracks = new (string Name, Func<int, Quaternion> Rotation, Func<int, Vector3> Position)[]
        {
            ("下半身", f => Quaternion.CreateFromAxisAngle(Vector3.UnitZ, Radians(0.5f * f)), _ => Vector3.Zero),
            ("上半身", f => Quaternion.CreateFromAxisAngle(Vector3.UnitX, Radians(1.0f * f)), _ => Vector3.Zero),
            ("頭", f => Quaternion.CreateFromAxisAngle(Vector3.UnitY, Radians(0.5f * f)), _ => Vector3.Zero),
            ("左目", f => Quaternion.CreateFromAxisAngle(Vector3.UnitZ, Radians(2.0f * f)), _ => Vector3.Zero)
        };

        using var ms = new MemoryStream();
        using (var writer = new BinaryWriter(ms, Encoding.ASCII, leaveOpen: true))
        {
            byte[] header = new byte[30];
            Encoding.ASCII.GetBytes("Vocaloid Motion Data 0002", header);
            writer.Write(header);

            byte[] model = new byte[20];
            shiftJis.GetBytes("timebase probe", model);
            writer.Write(model);

            writer.Write((uint)(tracks.Length * frames));
            foreach ((string name, Func<int, Quaternion> rotation, Func<int, Vector3> position) in tracks)
            {
                for (int f = 0; f < frames; f++)
                {
                    byte[] boneName = new byte[15];
                    shiftJis.GetBytes(name, boneName);
                    writer.Write(boneName);
                    writer.Write((uint)f);

                    Vector3 pos = position(f);
                    writer.Write(pos.X);
                    writer.Write(pos.Y);
                    writer.Write(pos.Z);

                    Quaternion rot = rotation(f);
                    writer.Write(rot.X);
                    writer.Write(rot.Y);
                    writer.Write(rot.Z);
                    writer.Write(rot.W);

                    byte[] curves = new byte[64];
                    for (int channel = 0; channel < 4; channel++)
                    {
                        int at = channel * 16;
                        curves[at + 0] = 20;
                        curves[at + 4] = 20;
                        curves[at + 8] = 107;
                        curves[at + 12] = 107;
                    }
                    writer.Write(curves);
                }
            }

            writer.Write((uint)0); // morph keyframes
            writer.Write((uint)0); // camera keyframes
            writer.Write((uint)0); // light keyframes
            writer.Write((uint)0); // self-shadow keyframes
            writer.Write((uint)0); // IK / visibility property keyframes
        }

        File.WriteAllBytes(path, ms.ToArray());
        return path;
    }

    public static GkClipData Import(string vmd, string root, string title, float requestFps, out ImportTiming timing)
    {
        ImportOutcome outcome = RunImport(vmd, root, title, requestFps);
        timing = outcome.Timing;
        byte[] bytes = File.ReadAllBytes(Path.Combine(outcome.Directory, "motion.gkclip"));
        return GkClipDecoder.Decode(bytes);
    }

    public static string ImportDirectory(string vmd, string root, string title, float requestFps)
        => RunImport(vmd, root, title, requestFps).Directory;

    public static (double Fps, double SourceFps, int Frames, double Duration) ReadInfo(string directory)
    {
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(Path.Combine(directory, "info.json")));
        JsonElement root = doc.RootElement;
        return (
            root.GetProperty("fps").GetDouble(),
            root.GetProperty("source_fps").GetDouble(),
            root.GetProperty("total_frames").GetInt32(),
            root.GetProperty("duration_seconds").GetDouble());
    }

    public static string NewTempRoot(string tag)
    {
        string path = Path.Combine(Path.GetTempPath(),
            "gkmotion-timebase-" + tag + "-" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(path);
        return path;
    }

    public static void TryDelete(string directory)
    {
        try
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
        catch
        {
            // Temp cleanup must not mask a test result.
        }
    }

    private static ImportOutcome RunImport(string vmd, string root, string title, float requestFps)
    {
        var request = new ImportRequest
        {
            MotionVmd = vmd,
            MotionsRoot = Path.Combine(root, "motions"),
            Title = title,
            Fps = requestFps,
            LogPath = Path.Combine(root, title + ".log")
        };

        using var log = new ImportLog(request.LogPath);
        ImportOutcome outcome = ImportPipeline.Run(request, log);
        if (!outcome.Ok)
            throw new Exception($"import '{title}' failed [{outcome.ErrorCode}]: {outcome.Error}");
        return outcome;
    }

    private static float Radians(float degrees) => degrees * MathF.PI / 180.0f;
}
