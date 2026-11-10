# run_all_tests.ps1 - 全量回归：单元/内核/参考对拍/GUI/性能基线/探针/审计
#   用法: .\scripts\run_all_tests.ps1 [-SkipGui] [-LogDir build\regress]
param(
    [switch]$SkipGui,
    [string]$LogDir = "build\regress"
)
$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $repo
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $LogDir "regress-$stamp.log"
$failed = New-Object System.Collections.Generic.List[string]

function Step([string]$name, [scriptblock]$body) {
    Write-Host "== $name =="
    $global:LASTEXITCODE = 0
    try { & $body } catch { Write-Host "  [异常] $_" }
    if ($global:LASTEXITCODE -ne 0) { $failed.Add($name) ; Write-Host "  [FAIL] $name (exit $global:LASTEXITCODE)" }
}

# 1) 单元测试
Step "单元测试" { & .\build\slime_tests.exe --unit 2>&1 | Tee-Object -Append $log | Out-Null; $LASTEXITCODE = $LASTEXITCODE }
if ($LASTEXITCODE -ne 0) { }

# 2) 内核测试
Step "内核测试" { & .\build\slime_kernel_tests.exe 2>&1 | Tee-Object -Append $log | Out-Null }

# 3) 参考对拍
Step "参考对拍 117 项" { py -3 tests\verify_against_reference.py 2>&1 | Tee-Object -Append $log | Out-Null }

# 4) 性能基线
Step "性能基线" { py -3 scripts\perf_baseline.py --check 2>&1 | Tee-Object -Append $log | Out-Null }

# 5) 探针（CPU 一致性/健壮性）
$probes = @(
    @{ exe = "opt_probe33.exe"; desc = "随机 A/B 120 例" },
    @{ exe = "opt_probe34.exe"; desc = "大区域分块路径" },
    @{ exe = "opt_probe36.exe"; desc = "退化参数/取消" },
    @{ exe = "opt_probe37.exe"; desc = "算法一致性" },
    @{ exe = "opt_probe38.exe"; desc = "缓存一致性" },
    @{ exe = "opt_probe39.exe"; desc = "线程/非法参数" },
    @{ exe = "opt_probe41.exe"; desc = "缓存+早停/压力" }
)
foreach ($p in $probes) {
    $exe = Join-Path "build" $p.exe
    if (Test-Path $exe) {
        Step ("探针 " + $p.desc) { & $exe 2>&1 | Tee-Object -Append $log | Out-Null }
    }
}

# 6) GPU 探针（无 GPU 时自行跳过）
foreach ($p in @("opt_probe35.exe", "opt_probe40.exe", "opt_probe30.exe", "opt_probe28.exe")) {
    $exe = Join-Path "build" $p
    if (Test-Path $exe) {
        Step ("GPU 探针 " + $p) {
            $env:SLIME_GPU_PATTERN = "1"
            if ($p -like "opt_probe30*") { & $exe 4096 4096 -2048 -2048 2>&1 | Tee-Object -Append $log | Out-Null }
            else { & $exe 2>&1 | Tee-Object -Append $log | Out-Null }
        }
    }
}

# 7) 审计
Step "完整性审计" { py -3 scripts\audit_corruption.py 2>&1 | Tee-Object -Append $log | Out-Null }

# 8) GUI 自检
if (-not $SkipGui) {
    Step "GUI 自检（GPU 开）" {
        Get-Process SlimeFinder -ErrorAction SilentlyContinue | Stop-Process -Force
        & .\scripts\qa\gui_selftest.ps1 -LogDir (Join-Path $LogDir "gui-gpu") -GpuOn 2>&1 | Tee-Object -Append $log | Out-Null
    }
}

Write-Host ""
if ($failed.Count -eq 0) {
    Write-Host "全部回归通过 ✔（日志 $log）"
    exit 0
} else {
    Write-Host ("以下步骤失败：" + ($failed -join ", "))
    exit 1
}
