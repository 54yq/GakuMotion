#Requires -Version 5.1
# Copy-only migration. Plan is read-only unless -PlanPath is explicitly supplied.
# Keep this script ASCII-only for Windows PowerShell 5.1.
[CmdletBinding()]
param(
    [string]$LocalRoot,
    [ValidateSet('Plan', 'Apply')][string]$Mode = 'Plan',
    [string]$PlanPath
)
$ErrorActionPreference = 'Stop'

function Get-MotionAbsolutePath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'A localRoot path is required.' }
    $absolute = [IO.Path]::GetFullPath($Path)
    $volume = [IO.Path]::GetPathRoot($absolute)
    if ($absolute.Length -le $volume.Length) { return $volume }
    return $absolute.TrimEnd([char[]]@('\', '/'))
}

function Assert-MotionNoLink([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    $volume = [IO.Path]::GetPathRoot($absolute)
    $current = $volume
    $parts = $absolute.Substring($volume.Length).Split([char[]]@('\', '/'), [StringSplitOptions]::RemoveEmptyEntries)
    $candidates = @($volume)
    foreach ($part in $parts) { $current = Join-Path $current $part; $candidates += $current }
    foreach ($candidate in $candidates) {
        try { $attributes = [IO.File]::GetAttributes($candidate) }
        catch [IO.FileNotFoundException] { continue }
        catch [IO.DirectoryNotFoundException] { continue }
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw ('Links/reparse points are not supported during migration: ' + $candidate)
        }
    }
}

function Assert-MotionChild([string]$Root, [string]$Path) {
    $rootPath = Get-MotionAbsolutePath $Root
    $childPath = Get-MotionAbsolutePath $Path
    $prefix = $rootPath.TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
    if (-not $childPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw ('Path is outside migration localRoot: ' + $childPath)
    }
    Assert-MotionNoLink $childPath
}

function New-MotionDirectory([string]$Root, [string]$Path) {
    Assert-MotionChild $Root $Path
    [IO.Directory]::CreateDirectory($Path) | Out-Null
    Assert-MotionNoLink $Path
}

function Get-MotionSha256([string]$Path) {
    Assert-MotionNoLink $Path
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Get-MotionLayoutRoots([string]$Root) {
    $rootPath = Get-MotionAbsolutePath $Root
    if ($rootPath -eq [IO.Path]::GetPathRoot($rootPath)) { throw 'localRoot must be a dedicated directory, not a filesystem root.' }
    return [ordered]@{
        configs = Join-Path $rootPath 'Configs\gakumotion'
        userData = Join-Path $rootPath 'UserData\gakumotion'
        pluginData = Join-Path $rootPath 'PluginData\gakumotion'
        logs = Join-Path $rootPath 'Logs\gakumotion'
    }
}

function Get-MotionActiveReceipt([string]$Root, $Roots) {
    $receiptPath = Join-Path $Roots.pluginData 'layout-v1.json'
    Assert-MotionChild $Root $receiptPath
    if (-not [IO.File]::Exists($receiptPath)) { return $null }
    $receipt = [IO.File]::ReadAllText($receiptPath) | ConvertFrom-Json
    if ($receipt.schemaVersion -ne 1 -or $receipt.pluginId -ne 'gakumotion' -or
        $receipt.layout -ne 'gakuloader-v1' -or $receipt.state -ne 'active' -or
        -not [string]::Equals((Get-MotionAbsolutePath $receipt.sourceRoot), $Root, [StringComparison]::OrdinalIgnoreCase)) {
        throw ('Invalid or foreign layout receipt; original files are untouched: ' + $receiptPath)
    }
    foreach ($key in $Roots.Keys) {
        if (-not [string]::Equals((Get-MotionAbsolutePath $receipt.paths.$key), $Roots[$key], [StringComparison]::OrdinalIgnoreCase)) {
            throw ('Layout receipt has a different ' + $key + ' root: ' + $receiptPath)
        }
    }
    return $receipt
}

function Add-MotionLayoutFile($Plan, [string]$Source, [string]$Target, [string]$Category) {
    Assert-MotionChild $Plan.sourceRoot $Source
    Assert-MotionChild $Plan.sourceRoot $Target
    if (-not [IO.File]::Exists($Source)) { return }
    if ([IO.Directory]::Exists($Target)) { throw ('Target is a directory instead of a file: ' + $Target) }
    $hash = Get-MotionSha256 $Source
    $state = 'copy'
    if ([IO.File]::Exists($Target)) {
        if ((Get-MotionSha256 $Target) -eq $hash) { $state = 'identical' }
        else { $state = 'conflict'; $Plan.conflicts.Add($Target) }
    }
    $Plan.files.Add([ordered]@{ source = $Source; target = $Target; category = $Category;
        length = ([IO.FileInfo]$Source).Length; sha256 = $hash; action = $state })
}

function Add-MotionLayoutTree($Plan, [string]$Source, [string]$Target, [string]$Category) {
    Assert-MotionChild $Plan.sourceRoot $Source
    Assert-MotionChild $Plan.sourceRoot $Target
    if (-not [IO.Directory]::Exists($Source)) { return }
    $pending = New-Object 'System.Collections.Generic.Stack[object]'
    $pending.Push(@($Source, $Target))
    while ($pending.Count -gt 0) {
        $entry = $pending.Pop(); $sourceDir = [string]$entry[0]; $targetDir = [string]$entry[1]
        Assert-MotionNoLink $sourceDir
        Assert-MotionChild $Plan.sourceRoot $targetDir
        if ([IO.File]::Exists($targetDir)) { throw ('Target directory is occupied by a file: ' + $targetDir) }
        $Plan.directories.Add([ordered]@{ source = $sourceDir; target = $targetDir; category = $Category })
        foreach ($item in [IO.Directory]::GetFileSystemEntries($sourceDir)) {
            Assert-MotionNoLink $item
            $destination = Join-Path $targetDir ([IO.Path]::GetFileName($item))
            if ([IO.Directory]::Exists($item)) { $pending.Push(@($item, $destination)) }
            else { Add-MotionLayoutFile $Plan $item $destination $Category }
        }
    }
    # A destination may be a verified prefix from an interrupted copy. Extra
    # entries are not silently merged into a same-named independent library.
    if ([IO.Directory]::Exists($Target)) {
        $existing = New-Object 'System.Collections.Generic.Stack[object]'
        $existing.Push(@($Target, $Source))
        while ($existing.Count -gt 0) {
            $pair = $existing.Pop()
            foreach ($item in [IO.Directory]::GetFileSystemEntries([string]$pair[0])) {
                Assert-MotionNoLink $item
                $original = Join-Path ([string]$pair[1]) ([IO.Path]::GetFileName($item))
                Assert-MotionChild $Plan.sourceRoot $original
                if ([IO.Directory]::Exists($item)) {
                    if (-not [IO.Directory]::Exists($original)) { $Plan.conflicts.Add($item) }
                    else { $existing.Push(@($item, $original)) }
                }
                elseif (-not [IO.File]::Exists($original)) { $Plan.conflicts.Add($item) }
            }
        }
    }
}

function New-MotionLayoutPlan([string]$Root) {
    $rootPath = Get-MotionAbsolutePath $Root
    Assert-MotionNoLink $rootPath
    if (-not [IO.Directory]::Exists($rootPath)) { throw ('localRoot does not exist: ' + $rootPath) }
    $roots = Get-MotionLayoutRoots $rootPath
    foreach ($key in $roots.Keys) { Assert-MotionChild $rootPath $roots[$key] }
    $plan = [ordered]@{
        schemaVersion = 1; pluginId = 'gakumotion'; layout = 'gakuloader-v1'; state = 'planned'
        sourceRoot = $rootPath; paths = $roots
        receiptPath = Join-Path $roots.pluginData 'layout-v1.json'
        files = New-Object 'System.Collections.Generic.List[object]'
        directories = New-Object 'System.Collections.Generic.List[object]'
        conflicts = New-Object 'System.Collections.Generic.List[string]'
        blockers = New-Object 'System.Collections.Generic.List[string]'
        warnings = New-Object 'System.Collections.Generic.List[string]'
    }
    $active = Get-MotionActiveReceipt $rootPath $roots
    if ($null -ne $active) { $plan.state = 'already-active'; return $plan }

    foreach ($item in [IO.Directory]::GetFileSystemEntries($rootPath)) {
        $name = [IO.Path]::GetFileName($item)
        if ($name.StartsWith('gkmotion-cs-edit-', [StringComparison]::OrdinalIgnoreCase)) {
            Assert-MotionNoLink $item
            $original = Join-Path $item 'original'
            $plan.blockers.Add(('Unfinished edit transaction: ' + $item + '. Keep the entire transaction, especially original. ' +
                'With the game stopped, identify its target track using the transaction files and source hashes. ' +
                'Back up both copies; restore the proven original to a missing target or preserve the chosen target, then move the whole resolved transaction to a recovery archive outside localRoot. ' +
                'No automatic deletion or restoration is attempted. Run Plan again after recovery.'))
        }
        elseif ($name.StartsWith('gkmotion-cs-', [StringComparison]::OrdinalIgnoreCase)) {
            Assert-MotionNoLink $item
            $plan.warnings.Add('Unpublished import staging is retained without scanning or deleting: ' + $item)
        }
    }
    $transactionRoot = Join-Path $rootPath 'Temp\gakumotion\transactions'
    Assert-MotionChild $rootPath $transactionRoot
    if ([IO.Directory]::Exists($transactionRoot)) {
        foreach ($item in [IO.Directory]::GetFileSystemEntries($transactionRoot)) {
            Assert-MotionNoLink $item
            $name = [IO.Path]::GetFileName($item)
            if ($name.StartsWith('gkmotion-cs-edit-', [StringComparison]::OrdinalIgnoreCase)) {
                $plan.blockers.Add(('Unfinished managed edit transaction: ' + $item + '. Preserve transaction.json and original; ' +
                    'with the game stopped, inspect its proven target and journal phase, back up both copies, complete or restore that transaction, ' +
                    'then archive the resolved directory outside localRoot before re-running Plan. Migration does not delete or restore it.'))
            }
            elseif ($name.StartsWith('gkmotion-cs-', [StringComparison]::OrdinalIgnoreCase)) {
                $plan.warnings.Add('Managed import staging is retained without scanning or deleting: ' + $item)
            }
        }
    }
    $legacyConfig = Join-Path $rootPath 'GakuMotion'
    foreach ($name in @('control.json', 'bone_overrides.json', 'initial_position.json', 'camera.json', 'sole_measurement.json', 'stage.json')) {
        foreach ($suffix in @('', '.bak')) {
            Add-MotionLayoutFile $plan (Join-Path $legacyConfig ($name + $suffix)) (Join-Path $roots.configs ($name + $suffix)) 'configs'
        }
    }
    Add-MotionLayoutFile $plan (Join-Path $legacyConfig 'playlist.json') (Join-Path $roots.pluginData 'playlist.json') 'pluginData'
    Add-MotionLayoutTree $plan (Join-Path $rootPath 'motions') (Join-Path $roots.userData 'motions') 'userData'
    if ([IO.Directory]::Exists($legacyConfig)) {
        Assert-MotionNoLink $legacyConfig
        foreach ($item in [IO.Directory]::GetFileSystemEntries($legacyConfig)) {
            Assert-MotionNoLink $item
            if ([IO.Directory]::Exists($item)) { continue }
            $name = [IO.Path]::GetFileName($item)
            if ($name -in @('import.log', 'camera_import.log', 'vmd_import_timing.jsonl', 'sole_pitch_snapshot.json',
                    'before_quartz_trace.json', 'before_quartz_snapshot.json') -or
                ($name -match '^(body_shadow_|ground_shadow_|eye_gaze_|shoe_mesh_probe_).+\.(json|jsonl)$' -and $name -notmatch '\.request\.json$')) {
                Add-MotionLayoutFile $plan $item (Join-Path $roots.logs $name) 'logs'
            }
            elseif ($name -match '\.request\.json$') {
                $plan.warnings.Add('One-shot developer request is retained at the legacy path and will not be replayed: ' + $item)
            }
        }
    }
    if ($plan.blockers.Count -gt 0 -or $plan.conflicts.Count -gt 0) { $plan.state = 'blocked' }
    return $plan
}

function Write-MotionNewJson([string]$Path, $Value) {
    Assert-MotionNoLink $Path
    $bytes = (New-Object Text.UTF8Encoding($false)).GetBytes(($Value | ConvertTo-Json -Depth 30))
    $file = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $completed = $false
    try { $file.Write($bytes, 0, $bytes.Length); $file.Flush($true); $completed = $true }
    finally {
        $file.Dispose()
        if (-not $completed -and [IO.File]::Exists($Path)) { [IO.File]::Delete($Path) }
    }
}

function Copy-MotionLayoutFile([string]$Root, $Entry) {
    Assert-MotionChild $Root $Entry.source
    Assert-MotionChild $Root $Entry.target
    if ((Get-MotionSha256 $Entry.source) -ne $Entry.sha256) { throw ('Source changed since Plan: ' + $Entry.source) }
    New-MotionDirectory $Root ([IO.Path]::GetDirectoryName($Entry.target))
    if ([IO.File]::Exists($Entry.target)) {
        if ((Get-MotionSha256 $Entry.target) -ne $Entry.sha256) { throw ('Target conflict; no overwrite: ' + $Entry.target) }
        return
    }
    # Every destination is beneath the same link-free localRoot, so this scratch
    # is on the same volume and never pollutes a scanned track after interruption.
    $scratch = Join-Path $Root 'PluginData\gakumotion\migrations\layout-v1'
    New-MotionDirectory $Root $scratch
    $temporary = Join-Path $scratch ('copy-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    $ownsTemporary = $false
    try {
        $input = [IO.File]::Open($Entry.source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        try {
            $output = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $ownsTemporary = $true
            try { $input.CopyTo($output); $output.Flush($true) }
            finally { $output.Dispose() }
        }
        finally { $input.Dispose() }
        if ((Get-MotionSha256 $temporary) -ne $Entry.sha256) { throw ('Copy hash mismatch: ' + $Entry.source) }
        Assert-MotionChild $Root $Entry.target
        [IO.File]::Move($temporary, $Entry.target) # No overwrite; racing target creation fails safely.
    }
    catch { throw ('Copy failed; original retained. Target: ' + $Entry.target + '. ' + $_.Exception.Message) }
    finally {
        Assert-MotionChild $Root $temporary
        if ($ownsTemporary -and [IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

function Assert-MotionPlanUnchanged($Original, $Current) {
    if ($Current.state -eq 'blocked' -or $Current.state -eq 'already-active') { throw 'Migration state changed before activation. Re-run Plan.' }
    if ($Original.files.Count -ne $Current.files.Count -or $Original.directories.Count -ne $Current.directories.Count) {
        throw 'Source tree changed before activation. Existing verified copies are retained; re-run Plan.'
    }
    $originalFiles = @{}; foreach ($entry in $Original.files) { $originalFiles[$entry.source] = $entry.sha256 }
    foreach ($entry in $Current.files) {
        if (-not $originalFiles.ContainsKey($entry.source) -or $originalFiles[$entry.source] -ne $entry.sha256 -or $entry.action -ne 'identical') {
            throw ('Source/target changed before activation: ' + $entry.source + '. Re-run Plan; no active receipt was written.')
        }
    }
    $originalDirs = @{}; foreach ($entry in $Original.directories) { $originalDirs[$entry.source] = $true }
    foreach ($entry in $Current.directories) {
        if (-not $originalDirs.ContainsKey($entry.source)) { throw 'Source directories changed before activation. Re-run Plan.' }
    }
}

function Invoke-MotionLayoutMigration([string]$Root, [string]$Operation = 'Plan', [string]$OutputPlan) {
    $plan = New-MotionLayoutPlan $Root
    if (-not [string]::IsNullOrWhiteSpace($OutputPlan)) {
        $planOutput = [IO.Path]::GetFullPath($OutputPlan)
        Assert-MotionNoLink $planOutput
        Write-MotionNewJson $planOutput $plan
    }
    if ($Operation -eq 'Plan' -or $plan.state -eq 'already-active') { return $plan }
    if ($Operation -ne 'Apply') { throw ('Unknown operation: ' + $Operation) }
    if ($plan.state -eq 'blocked') {
        throw ('Migration blocked. No data was copied. Conflicts: ' + ($plan.conflicts -join '; ') + ' Blockers: ' + ($plan.blockers -join '; '))
    }
    New-MotionDirectory $plan.sourceRoot $plan.paths.pluginData
    $lockPath = Join-Path $plan.paths.pluginData 'layout-v1.lock'
    Assert-MotionChild $plan.sourceRoot $lockPath
    $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        # The lock protects this tool's writers. Re-plan while holding it.
        $plan = New-MotionLayoutPlan $Root
        if ($plan.state -eq 'already-active') { return $plan }
        if ($plan.state -eq 'blocked') { throw 'Migration became blocked before copying. Re-run Plan.' }
        $attempt = [Guid]::NewGuid().ToString('N')
        $manifestDirectory = Join-Path $plan.paths.pluginData 'migrations\layout-v1'
        New-MotionDirectory $plan.sourceRoot $manifestDirectory
        $manifestPath = Join-Path $manifestDirectory ('attempt-' + $attempt + '.json')
        Write-MotionNewJson $manifestPath $plan
        foreach ($entry in $plan.directories) { New-MotionDirectory $plan.sourceRoot $entry.target }
        foreach ($entry in $plan.files) { Copy-MotionLayoutFile $plan.sourceRoot $entry }
        foreach ($key in $plan.paths.Keys) { New-MotionDirectory $plan.sourceRoot $plan.paths[$key] }
        New-MotionDirectory $plan.sourceRoot (Join-Path $plan.paths.userData 'motions')
        $verified = New-MotionLayoutPlan $Root
        Assert-MotionPlanUnchanged $plan $verified
        $receipt = [ordered]@{
            schemaVersion = 1; pluginId = 'gakumotion'; layout = 'gakuloader-v1'; state = 'active'
            sourceRoot = $plan.sourceRoot; paths = $plan.paths; attemptId = $attempt
            activatedUtc = [DateTime]::UtcNow.ToString('o'); manifest = $manifestPath
            sourcePolicy = 'retained'; filesVerified = $plan.files.Count
        }
        $temporaryReceipt = Join-Path $plan.paths.pluginData ('layout-v1-' + $attempt + '.tmp')
        $ownsReceipt = $false
        try {
            Write-MotionNewJson $temporaryReceipt $receipt
            $ownsReceipt = $true
            Assert-MotionChild $plan.sourceRoot $plan.receiptPath
            [IO.File]::Move($temporaryReceipt, $plan.receiptPath) # Atomic activation; never replaces a receipt.
        }
        finally {
            Assert-MotionChild $plan.sourceRoot $temporaryReceipt
            if ($ownsReceipt -and [IO.File]::Exists($temporaryReceipt)) { [IO.File]::Delete($temporaryReceipt) }
        }
        return $receipt
    }
    finally { $lock.Dispose() }
}

if ($MyInvocation.InvocationName -ne '.') {
    Invoke-MotionLayoutMigration $LocalRoot $Mode $PlanPath | ConvertTo-Json -Depth 30
}
