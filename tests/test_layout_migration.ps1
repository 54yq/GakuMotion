#Requires -Version 5.1
# Synthetic bytes only; no game, user configuration or motion assets are read.
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\migrate-layout.ps1')
$script:MotionChecks = 0
$script:MotionFailures = 0
$script:MotionSkips = 0
function Assert-MotionTest([bool]$Condition, [string]$Message) {
    ++$script:MotionChecks
    if (-not $Condition) { ++$script:MotionFailures; Write-Host ('FAIL: ' + $Message) }
}
function Assert-MotionThrows([scriptblock]$Action, [string]$Message) {
    $threw = $false
    try { & $Action | Out-Null } catch { $threw = $true }
    Assert-MotionTest $threw $Message
}
function Write-MotionFixture([string]$Path, [string]$Text) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}
function New-MotionFixture([string]$Root) {
    $config = Join-Path $Root 'GakuMotion'
    foreach ($name in @('control.json', 'bone_overrides.json', 'initial_position.json', 'camera.json', 'sole_measurement.json', 'stage.json', 'stage.json.bak', 'playlist.json')) {
        Write-MotionFixture (Join-Path $config $name) ('synthetic ' + $name)
    }
    Write-MotionFixture (Join-Path $config 'import.log') 'synthetic log'
    Write-MotionFixture (Join-Path $config 'shoe_mesh_probe.request.json') 'synthetic one-shot request'
    Write-MotionFixture (Join-Path $config 'shoe_mesh_probe_1.trace.json') 'synthetic diagnostics'
    Write-MotionFixture (Join-Path $Root 'motions\track\motion.gkclip') 'fake bytes; not a real clip'
    Write-MotionFixture (Join-Path $Root 'motions\track\custom\unknown.bin') 'preserve unknown user bytes'
    Write-MotionFixture (Join-Path $Root 'motions\.gakumotion-trash\receipt\deleted\source.vmd') 'fake source bytes'
    [IO.Directory]::CreateDirectory((Join-Path $Root 'motions\track\custom\empty')) | Out-Null
    return $Root
}

