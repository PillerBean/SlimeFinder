# gui_selftest.ps1 -- run the GUI self-test with explicit QPA/font settings (ASCII only).
# Moved here from the workspace root in the v3.3.8 tidy-up; paths derive from this file's location
# (scripts\ -> repo root -> build\).  Parameters carry non-ASCII paths; the script file itself stays
# ASCII because Windows PowerShell 5.1 reads .ps1 as ANSI unless a BOM is present.
param(
    [string]$Shot = "",
    [string]$FontDir = "",
    [string]$Platform = "offscreen",
    [switch]$GpuOn,
    [string]$LogDir = ""
)
$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$exe  = Join-Path $repo "build\SlimeFinder.exe"
$cfg  = Join-Path $repo "build\config.json"
if (-not $LogDir) { $LogDir = Join-Path $repo "build\selftest_logs" }
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$env:PATH = "C:\Qt\6.11.2\msvc2022_64\bin;$env:PATH"
$env:QT_QPA_PLATFORM = $Platform
if ($FontDir) { $env:QT_QPA_FONTDIR = $FontDir } else { Remove-Item Env:QT_QPA_FONTDIR -ErrorAction SilentlyContinue }
$env:SLIME_SELFTEST = "1"
if ($Shot) { $env:SLIME_SHOT = $Shot } else { Remove-Item Env:SLIME_SHOT -ErrorAction SilentlyContinue }

if (Test-Path $cfg) {
    # v3.4.12: round-trip through UTF-8 (no BOM) explicitly. Windows PowerShell 5.1 reads
    # Get-Content as ANSI (GBK here) and Set-Content -Encoding UTF8 adds a BOM, which turned
    # non-ASCII device names into mojibake and made config.json unparsable (audit finding).
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $c = [System.IO.File]::ReadAllText($cfg, $utf8)
    $c = $c -replace '"enabled":\s*(true|false)', ('"enabled": ' + $(if ($GpuOn) { "true" } else { "false" }))
    [System.IO.File]::WriteAllText($cfg, $c, $utf8)
}

$tag = if ($GpuOn) { "on" } else { "off" }
$out = Join-Path $LogDir "gui_selftest_gpu_$tag.log"
$err = Join-Path $LogDir "gui_selftest_gpu_${tag}_stderr.log"
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $exe -Wait -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err
$sw.Stop()
Write-Host ("gui selftest gpu={0} platform={1} fontdir='{2}' -> exit={3}  ({4:N1}s)" -f $tag, $Platform, $FontDir, $p.ExitCode, $sw.Elapsed.TotalSeconds)
Write-Host ("  stdout: {0}" -f $out)
Write-Host ("  stderr: {0}" -f $err)
exit $p.ExitCode
