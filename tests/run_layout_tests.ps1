#Requires -Version 5.1
[CmdletBinding()]
param([string]$VisualStudioPath = '', [string]$WindowsSdkRoot = '',
      [string]$NativeDllDirectory = '', [string]$OldAbiDllDirectory = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = Join-Path $repo 'build/tests/layout'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not $VisualStudioPath) {
    $VisualStudioPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$msvc = Get-ChildItem -LiteralPath (Join-Path $VisualStudioPath 'VC/Tools/MSVC') -Directory |
    Sort-Object Name | Select-Object -Last 1
if (-not $msvc) { throw 'MSVC x64 toolset not found.' }
$sdk = @($WindowsSdkRoot, 'D:/Windows Kits/10', "${env:ProgramFiles(x86)}/Windows Kits/10") |
    Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'Include')) } | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK not found.' }
$version = Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Sort-Object Name | Select-Object -Last 1
$env:INCLUDE = @((Join-Path $msvc.FullName 'include'), (Join-Path $sdk "Include/$($version.Name)/ucrt"),
    (Join-Path $sdk "Include/$($version.Name)/um"), (Join-Path $sdk "Include/$($version.Name)/shared")) -join ';'
$env:LIB = @((Join-Path $msvc.FullName 'lib/x64'), (Join-Path $sdk "Lib/$($version.Name)/ucrt/x64"),
    (Join-Path $sdk "Lib/$($version.Name)/um/x64")) -join ';'
$compiler = Join-Path $msvc.FullName 'bin/Hostx64/x64/cl.exe'
$executable = Join-Path $build 'test_data_layout.exe'
& $compiler /nologo /std:c++20 /utf-8 /MT /O1 /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX `
    "/I$(Join-Path $repo 'src')" "/I$(Join-Path $repo 'include')" "/I$(Join-Path $repo 'deps/KineTexture/src')" `
    (Join-Path $PSScriptRoot 'test_data_layout.cpp') "/Fe:$executable" "/Fo:$(Join-Path $build 'test_data_layout.obj')"
if ($LASTEXITCODE -ne 0) { throw "Layout test compile failed: $LASTEXITCODE" }
& $executable $build
if ($LASTEXITCODE -ne 0) { throw "Layout test failed: $LASTEXITCODE" }
if ($NativeDllDirectory) {
    if (-not $OldAbiDllDirectory) { throw 'OldAbiDllDirectory is required for the optional real bridge test.' }
    $bridge = Join-Path $build 'test_layout_bridge.exe'
    & $compiler /nologo /std:c++20 /utf-8 /MT /O1 /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX `
        "/I$(Join-Path $repo 'src')" "/I$(Join-Path $repo 'include')" "/I$(Join-Path $repo 'deps/KineTexture/src')" `
        (Join-Path $PSScriptRoot 'test_native_import_bridge.cpp') "/Fe:$bridge" "/Fo:$(Join-Path $build 'test_layout_bridge.obj')"
    if ($LASTEXITCODE -ne 0) { throw "Bridge test compile failed: $LASTEXITCODE" }
    $work = Join-Path $build ('bridge-' + [Guid]::NewGuid().ToString('N'))
    & $bridge $NativeDllDirectory $OldAbiDllDirectory $work
    if ($LASTEXITCODE -ne 0) { throw "Native bridge test failed: $LASTEXITCODE" }
}
