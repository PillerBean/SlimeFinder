# deploy_telemetry.ps1 — 使用 Cloudflare API Token 非交互部署匿名统计 Worker。
#
# 用法（在你自己的终端里，不要发 Token 到聊天）：
#   $env:CF_TOKEN = "你的 API Token"
#   $env:CF_ACCOUNT = "你的 Account ID"
#   powershell -ExecutionPolicy Bypass -File telemetry\deploy_telemetry.ps1
#
# Token 权限（最小）：
#   Account -> Workers Scripts -> Edit
#   Account -> D1 -> Edit
# Account ID 在 Cloudflare 控制台 URL 里：dash.cloudflare.com/<account_id>/...
param(
    [string]$Token = $env:CF_TOKEN,
    [string]$AccountId = $env:CF_ACCOUNT,
    [string]$DatabaseName = "slimefinder-telemetry",
    [string]$WorkerName = "slimefinder-telemetry",
    [string]$Schema = ""
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($Schema)) { $Schema = Join-Path $here "schema.sql" }
if ([string]::IsNullOrWhiteSpace($Token) -or [string]::IsNullOrWhiteSpace($AccountId)) {
    throw "请先设置 CF_TOKEN / CF_ACCOUNT（环境变量），或在参数中传入 -Token -AccountId"
}

$headers = @{ Authorization = "Bearer $Token"; "Content-Type" = "application/json" }
$api = "https://api.cloudflare.com/client/v4"

function Invoke-CF($method, $path, $body = $null) {
    $p = @{ Method = $method; Uri = "$api$path"; Headers = $headers }
    if ($null -ne $body) { $p.Body = ($body | ConvertTo-Json -Depth 8) }
    $r = Invoke-RestMethod @p
    if (-not $r.success) { throw ("Cloudflare API 失败: " + ($r.errors | ConvertTo-Json -Compress)) }
    return $r.result
}

Write-Host "== 查询 D1 ==" -ForegroundColor Cyan
$dbs = Invoke-CF "GET" "/accounts/$AccountId/d1/database"
$db = $dbs | Where-Object { $_.name -eq $DatabaseName } | Select-Object -First 1
if (-not $db) {
    Write-Host "创建 D1: $DatabaseName"
    $db = Invoke-CF "POST" "/accounts/$AccountId/d1/database" @{ name = $DatabaseName }
}
$dbId = $db.uuid
if ([string]::IsNullOrWhiteSpace($dbId)) { throw "没有拿到 D1 database_id" }
Write-Host ("D1: " + $DatabaseName + " = " + $dbId) -ForegroundColor Green

$toml = Join-Path $here "wrangler.toml"
if (Test-Path $toml) { Copy-Item $toml "$toml.bak" -Force }
$base = Join-Path $here "wrangler.toml.example"
$text = Get-Content $base -Raw
$text = $text -replace 'database_id = "REPLACE_WITH_D1_DATABASE_ID"', ('database_id = "' + $dbId + '"')
$text = $text -replace 'name = "slimefinder-telemetry"', ('name = "' + $WorkerName + '"')
[System.IO.File]::WriteAllText($toml, $text, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "== 建表 ==" -ForegroundColor Cyan
$env:CLOUDFLARE_API_TOKEN = $Token
$env:CLOUDFLARE_ACCOUNT_ID = $AccountId
Push-Location $here
try {
    npx --yes wrangler@latest d1 execute $DatabaseName --remote --file $Schema --config $toml
    if ($LASTEXITCODE -ne 0) { throw "D1 schema 执行失败" }
    Write-Host "== 部署 Worker ==" -ForegroundColor Cyan
    $out = npx --yes wrangler@latest deploy --config $toml 2>&1 | Tee-Object -Variable deployOut
    $deployOut | Write-Host
    $m = [regex]::Match(($deployOut -join "`n"), 'https://[A-Za-z0-9.-]+\.workers\.dev')
    if ($m.Success) {
        $url = $m.Value + "/v1/event"
        Write-Host ("EVENT_URL=" + $url) -ForegroundColor Green
        [System.IO.File]::WriteAllText((Join-Path $here "telemetry_url.txt"), $url + "`n", (New-Object System.Text.UTF8Encoding($false)))
    } else {
        Write-Host "部署完成，但未从输出解析到 workers.dev URL；请查看上面的 Wrangler 输出。" -ForegroundColor Yellow
    }
} finally {
    Pop-Location
}
