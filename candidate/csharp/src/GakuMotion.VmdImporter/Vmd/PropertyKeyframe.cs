namespace GakuMotion.VmdImporter.Vmd;

public readonly record struct PropertyKeyframe(
    uint Frame,
    bool Visible,
    IReadOnlyDictionary<string, bool> IkSwitches);
