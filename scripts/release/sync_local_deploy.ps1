# sync_local_deploy.ps1 - copy the current development build into the local deployment dir.
#
# Why: the user's primary manual-test path is <main-repo>\dist\SlimeFinder\SlimeFinder.exe.
# It must always carry the latest development build (any version, even unpublished).
# This script refreshes binaries/assets in that folder WITHOUT touching runtime data
# (config*.json / cache / logs / crashes are preserved), and does NOT publish anything.
#
# Usage (from the worktree, scripts keep themselves ASCII-only):
#   powershell -ExecutionPolicy Bypass -File scripts\release\sync_local_deploy.ps1
#   ... -DeployDir "<explicit deploy dir>"   (override auto-detection)
#   ... -Repo "<repo root>"                  (override build source)
#
# Auto-detection: uses `git rev-parse --git-common-dir` so it also works from
# .agents/worktrees/<branch> checkouts (common .git belongs to the main repo).
#
# Exit code: 0 = synced and verified; 1 = failure (missing build, copy mismatch, ...).
param(
    [string]$Repo = "",
    [string]$DeployDir = ""
)
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Repo)) {
    $Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
Set-Location $Repo

if ([string]::IsNullOrWhiteSpace($DeployDir)) {
    $common = (& git -C $Repo rev-parse --git-common-dir 2>$null | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($common)) {
        throw "cannot resolve git common dir; pass -DeployDir explicitly"
    }
    if ([System.IO.Path]::IsPathRooted($common)) {
        $commonPath = [System.IO.Path]::GetFullPath($common)
    } else {
        $commonPath = [System.IO.Path]::GetFullPath((Join-Path $Repo $common))
    }
    $mainRepo = Split-Path -Parent $commonPath
    $DeployDir = Join-Path $mainRepo "dist\SlimeFinder"
}

$buildDir = Join-Path $Repo "build"
$buildExe = Join-Path $buildDir "SlimeFinder.exe"
if (-not (Test-Path $buildExe)) {
    throw "missing build artifact: $buildExe (run scripts\build\build_repo.cmd first)"
}
if (-not (Test-Path $DeployDir)) {
    throw "deployment dir missing: $DeployDir"
}

Write-Host "== sync local deployment ==" -ForegroundColor Cyan
Write-Host ("build : " + $Repo)
Write-Host ("deploy: " + $DeployDir)

# The deployment copy is usually the app the user is testing right now, so
# SlimeFinder.exe may be locked. Windows refuses overwrite but allows RENAME of a
# running exe: move the old file aside, then copy the fresh one into place.
function Copy-WithReplace([string]$src, [string]$dst) {
    try {
        Copy-Item $src $dst -Force
        return
    } catch {
        if (-not (Test-Path $dst)) { throw }
        $bak = $dst + ".old-" + (Get-Date -Format "yyyyMMddHHmmss")
        Move-Item -Path $dst -Destination $bak -Force
        Write-Host ("  ! target locked by a running app, renamed old -> " + (Split-Path -Leaf $bak)) -ForegroundColor Yellow
        Copy-Item $src $dst -Force
    }
}

# 1) binaries shipped in the deployment folder (same list as package_release.ps1)
$artifacts = @("SlimeFinder.exe", "slime_bedrock_biome_query.exe", "slime_tests.exe", "slime_ocl_probe.exe",
               "slime_kernel_tests.exe")
$copied = 0
foreach ($f in $artifacts) {
    $src = Join-Path $buildDir $f
    if (-not (Test-Path $src)) {
        Write-Warning ("skip missing artifact: " + $f)
        continue
    }
    $dst = Join-Path $DeployDir $f
    Copy-WithReplace $src $dst
    $srcHash = (Get-FileHash $src -Algorithm SHA256).Hash
    $dstHash = (Get-FileHash $dst -Algorithm SHA256).Hash
    if ($srcHash -ne $dstHash) {
        throw ("copy verify failed: " + $f)
    }
    Write-Host ("  + {0,-30} {1,12:N0} B  SHA256={2}" -f $f, (Get-Item $src).Length, $srcHash)
    ++$copied
}

# 2) language packs + inline help (they live next to the exe at runtime)
$assetPairs = @(@("assets\i18n", "assets\i18n"), @("assets\help", "assets\help"))
foreach ($pair in $assetPairs) {
    $srcDir = Join-Path $Repo $pair[0]
    if (-not (Test-Path $srcDir)) { continue }
    $dstDir = Join-Path $DeployDir $pair[1]
    New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
    Copy-Item (Join-Path $srcDir "*") $dstDir -Force -Recurse
    Write-Host ("  + " + $pair[1])
}

# 3) build_date marker used by the in-app update reminder (offline, day precision)
$buildDate = (Get-Date -Format "yyyy-MM-dd")
Set-Content -Path (Join-Path $DeployDir "build_date.txt") -Value $buildDate -Encoding ASCII
Write-Host ("  + build_date.txt = " + $buildDate)

# 4) report (do NOT touch config*.json / cache / logs / crashes)
$vi = (Get-Item (Join-Path $DeployDir "SlimeFinder.exe")).VersionInfo
Write-Host ""
Write-Host ("deployed exe : " + (Join-Path $DeployDir "SlimeFinder.exe"))
Write-Host ("FileVersion  : " + $vi.FileVersion)
Write-Host ("ProductName  : " + $vi.ProductName)
Write-Host ("binaries     : " + $copied)
$running = Get-Process SlimeFinder -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "NOTE: SlimeFinder is running now; close and relaunch it to use the new build." -ForegroundColor Yellow
}
Write-Host "SYNC OK"
exit 0
