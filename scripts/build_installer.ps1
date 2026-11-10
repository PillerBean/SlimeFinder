# build_installer.ps1 - build the v3.5.0 NSIS installer from dist\SlimeFinder (ASCII only!)
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1
#
# Checks before compiling:
#   * NSIS (makensis.exe) is present (PATH, Program Files, or -NsisDir)
#   * the payload directory exists and carries the expected executable
#   * no runtime leftovers (cache / config / logs / crashes) are about to be packed
#     (a multi-GB cache file must never end up inside the installer)
param(
    [string]$Repo = "",
    [string]$NsisDir = "",
    [string]$Version = "3.11.0"
)
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Repo)) {
    $Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$payload = Join-Path $Repo "dist\SlimeFinder"
$nsi = Join-Path $Repo "scripts\install.nsi"
$out = Join-Path $Repo ("dist\SlimeFinder-{0}-Setup.exe" -f $Version)

if (-not (Test-Path (Join-Path $payload "SlimeFinder.exe"))) {
    throw "payload missing: run scripts\package_release.ps1 first ($payload)"
}
$leftovers = Get-ChildItem $payload -Recurse -File -Include "*.slimecache", "*.cache", "*.legacy", "*.log", "*.dmp" -ErrorAction SilentlyContinue
if ($leftovers) {
    Write-Host "refusing to pack runtime leftovers:" -ForegroundColor Red
    $leftovers | ForEach-Object { Write-Host ("  " + $_.FullName) }
    throw "clean dist (run package_release.ps1) before building the installer"
}

$makensis = $null
$cands = @()
if (-not [string]::IsNullOrWhiteSpace($NsisDir)) { $cands += (Join-Path $NsisDir "makensis.exe") }
$cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
if ($cmd) { $cands += $cmd.Source }
$cands += "C:\Program Files (x86)\NSIS\makensis.exe"
$cands += "C:\Program Files\NSIS\makensis.exe"
foreach ($c in $cands) {
    if ($c -and (Test-Path $c)) { $makensis = $c; break }
}
if (-not $makensis) { throw "makensis.exe not found (install NSIS or pass -NsisDir)" }
Write-Host ("NSIS: " + $makensis)

Remove-Item $out -Force -ErrorAction SilentlyContinue
& $makensis /V2 $nsi
if ($LASTEXITCODE -ne 0) { throw ("makensis failed with exit " + $LASTEXITCODE) }
if (-not (Test-Path $out)) { throw "installer not produced: $out" }

$f = Get-Item $out
$hash = (Get-FileHash $out -Algorithm SHA256).Hash
Write-Host ""
Write-Host ("installer: " + $f.FullName)
Write-Host ("size     : {0:N0} bytes" -f $f.Length)
Write-Host ("sha256   : " + $hash)
