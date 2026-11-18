namespace GakuMotion.VmdImporter;

/// <summary>
/// Bootstrap entry and metadata for GakuMotion VMD candidate importer.
/// </summary>
public static class VmdImporterBootstrap
{
    public const string Version = "1.0.0";
    public const string TargetFormat = "net8.0";

    public static string GetStatus()
    {
        return $"GakuMotion.VmdImporter v{Version} initialized";
    }
}