$testRepo = Get-MotionAbsolutePath (Split-Path -Parent $PSScriptRoot)
$tempParent = Join-Path $testRepo 'build\layout-migration-tests'
Assert-MotionChild $testRepo $tempParent
[IO.Directory]::CreateDirectory($tempParent) | Out-Null
$testRoot = Join-Path $tempParent ('gkmotion-layout-tests-' + [Guid]::NewGuid().ToString('N'))
Assert-MotionChild $tempParent $testRoot
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
try {
    $unicodeName = 'space ' + [char]0x5b66 + [char]0x5712 + [char]::ConvertFromUtf32(0x1f3b5)
    $fixture = New-MotionFixture (Join-Path $testRoot $unicodeName)
    $plan = New-MotionLayoutPlan $fixture
    Assert-MotionTest ($plan.state -eq 'planned' -and $plan.conflicts.Count -eq 0) 'read-only plan succeeds'
    Assert-MotionTest (-not [IO.Directory]::Exists((Join-Path $fixture 'PluginData'))) 'plan creates no category directories'
    Assert-MotionTest (@($plan.files | Where-Object { $_.source -like '*unknown.bin' }).Count -eq 1) 'unknown user files included'
    Assert-MotionTest (@($plan.files | Where-Object { $_.source -like '*.gakumotion-trash*' }).Count -eq 1) 'recoverable trash included'
    Assert-MotionTest (@($plan.files | Where-Object { $_.source -like '*request.json' }).Count -eq 0) 'one-shot requests are not replayed'
    $receipt = Invoke-MotionLayoutMigration $fixture Apply
    Assert-MotionTest ($receipt.state -eq 'active' -and $receipt.filesVerified -eq $plan.files.Count) 'receipt activates only a verified complete copy'
    foreach ($entry in $plan.files) {
        Assert-MotionTest ([IO.File]::Exists($entry.source) -and (Get-MotionSha256 $entry.source) -eq $entry.sha256) 'original file remains unchanged'
        Assert-MotionTest ((Get-MotionSha256 $entry.target) -eq $entry.sha256) 'destination hash matches source'
    }
    Assert-MotionTest ([IO.Directory]::Exists((Join-Path $receipt.paths.userData 'motions\track\custom\empty'))) 'empty user directories retained'
    Assert-MotionTest ([IO.File]::Exists((Join-Path $receipt.paths.pluginData 'layout-v1.json'))) 'atomic active receipt exists'
    Write-MotionFixture (Join-Path $fixture 'GakuMotion\control.json') 'later stale legacy change'
    Assert-MotionTest ((Invoke-MotionLayoutMigration $fixture Apply).state -eq 'already-active') 'active layout never reimports stale originals'
    Assert-MotionTest ([IO.File]::ReadAllText((Join-Path $receipt.paths.configs 'control.json')) -eq 'synthetic control.json') 'active destination user data is not overwritten'

    $fixture = New-MotionFixture (Join-Path $testRoot 'conflict')
    Write-MotionFixture (Join-Path $fixture 'Configs\gakumotion\control.json') 'independent target'
    $plan = New-MotionLayoutPlan $fixture
    Assert-MotionTest ($plan.state -eq 'blocked' -and $plan.conflicts.Count -eq 1) 'different target bytes block before copying'
    Assert-MotionThrows { Invoke-MotionLayoutMigration $fixture Apply } 'apply refuses conflict'
    Assert-MotionTest (-not [IO.Directory]::Exists((Join-Path $fixture 'UserData'))) 'preflight conflict makes no partial user data tree'
    Assert-MotionTest ([IO.File]::ReadAllText((Join-Path $fixture 'Configs\gakumotion\control.json')) -eq 'independent target') 'conflict target preserved'

    $fixture = New-MotionFixture (Join-Path $testRoot 'extra-target')
    Write-MotionFixture (Join-Path $fixture 'UserData\gakumotion\motions\track\independent.bin') 'unrelated target'
    Assert-MotionTest ((New-MotionLayoutPlan $fixture).state -eq 'blocked') 'independent extra destination file is not silently merged'

    $fixture = New-MotionFixture (Join-Path $testRoot 'interrupted')
    $script:MotionOriginalCopy = (Get-Item Function:Copy-MotionLayoutFile).ScriptBlock
    $script:MotionCopyCount = 0
    Set-Item Function:Copy-MotionLayoutFile {
        param($Root, $Entry)
        ++$script:MotionCopyCount
        if ($script:MotionCopyCount -eq 3) { throw 'Injected interruption after verified prefix.' }
        & $script:MotionOriginalCopy $Root $Entry
    }
    try { Assert-MotionThrows { Invoke-MotionLayoutMigration $fixture Apply } 'interrupted copy reports failure' }
    finally { Set-Item Function:Copy-MotionLayoutFile $script:MotionOriginalCopy }
    Assert-MotionTest (-not [IO.File]::Exists((Join-Path $fixture 'PluginData\gakumotion\layout-v1.json'))) 'interruption does not activate partial layout'
    Assert-MotionTest (@((New-MotionLayoutPlan $fixture).files | Where-Object { $_.action -eq 'identical' }).Count -gt 0) 'verified prefix recognized for retry'
    Assert-MotionTest ((Invoke-MotionLayoutMigration $fixture Apply).state -eq 'active') 'retry completes without overwriting verified prefix'

    $fixture = New-MotionFixture (Join-Path $testRoot 'late-source-change')
    $script:MotionCopyCount = 0
    $script:MotionExpectedCopies = (New-MotionLayoutPlan $fixture).files.Count
    Set-Item Function:Copy-MotionLayoutFile {
        param($Root, $Entry)
        & $script:MotionOriginalCopy $Root $Entry
        ++$script:MotionCopyCount
        if ($script:MotionCopyCount -eq $script:MotionExpectedCopies) { Write-MotionFixture $Entry.source 'late synthetic source change' }
    }
    try { Assert-MotionThrows { Invoke-MotionLayoutMigration $fixture Apply } 'late source change is detected by final full verification' }
    finally { Set-Item Function:Copy-MotionLayoutFile $script:MotionOriginalCopy }
    Assert-MotionTest (-not [IO.File]::Exists((Join-Path $fixture 'PluginData\gakumotion\layout-v1.json'))) 'changing source never activates layout'

    $fixture = New-MotionFixture (Join-Path $testRoot 'old-edit')
    Write-MotionFixture (Join-Path $fixture 'gkmotion-cs-edit-old\original\unknown.bin') 'critical old original'
    $plan = New-MotionLayoutPlan $fixture
    Assert-MotionTest ($plan.state -eq 'blocked' -and $plan.blockers[0] -like '*Keep the entire transaction*') 'old unfinished edit blocks with recovery guidance'
    Assert-MotionThrows { Invoke-MotionLayoutMigration $fixture Apply } 'old edit original is never automatically moved or deleted'
    Assert-MotionTest ([IO.File]::ReadAllText((Join-Path $fixture 'gkmotion-cs-edit-old\original\unknown.bin')) -eq 'critical old original') 'old transaction original preserved'

    $fixture = New-MotionFixture (Join-Path $testRoot 'managed-edit')
    Write-MotionFixture (Join-Path $fixture 'Temp\gakumotion\transactions\gkmotion-cs-edit-new\original\unknown.bin') 'critical managed original'
    Assert-MotionTest ((New-MotionLayoutPlan $fixture).state -eq 'blocked') 'new unfinished edit transactions also block migration'

    $fixture = New-MotionFixture (Join-Path $testRoot 'unpublished')
    Write-MotionFixture (Join-Path $fixture 'gkmotion-cs-unpublished\motion.gkclip') 'unpublished preserved'
    $plan = New-MotionLayoutPlan $fixture
    Assert-MotionTest ($plan.state -eq 'planned' -and @($plan.warnings | Where-Object { $_ -like 'Unpublished import staging*' }).Count -eq 1) 'unpublished import staging is retained and reported'
    Invoke-MotionLayoutMigration $fixture Apply | Out-Null
    Assert-MotionTest ([IO.File]::ReadAllText((Join-Path $fixture 'gkmotion-cs-unpublished\motion.gkclip')) -eq 'unpublished preserved') 'unpublished staging not deleted'

    $fixture = New-MotionFixture (Join-Path $testRoot 'invalid-receipt')
    Write-MotionFixture (Join-Path $fixture 'PluginData\gakumotion\layout-v1.json') '{"schemaVersion":99}'
    Assert-MotionThrows { New-MotionLayoutPlan $fixture } 'malformed receipt never causes legacy fallback or replacement'

    $fixture = New-MotionFixture (Join-Path $testRoot 'plan-output')
    $planOutput = Join-Path $testRoot 'explicit-plan.json'
    Invoke-MotionLayoutMigration $fixture Plan $planOutput | Out-Null
    Assert-MotionTest (([IO.File]::ReadAllText($planOutput) | ConvertFrom-Json).state -eq 'planned') 'explicit plan JSON is reviewable'
    Assert-MotionThrows { Invoke-MotionLayoutMigration $fixture Plan $planOutput } 'plan output never overwrites an existing unrelated file'
    $cliOutput = Join-Path $testRoot 'cli-plan.json'
    $cliResult = & (Join-Path $testRepo 'tools\migrate-layout.ps1') -LocalRoot $fixture -Mode Plan -PlanPath $cliOutput
    Assert-MotionTest (($cliResult | ConvertFrom-Json).state -eq 'planned') 'documented CLI Plan entry point is executable'
    $volumeRoot = [IO.Path]::GetPathRoot($testRoot)
    Assert-MotionTest ((Get-MotionAbsolutePath $volumeRoot) -eq $volumeRoot) 'volume root is never changed to drive-relative syntax'
    Assert-MotionThrows { New-MotionLayoutPlan $volumeRoot } 'filesystem root cannot be mistaken for dedicated localRoot'

    $fixture = New-MotionFixture (Join-Path $testRoot 'source-lock')
    $sourceLock = [IO.File]::Open((Join-Path $fixture 'GakuMotion\control.json'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try { Assert-MotionThrows { Invoke-MotionLayoutMigration $fixture Apply } 'unreadable locked source blocks migration before copies' }
    finally { $sourceLock.Dispose() }
    Assert-MotionTest (-not [IO.Directory]::Exists((Join-Path $fixture 'PluginData'))) 'locked source causes no activation or partial tree'

    $fixture = New-MotionFixture (Join-Path $testRoot 'locked')
    $lockDirectory = Join-Path $fixture 'PluginData\gakumotion'
    [IO.Directory]::CreateDirectory($lockDirectory) | Out-Null
    $held = [IO.File]::Open((Join-Path $lockDirectory 'layout-v1.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try { Assert-MotionThrows { Invoke-MotionLayoutMigration $fixture Apply } 'concurrent migration writer is rejected' }
    finally { $held.Dispose() }
    Assert-MotionTest (-not [IO.File]::Exists((Join-Path $lockDirectory 'layout-v1.json'))) 'concurrent attempt cannot activate'

    $fixture = New-MotionFixture (Join-Path $testRoot 'link')
    $outside = Join-Path $testRoot 'outside'
    [IO.Directory]::CreateDirectory($outside) | Out-Null
    $junction = Join-Path $fixture 'motions\track\linked'
    $created = $false
    try { New-Item -ItemType Junction -Path $junction -Target $outside -ErrorAction Stop | Out-Null; $created = $true }
    catch { ++$script:MotionSkips; Write-Host ('SKIP: junction fixture unavailable: ' + $_.Exception.Message) }
    if ($created) { Assert-MotionThrows { New-MotionLayoutPlan $fixture } 'junction in source tree is rejected without traversal' }
}
finally {
    # Only the unique synthetic test root is eligible for cleanup. Never follow
    # junctions recursively; remove the fixture link itself first if it exists.
    Assert-MotionChild $tempParent $testRoot
    if ($null -ne $junction -and [IO.Directory]::Exists($junction)) {
        [IO.Directory]::Delete($junction, $false)
    }
    Assert-MotionNoLink $testRoot
    [IO.Directory]::Delete($testRoot, $true)
}
Write-Host ('Motion layout migration: checks=' + $script:MotionChecks + ' failures=' + $script:MotionFailures + ' skips=' + $script:MotionSkips)
if ($script:MotionFailures -gt 0) { throw 'Motion layout migration checks failed.' }
