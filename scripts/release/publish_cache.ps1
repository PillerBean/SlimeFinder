param(
    [string]$Version = "3.12.3",
    [string]$CacheRoot = "cache",
    [string]$OutDir = "dist/cache-v3.12.3",
    [string]$BaseUrl = "",
    [string]$FallbackUrl = "",
    [string]$ManualFallbackUrl = "",
    [string]$R2Bucket = "",
    [string]$R2Prefix = "",
    [switch]$Upload
)

# 生成 manifest；默认只落盘，不联网。
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $Repo
$manifest = Join-Path $OutDir "manifest.json"
py -3 scripts/release/build_cache_manifest.py `
    --cache-root $CacheRoot `
    --out $manifest `
    --base-url $BaseUrl `
    --fallback-url $FallbackUrl `
    --manual-fallback-url $ManualFallbackUrl `
    --app-version ("v" + $Version)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $Upload) {
    Write-Host "manifest written: $manifest"
    Write-Host "upload skipped (pass -Upload and R2Bucket/R2Prefix to upload)"
    exit 0
}

if (-not $R2Bucket) { throw "R2Bucket is required for -Upload" }
$wrangler = Get-Command wrangler -ErrorAction SilentlyContinue
if (-not $wrangler) { throw "wrangler not found in PATH" }

# 上传 pack（只读对象），key = prefix + 相对路径。
Get-ChildItem (Join-Path $CacheRoot "local"), (Join-Path $CacheRoot "downloaded") -Filter *.slmz -ErrorAction SilentlyContinue |
    ForEach-Object {
        $rel = $_.FullName.Substring((Resolve-Path $CacheRoot).Path.Length + 1).Replace('\', '/')
        $key = ($R2Prefix.TrimEnd('/') + "/" + $rel).TrimStart('/')
        Write-Host "upload $key"
        & wrangler r2 object put "$R2Bucket/$key" --file $_.FullName --remote
        if ($LASTEXITCODE -ne 0) { throw "upload failed: $key" }
    }

$manifestKey = ($R2Prefix.TrimEnd('/') + "/manifest.json").TrimStart('/')
Write-Host "upload $manifestKey"
& wrangler r2 object put "$R2Bucket/$manifestKey" --file $manifest --content-type application/json --remote
if ($LASTEXITCODE -ne 0) { throw "manifest upload failed" }
Write-Host "upload complete"
