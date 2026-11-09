# verify_release.ps1 - SlimeFinder release verification harness (v3.3.8.1)
#
# Runs the full delivery gate on a frozen source tree and prints a summary table:
#   1) clean full rebuild (exit code + error-line count checked)
#   2) unit tests          : build\slime_tests.exe --unit
#   3) reference comparison: py -3 tests\verify_against_reference.py (111 items)
#   4) OpenCL probe        : build\slime_ocl_probe.exe
#   5) standalone SIMD kernel self-test : build\slime_kernel_tests.exe
#      (includes the embedded-OpenCL-kernel byte-exactness check, section D)
#   6) GUI self-test       : SLIME_SELFTEST=1, config gpu.enabled = false and true
#   7) benchmark           : build\slime_tests.exe --bench (+ v3.2.1 baseline when present)
#   8) dist self-test + hash consistency (build vs dist vs extracted zip)
#
# Note (v3.3.1): the GPU density-match kernel was paused by delivery instruction and is
# parked in src/gpu/experimental/ (not built); there is no GPU density self-test target.
#
# Usage (from the SlimeFinder directory, or pass -Repo):
#   pwsh -File scripts\verify_release.ps1 -Zip dist\SlimeFinder-3.10.12.10-win64.zip
#   powershell -ExecutionPolicy Bypass -File scripts\verify_release.ps1 -SkipClean -SkipBench
#
# Notes:
#   * SlimeFinder.exe is a GUI-subsystem binary: it must be started through
#     Start-Process -Wait to obtain a real exit code.
#   * Requires MSVC (vcvars64.bat), Qt 6.11.2 msvc2022_64, CMake+Ninja, Python 3 (py -3).
#   * Never deletes build/CMakeCache.txt or other build-critical files.
#   * This file is intentionally ASCII-only so it parses identically under
#     Windows PowerShell 5.1 (ANSI default) and PowerShell 7 (UTF-8 default).

param(
    [string]$Repo = "",
    [string]$Zip = "",
    [string]$Vcvars = "D:\Visual Studio\Visual Studio\VC\Auxiliary\Build\vcvars64.bat",
    [string]$QtDir = "C:\Qt\6.11.2\msvc2022_64",
    [string]$Cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe",
    [string]$Reference = "",
    [switch]$SkipClean,
    [switch]$SkipBench
)

$ErrorActionPreference = "Continue"
$script:results = @()

