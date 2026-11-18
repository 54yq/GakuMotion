namespace GakuMotion.VmdImporter.Vmd;

using System.Numerics;

public enum InterpolationLayout
{
    Legacy,
    ChannelBlocks
}

public enum RotationInterpolationMode
{
    Slerp,
    NormalizedComponent
}

public readonly record struct BezierControlPoints(
    float P1X, float P1Y, float P2X, float P2Y);

public readonly record struct BoneKeyframe(
    string Name,
    uint Frame,
    Vector3 Position,
    Quaternion Rotation,
    BezierControlPoints CurveX,
    BezierControlPoints CurveY,
    BezierControlPoints CurveZ,
    BezierControlPoints CurveR);
