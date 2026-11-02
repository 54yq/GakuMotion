#Requires -Version 5.1
# Self-authored synthetic files only. Does not launch Unity/game or play audio.
[CmdletBinding()]
param([string]$VisualStudioPath = '', [string]$WindowsSdkRoot = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build\tests\unicode-paths'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$msvc = Get-ChildItem -LiteralPath (Join-Path $VisualStudioPath 'VC\Tools\MSVC') -Directory |
    Sort-Object Name | Select-Object -Last 1
if (-not $msvc) { throw 'No MSVC x64 toolset found.' }
$sdkRoot = $null
foreach ($candidate in @($WindowsSdkRoot, 'D:\Windows Kits\10', "${env:ProgramFiles(x86)}\Windows Kits\10")) {
    if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'Include'))) { $sdkRoot = $candidate; break }
}
if (-not $sdkRoot) { throw 'No Windows SDK found.' }
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name | Select-Object -Last 1
$env:INCLUDE = @("$($msvc.FullName)\include", "$sdkRoot\Include\$($sdkVersion.Name)\ucrt",
    "$sdkRoot\Include\$($sdkVersion.Name)\um", "$sdkRoot\Include\$($sdkVersion.Name)\shared") -join ';'
$env:LIB = @("$($msvc.FullName)\lib\x64", "$sdkRoot\Lib\$($sdkVersion.Name)\ucrt\x64",
    "$sdkRoot\Lib\$($sdkVersion.Name)\um\x64") -join ';'
$compilerDirectory = Join-Path $msvc.FullName 'bin\Hostx64\x64'
$env:PATH = "$compilerDirectory;$env:PATH"
$executable = Join-Path $buildRoot 'test_unicode_paths.exe'
& (Join-Path $compilerDirectory 'cl.exe') /nologo /std:c++20 /utf-8 /EHsc /MT /W4 `
    "/I$(Join-Path $repoRoot 'src')" "/I$(Join-Path $repoRoot 'deps\KineTexture\src')" `
    (Join-Path $PSScriptRoot 'test_unicode_paths.cpp') "/Fe:$executable" `
    "/Fo:$(Join-Path $buildRoot 'test_unicode_paths.obj')" /link winmm.lib
if ($LASTEXITCODE -ne 0) { throw 'Unicode path test build failed.' }
& $executable $buildRoot
if ($LASTEXITCODE -ne 0) { throw 'Unicode path tests failed.' }
