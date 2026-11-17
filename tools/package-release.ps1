#Requires -Version 5.1
# NOTE: Keep this script ASCII-only (Windows PowerShell 5.1 ANSI parsing).
[CmdletBinding()]
param(
    [switch]$BuildNative,
    [switch]$BuildNativeAot,
    [switch]$BuildFrontend,
    [ValidateSet('Ci', 'Install', 'Skip')]
    [string]$FrontendDependencies = 'Ci',
    [switch]$LegacyImGui,
    [string]$DotNet = 'dotnet',
    [string]$OutputDir = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$repoRoot = [System.IO.Path]::GetFullPath($repo)
$releaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build\release'))

function Get-CanonicalPath {
    param([string]$Base, [string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'Path must not be empty.'
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Assert-PathUnder {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $rootPrefix = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must stay under $rootPrefix; got $fullPath"
    }
    return $fullPath
}

function Assert-SafeOutputChain {
    param([string]$Root, [string]$Target)
    $current = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $targetPath = [System.IO.Path]::GetFullPath($Target)
    $chain = @($current)
    $relative = $targetPath.Substring($current.Length).TrimStart('\')
    foreach ($segment in $relative.Split('\')) {
        if ([string]::IsNullOrEmpty($segment)) { continue }
        $current = Join-Path $current $segment
        $chain += $current
    }
    foreach ($path in $chain) {
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $item = Get-Item -LiteralPath $path -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to package through a reparse point: $path"
        }
        if (-not $item.PSIsContainer -and $path -ne $targetPath) {
            throw "Output parent is a file, not a directory: $path"
        }
    }
}

function Assert-RequiredFile {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing $Label`: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to package a reparse-point file: $Path"
    }
}

function Assert-NoReparseEntries {
    param([string]$Directory)
    $rootItem = Get-Item -LiteralPath $Directory -Force
    if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to package through a reparse point: $Directory"
    }
    foreach ($entry in @(Get-ChildItem -LiteralPath $Directory -Recurse -Force)) {
        if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Package input contains a reparse point: $($entry.FullName)"
        }
    }
}

function Assert-FrontendAssets {
    param([string]$Directory)
    Assert-NoReparseEntries -Directory $Directory
    $index = Join-Path $Directory 'index.html'
    Assert-RequiredFile -Path $index -Label 'frontend entry'

    $entries = @(Get-ChildItem -LiteralPath $Directory -Recurse -File -Force)
    foreach ($entry in $entries) {
        if ($entry.Extension -ieq '.map' -or $entry.Extension -ieq '.tsbuildinfo') {
            throw "Frontend package must not contain source maps or build info: $($entry.FullName)"
        }
        if ($entry.FullName -match '[\\/]node_modules[\\/]') {
            throw "Frontend package must not contain node_modules: $($entry.FullName)"
        }
    }
    if (-not @($entries | Where-Object { $_.Extension -ieq '.js' }).Count) {
        throw 'Frontend package contains no JavaScript asset.'
    }
    if (-not @($entries | Where-Object { $_.Extension -ieq '.css' }).Count) {
        throw 'Frontend package contains no CSS asset.'
    }

    $indexText = Get-Content -LiteralPath $index -Raw -Encoding UTF8
    foreach ($match in [regex]::Matches($indexText, '(?i)\b(?:src|href)\s*=\s*(?:"([^"]*)"|''([^'']*)'')')) {
        $value = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
        if ([string]::IsNullOrWhiteSpace($value) -or $value.StartsWith('#') -or $value.StartsWith('data:')) {
            continue
        }
        if ($value.StartsWith('/') -or $value.StartsWith('\') -or $value -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') {
            throw "Frontend entry must use relative local assets, found: $value"
        }
        $localPath = ($value -split '[?#]', 2)[0]
        $target = [System.IO.Path]::GetFullPath((Join-Path $Directory ([Uri]::UnescapeDataString($localPath))))
        $target = Assert-PathUnder -Path $target -Root $Directory -Label 'Frontend asset reference'
        Assert-RequiredFile -Path $target -Label "frontend asset referenced by index.html ($value)"
    }

    foreach ($cssFile in @($entries | Where-Object { $_.Extension -ieq '.css' })) {
        $cssText = Get-Content -LiteralPath $cssFile.FullName -Raw -Encoding UTF8
        $patterns = @(
            '(?i)url\(\s*(?:"([^"]*)"|''([^'']*)''|([^)''"\s]+))\s*\)',
            '(?i)@import\s+(?:"([^"]*)"|''([^'']*)'')'
        )
        foreach ($pattern in $patterns) {
            foreach ($match in [regex]::Matches($cssText, $pattern)) {
                $value = $null
                foreach ($groupIndex in 1..3) {
                    if ($match.Groups.Count -gt $groupIndex -and $match.Groups[$groupIndex].Success) {
                        $value = $match.Groups[$groupIndex].Value
                        break
                    }
                }
                if ([string]::IsNullOrWhiteSpace($value) -or $value.StartsWith('#') -or $value.StartsWith('data:')) {
                    continue
                }
                if ($value.StartsWith('/') -or $value.StartsWith('\') -or $value -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') {
                    throw "Frontend CSS must use relative local assets, found: $value"
                }
                $localPath = ($value -split '[?#]', 2)[0]
                $target = [System.IO.Path]::GetFullPath((Join-Path $cssFile.DirectoryName ([Uri]::UnescapeDataString($localPath))))
                $target = Assert-PathUnder -Path $target -Root $Directory -Label 'Frontend CSS asset reference'
                Assert-RequiredFile -Path $target -Label "frontend asset referenced by $($cssFile.Name) ($value)"
            }
        }
    }
}

function Assert-X64Dll {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "Artifact is not a PE DLL: $Path"
    }
    $offset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($offset -lt 0 -or $offset -gt ($bytes.Length - 26)) {
        throw "Invalid PE header offset: $Path"
    }
    if ([BitConverter]::ToUInt32($bytes, $offset) -ne 0x00004550 -or
        [BitConverter]::ToUInt16($bytes, $offset + 4) -ne 0x8664 -or
        ([BitConverter]::ToUInt16($bytes, $offset + 22) -band 0x2000) -eq 0 -or
        [BitConverter]::ToUInt16($bytes, $offset + 24) -ne 0x020b) {
        throw "Artifact must be a Windows x64 PE32+ DLL: $Path"
    }
}

function Resolve-Dumpbin {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'vswhere.exe not found; install Visual Studio with Desktop development with C++.'
    }
    $vs = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ([string]::IsNullOrWhiteSpace($vs)) {
        throw 'MSVC x64 toolset not found.'
    }
    $msvc = Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $msvc) {
        throw 'MSVC compiler directory not found.'
    }
    $dumpbin = Join-Path $msvc.FullName 'bin\Hostx64\x64\dumpbin.exe'
    Assert-RequiredFile -Path $dumpbin -Label 'dumpbin.exe'
    return $dumpbin
}

function Get-RepoRelativePath {
    param([string]$Path)
    $fullPath = Assert-PathUnder -Path $Path -Root $repoRoot -Label 'Repository input'
    return $fullPath.Substring($repoRoot.TrimEnd('\').Length + 1).Replace('\', '/')
}

function Copy-PayloadFile {
    param([string]$Source, [string]$Destination)
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $Destination
    $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    if ($sourceHash -ne $destinationHash) {
        throw "Copied payload hash mismatch: $Destination"
    }
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $packageName = 'GakuMotion-WebView2-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + ([guid]::NewGuid().ToString('N').Substring(0, 8))
    $outputPath = Join-Path $releaseRoot $packageName
}
else {
    $outputPath = Get-CanonicalPath -Base $repoRoot -Path $OutputDir
}
if ([string]::Equals($outputPath.TrimEnd('\'), $releaseRoot.TrimEnd('\'), [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDir must be a new child directory under build/release, not build/release itself.'
}
$outputPath = Assert-PathUnder -Path $outputPath -Root $releaseRoot -Label 'OutputDir'
if (Test-Path -LiteralPath $outputPath) {
    throw "OutputDir already exists; existing directories are never overwritten: $outputPath"
}
Assert-SafeOutputChain -Root $repoRoot -Target $outputPath

$buildScript = Join-Path $repoRoot 'build.ps1'
$nativeDll = Join-Path $repoRoot 'build\native\GakuMotion.dll'
$nativeAotDll = Join-Path $repoRoot 'build\nativeaot\GakuMotion.VmdImporter.Native.dll'
$frontendRoot = Join-Path $repoRoot 'frontend'
$frontendDist = Join-Path $frontendRoot 'dist'
$frontendBuildScript = Join-Path $frontendRoot 'build.ps1'
$packageLock = Join-Path $frontendRoot 'package-lock.json'
$viteConfig = Join-Path $frontendRoot 'vite.config.ts'

Assert-RequiredFile -Path $buildScript -Label 'build.ps1'
Assert-RequiredFile -Path $packageLock -Label 'frontend package-lock.json'
Assert-RequiredFile -Path $viteConfig -Label 'frontend vite.config.ts'
if ($BuildFrontend) {
    Assert-RequiredFile -Path $frontendBuildScript -Label 'frontend build script'
}

$legalPayload = [ordered]@{
    'LICENSE' = (Join-Path $repoRoot 'LICENSE')
    'THIRD_PARTY_NOTICES.md' = (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md')
    'candidate/csharp/THIRD_PARTY_NOTICES.md' = (Join-Path $repoRoot 'candidate\csharp\THIRD_PARTY_NOTICES.md')
    'candidate/csharp/third_party/numpy/LICENSE.txt' = (Join-Path $repoRoot 'candidate\csharp\third_party\numpy\LICENSE.txt')
    'include/minhook/LICENSE.txt' = (Join-Path $repoRoot 'include\minhook\LICENSE.txt')
    'deps/KineTexture/src/KineTexture/LICENSE' = (Join-Path $repoRoot 'deps\KineTexture\src\KineTexture\LICENSE')
    'deps/KineTexture/src/KineTexture/NOTICE' = (Join-Path $repoRoot 'deps\KineTexture\src\KineTexture\NOTICE')
}
foreach ($relative in $legalPayload.Keys) {
    Assert-RequiredFile -Path $legalPayload[$relative] -Label "license/notice payload $relative"
}

if ($BuildNative) {
    $nativeArgs = @('-Target', 'Runtime')
    if ($LegacyImGui) { $nativeArgs += '-LegacyImGui' }
    & $buildScript @nativeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Native build failed with exit code $LASTEXITCODE"
    }
}
if ($BuildNativeAot) {
    & $buildScript -Target NativeAOT -DotNet $DotNet
    if ($LASTEXITCODE -ne 0) {
        throw "NativeAOT build failed with exit code $LASTEXITCODE"
    }
}
if ($BuildFrontend) {
    & $frontendBuildScript -Dependencies $FrontendDependencies -StageDir (Join-Path $repoRoot 'build\native\GakuMotion.UI')
    if ($LASTEXITCODE -ne 0) {
        throw "Frontend build failed with exit code $LASTEXITCODE"
    }
}

Assert-RequiredFile -Path $nativeDll -Label 'build/native/GakuMotion.dll'
Assert-RequiredFile -Path $nativeAotDll -Label 'build/nativeaot/GakuMotion.VmdImporter.Native.dll'
Assert-X64Dll -Path $nativeDll
Assert-X64Dll -Path $nativeAotDll
Assert-FrontendAssets -Directory $frontendDist

$dumpbin = Resolve-Dumpbin
$global:LASTEXITCODE = 0
$exports = & $dumpbin /exports $nativeDll | Out-String
if ($LASTEXITCODE -ne 0 -or $exports -notmatch '\bGakuPlugin_Query\b') {
    throw 'GakuMotion.dll is missing required export GakuPlugin_Query.'
}
$global:LASTEXITCODE = 0
$dependents = & $dumpbin /dependents $nativeDll | Out-String
if ($LASTEXITCODE -ne 0) {
    throw 'Cannot read GakuMotion.dll dependents.'
}
if ($dependents -match '(?i)\b(?:vcruntime|msvcp|ucrtbase|ucrtbased)[^\s]*\.dll') {
    throw "Dynamic CRT dependency violates the /MT package contract: $($Matches[0])"
}

$revision = (& git -C $repoRoot rev-parse HEAD 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
    throw 'Cannot resolve source HEAD for the package manifest.'
}
$statusLines = @(& git -C $repoRoot status --porcelain --untracked-files=all 2>$null)
$sourceDirty = $statusLines.Count -gt 0

if (-not (Test-Path -LiteralPath $releaseRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
}
New-Item -ItemType Directory -Path $outputPath | Out-Null

$pluginTargetDir = Join-Path $outputPath 'GakuLoader\plugins'
Copy-PayloadFile -Source $nativeDll -Destination (Join-Path $pluginTargetDir 'GakuMotion.dll')
Copy-PayloadFile -Source $nativeAotDll -Destination (Join-Path $pluginTargetDir 'GakuMotion.VmdImporter.Native.dll')

foreach ($relative in $legalPayload.Keys) {
    $destination = Join-Path $outputPath $relative.Replace('/', '\')
    Copy-PayloadFile -Source $legalPayload[$relative] -Destination $destination
}

$assetRoot = Join-Path $pluginTargetDir 'GakuMotion.UI'
$distPrefix = [System.IO.Path]::GetFullPath($frontendDist).TrimEnd('\')
foreach ($asset in @(Get-ChildItem -LiteralPath $frontendDist -Recurse -File -Force)) {
    $relative = $asset.FullName.Substring($distPrefix.Length + 1)
    Copy-PayloadFile -Source $asset.FullName -Destination (Join-Path $assetRoot $relative)
}
Assert-FrontendAssets -Directory $assetRoot

$installText = @'
GakuMotion WebView2 local staging package

This package contains the GakuMotion plugin, its NativeAOT VMD importer, and complete offline
GakuMotion.UI assets placed in the plugin directory. It contains no GakuLoader host binaries,
game assets, personal configurations, node_modules, source maps, or PDB files.

Install layout (merge into <game root>/; the packaging script never deploys):
  <game root>/GakuLoader/plugins/GakuMotion.dll
  <game root>/GakuLoader/plugins/GakuMotion.VmdImporter.Native.dll
  <game root>/GakuLoader/plugins/GakuMotion.UI/index.html
  <game root>/GakuLoader/plugins/GakuMotion.UI/assets/...

Prerequisites:
  - GakuLoader (version.dll + WebView2Loader.dll beside gakumas.exe)
  - Microsoft Edge WebView2 Evergreen Runtime on the target machine

GakuMotion serves its own GakuMotion.UI directory from <plugin DLL dir>/GakuMotion.UI via the
gakumotion.web-panel/v1 service (virtual host https://gakumotion.invalid/index.html). GakuLoader
never supplies or hosts GakuMotion.UI directly.
'@
[System.IO.File]::WriteAllText((Join-Path $outputPath 'INSTALL.txt'), $installText, [System.Text.Encoding]::ASCII)

$payloadFiles = @(Get-ChildItem -LiteralPath $outputPath -Recurse -File -Force | Sort-Object FullName)
$fileRecords = foreach ($file in $payloadFiles) {
    [ordered]@{
        path = $file.FullName.Substring($outputPath.TrimEnd('\').Length + 1).Replace('\', '/')
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$manifest = [ordered]@{
    schema = 'gakumotion.web-release/1'
    package = Split-Path -Leaf $outputPath
    created_utc = [DateTime]::UtcNow.ToString('o')
    source_commit = $revision
    source_dirty = $sourceDirty
    build = [ordered]@{
        native_invoked = [bool]$BuildNative
        nativeaot_invoked = [bool]$BuildNativeAot
        frontend_invoked = [bool]$BuildFrontend
        frontend_dependencies = if ($BuildFrontend) { $FrontendDependencies } else { 'not invoked' }
        legacy_imgui = [bool]$LegacyImGui
    }
    inputs = [ordered]@{
        native_dll = [ordered]@{ path = Get-RepoRelativePath -Path $nativeDll; sha256 = (Get-FileHash -LiteralPath $nativeDll -Algorithm SHA256).Hash.ToLowerInvariant() }
        nativeaot_dll = [ordered]@{ path = Get-RepoRelativePath -Path $nativeAotDll; sha256 = (Get-FileHash -LiteralPath $nativeAotDll -Algorithm SHA256).Hash.ToLowerInvariant() }
        frontend_index = [ordered]@{ path = Get-RepoRelativePath -Path (Join-Path $frontendDist 'index.html'); sha256 = (Get-FileHash -LiteralPath (Join-Path $frontendDist 'index.html') -Algorithm SHA256).Hash.ToLowerInvariant() }
        frontend_package_lock = [ordered]@{ path = Get-RepoRelativePath -Path $packageLock; sha256 = (Get-FileHash -LiteralPath $packageLock -Algorithm SHA256).Hash.ToLowerInvariant() }
        vite_config = [ordered]@{ path = Get-RepoRelativePath -Path $viteConfig; sha256 = (Get-FileHash -LiteralPath $viteConfig -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    runtime = [ordered]@{
        plugin_directory_assets = 'GakuLoader/plugins/GakuMotion.UI'
        entry_url = 'https://gakumotion.invalid/index.html'
        host_prerequisite = 'GakuLoader (version.dll + WebView2Loader.dll) and Microsoft Edge WebView2 Evergreen Runtime'
    }
    checks = @(
        'GakuMotion.dll: x64 PE32+ DLL, GakuPlugin_Query export, and no dynamic CRT dependency',
        'GakuMotion.VmdImporter.Native.dll: x64 PE32+ DLL',
        'GakuMotion.UI: index.html, JavaScript/CSS assets, relative local HTML references, no node_modules/source maps/build info',
        'Every copied payload file matched its source SHA256'
    )
    excluded = @('node_modules', 'source maps', 'tsbuildinfo', 'PDB', 'GakuLoader host binaries', 'game assets', 'personal configurations', 'WebView2 Evergreen Runtime')
    files = @($fileRecords)
}
$manifestPath = Join-Path $outputPath 'package-manifest.json'
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Packaged GakuMotion plugin + Web UI release: $outputPath" -ForegroundColor Green
Write-Host ("Payload files: $($payloadFiles.Count); manifest: $manifestPath") -ForegroundColor Green
Write-Host ('Manifest SHA256: ' + (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant())
Write-Host 'Local staging only; no deployment or game launch was performed.' -ForegroundColor Yellow
