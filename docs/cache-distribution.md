# 官方缓存分发（v3.12.3 前置）

## 主源：Cloudflare R2 + Workers

```text
slimefinder-cache/
├─ v3.12.3/
│  ├─ manifest.json
│  ├─ local/...
│  └─ downloaded/...
└─ latest.json
```

- bucket 只读公开；
- 客户端只有 GET；
- 上传只在构建机执行；
- pack 不可变，按 URL 长期缓存；
- 全图压缩缓存约 4.3 GB，R2 免费存储 10 GB、出网免费。

## 备用源

环境变量：

```text
SLIME_CACHE_MANIFEST=https://cache.example.com/v3.12.3/manifest.json
SLIME_CACHE_MANIFEST_FALLBACK=https://国内网盘占位/
```

备用源先预留地址；主源失败时由客户端回退。

## 生成/上传

```powershell
# 只生成 manifest，不联网
powershell -File scripts\release\publish_cache.ps1 `
  -Version 3.12.3 `
  -BaseUrl "https://cache.example.com/v3.12.3" `
  -FallbackUrl "https://国内网盘占位/"

# 上传 R2（需要 wrangler 登录，且 bucket 写权限）
powershell -File scripts\release\publish_cache.ps1 `
  -Version 3.12.3 `
  -BaseUrl "https://cache.example.com/v3.12.3" `
  -R2Bucket "slimefinder-cache" `
  -R2Prefix "v3.12.3" `
  -Upload
```

## 客户端

- 先取 manifest；
- 按 pack 选择下载；
- SHA256 校验；
- 写入 `cache/downloaded/`；
- 只读、可整包删除；
- 主源失败且备用源非空时尝试备用源。

当前 v3.12.3 仍以本地缓存为主，联网下载为前置接入，未配置官方地址。

## 客户端配置（无需改环境变量）

在程序目录 `config.json` 中填写：

```json
{
  "cacheDownload": {
    "primary": "https://<R2 自定义域名>/v3.12.3/manifest.json",
    "fallback": "https://<国内网盘占位>/manifest.json"
  }
}
```

环境变量优先级高于 `config.json`：

```text
SLIME_CACHE_MANIFEST
SLIME_CACHE_MANIFEST_FALLBACK
```
