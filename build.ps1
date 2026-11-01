#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Local source validation/build only. There is no deployment path in this script.
[CmdletBinding()]
param(
    [ValidateSet('Preflight', 'Contracts', 'Runtime', 'NativeAOT', 'All')]
    [string]$Target = 'All',
    [string]$OutputRoot = '',
    [string]$WindowsSdkRoot = ''
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path (Split-Path -Parent $repo) 'GakuMotion-build'
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if ($OutputRoot.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or $OutputRoot -eq $repo) {
    throw 'Use an external output directory for this source staging.'
}
$manifest = Get-Content -LiteralPath (Join-Path $repo 'freeze-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($file in $manifest.payload) {
    $path = [IO.Path]::GetFullPath((Join-Path $repo $file.path))
    if (-not $path.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid manifest path.' }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('Missing frozen file: ' + $file.path) }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw ('Frozen file hash mismatch: ' + $file.path) }
}
Write-Host ('Frozen selected source hashes: PASS (' + $manifest.payload.Count + ' files).')
if ($Target -ne 'Contracts') {
    $requiredTargets = if ($Target -eq 'Runtime') { @('runtime') } elseif ($Target -eq 'NativeAOT') { @('nativeaot') } else { @('runtime', 'nativeaot') }
    $blocked = @($manifest.blocked_required_inputs | Where-Object { @($_.required_for | Where-Object { $_ -in $requiredTargets }).Count -gt 0 })
    if ($blocked.Count -gt 0) {
        Write-Host 'BLOCKED: full source builds require separate provenance review of these excluded inputs:'
        foreach ($item in $blocked) { Write-Host ('  ' + $item.path + ' -- ' + $item.reason) }
        Write-Host 'No compiler, restore, deployment or game action was started.'
        exit 2
    }
    if ($manifest.status -ne 'reviewed-complete-source') { throw 'A complete-source review is required before building a product.' }
    if ($Target -eq 'Preflight') { Write-Host 'Complete reviewed source preflight passed.'; exit 0 }
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio with Desktop development with C++.' }
$vs = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($vs)) { throw 'MSVC x64 toolset not found.' }
$msvc = Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $msvc) { throw 'MSVC compiler directory not found.' }
$sdk = $WindowsSdkRoot
if ([string]::IsNullOrWhiteSpace($sdk)) {
    $sdk = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
}
if ([string]::IsNullOrWhiteSpace($sdk)) {
    $sdk = @(Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'; Join-Path $env:ProgramFiles 'Windows Kits\10') |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Include') } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($sdk)) { throw 'Windows SDK not found; pass -WindowsSdkRoot.' }
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdkVersion) { throw 'Windows SDK Include directory not found.' }
$env:INCLUDE = @((Join-Path $msvc.FullName 'include'), (Join-Path $sdk "Include\$($sdkVersion.Name)\ucrt"),
    (Join-Path $sdk "Include\$($sdkVersion.Name)\um"), (Join-Path $sdk "Include\$($sdkVersion.Name)\shared")) -join ';'
$env:LIB = @((Join-Path $msvc.FullName 'lib\x64'), (Join-Path $sdk "Lib\$($sdkVersion.Name)\ucrt\x64"),
    (Join-Path $sdk "Lib\$($sdkVersion.Name)\um\x64")) -join ';'
if ($Target -eq 'Runtime' -or $Target -eq 'All') {
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
    $toolset = if ([IO.File]::Exists((Join-Path $vs 'MSBuild\Microsoft\VC\v180\Platforms\x64\PlatformToolsets\v145\Toolset.props'))) { 'v145' } else { 'v143' }
    $nativeOutput = Join-Path $OutputRoot 'native'
    $nativeObjects = Join-Path $OutputRoot 'obj\native'
    New-Item -ItemType Directory -Path $nativeOutput,$nativeObjects -Force | Out-Null
    & $msbuild (Join-Path $repo 'GakuMotion.vcxproj') '/p:Configuration=Release ImGui' '/p:Platform=x64' "/p:PlatformToolset=$toolset" `
        "/p:OutDir=$nativeOutput\" "/p:IntDir=$nativeObjects\" "/p:ImGuiIncludeDir=$(Join-Path $repo 'deps\imgui')" `
        "/p:KineTextureIncludeDir=$(Join-Path $repo 'deps\KineTexture\src')" '/p:GkCaptureDiagnostics=false' '/nologo' '/verbosity:minimal'
    if ($LASTEXITCODE -ne 0) { throw ('Runtime build failed: ' + $LASTEXITCODE) }
}
if ($Target -eq 'NativeAOT' -or $Target -eq 'All') {
    $aotOutput = Join-Path $OutputRoot 'nativeaot'
    $aotObjects = Join-Path $OutputRoot 'obj\nativeaot'
    New-Item -ItemType Directory -Path $aotOutput,$aotObjects -Force | Out-Null
    $linker = Join-Path $msvc.FullName 'bin\Hostx64\x64\link.exe'
    $libCreator = Join-Path $msvc.FullName 'bin\Hostx64\x64\lib.exe'
    & dotnet publish (Join-Path $repo 'interop\GakuMotion.VmdImporter.Native\GakuMotion.VmdImporter.Native.csproj') `
        -c Release -r win-x64 -o $aotOutput "-p:ArtifactsPath=$aotObjects" -p:IlcUseEnvironmentalTools=true `
        "-p:CppLinker=$linker" "-p:CppLibCreator=$libCreator"
    if ($LASTEXITCODE -ne 0) { throw ('NativeAOT build failed: ' + $LASTEXITCODE) }
}
if ($Target -ne 'Contracts') { Write-Host 'Requested product build completed locally.'; exit 0 }
$compiler = Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe'
$exe = Join-Path $OutputRoot 'stage_runtime_contracts.exe'
$obj = Join-Path $OutputRoot 'stage_runtime_contracts.obj'
$argsForCompiler = @('/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/W4',
    "/I$(Join-Path $repo 'src')", "/I$(Join-Path $repo 'include')",
    (Join-Path $repo 'tests\test_stage_load_runtime.cpp'), "/Fe:$exe", "/Fo:$obj")
Push-Location -LiteralPath $OutputRoot
try {
    & $compiler @argsForCompiler
    if ($LASTEXITCODE -ne 0) { throw ('Synthetic contract build failed: ' + $LASTEXITCODE) }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw ('Synthetic contract tests failed: ' + $LASTEXITCODE) }
} finally { Pop-Location }
Write-Host 'Synthetic contracts passed. Complete Runtime/NativeAOT remains blocked-review.'
