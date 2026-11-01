namespace GakuMotion.VmdImporter.Vmd;

using System.Numerics;

public static class MotionSampling
{
    /// <summary>
    /// Samples dense position and rotation arrays across all frames [0, totalFrames - 1]
    /// using cubic Bézier curve interpolation.
    /// Strictly replicates sample_bone_keyframes from convert.py.
    /// </summary>
    public static (Vector3[] Positions, Quaternion[] Rotations) SampleBoneKeyframes(
        IReadOnlyList<BoneKeyframe> keyframes,
        int totalFrames,
        RotationInterpolationMode mode = RotationInterpolationMode.Slerp)
    {
        if (totalFrames <= 0)
            return (Array.Empty<Vector3>(), Array.Empty<Quaternion>());

        IReadOnlyList<BoneKeyframe> activeKeys = keyframes;

        if (mode == RotationInterpolationMode.NormalizedComponent)
        {
            var converted = new List<BoneKeyframe>(keyframes.Count);
            foreach (var key in keyframes)
            {
                float x = key.Rotation.X;
                float y = key.Rotation.Y;
                float z = key.Rotation.Z;
                float w = key.Rotation.W;
                float square = ((w * w + x * x) + y * y) + z * z;
                if (!float.IsFinite(square) || square <= 0.0f)
                    throw new InvalidOperationException("Source quaternion keys must be finite and nonzero");

                float inv = 1.0f / MathF.Sqrt(square);
                float qx = x * inv;
                float qy = y * inv;
                float qz = z * inv;
                float qw = w * inv;
                float half = MathF.Acos(Math.Clamp(qw, -1.0f, 1.0f));
                double len = Math.Sqrt((double)qx * qx + (double)qy * qy + (double)qz * qz);
                Vector3 axis = len > 0.0
                    ? new Vector3((float)(qx / len), (float)(qy / len), (float)(qz / len))
                    : new Vector3(1.0f, 0.0f, 0.0f);
                float sinHalf = MathF.Sin(half);
                var prepped = new Quaternion(axis.X * sinHalf, axis.Y * sinHalf, axis.Z * sinHalf, MathF.Cos(half));
                converted.Add(key with { Rotation = prepped });
            }
            activeKeys = converted;
        }

        var positions = new Vector3[totalFrames];
        var rotations = new Quaternion[totalFrames];
        Array.Fill(rotations, Quaternion.Identity);

        if (activeKeys.Count == 0)
            return (positions, rotations);

        if (activeKeys.Count == 1)
        {
            Array.Fill(positions, activeKeys[0].Position);
            Array.Fill(rotations, activeKeys[0].Rotation);
            return (positions, rotations);
        }

        int kfIdx = 0;
        int numKf = activeKeys.Count;

        for (int f = 0; f < totalFrames; f++)
        {
            while (kfIdx < numKf - 1 && activeKeys[kfIdx + 1].Frame <= (uint)f)
            {
                kfIdx++;
            }

            if (kfIdx >= numKf - 1)
            {
                positions[f] = activeKeys[^1].Position;
                rotations[f] = activeKeys[^1].Rotation;
                continue;
            }

            var k1 = activeKeys[kfIdx];
            var k2 = activeKeys[kfIdx + 1];

            if (f <= k1.Frame)
            {
                positions[f] = k1.Position;
                rotations[f] = k1.Rotation;
                continue;
            }

            int span = (int)(k2.Frame - k1.Frame);
            if (span <= 0)
            {
                positions[f] = k1.Position;
                rotations[f] = k1.Rotation;
                continue;
            }

            double rawT = (double)(f - k1.Frame) / span;

            double tx = BezierCurve.EvalBezier(k2.CurveX.P1X, k2.CurveX.P1Y, k2.CurveX.P2X, k2.CurveX.P2Y, rawT);
            double ty = BezierCurve.EvalBezier(k2.CurveY.P1X, k2.CurveY.P1Y, k2.CurveY.P2X, k2.CurveY.P2Y, rawT);
            double tz = BezierCurve.EvalBezier(k2.CurveZ.P1X, k2.CurveZ.P1Y, k2.CurveZ.P2X, k2.CurveZ.P2Y, rawT);

            float px = k1.Position.X + (k2.Position.X - k1.Position.X) * (float)tx;
            float py = k1.Position.Y + (k2.Position.Y - k1.Position.Y) * (float)ty;
            float pz = k1.Position.Z + (k2.Position.Z - k1.Position.Z) * (float)tz;
            positions[f] = new Vector3(px, py, pz);

            double tr = BezierCurve.EvalBezier(k2.CurveR.P1X, k2.CurveR.P1Y, k2.CurveR.P2X, k2.CurveR.P2Y, rawT);

            var q1 = k1.Rotation;
            var q2 = k2.Rotation;

            if (mode == RotationInterpolationMode.NormalizedComponent)
            {
                if (Vector4.Dot(new Vector4(q1.X, q1.Y, q1.Z, q1.W), new Vector4(q2.X, q2.Y, q2.Z, q2.W)) < 0.0f)
                {
                    q2 = new Quaternion(-q2.X, -q2.Y, -q2.Z, -q2.W);
                }

                float t = (float)tr;
                float vx = q1.X + (q2.X - q1.X) * t;
                float vy = q1.Y + (q2.Y - q1.Y) * t;
                float vz = q1.Z + (q2.Z - q1.Z) * t;
                float vw = q1.W + (q2.W - q1.W) * t;
                double norm = Math.Sqrt((double)vx * vx + (double)vy * vy + (double)vz * vz + (double)vw * vw);
                rotations[f] = norm > 0.0
                    ? new Quaternion((float)(vx / norm), (float)(vy / norm), (float)(vz / norm), (float)(vw / norm))
                    : Quaternion.Identity;
            }
            else
            {
                if (Quaternion.Dot(q1, q2) < 0.0f)
                {
                    q2 = -q2;
                }
                rotations[f] = Quaternion.Slerp(q1, q2, (float)tr);
            }
        }

        return (positions, rotations);
    }

