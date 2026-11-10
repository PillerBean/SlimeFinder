# package_release.ps1 - build the SlimeFinder Windows distribution folder and zip (v3.3.8.1)
#
# Steps:
#   1) copy build artifacts (exe + tools) into dist\SlimeFinder
#   2) refresh bundled docs (docs\*.md, docs\*.txt, docs\screenshots) and README.md
#   3) remove runtime leftovers from the dist folder (logs, crashes, caches, config)
#   4) compress dist\SlimeFinder\* -> dist\SlimeFinder-<version>-win64.zip
#   5) verify the package: build-vs-dist-vs-zip SHA256 equality, version string in the
#      exe, in-package GUI self-test and an in-package unit-test subset
#   6) write dist\SHA256SUMS-<version>.txt
#
# Usage (from the SlimeFinder directory, or pass -Repo):
#   pwsh -File scripts\package_release.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -SkipSelfTest
#
# Notes:
#   * Requires a completed Release build (build\SlimeFinder.exe etc.) and Qt's bin
#     directory on PATH for the in-package self-test.
#   * SlimeFinder.exe is a GUI-subsystem binary: always start it via Start-Process -Wait.
#   * This file is intentionally ASCII-only (parses identically under Windows
#     PowerShell 5.1 and PowerShell 7, independent of the console code page).

