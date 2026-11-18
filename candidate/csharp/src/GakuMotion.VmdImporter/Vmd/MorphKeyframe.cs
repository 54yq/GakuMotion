namespace GakuMotion.VmdImporter.Vmd;

public readonly record struct MorphKeyframe(
    string Name,
    uint Frame,
    float Weight);