# ---------------------------------------------------------------- encoding
# Diagnostic output is UTF-8 end to end: the GUI, the Qt-free test binaries and
# the Python comparison script all emit UTF-8 bytes.  Windows PowerShell 5.1
# otherwise decodes a native command's stdout with the ANSI code page, which
# garbled the captured evidence logs (found by the v3.3.5 corruption audit:
# reference111.log had 503 U+FFFD and the stderr captures 449 each).  Pin the
# decoding/encoding for both directions, and force Python into UTF-8 mode.
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }
try { $OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"

function Add-Result([string]$name, [bool]$ok, [string]$detail) {
    $script:results += [pscustomobject]@{ Step = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = $detail }
    $color = if ($ok) { "Green" } else { "Red" }
    Write-Host ("[{0}] {1} - {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail) -ForegroundColor $color
}

# Locate the reference implementation (SlimeChecker.exe) by searching the repo parent
# (its folder name contains non-ASCII characters, so no literal path is hard-coded here).
function Find-Reference([string]$repo) {
    if (-not [string]::IsNullOrWhiteSpace($Reference)) { return $Reference }
    $parent = Split-Path -Parent $repo
    $hit = Get-ChildItem -Path $parent -Filter "SlimeChecker.exe" -Recurse -File -ErrorAction SilentlyContinue |
           Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return ""
}

if ([string]::IsNullOrWhiteSpace($Repo)) {
    $Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
Set-Location $Repo
Write-Host "== SlimeFinder release verification ==" -ForegroundColor Cyan
Write-Host "repo: $Repo"

$logDir = Join-Path $env:TEMP ("slime_verify_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
Write-Host "logs: $logDir"

# ---------------------------------------------------------------- 1) clean rebuild
$buildArgs = @("--build", "build")
if (-not $SkipClean) { $buildArgs += "--clean-first" }
$buildCmd = Join-Path $env:TEMP "slime_verify_build.cmd"
@"
@echo off
call "$Vcvars" >nul 2>&1
if errorlevel 1 exit /b 90
cd /d "$Repo"
"$Cmake" $($buildArgs -join ' ')
exit /b %errorlevel%
"@ | Set-Content -Path $buildCmd -Encoding ASCII
$buildLog = Join-Path $logDir "clean_build.log"
& cmd /c "`"$buildCmd`" > `"$buildLog`" 2>&1"
$buildExit = $LASTEXITCODE
$errLines = (Select-String -Path $buildLog -Pattern ": error|FAILED:" -ErrorAction SilentlyContinue | Measure-Object).Count
Add-Result "clean rebuild" ($buildExit -eq 0 -and $errLines -eq 0) "exit=$buildExit, errorLines=$errLines, log=$buildLog"

# ---------------------------------------------------------------- 2) unit tests
$unitLog = Join-Path $logDir "unit.log"
& (Join-Path $Repo "build\slime_tests.exe") --unit *> $unitLog
$unitExit = $LASTEXITCODE
$unitTail = (Get-Content $unitLog -Encoding UTF8 -Tail 1)
Add-Result "unit tests" ($unitExit -eq 0) "exit=$unitExit, $unitTail"

# ---------------------------------------------------------------- 3) 111 reference comparison
$refPath = Find-Reference $Repo
if (-not [string]::IsNullOrWhiteSpace($refPath) -and (Test-Path $refPath)) {
    $refLog = Join-Path $logDir "reference111.log"
    & py -3 (Join-Path $Repo "tests\verify_against_reference.py") $refPath (Join-Path $Repo "build\slime_tests.exe") *> $refLog
    $refExit = $LASTEXITCODE
    Add-Result "reference 111" ($refExit -eq 0) "exit=$refExit, ref=$refPath, log=$refLog"
} else {
    Add-Result "reference 111" $false "SlimeChecker.exe not found (searched repo parent; use -Reference <path>)"
}

# ---------------------------------------------------------------- 4) OpenCL probe
$probeLog = Join-Path $logDir "ocl_probe.log"
& (Join-Path $Repo "build\slime_ocl_probe.exe") *> $probeLog
$probeExit = $LASTEXITCODE
$probeTail = (Get-Content $probeLog -Encoding UTF8 -Tail 1)
$probeText = Get-Content $probeLog -Raw -Encoding UTF8
# The probe exits non-zero when it cannot initialise an OpenCL session (no device /
# kernel build failure), so both the exit code and an explicit PASS line are required.
$probeContentOk = ($probeText -match "PASS")
Add-Result "ocl probe" (($probeExit -eq 0) -and $probeContentOk) "exit=$probeExit, $probeTail"

# ---------------------------------------------------------------- 5) SIMD kernel self-test
$kernExe = Join-Path $Repo "build\slime_kernel_tests.exe"
if (Test-Path $kernExe) {
    $kernLog = Join-Path $logDir "kernel_selftest.log"
    $env:SLIME_SELFTEST_QUICK = "1"
    & $kernExe --correct-only *> $kernLog
    $kernExit = $LASTEXITCODE
    Remove-Item Env:SLIME_SELFTEST_QUICK -ErrorAction SilentlyContinue
    # Require BOTH the exit code and an explicit "ALL PASS" line: a truncated or
    # silently-skipping run must not count as a pass.
    $kernText = Get-Content $kernLog -Raw -Encoding UTF8
    $kernOk = ($kernExit -eq 0) -and ($kernText -match "ALL PASS")
    $kernTail = (Get-Content $kernLog -Encoding UTF8 | Select-String -Pattern "ALL PASS|FAIL" | Select-Object -Last 1)
    Add-Result "SIMD kernel self-test" $kernOk "exit=$kernExit, $($kernTail.Line)"
}

# ---------------------------------------------------------------- 6) GUI self-test (gpu off / on)
$env:PATH = "$QtDir\bin;$env:PATH"
$env:QT_QPA_PLATFORM = "offscreen"
# v3.3.7.2（F-1）：offscreen 平台必须先有字体目录，否则所有文字渲染成 □（截图不可读）。
# 程序内部已自动补该变量（src/main.cpp），这里显式设置一次作为双保险与证据。
$env:QT_QPA_FONTDIR = Join-Path $env:WINDIR "Fonts"
$env:SLIME_SELFTEST = "1"
$cfgPath = Join-Path $Repo "build\config.json"
foreach ($gpuOn in @($false, $true)) {
    if (Test-Path $cfgPath) {
        # v3.4.12: UTF-8 (no BOM) round trip - see gui_selftest.ps1 for the rationale.
        $utf8 = New-Object System.Text.UTF8Encoding($false)
        $cfg = [System.IO.File]::ReadAllText($cfgPath, $utf8)
        $cfg = $cfg -replace '"enabled":\s*(true|false)', ('"enabled": ' + $(if ($gpuOn) { "true" } else { "false" }))
        [System.IO.File]::WriteAllText($cfgPath, $cfg, $utf8)
    }
    $tag = if ($gpuOn) { "on" } else { "off" }
    $out = Join-Path $logDir "gui_selftest_gpu_$tag.log"
    $err = Join-Path $logDir "gui_selftest_gpu_${tag}_stderr.log"
    $p = Start-Process -FilePath (Join-Path $Repo "build\SlimeFinder.exe") -Wait -NoNewWindow -PassThru `
                       -RedirectStandardOutput $out -RedirectStandardError $err
    Add-Result "gui selftest (gpu=$tag)" ($p.ExitCode -eq 0) "exit=$($p.ExitCode), log=$out"
}

# ---------------------------------------------------------------- 7) benchmark
if (-not $SkipBench) {
    $benchLog = Join-Path $logDir "bench.log"
    & (Join-Path $Repo "build\slime_tests.exe") --bench *> $benchLog
    $benchExit = $LASTEXITCODE
    Add-Result "benchmark" ($benchExit -eq 0) "exit=$benchExit, log=$benchLog"
    $baseline = Join-Path $Repo "bench_v321\slime_tests_v321.exe"
    if (Test-Path $baseline) {
        $baseLog = Join-Path $logDir "bench_v321_baseline.log"
        & $baseline --bench *> $baseLog
        Add-Result "benchmark (v3.2.1 baseline)" ($LASTEXITCODE -eq 0) "log=$baseLog"
    }
}

# ---------------------------------------------------------------- 8) dist self-test + hashes
$distExe = Join-Path $Repo "dist\SlimeFinder\SlimeFinder.exe"
if (Test-Path $distExe) {
    $out = Join-Path $logDir "dist_selftest.log"
    $err = Join-Path $logDir "dist_selftest_stderr.log"
    $p = Start-Process -FilePath $distExe -Wait -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    Add-Result "dist self-test" ($p.ExitCode -eq 0) "exit=$($p.ExitCode), log=$out"

    $hBuild = (Get-FileHash (Join-Path $Repo "build\SlimeFinder.exe") -Algorithm SHA256).Hash
    $hDist = (Get-FileHash $distExe -Algorithm SHA256).Hash
    Add-Result "hash build==dist" ($hBuild -eq $hDist) "$hBuild"
    foreach ($f in @("slime_tests.exe", "slime_ocl_probe.exe", "slime_kernel_tests.exe")) {
        $b = Join-Path $Repo "build\$f"
        $d = Join-Path $Repo "dist\SlimeFinder\$f"
        if ((Test-Path $b) -and (Test-Path $d)) {
            $hb = (Get-FileHash $b -Algorithm SHA256).Hash
            $hd = (Get-FileHash $d -Algorithm SHA256).Hash
            Add-Result "hash $f build==dist" ($hb -eq $hd) $hb
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($Zip) -and (Test-Path $Zip)) {
        $ex = Join-Path $env:TEMP ("slime_zip_check_" + [guid]::NewGuid().ToString("N").Substring(0, 8))
        Expand-Archive -Path $Zip -DestinationPath $ex -Force
        $hZip = (Get-FileHash (Join-Path $ex "SlimeFinder.exe") -Algorithm SHA256).Hash
        Add-Result "hash zip==dist" ($hZip -eq $hDist) "$hZip"
        $out = Join-Path $logDir "zip_selftest.log"
        $err = Join-Path $logDir "zip_selftest_stderr.log"
        $p = Start-Process -FilePath (Join-Path $ex "SlimeFinder.exe") -Wait -NoNewWindow -PassThru `
                           -RedirectStandardOutput $out -RedirectStandardError $err
        Add-Result "zip self-test" ($p.ExitCode -eq 0) "exit=$($p.ExitCode)"
        Remove-Item $ex -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    Add-Result "dist self-test" $false "dist exe missing: $distExe"
}

# ---------------------------------------------------------------- summary
Write-Host ""
Write-Host "== summary ==" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failed = ($script:results | Where-Object { $_.Result -eq "FAIL" } | Measure-Object).Count
Write-Host ("total={0} pass={1} fail={2}" -f $script:results.Count, ($script:results.Count - $failed), $failed) `
    -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })
Write-Host "logs kept in: $logDir"
exit $(if ($failed -eq 0) { 0 } else { 1 })
