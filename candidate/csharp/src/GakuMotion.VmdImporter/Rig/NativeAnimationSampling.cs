namespace GakuMotion.VmdImporter.Rig;

using System.Numerics;
using GakuMotion.VmdImporter.Core;
using GakuMotion.VmdImporter.Vmd;

/// <summary>NativeSourceFK.sample_animation: transform keys before scalar F-curve sampling.</summary>
internal static class NativeAnimationSampling
{
    internal static (Vector3[] Positions, Quaternion[] Rotations) Sample(
        IReadOnlyList<BoneKeyframe> sourceKeys, int totalFrames,
        Matrix3x3 sourceAxis, (int X, int Y, int Z) curveOrder)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(totalFrames);
        var positions = new Vector3[totalFrames];
        var rotations = new Quaternion[totalFrames];
        Array.Fill(rotations, Quaternion.Identity);
        if (sourceKeys.Count == 0 || totalFrames == 0)
            return (positions, rotations);

        var keys = sourceKeys.OrderBy(key => key.Frame).ToArray();
        var keyPositions = new Vector3[keys.Length];
        var keyRotations = new Quaternion[keys.Length];
        for (int i = 0; i < keys.Length; i++)
        {
            keyPositions[i] = Math32.MatVec32(-sourceAxis, keys[i].Position);
            keyRotations[i] = Math32.NativeKeyQuaternion(keys[i].Rotation, sourceAxis);
            if (i > 0)
            {
                Quaternion a = keyRotations[i - 1], b = keyRotations[i];
                double dot = (double)a.X * b.X + (double)a.Y * b.Y + (double)a.Z * b.Z + (double)a.W * b.W;
                if (dot < 0) keyRotations[i] = -b;
            }
        }

        Array.Fill(positions, keyPositions[0]);
        Array.Fill(rotations, keyRotations[0]);
        for (int i = 0; i < keys.Length; i++)
        {
            if (keys[i].Frame < (uint)totalFrames)
            {
                positions[keys[i].Frame] = keyPositions[i];
                rotations[keys[i].Frame] = keyRotations[i];
            }
        }

        int[] order = [curveOrder.X, curveOrder.Y, curveOrder.Z];
        for (int i = 0; i + 1 < keys.Length; i++)
        {
            var first = keys[i];
            var second = keys[i + 1];
            int start = (int)Math.Min((long)totalFrames, (long)first.Frame + 1);
            int stop = (int)Math.Min((long)totalFrames, second.Frame);
            if (stop <= start) continue;
            var times = new float[stop - start];
            for (int j = 0; j < times.Length; j++) times[j] = (float)(start + j) + 1.0f;
            BezierControlPoints[] curves = [second.CurveX, second.CurveY, second.CurveZ];
            for (int axis = 0; axis < 3; axis++)
            {
                float[] values = BezierCurve.EvaluateNativeComponentValues(
                    keyPositions[i][axis], keyPositions[i + 1][axis],
                    (float)first.Frame + 1.0f, (float)second.Frame + 1.0f, curves[order[axis]], times);
                for (int j = 0; j < values.Length; j++) positions[start + j][axis] = values[j];
            }
            for (int component = 0; component < 4; component++)
            {
                float[] values = BezierCurve.EvaluateNativeComponentValues(
                    Component(keyRotations[i], component), Component(keyRotations[i + 1], component),
                    (float)first.Frame + 1.0f, (float)second.Frame + 1.0f, second.CurveR, times);
                for (int j = 0; j < values.Length; j++)
                {
                    var q = rotations[start + j];
                    if (component == 0) q.X = values[j];
                    else if (component == 1) q.Y = values[j];
                    else if (component == 2) q.Z = values[j];
                    else q.W = values[j];
                    rotations[start + j] = q;
                }
            }
        }

        int tail = (int)Math.Min((long)totalFrames, keys[^1].Frame);
        Array.Fill(positions, keyPositions[^1], tail, totalFrames - tail);
        Array.Fill(rotations, keyRotations[^1], tail, totalFrames - tail);
        return (positions, rotations);
    }

    private static float Component(Quaternion q, int index) => index switch
    {
        0 => q.X, 1 => q.Y, 2 => q.Z, _ => q.W,
    };
}
