# Collect release assets for one version into release\<version>\ for GitHub Releases upload.
# Example:
#   powershell -ExecutionPolicy Bypass -File scripts\collect_release_assets.ps1 -Version 3.10.12.8
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$DistDir = "dist",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (-not $OutDir) { $OutDir = Join-Path "release" $Version }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

$zip = Join-Path $DistDir ("SlimeFinder-" + $Version + "-win64.zip")
$setup = Join-Path $DistDir ("SlimeFinder-" + $Version + "-Setup.exe")
$sums = Join-Path $DistDir ("SHA256SUMS-" + $Version + ".txt")

$wanted = @()
if (Test-Path $zip) { $wanted += $zip } else { Write-Warning ("missing " + $zip) }
if (Test-Path $setup) { $wanted += $setup } else { Write-Warning ("missing " + $setup + " (ok if this version has no installer)") }
if (Test-Path $sums) { $wanted += $sums } else { Write-Warning ("missing " + $sums) }

if ($wanted.Count -eq 0) { throw ("no release assets found for " + $Version) }

foreach ($f in $wanted) {
    $name = Split-Path -Leaf $f
    Copy-Item -Force $f (Join-Path $OutDir $name)
    $bytes = (Get-Item $f).Length
    $hash = (Get-FileHash $f -Algorithm SHA256).Hash
    Write-Host ('  + {0}  {1:N0} B  SHA256={2}' -f $name, $bytes, $hash)
}

Write-Host ''
Write-Host ('assets collected: ' + $OutDir)
Write-Host 'upload examples (after gh auth login):'
Write-Host ('  gh release create v' + $Version + ' "release\' + $Version + '\*" --title "SlimeFinder v' + $Version + '" --notes-file <release-notes.md>')
Write-Host ('  gh release upload v' + $Version + ' "release\' + $Version + '\*" --clobber')
