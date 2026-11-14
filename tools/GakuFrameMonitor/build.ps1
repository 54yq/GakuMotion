[CmdletBinding()]
param([string]$OutputRoot='', [string]$PortableVisualStudio='', [string]$WindowsSdkRoot='')
$ErrorActionPreference='Stop'
$repo=$PSScriptRoot
if(-not $OutputRoot){$OutputRoot=Join-Path $repo 'build'}
if(-not $PortableVisualStudio -or -not $WindowsSdkRoot){throw 'Pass -PortableVisualStudio and -WindowsSdkRoot.'}
$toolset=Get-ChildItem -LiteralPath (Join-Path $PortableVisualStudio 'VC/Tools/MSVC') -Directory | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
$sdk=Get-ChildItem -LiteralPath (Join-Path $WindowsSdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$compiler=Join-Path $toolset.FullName 'bin/Hostx64/x64/cl.exe'
$env:INCLUDE=@((Join-Path $toolset.FullName 'include'),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/ucrt"),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/um"),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/shared")) -join ';'
$env:LIB=@((Join-Path $toolset.FullName 'lib/x64'),(Join-Path $WindowsSdkRoot "Lib/$($sdk.Name)/ucrt/x64"),(Join-Path $WindowsSdkRoot "Lib/$($sdk.Name)/um/x64")) -join ';'
$env:PATH=(Join-Path $toolset.FullName 'bin/Hostx64/x64')+';'+$env:PATH
New-Item -ItemType Directory -Path $OutputRoot,(Join-Path $OutputRoot 'obj') -Force | Out-Null
$motionRoot=Split-Path (Split-Path $repo -Parent) -Parent
$inc=Join-Path $motionRoot 'include'
$imgui=Join-Path $motionRoot 'deps/imgui'
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W3',"/I$inc","/I$imgui","/I$repo/src")
& $compiler @common (Join-Path $repo 'tests/frame_tests.cpp') "/Fo$OutputRoot/obj/frame_tests.obj" "/Fe$OutputRoot/frame_tests.exe"
if($LASTEXITCODE -ne 0){throw 'Frame detector tests did not compile.'}
& (Join-Path $OutputRoot 'frame_tests.exe')
if($LASTEXITCODE -ne 0){throw 'Frame detector tests failed.'}
$sources=@((Join-Path $repo 'src/Plugin.cpp'),(Join-Path $imgui 'imgui.cpp'),(Join-Path $imgui 'imgui_draw.cpp'),(Join-Path $imgui 'imgui_widgets.cpp'),(Join-Path $imgui 'imgui_tables.cpp'))
& $compiler @common /LD /O2 @sources "/Fo$OutputRoot/obj/" user32.lib /link "/OUT:$OutputRoot/GakuFrameMonitor.dll"
if($LASTEXITCODE -ne 0){throw 'Frame monitor build failed.'}
& $compiler @common (Join-Path $repo 'tests/abi_tests.cpp') "/Fo$OutputRoot/obj/abi_tests.obj" "/Fe$OutputRoot/abi_tests.exe"
if($LASTEXITCODE -ne 0){throw 'ABI test did not compile.'}
& (Join-Path $OutputRoot 'abi_tests.exe') (Join-Path $OutputRoot 'GakuFrameMonitor.dll')
if($LASTEXITCODE -ne 0){throw 'ABI test failed.'}
Write-Host 'Frame detector tests and local plugin build passed.'
