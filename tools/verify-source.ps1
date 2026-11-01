#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Optional maintainer check; normal builds never invoke this script.
[CmdletBinding()]
param([string]$Manifest = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Manifest)) { $Manifest = Join-Path $repo 'freeze-manifest.json' }
$record = Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
$failures = @()
foreach ($file in $record.payload) {
    $path = [IO.Path]::GetFullPath((Join-Path $repo $file.path))
    if (-not $path.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        $failures += ('Invalid snapshot path: ' + $file.path)
    } elseif (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $failures += ('Missing snapshot file: ' + $file.path)
    } elseif ((Get-Item -LiteralPath $path).Length -ne $file.size -or
              (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) {
        $failures += ('Snapshot differs: ' + $file.path)
    }
}
if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Output $_ }
    exit 1
}
Write-Output ('Source snapshot matches: ' + $record.payload.Count + ' files.')
