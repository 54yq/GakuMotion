#Requires -Version 5.1
# ASCII-only runner. Compile the production observer in its fake IL2CPP boundary.
[CmdletBinding()]
param([string]$RepoRoot = '', [string]$VisualStudioPath = '', [string]$WindowsSdkRoot = '')
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = Split-Path -Parent $PSScriptRoot }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe not found.' }
$stageVsPath = $VisualStudioPath
if ([string]::IsNullOrWhiteSpace($stageVsPath)) {
    $stageVsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if ([string]::IsNullOrWhiteSpace($stageVsPath)) { throw 'MSVC x64 installation not found.' }
$stageToolset = Get-ChildItem -LiteralPath (Join-Path $stageVsPath 'VC\Tools\MSVC') -Directory |
    Sort-Object Name | Select-Object -Last 1
if (-not $stageToolset) { throw 'MSVC toolset not found.' }
$stageSdkRoot = $null
foreach ($candidate in @($WindowsSdkRoot, 'D:\Windows Kits\10',
    "${env:ProgramFiles(x86)}\Windows Kits\10", "${env:ProgramFiles}\Windows Kits\10")) {
    if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'Include'))) { $stageSdkRoot = $candidate; break }
}
if (-not $stageSdkRoot) { throw 'Windows SDK not found.' }
$stageSdkVersion = Get-ChildItem -LiteralPath (Join-Path $stageSdkRoot 'Include') -Directory |
    Sort-Object Name | Select-Object -Last 1
$stageMsvc = $stageToolset.FullName
$stageSdk = $stageSdkVersion.Name
$env:INCLUDE = (@("$stageMsvc\include", "$stageSdkRoot\Include\$stageSdk\ucrt",
    "$stageSdkRoot\Include\$stageSdk\um", "$stageSdkRoot\Include\$stageSdk\shared",
    "$stageSdkRoot\Include\$stageSdk\winrt", "$stageSdkRoot\Include\$stageSdk\cppwinrt") -join ';')
$env:LIB = (@("$stageMsvc\lib\x64", "$stageSdkRoot\Lib\$stageSdk\ucrt\x64",
    "$stageSdkRoot\Lib\$stageSdk\um\x64") -join ';')
$env:PATH = "$stageMsvc\bin\Hostx64\x64;$env:PATH"
$stageBuild = Join-Path $RepoRoot 'build\tests'
New-Item -ItemType Directory -Path $stageBuild -Force | Out-Null
$studioSources = @('test_studio_owned_runtime', 'test_studio_lighting_runtime', 'test_studio_video_runtime')
foreach ($studioSource in $studioSources) {
    $studioExecutable = Join-Path $stageBuild ($studioSource + '.exe')
    $studioArguments = @('/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/W4',
        '/D_CRT_SECURE_NO_WARNINGS', "/I$(Join-Path $RepoRoot 'src')", "/I$(Join-Path $RepoRoot 'include')",
        "/I$(Join-Path $RepoRoot 'deps\KineTexture\src')", (Join-Path $PSScriptRoot ($studioSource + '.cpp')),
        "/Fe:$studioExecutable", "/Fo:$(Join-Path $stageBuild ($studioSource + '.obj'))")
    & (Join-Path $stageMsvc 'bin\Hostx64\x64\cl.exe') @studioArguments
    if ($LASTEXITCODE -ne 0) { throw "Studio runtime build failed: $studioSource (exit=$LASTEXITCODE)" }
    & $studioExecutable
    if ($LASTEXITCODE -ne 0) { throw "Studio runtime test failed: $studioSource (exit=$LASTEXITCODE)" }
}