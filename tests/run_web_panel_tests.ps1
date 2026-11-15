#Requires -Version 5.1
# NOTE: Keep this script ASCII-only (Windows PowerShell 5.1 ANSI parsing).
#
# tests/run_web_panel_tests.ps1 -- headless web-panel bridge contract tests.
# Compiles tests/test_web_panel_bridge.cpp as a single offline TU (no game,
# no ImGui, no real user assets) and runs it. Mirrors the synthetic-contract
# compile in build.ps1.
#
# Usage: powershell -NoProfile -File tests\run_web_panel_tests.ps1
[CmdletBinding()]
param(
    [string]$RepoRoot = '',
    [string]$VisualStudioPath = '',
    [string]$WindowsSdkRoot = '',
    [string]$OutputRoot = ''
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = Split-Path -Parent $PSScriptRoot }
$buildDir = if ($OutputRoot) { Join-Path $OutputRoot 'web-panel-tests' } else { Join-Path $RepoRoot 'build\web-panel-tests' }
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe not found; install Visual Studio with C++ tools.' }
$vs = $VisualStudioPath
if ([string]::IsNullOrWhiteSpace($vs)) {
    $vs = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if ([string]::IsNullOrWhiteSpace($vs)) { throw 'MSVC x64 toolset not found.' }
$msvc = Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $msvc) { throw 'MSVC compiler directory not found.' }
$sdk = $WindowsSdkRoot
if ([string]::IsNullOrWhiteSpace($sdk)) {
    $sdk = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
}
if ([string]::IsNullOrWhiteSpace($sdk)) { throw 'Windows SDK not found; pass -WindowsSdkRoot.' }
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdkVersion) { throw 'Windows SDK Include directory not found.' }

$env:INCLUDE = @((Join-Path $msvc.FullName 'include'), (Join-Path $sdk "Include\$($sdkVersion.Name)\ucrt"),
    (Join-Path $sdk "Include\$($sdkVersion.Name)\um"), (Join-Path $sdk "Include\$($sdkVersion.Name)\shared")) -join ';'
$env:LIB = @((Join-Path $msvc.FullName 'lib\x64'), (Join-Path $sdk "Lib\$($sdkVersion.Name)\ucrt\x64"),
    (Join-Path $sdk "Lib\$($sdkVersion.Name)\um\x64")) -join ';'
$env:PATH = (Join-Path $msvc.FullName 'bin\Hostx64\x64') + ';' + $env:PATH
$compiler = Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe'

$exe = Join-Path $buildDir 'web_panel_contracts.exe'
$obj = Join-Path $buildDir 'web_panel_contracts.obj'
$source = Join-Path $RepoRoot 'tests\test_web_panel_bridge.cpp'
$compilerArgs = @('/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/W3', '/D_CRT_SECURE_NO_WARNINGS',
    "/I$(Join-Path $RepoRoot 'src')", "/I$(Join-Path $RepoRoot 'include')", "/I$(Join-Path $RepoRoot 'tests')",
    "/I$(Join-Path $RepoRoot 'deps\KineTexture\src')",
    $source, "/Fe:$exe", "/Fo:$obj")
Push-Location -LiteralPath $buildDir
try {
    & $compiler @compilerArgs
    if ($LASTEXITCODE -ne 0) { throw ('Web-panel contract build failed: ' + $LASTEXITCODE) }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw ('Web-panel contract tests failed: ' + $LASTEXITCODE) }
} finally { Pop-Location }
Write-Host 'Web-panel bridge contracts passed.'
