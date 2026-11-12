# Publish docs/wiki/*.md to the GitHub Wiki.
# Prerequisite: the Wiki must have at least one page (created once via the web UI);
# GitHub has no API to create the first wiki page.
param(
    [string]$WikiUrl = "https://github.com/PillerBean/SlimeFinder.wiki.git",
    [string]$Message = "docs: sync wiki pages from repo"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$src = Join-Path $repo "docs\wiki"
$tmp = Join-Path $env:TEMP "slimefinder_wiki_publish"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
git clone $WikiUrl $tmp
if ($LASTEXITCODE -ne 0) {
    Write-Host "Wiki repo not found."
    Write-Host "Open https://github.com/PillerBean/SlimeFinder/wiki , create the first page (title: Home), then rerun this script."
    exit 1
}
Copy-Item (Join-Path $src "*.md") $tmp -Force
Push-Location $tmp
git config user.name "PillerBean"
git config user.email "320208917+PillerBean@users.noreply.github.com"
git add -A
git commit -m $Message
if ($LASTEXITCODE -ne 0) {
    Write-Host "No wiki changes to publish."
    Pop-Location
    exit 0
}
git push
Pop-Location
Write-Host "Wiki published: https://github.com/PillerBean/SlimeFinder/wiki"
