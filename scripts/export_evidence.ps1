# export_evidence.ps1 - convert a verify_release.ps1 log directory into the delivered
# evidence files under docs\ as UTF-8 (with BOM), and report any remaining mojibake.
#
# Why: verify_release.ps1 captures logs with PowerShell redirection, which writes
# UTF-16LE, while the captured bytes come from UTF-8 producers; a raw copy into
# docs\ therefore either kept the UTF-16 encoding or -- when the producer emitted
# ANSI/GBK -- produced U+FFFD replacement characters (the v3.3.5 corruption audit
# found 503 of them in the reference-comparison evidence).  This script decodes
# each log honouring its BOM and rewrites it as UTF-8, then counts U+FFFD.
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts\export_evidence.ps1 `
#            -LogDir "%TEMP%\slime_verify_20260920_204221" -Tag v335
#
# This file is intentionally ASCII-only so it parses identically under Windows
# PowerShell 5.1 (ANSI default) and PowerShell 7 (UTF-8 default).

param(
    [Parameter(Mandatory = $true)][string]$LogDir,
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Repo = "",
    [string]$OutDir = "",
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Repo)) {
    $Repo = Split-Path -Parent $PSScriptRoot          # ...\SlimeFinder
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $Repo "docs"
}
if (-not (Test-Path $LogDir)) { throw "log directory not found: $LogDir" }
if (-not (Test-Path $OutDir)) { throw "output directory not found: $OutDir" }

$map = [ordered]@{
    "unit.log"                      = "unit_test_$Tag.txt"
    "reference111.log"              = "verify111_$Tag.txt"
    "ocl_probe.log"                 = "ocl_probe_$Tag.txt"
    "kernel_selftest.log"           = "kernel_selftest_$Tag.txt"
    "bench.log"                     = "bench_$Tag.txt"
    "gui_selftest_gpu_off.log"      = "gui_selftest_${Tag}_gpu_off.txt"
    "gui_selftest_gpu_on.log"       = "gui_selftest_${Tag}_gpu_on.txt"
    "gui_selftest_gpu_on_stderr.log" = "gui_selftest_${Tag}_gpu_on_stderr.txt"
    "dist_selftest.log"             = "pkgverify_selftest_$Tag.txt"
}

function Read-LogText([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        return [System.Text.Encoding]::Unicode.GetString($bytes, 2, $bytes.Length - 2)
    }
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        return [System.Text.Encoding]::BigEndianUnicode.GetString($bytes, 2, $bytes.Length - 2)
    }
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        return [System.Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3)
    }
    return [System.Text.Encoding]::UTF8.GetString($bytes)
}

$utf8Bom = New-Object System.Text.UTF8Encoding($true)
$rows = @()
$problems = 0

foreach ($src in $map.Keys) {
    $srcPath = Join-Path $LogDir $src
    if (-not (Test-Path $srcPath)) {
        $rows += [pscustomobject]@{ Log = $src; Evidence = $map[$src]; Chars = 0; Bad = 0; Note = "missing (skipped)" }
        continue
    }
    $text = Read-LogText $srcPath
    $bad = ([regex]::Matches($text, [string][char]0xFFFD)).Count
    $nul = ([regex]::Matches($text, [string][char]0x0000)).Count
    $destPath = Join-Path $OutDir $map[$src]
    [System.IO.File]::WriteAllText($destPath, $text, $utf8Bom)
    $note = "ok"
    if ($bad -gt 0 -or $nul -gt 0) {
        $note = "MOJIBAKE (U+FFFD=$bad, NUL=$nul)"
        $problems++
    }
    $rows += [pscustomobject]@{ Log = $src; Evidence = $map[$src]; Chars = $text.Length; Bad = $bad; Note = $note }
}

if (-not $Quiet) {
    Write-Host "== evidence export ==" -ForegroundColor Cyan
    Write-Host ("log dir : {0}" -f $LogDir)
    Write-Host ("out dir : {0}" -f $OutDir)
    $rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
    if ($problems -eq 0) {
        Write-Host "all evidence files are valid UTF-8 (no U+FFFD, no NUL)" -ForegroundColor Green
    } else {
        Write-Host ("{0} evidence file(s) still contain replacement characters" -f $problems) -ForegroundColor Red
    }
}

if ($problems -gt 0) { exit 1 }
exit 0
