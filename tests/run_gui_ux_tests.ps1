#Requires -Version 5.1
[CmdletBinding()]
param([string]$RepoRoot = '', [string]$OutputRoot = '')
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = Split-Path -Parent $PSScriptRoot }
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs = & $taskVswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($taskVs)) { throw 'Visual Studio MSVC x64 was not found.' }
$taskMsvc = (Get-ChildItem -LiteralPath (Join-Path $taskVs 'VC\Tools\MSVC') -Directory | Sort-Object Name | Select-Object -Last 1).FullName
$taskSdk = @('D:\Windows Kits\10', "${env:ProgramFiles(x86)}\Windows Kits\10", "${env:ProgramFiles}\Windows Kits\10") |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Include') } | Select-Object -First 1
if (-not $taskSdk) { throw 'Windows SDK was not found.' }
$taskSdkVersion = (Get-ChildItem -LiteralPath (Join-Path $taskSdk 'Include') -Directory | Sort-Object Name | Select-Object -Last 1).Name
$env:INCLUDE = @("$taskMsvc\include", "$taskSdk\Include\$taskSdkVersion\ucrt", "$taskSdk\Include\$taskSdkVersion\um", "$taskSdk\Include\$taskSdkVersion\shared", "$taskSdk\Include\$taskSdkVersion\winrt") -join ';'
$env:LIB = @("$taskMsvc\lib\x64", "$taskSdk\Lib\$taskSdkVersion\ucrt\x64", "$taskSdk\Lib\$taskSdkVersion\um\x64") -join ';'
$env:PATH = "$taskMsvc\bin\Hostx64\x64;$env:PATH"
$taskBuild = if ($OutputRoot) { Join-Path $OutputRoot 'gui-tests' } else { Join-Path $RepoRoot 'build\tests\gui-ux' }
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskCompiler = Join-Path $taskMsvc 'bin\Hostx64\x64\cl.exe'
$taskExe = Join-Path $taskBuild 'test_gui_ux.exe'
$taskArgs = @('/nologo', '/std:c++20', '/utf-8', '/EHsc', '/O1', '/MT', '/W3', '/D_CRT_SECURE_NO_WARNINGS',
    '/DGKMOTION_ENABLE_IMGUI', '/DIMGUI_ENABLE_TEST_ENGINE', "/I$RepoRoot\src", "/I$RepoRoot\include",
    "/I$RepoRoot\deps\KineTexture\src", "/I$RepoRoot\deps\imgui", "$RepoRoot\tests\test_gui_ux.cpp",
    "$RepoRoot\deps\imgui\imgui.cpp", "$RepoRoot\deps\imgui\imgui_draw.cpp", "$RepoRoot\deps\imgui\imgui_tables.cpp",
    "$RepoRoot\deps\imgui\imgui_widgets.cpp", "/Fe:$taskExe", '/link', 'user32.lib', 'shell32.lib', 'ole32.lib', 'comdlg32.lib')
Push-Location -LiteralPath $taskBuild
try {
    & $taskCompiler @taskArgs
    if ($LASTEXITCODE -ne 0) { throw "GUI UX test build failed (exit=$LASTEXITCODE)." }
    & $taskExe $RepoRoot
    if ($LASTEXITCODE -ne 0) { throw "GUI UX interaction tests failed (exit=$LASTEXITCODE)." }
} finally { Pop-Location }