    /// <summary>
    /// Samples dense morph weight array across all frames [0, totalFrames - 1].
    /// Strictly replicates sample_morph_keyframes from morph_translation.py.
    /// Duplicate frame entries retain the last value; boundaries hold the nearest key.
    /// </summary>
    public static float[] SampleMorphKeyframes(
        IReadOnlyList<MorphKeyframe> keyframes,
        int totalFrames)
    {
        if (totalFrames <= 0)
            return Array.Empty<float>();

        var weights = new float[totalFrames];
        if (keyframes.Count == 0)
            return weights;

        var keys = new Dictionary<uint, float>();
        foreach (var key in keyframes)
        {
            if (!float.IsFinite(key.Weight))
                throw new ArgumentException($"Invalid morph key weight: {key.Weight}", nameof(keyframes));
            keys[key.Frame] = key.Weight;
        }

        var sortedFrames = keys.Keys.OrderBy(f => f).ToList();
        if (sortedFrames.Count == 1)
        {
            Array.Fill(weights, keys[sortedFrames[0]]);
            return weights;
        }

        int kfIdx = 0;
        int numKf = sortedFrames.Count;

        for (int f = 0; f < totalFrames; f++)
        {
            uint currentFrame = (uint)f;
            while (kfIdx < numKf - 1 && sortedFrames[kfIdx + 1] <= currentFrame)
            {
                kfIdx++;
            }

            if (kfIdx >= numKf - 1)
            {
                weights[f] = keys[sortedFrames[^1]];
                continue;
            }

            uint f1 = sortedFrames[kfIdx];
            uint f2 = sortedFrames[kfIdx + 1];

            if (currentFrame <= f1)
            {
                weights[f] = keys[f1];
                continue;
            }

            float w1 = keys[f1];
            float w2 = keys[f2];
            float t = (float)(currentFrame - f1) / (float)(f2 - f1);
            weights[f] = w1 + (w2 - w1) * t;
        }

        return weights;
    }
}
