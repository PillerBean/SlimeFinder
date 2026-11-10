# build_setup_release.ps1 - v3.10.12.11：只产出 Setup 安装包 + Setup 校验文件。
# 用法: powershell -ExecutionPolicy Bypass -File scripts\build_setup_release.ps1 -Version 3.10.12.11
param(
    [string]$Version = "3.10.12.11",
    [switch]$SkipSelfTest
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
Set-Location $Repo

Write-Host "== 1/3 package payload (internal zip is removed afterwards) ==" -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "package_release.ps1") -Version $Version -SkipSelfTest:$SkipSelfTest
if ($LASTEXITCODE -ne 0) { throw "package_release.ps1 failed" }

Write-Host "== 2/3 build NSIS installer (payload without config.json: keep existing settings on update) ==" -ForegroundColor Cyan
$cfg = Join-Path $Repo "dist\SlimeFinder\config.json"
if (Test-Path $cfg) { Remove-Item $cfg -Force }
& (Join-Path $PSScriptRoot "build_installer.ps1") -Version $Version
if ($LASTEXITCODE -ne 0) { throw "build_installer.ps1 failed" }

$setup = Join-Path $Repo ("dist\SlimeFinder-{0}-Setup.exe" -f $Version)
if (-not (Test-Path $setup)) { throw "missing installer: $setup" }
$zip = Join-Path $Repo ("dist\SlimeFinder-{0}-win64.zip" -f $Version)
if (Test-Path $zip) { Remove-Item $zip -Force }

$hash = (Get-FileHash $setup -Algorithm SHA256).Hash
$size = (Get-Item $setup).Length

Write-Host "== 3/3 write Setup checksums ==" -ForegroundColor Cyan
$lines = @()
$lines += "# SlimeFinder v$Version - Setup checksum"
$lines += "# packed {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
$lines += ("SHA256  {0} = {1}" -f (Split-Path -Leaf $setup), $hash)
$lines += ("SETUP SIZE {0} bytes" -f $size)
$sumPath = Join-Path $Repo ("dist\SHA256SUMS-{0}.txt" -f $Version)
[System.IO.File]::WriteAllLines($sumPath, [string[]]$lines, (New-Object System.Text.UTF8Encoding($false)))

$vi = (Get-Item $setup).VersionInfo
Write-Host ("installer: " + $setup)
Write-Host ("size     : {0:N0} bytes" -f $size)
Write-Host ("sha256   : " + $hash)
Write-Host ("FileVersion={0} ProductVersion={1}" -f $vi.FileVersion, $vi.ProductVersion)
Write-Host "PACKAGE OK (Setup only)" -ForegroundColor Green
exit 0
