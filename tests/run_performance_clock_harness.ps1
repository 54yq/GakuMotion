#Requires -Version 5.1
# ASCII-only for Windows PowerShell. Builds the dependency-free module first;
# optional adapter coverage includes the real MotionPlayer and KineTexture.
[CmdletBinding()]
param(
    [switch]$WithMotionPlayerAdapter,
    [string]$KineTextureInclude = '',
    [string]$VisualStudioPath = '',
    [string]$WindowsSdkRoot = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build\performance-clock'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
if (!$KineTextureInclude) { $KineTextureInclude = Join-Path $repoRoot 'deps\KineTexture\src' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!$VisualStudioPath) {
    $VisualStudioPath = & $vswhere -latest -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (!$VisualStudioPath) { throw 'MSVC installation not found.' }
$toolset = Get-ChildItem (Join-Path $VisualStudioPath 'VC\Tools\MSVC') -Directory |
    Sort-Object Name | Select-Object -Last 1
if (!$toolset) { throw 'MSVC toolset not found.' }
$sdkRoot = $null
foreach ($candidate in @($WindowsSdkRoot, 'D:\Windows Kits\10', "${env:ProgramFiles(x86)}\Windows Kits\10")) {
    if ($candidate -and (Test-Path (Join-Path $candidate 'Include'))) { $sdkRoot = $candidate; break }
}
if (!$sdkRoot) { throw 'Windows SDK not found.' }
$sdkVer = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name | Select-Object -Last 1
if (!$sdkVer) { throw 'Windows SDK version not found.' }
$compiler = Join-Path $toolset.FullName 'bin\Hostx64\x64\cl.exe'
$savedInclude = $env:INCLUDE; $savedLib = $env:LIB; $savedPath = $env:PATH
try {
    $env:INCLUDE = @("$($toolset.FullName)\include", "$sdkRoot\Include\$($sdkVer.Name)\ucrt",
        "$sdkRoot\Include\$($sdkVer.Name)\um", "$sdkRoot\Include\$($sdkVer.Name)\shared") -join ';'
    $env:LIB = @("$($toolset.FullName)\lib\x64", "$sdkRoot\Lib\$($sdkVer.Name)\ucrt\x64",
        "$sdkRoot\Lib\$($sdkVer.Name)\um\x64") -join ';'
    $env:PATH = "$(Split-Path $compiler);$savedPath"
    $modes = @('standalone')
    if ($WithMotionPlayerAdapter) { $modes += 'adapter' }
    foreach ($mode in $modes) {
        $exe = Join-Path $buildDir "$mode.exe"
        $compileArgs = @('/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/W4',
            "/I$(Join-Path $repoRoot 'src')", "/I$(Join-Path $repoRoot 'include')",
            (Join-Path $PSScriptRoot 'performance_clock_harness.cpp'),
            "/Fe:$exe", "/Fo:$(Join-Path $buildDir "$mode.obj")")
        if ($mode -eq 'adapter') {
            if (!(Test-Path (Join-Path $KineTextureInclude 'KineTexture\Sampling.hpp'))) {
                throw 'Adapter tests require KineTexture. Supply -KineTextureInclude or initialize the submodule.'
            }
            $compileArgs += @('/DGKMOTION_CLOCK_ADAPTER_TEST', "/I$KineTextureInclude")
        } else { $compileArgs += '/WX' }
        Write-Host "Building Performance Clock $mode harness (MSVC C++20 /MT)..."
        & $compiler @compileArgs
        if ($LASTEXITCODE -ne 0) { throw "$mode compilation failed ($LASTEXITCODE)" }
        & $exe
        if ($LASTEXITCODE -ne 0) { throw "$mode harness failed ($LASTEXITCODE)" }
    }
} finally {
    $env:INCLUDE = $savedInclude; $env:LIB = $savedLib; $env:PATH = $savedPath
}
