namespace GakuMotion.VmdImporter.Vmd;

using System.Numerics;

public readonly record struct CameraKeyframe(
    uint Frame,
    float Distance,
    Vector3 TargetPosition,
    Vector3 RotationEulerRad,
    byte[] CurveBytes,
    uint Fov,
    bool Perspective);
