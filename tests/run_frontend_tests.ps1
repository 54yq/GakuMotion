#Requires -Version 5.1
# NOTE: Keep this script ASCII-only (Windows PowerShell 5.1 ANSI parsing).
#
# tests/run_frontend_tests.ps1 -- frontend typecheck, vitest, preview build,
# and offline production asset verification for GakuMotion.
[CmdletBinding()]
param(
    [ValidateSet('Auto', 'Ci', 'Install', 'Skip')]
    [string]$Dependencies = 'Auto'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$frontendRoot = Join-Path $repoRoot 'frontend'

if (-not (Get-Command npm.cmd -ErrorAction SilentlyContinue)) {
    throw 'npm.cmd was not found. Install Node.js/npm and make npm.cmd available on PATH.'
}

$resolvedDeps = $Dependencies
if ($resolvedDeps -eq 'Auto') {
    $viteBin = Join-Path $frontendRoot 'node_modules\.bin\vite.cmd'
    $resolvedDeps = if (Test-Path -LiteralPath $viteBin -PathType Leaf) { 'Skip' } else { 'Ci' }
}

Push-Location -LiteralPath $frontendRoot
try {
    if ($resolvedDeps -ne 'Skip') {
        $verb = if ($resolvedDeps -eq 'Ci') { 'ci' } else { 'install' }
        & npm.cmd $verb '--ignore-scripts' '--no-audit' '--no-fund'
        if ($LASTEXITCODE -ne 0) { throw "npm $verb failed with exit code $LASTEXITCODE" }
    }

    & npm.cmd run typecheck
    if ($LASTEXITCODE -ne 0) { throw "npm run typecheck failed with exit code $LASTEXITCODE" }

    & npm.cmd test
    if ($LASTEXITCODE -ne 0) { throw "npm test failed with exit code $LASTEXITCODE" }

    & npm.cmd run preview:build
    if ($LASTEXITCODE -ne 0) { throw "npm run preview:build failed with exit code $LASTEXITCODE" }

    & (Join-Path $frontendRoot 'build.ps1') -Dependencies Skip
    if ($LASTEXITCODE -ne 0) { throw "frontend/build.ps1 failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host 'GakuMotion frontend checks passed.' -ForegroundColor Green
