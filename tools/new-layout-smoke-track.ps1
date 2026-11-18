#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$target = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $target) { throw 'OutputDirectory already exists; no files were changed.' }
New-Item -ItemType Directory -Path $target | Out-Null
$path = Join-Path $target 'motion.gkclip'
$stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
$writer = New-Object IO.BinaryWriter($stream, [Text.Encoding]::UTF8)
try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes('GKCLIPCL'))
    $writer.Write([uint32]4); $writer.Write([single]30); $writer.Write([uint32]61)
    $writer.Write([single]2); $writer.Write([uint32]1); $writer.Write([single]0.07795)
    $writer.Write([uint16]0); $writer.Write([byte]4); $writer.Write([byte]0)
    $writer.Write([Text.Encoding]::ASCII.GetBytes('Hips'))
    for ($frame = 0; $frame -lt 61; $frame++) {
        # Stationary root and identity Hips rotation. No motion asset, camera,
        # facial data or audio is copied from a user or third party.
        for ($component = 0; $component -lt 6; $component++) { $writer.Write([single]0) }
        $writer.Write([single]1)
    }
    $writer.Write([byte]0); $writer.Write([uint32]0)
    $writer.Flush(); $stream.Flush($true)
} finally { $writer.Dispose() }
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $target 'info.json'), @'
{"title":"Layout smoke test (synthetic neutral 2s)","fps":30,"total_frames":61,"duration_seconds":2,"bone_count":1,"has_camera":false,"has_facial":false,"synthetic_fixture":true}
'@, $utf8)
[IO.File]::WriteAllText((Join-Path $target 'SYNTHETIC_FIXTURE.txt'),
    'Generated neutral 2-second layout/play/stop fixture. Contains no user or third-party motion/audio/camera data.', $utf8)
Get-FileHash -LiteralPath $path -Algorithm SHA256 | Select-Object Path,Hash