param(
    [string]$Repo = "",
    [string]$Version = "3.11.1",
    [string]$QtDir = "C:\Qt\6.11.2\msvc2022_64",
    [switch]$SkipSelfTest
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Repo)) {
    $Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
Set-Location $Repo

$distDir = Join-Path $Repo "dist\SlimeFinder"
$zipPath = Join-Path $Repo ("dist\SlimeFinder-{0}-win64.zip" -f $Version)
$binDir = Join-Path $Repo "build"
$artifacts = @("SlimeFinder.exe", "slime_tests.exe", "slime_ocl_probe.exe", "slime_kernel_tests.exe")

Write-Host "== SlimeFinder packaging (v$Version) ==" -ForegroundColor Cyan
Write-Host "repo: $Repo"

if (-not (Test-Path $distDir)) { New-Item -ItemType Directory -Force -Path $distDir | Out-Null }

# ---------------------------------------------------------------- 1) binaries
Write-Host "== 1) copy build artifacts ==" -ForegroundColor Cyan
$hashes = @{}
foreach ($f in $artifacts) {
    $src = Join-Path $binDir $f
    if (-not (Test-Path $src)) { throw "missing build artifact: $src (run a full build first)" }
    Copy-Item $src (Join-Path $distDir $f) -Force
    $hashes[$f] = (Get-FileHash $src -Algorithm SHA256).Hash
    Write-Host ("  + {0}" -f $f)
}

# v3.5.12：必须带上 offscreen/minimal 平台插件。windeployqt 有时只部署 qwindows，
# 缺了它们无头自检（QT_QPA_PLATFORM=offscreen）会卡在一个不可见的错误框上把打包挂死。
foreach ($pl in @("qoffscreen.dll", "qminimal.dll")) {
    $srcPl = Join-Path $QtDir ("plugins\platforms\" + $pl)
    if (Test-Path $srcPl) {
        $platDir = Join-Path $distDir "platforms"
        if (-not (Test-Path $platDir)) { New-Item -ItemType Directory -Path $platDir | Out-Null }
        Copy-Item $srcPl (Join-Path $platDir $pl) -Force
    }
}

# ---------------------------------------------------------------- 2) docs
# v3.11.1：程序内更新历史已移除，更新日志只到 GitHub 查看，安装包不再附带 docs/。
Write-Host "== 2) refresh bundled docs ==" -ForegroundColor Cyan
$distDocs = Join-Path $distDir "docs"
if (Test-Path $distDocs) { Remove-Item $distDocs -Recurse -Force -ErrorAction SilentlyContinue }

# ---------------------------------------------------------------- 2b) build_date.txt
# v3.3.4 (task 3.1): release date used by the in-app update reminder (local, offline).
# ISO 8601, day precision; the app falls back to its compiled-in default if missing/invalid.
$buildDate = (Get-Date -Format "yyyy-MM-dd")
Set-Content -Path (Join-Path $distDir "build_date.txt") -Value $buildDate -Encoding ASCII
Write-Host ("  + build_date.txt = {0}" -f $buildDate)

# ---------------------------------------------------------------- 3) cleanup
Write-Host "== 3) clean runtime leftovers ==" -ForegroundColor Cyan
foreach ($p in @("logs", "crashes", "cache")) {
    $d = Join-Path $distDir $p
    if (Test-Path $d) {
        Get-ChildItem $d -Recurse -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
        Write-Host ("  cleaned {0}\" -f $p)
    }
}
foreach ($pat in @("*.slimecache", "*.cache")) {
    Get-ChildItem $distDir -Filter $pat -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
}
# v3.3.8.1: the one-time legacy-cache migration marker is a runtime leftover too -- a fresh
# install must run the migration check itself instead of inheriting this machine's marker.
Get-ChildItem $distDir -Filter "slime_cache.migrated*" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
Get-ChildItem $distDir -Recurse -File -Include "*.log", "*.dmp" -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
# ship a clean default config (GPU off, exact algorithm, current version field).
# Always rewritten on purpose: dist\ is a build output, so the package must not carry
# settings left over from a previous run (task 4.1 of the v3.3.2/v3.3.3 books).
$cfg = Join-Path $distDir "config.json"
$cfgText = @"
{
  "version": "v$Version",
  "settings": {
    "language": "zh"
  },
  "cache": {
    "maxSizeMB": 1024
  },
  "gpu": {
    "enabled": false
  },
  "algorithm": {
    "mode": "exact",
    "layerBlock": 16,
    "layerRatio": 1,
    "sampleStep": 8,
    "sampleExact": false
  }
}
"@
# v3.4.12: UTF-8 without BOM (Set-Content -Encoding UTF8 adds one under Windows PowerShell 5.1)
[System.IO.File]::WriteAllText($cfg, $cfgText, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "  + config.json (defaults, version field)"
# v3.11.1：安装器可选择安装语言，并把它写成程序首次启动语言
foreach ($lang in @("zh", "zh-TW", "en")) {
    $langText = $cfgText -replace '"language": "zh"', ('"language": "' + $lang + '"')
    $langCfg = Join-Path $distDir ("config." + $lang + ".json")
    [System.IO.File]::WriteAllText($langCfg, $langText, (New-Object System.Text.UTF8Encoding($false)))
}

# ---------------------------------------------------------------- 3b) languages
Write-Host "== 3b) copy language pack ==" -ForegroundColor Cyan
$distAssets = Join-Path $distDir "assets\i18n"
New-Item -ItemType Directory -Force -Path $distAssets | Out-Null
Copy-Item (Join-Path $Repo "assets\i18n\*.json") $distAssets -Force
Write-Host ("  + assets/i18n: {0} files" -f (Get-ChildItem $distAssets -File).Count)

# ---------------------------------------------------------------- 4) zip
Write-Host "== 4) compress ==" -ForegroundColor Cyan
Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $distDir "*") -DestinationPath $zipPath -CompressionLevel Optimal
$zipSize = (Get-Item $zipPath).Length
$zipHash = (Get-FileHash $zipPath -Algorithm SHA256).Hash
Write-Host ("  {0}" -f $zipPath)
Write-Host ("  size={0:N0} B  sha256={1}" -f $zipSize, $zipHash)

# ---------------------------------------------------------------- 5) verify package
Write-Host "== 5) verify package ==" -ForegroundColor Cyan
$tmp = Join-Path $env:TEMP ("slime_pkg_check_" + [guid]::NewGuid().ToString("N").Substring(0, 8))
Expand-Archive -Path $zipPath -DestinationPath $tmp -Force
$fail = 0
foreach ($f in $artifacts) {
    $hz = (Get-FileHash (Join-Path $tmp $f) -Algorithm SHA256).Hash
    $ok = ($hz -eq $hashes[$f])
    if (-not $ok) { $fail++ }
    Write-Host ("  {0,-24} build==zip: {1}" -f $f, $ok) -ForegroundColor $(if ($ok) { "Green" } else { "Red" })
}

# version string embedded in the exe (ASCII scan of the PE image)
$exeAscii = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes((Join-Path $tmp "SlimeFinder.exe")))
$verOk = $exeAscii.Contains("v$Version")
if (-not $verOk) { $fail++ }
Write-Host ("  version string v{0} present: {1}" -f $Version, $verOk) -ForegroundColor $(if ($verOk) { "Green" } else { "Red" })

# v3.3.3 (task 2.1): the PE VERSIONINFO must expose the file/product version too
$vi = (Get-Item (Join-Path $tmp "SlimeFinder.exe")).VersionInfo
$viOk = ($vi.FileVersion -like "$Version*") -and ($vi.ProductVersion -like "$Version*")
if (-not $viOk) { $fail++ }
Write-Host ("  VERSIONINFO FileVersion={0} ProductVersion={1} (expect {2}.x)" -f $vi.FileVersion, $vi.ProductVersion, $Version) `
    -ForegroundColor $(if ($viOk) { "Green" } else { "Red" })

if (-not $SkipSelfTest) {
    $env:PATH = "$QtDir\bin;$env:PATH"
    $env:QT_QPA_PLATFORM = "offscreen"
    $env:SLIME_SELFTEST = "1"
    $out = Join-Path $env:TEMP "slime_pkg_gui_selftest.log"
    $err = Join-Path $env:TEMP "slime_pkg_gui_selftest_err.log"
    $p = Start-Process -FilePath (Join-Path $tmp "SlimeFinder.exe") -NoNewWindow -PassThru `
                       -RedirectStandardOutput $out -RedirectStandardError $err
    # v3.5.12：以前用 -Wait 且无超时——自检偶发卡住会把整个打包挂死 20 分钟以上。现在最多等 4 分钟。
    if (-not $p.WaitForExit(240000)) {
        try { $p.Kill() } catch {}
        $fail++
        Write-Host "  in-package GUI self-test TIMEOUT (>240s), killed" -ForegroundColor Red
    } else {
        $p.Refresh()
        $code = 1
        try { $code = [int]$p.ExitCode } catch { $code = 1 }
        if ($code -ne 0) { $fail++ }
        Write-Host ("  in-package GUI self-test exit={0}" -f $code) -ForegroundColor $(if ($code -eq 0) { "Green" } else { "Red" })
    }

    & (Join-Path $tmp "slime_tests.exe") --unit 15 *> (Join-Path $env:TEMP "slime_pkg_unit.log")
    $u = $LASTEXITCODE
    if ($u -ne 0) { $fail++ }
    Write-Host ("  in-package unit test (group 15) exit={0}" -f $u) -ForegroundColor $(if ($u -eq 0) { "Green" } else { "Red" })

    & (Join-Path $tmp "slime_kernel_tests.exe") --correct-only *> (Join-Path $env:TEMP "slime_pkg_kernel.log")
    $k = $LASTEXITCODE
    if ($k -ne 0) { $fail++ }
    Write-Host ("  in-package kernel self-test exit={0}" -f $k) -ForegroundColor $(if ($k -eq 0) { "Green" } else { "Red" })
}
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue

# ---------------------------------------------------------------- 6) checksums
Write-Host "== 6) write SHA256SUMS ==" -ForegroundColor Cyan
$lines = @()
$lines += "# SlimeFinder v$Version - SHA256 checksums"
$lines += "# packed {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
foreach ($k in ($hashes.Keys | Sort-Object)) {
    $lines += ("SHA256  {0,-24} = {1}" -f $k, $hashes[$k])
}
$lines += ("SHA256  SlimeFinder-{0}-win64.zip = {1}" -f $Version, $zipHash)
$lines += ("ZIP SIZE {0} bytes" -f $zipSize)
$sumPath = Join-Path $Repo ("dist\SHA256SUMS-{0}.txt" -f $Version)
# v3.4.12: UTF-8 without BOM (a BOM breaks strict checkers such as `sha256sum -c`)
[System.IO.File]::WriteAllLines($sumPath, [string[]]$lines, (New-Object System.Text.UTF8Encoding($false)))
Get-Content $sumPath | ForEach-Object { Write-Host "  $_" }

if ($fail -ne 0) {
    Write-Host ("PACKAGE FAILED: {0} check(s) failed" -f $fail) -ForegroundColor Red
    exit 1
}
Write-Host "PACKAGE OK" -ForegroundColor Green
exit 0
