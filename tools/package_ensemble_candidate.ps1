#Requires -Version 5.1
# Offline-only packaging. This script never discovers or writes a game install.
[CmdletBinding()]
param([string]$MotionDll = '', [string]$BridgeDll = '', [string]$OutputDirectory = '', [string]$SourceTree = '')
$ErrorActionPreference = 'Stop'
$ensembleRoot = Split-Path -Parent $PSScriptRoot
$ensembleBridgeRoot = Join-Path (Split-Path -Parent $ensembleRoot) 'GakuBurstBridge'
if (-not $BridgeDll) { $BridgeDll = Join-Path $ensembleBridgeRoot 'build\multi-stage-v3\GakuBurstBridge.dll' }
if (-not $MotionDll) { $MotionDll = Join-Path $ensembleRoot 'build\GakuMotion.dll' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $ensembleRoot 'build\ensemble-20261106\candidate' }
$ensembleOutput = [IO.Path]::GetFullPath($OutputDirectory)
$ensembleBuildRoot = [IO.Path]::GetFullPath((Join-Path $ensembleRoot 'build')) + [IO.Path]::DirectorySeparatorChar
if (-not $ensembleOutput.StartsWith($ensembleBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Candidate output must remain inside this checkout build directory.'
}
New-Item -ItemType Directory -Path $ensembleOutput -Force | Out-Null
$ensembleItems = @(
    @{ Name = 'GakuMotion.dll'; Source = $MotionDll },
    @{ Name = 'GakuBurstBridge.dll'; Source = $BridgeDll },
    @{ Name = 'ENSEMBLE_STAGE_V1.md'; Source = (Join-Path $ensembleRoot 'docs\ENSEMBLE_STAGE_V1.md') }
)
foreach ($ensembleItem in $ensembleItems) {
    $ensembleItem.Source = (Resolve-Path -LiteralPath $ensembleItem.Source).Path
    $ensembleItem.Sha256 = (Get-FileHash -LiteralPath $ensembleItem.Source -Algorithm SHA256).Hash.ToLowerInvariant()
    $ensembleItem.Bytes = (Get-Item -LiteralPath $ensembleItem.Source).Length
    Copy-Item -LiteralPath $ensembleItem.Source -Destination (Join-Path $ensembleOutput $ensembleItem.Name) -Force
    if ((Get-FileHash -LiteralPath (Join-Path $ensembleOutput $ensembleItem.Name)).Hash.ToLowerInvariant() -ne $ensembleItem.Sha256) {
        throw ('Candidate hash mismatch: ' + $ensembleItem.Name)
    }
}
$ensembleManifest = [ordered]@{
    Format = 'gakumotion-ensemble-offline-candidate-v1'
    CreatedUtc = [DateTime]::UtcNow.ToString('o')
    MotionCommit = (& git -C $ensembleRoot rev-parse HEAD)
    BuildSourceTree = $SourceTree
    BridgeCommit = (& git -C $ensembleBridgeRoot rev-parse HEAD)
    MotionWorkingTree = @(& git -C $ensembleRoot status --short)
    BridgeWorkingTree = @(& git -C $ensembleBridgeRoot status --short)
    Deployed = $false
    RuntimeLoaded = $false
    VisualAccepted = $false
    Note = $(if ($SourceTree) { 'Built from the immutable source tree identified above. Working-tree edits listed for provenance were excluded from this build. No game or staging deployment.' } else { 'Working-tree build; preserved and concurrent edits may be included. No game or staging deployment.' })
    Files = @($ensembleItems | ForEach-Object { [ordered]@{ Name=$_.Name; Sha256=$_.Sha256; Bytes=$_.Bytes } })
}
$ensembleManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $ensembleOutput 'manifest.json') -Encoding UTF8
$ensembleZip = Join-Path (Split-Path -Parent $ensembleOutput) 'GakuMotion-ensemble-v1-offline-20261106.zip'
Compress-Archive -LiteralPath @($ensembleItems | ForEach-Object { Join-Path $ensembleOutput $_.Name }) -DestinationPath $ensembleZip -Force
# Include the manifest in the same archive after all file hashes are verified.
Compress-Archive -LiteralPath (Join-Path $ensembleOutput 'manifest.json') -DestinationPath $ensembleZip -Update
Write-Host ('Offline candidate: ' + $ensembleZip)
Write-Host 'No game or staging directory was modified.'
