# check.ps1 - 分档验证（v3.6.15）
#
#   -Level fast      : 增量构建 + 单元测试
#   -Level standard  : + 内核 + 111 参考 + GUI 自检(关 GPU)
#   -Level full      : + GUI 自检(开 GPU) + 打包(-SkipSelfTest) + 安装包
#   -Level release   : + verify_release(干净重建) + 审计
#
#   -Only map|table  : 只跑 GUI 自检的某个区域（省时间，用于改界面局部）
#   -NoGui           : 跳过 GUI 自检（纯算法/文档改动）
#
# 例：
#   powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Level fast
#   powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Level standard -Only map
#   powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Level full -Version 3.6.16
param(
    [ValidateSet("fast", "standard", "full", "release")]
    [string]$Level = "standard",
    [string]$Version = "",
    [string]$LogDir = "",
    [string]$Only = "",
    [switch]$NoGui
)

$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $repo
$build = Join-Path $repo "build"
if (-not $LogDir) { $LogDir = Join-Path $build ("check_" + (Get-Date -Format "yyyyMMdd_HHmmss")) }
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
if ($Only) { $env:SLIME_SELFTEST_ONLY = $Only } else { Remove-Item Env:SLIME_SELFTEST_ONLY -ErrorAction SilentlyContinue }

$timings = [ordered]@{}

function Step([string]$name, [scriptblock]$body) {
    $t0 = Get-Date
    Write-Host ("== {0} ==" -f $name) -ForegroundColor Cyan
    & $body | Out-Host
    $sec = [int]((Get-Date) - $t0).TotalSeconds
    Write-Host ("   [{0}] {1}s" -f $name, $sec) -ForegroundColor DarkGray
    return $sec
}

# ---- 构建 ----
$timings["build"] = Step "build (incremental)" {
    & cmd.exe /c "scripts\build\build_repo.cmd" *> (Join-Path $LogDir "build.log")
    $errs = Select-String -Path (Join-Path $LogDir "build.log") -Pattern "error C|error LNK" -ErrorAction SilentlyContinue
    if ($errs) { Write-Host $errs; throw "build failed" }
}

# ---- 单元测试 ----
$timings["unit"] = Step "unit tests" {
    & (Join-Path $build "slime_tests.exe") --unit *> (Join-Path $LogDir "unit.log")
    if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $LogDir "unit.log") | Select-Object -Last 15; throw "unit tests failed" }
    Get-Content (Join-Path $LogDir "unit.log") | Select-String "单元测试"
}

if ($Level -ne "fast") {
    $timings["kernel"] = Step "SIMD kernel" {
        & (Join-Path $build "slime_kernel_tests.exe") *> (Join-Path $LogDir "kernel.log")
        if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $LogDir "kernel.log") | Select-Object -Last 15; throw "kernel failed" }
        Get-Content (Join-Path $LogDir "kernel.log") | Select-String "RESULT"
    }
    $timings["reference"] = Step "reference 111" {
        & py -3 tests\verify_against_reference.py *> (Join-Path $LogDir "reference.log")
        if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $LogDir "reference.log") | Select-Object -Last 10; throw "reference failed" }
        Get-Content (Join-Path $LogDir "reference.log") | Select-String "汇总"
    }

    if (-not $NoGui) {
        $timings["gui-off"] = Step "GUI selftest (gpu off)" {
            Get-Process SlimeFinder -ErrorAction SilentlyContinue | Stop-Process -Force
            & (Join-Path $PSScriptRoot "gui_selftest.ps1") -LogDir $LogDir *> (Join-Path $LogDir "gui_off.log")
            $errLog = Join-Path $LogDir "gui_selftest_gpu_off_stderr.log"
            if (Test-Path $errLog) { Get-Content $errLog | Select-String "FAIL|全部通过" | Select-Object -First 3 }
            if ($LASTEXITCODE -ne 0) { throw "gui selftest (off) failed" }
        }
    }
}

if ($Level -eq "full" -or $Level -eq "release") {
    if (-not $NoGui) {
        $timings["gui-on"] = Step "GUI selftest (gpu on)" {
            & (Join-Path $PSScriptRoot "gui_selftest.ps1") -LogDir $LogDir -GpuOn *> (Join-Path $LogDir "gui_on.log")
            if ($LASTEXITCODE -ne 0) { throw "gui selftest (on) failed" }
        }
    }
    if ($Version) {
        $timings["package"] = Step "package" {
            & (Join-Path $PSScriptRoot "..\release\package_release.ps1") -Version $Version -SkipSelfTest *> (Join-Path $LogDir "package.log")
            Get-Content (Join-Path $LogDir "package.log") | Select-String "size=|PACKAGE"
            & (Join-Path $PSScriptRoot "..\release\build_installer.ps1") -Version $Version *> (Join-Path $LogDir "installer.log")
            Get-Content (Join-Path $LogDir "installer.log") | Select-String "installer:|size"
        }
    }
}

if ($Level -eq "release") {
    $timings["verify"] = Step "verify_release (clean rebuild)" {
        & (Join-Path $PSScriptRoot "verify_release.ps1") -Zip ("dist\SlimeFinder-" + $Version + "-win64.zip") -SkipBench *> (Join-Path $LogDir "verify.log")
        Get-Content (Join-Path $LogDir "verify.log") | Select-String "total=|FAIL"
    }
    $timings["audit"] = Step "audit" {
        & py -3 scripts\audit_corruption.py *> (Join-Path $LogDir "audit.log")
        Get-Content (Join-Path $LogDir "audit.log") | Select-Object -Last 3
    }
}

Write-Host ""
Write-Host "== summary ==" -ForegroundColor Green
$total = 0
foreach ($k in $timings.Keys) { Write-Host ("   {0,-10} {1,5}s" -f $k, $timings[$k]); $total += $timings[$k] }
Write-Host ("   total      {0,5}s" -f $total)
Write-Host ("logs: {0}" -f $LogDir)
