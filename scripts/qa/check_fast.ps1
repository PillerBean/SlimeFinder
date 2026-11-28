# check_fast.ps1 - fast beta-development gate (skip time-consuming, low-risk checks).
#
# Policy (user, 2026-11-28): during test-version development we may skip self-checks that
# are slow and rarely catch anything. The fast gate is:
#   1) incremental build
#   2) unit tests (build\slime_tests.exe --unit)
#   3) UI text catalog check (three languages)
#   4) sync the local deployment folder (the user's primary manual-test path MUST stay current)
#
# Release-only (run manually before any packaging/release): GUI self-test GPU off/on,
# reference comparison (117 items), kernel tests, packaging verification, etc.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\qa\check_fast.ps1
#   ... -SkipBuild            (reuse the existing build)
#   ... -NoDeploy             (do not sync the local deployment folder)
#   ... -LogDir build\fast    (where to append logs)
#
# ASCII only on purpose (Windows PowerShell 5.1 parses .ps1 as ANSI unless BOM).
param(
    [switch]$SkipBuild,
    [switch]$NoDeploy,
    [string]$LogDir = "build\fastcheck"
)
$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
Set-Location $repo
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $LogDir ("fast-" + $stamp + ".log")
$failed = New-Object System.Collections.Generic.List[string]

Write-Host "== fast beta gate ==" -ForegroundColor Cyan
Write-Host ("log: " + $log)

# 1) incremental build
if (-not $SkipBuild) {
    Write-Host "== 1/4 build (incremental) =="
    cmd /c scripts\build\build_repo.cmd 2>&1 | Tee-Object -Append $log | Out-Null
    if ($LASTEXITCODE -ne 0) { $failed.Add("build") }
}

# 2) unit tests
Write-Host "== 2/4 unit tests =="
& .\build\slime_tests.exe --unit 2>&1 | Tee-Object -Append $log | Out-Null
if ($LASTEXITCODE -ne 0) { $failed.Add("unit") }

# 3) three-language text catalog
Write-Host "== 3/4 i18n =="
py -3 scripts\qa\check_ui_text.py 2>&1 | Tee-Object -Append $log | Out-Null
if ($LASTEXITCODE -ne 0) { $failed.Add("i18n") }

# 4) keep the local deployment folder current (user's primary manual-test path)
if (-not $NoDeploy) {
    Write-Host "== 4/4 sync local deployment =="
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\sync_local_deploy.ps1 2>&1 |
        Tee-Object -Append $log | Out-Null
    if ($LASTEXITCODE -ne 0) { $failed.Add("deploy") }
}

Write-Host ""
if ($failed.Count -eq 0) {
    Write-Host "FAST GATE OK (build + unit + i18n + deploy)" -ForegroundColor Green
    exit 0
}
Write-Host ("FAST GATE FAILED: " + ($failed -join ", ")) -ForegroundColor Red
exit 1
