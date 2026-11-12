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

## 国内网盘人工备用

当前备用文件夹：

```text
https://wwben.lanzoul.com/b01d7e4lre
```

- 这是人工备用入口，不是 manifest JSON；
- 客户端通过“国内网盘备用”按钮打开；
- 需要手动下载文件后放入 `cache/downloaded/`；
- 后续如果网盘目录提供可直链 manifest，再接入自动回退。

## 国内网盘上传规则

网盘只接受部分后缀（zip/rar/7z 等），不要直接上传 `.slmz`。

当前样例包：

```text
dist/SlimeFinder-Cache-v3.12.3-sample-64packs.zip
```

内容：

```text
manifest.json
local/pack-*.slmz  (64 个 pack，覆盖 X/Z -4096..4095)
README.txt
```

上传到网盘文件夹后，用户下载 zip、解压，把 `local` 合并到程序目录的 `cache/local`；
或先放入 `cache/downloaded` 让程序重建索引。

R2 不受后缀限制，仍可直接上传 `.slmz` 与 manifest.json。

## 网盘分批命名

规则：

```text
基岩版区块缓存-<范围>-<序列号>.zip
```

当前样例：

```text
基岩版区块缓存-附近-1.zip      4 packs
基岩版区块缓存-局部-1.zip     12 packs
基岩版区块缓存-较大-1.zip     16 packs
基岩版区块缓存-较大-2.zip     16 packs
基岩版区块缓存-较大-3.zip     16 packs
```

每份内含：

```text
manifest.json
local/*.slmz
README.txt
```

单文件远低于网盘 100 MB 限制；后续更大的全部缓存按 16 个 pack/批继续递增序列号。

重新生成命令：

```powershell
py -3 scripts/release/package_netdisk_batches.py `
  --manifest cache-official/manifest.json `
  --source-root cache-official `
  --out-dir dist/netdisk-batches
```

## v3.12.3 当前网盘分卷（已确认方案）

- 每个范围独立完整覆盖；
- `附近`：±2,048 区块，16 packs，`基岩版区块缓存-附近-1.zip`
- `局部`：±8,192 区块，256 packs，`基岩版区块缓存-局部-1.zip`
- `较大` / `全部` 后续按同样规则递增序列号；
- 本地加载时自动扫描 `local/downloaded`，按覆盖矩形去重，重复范围不会产生重复 index 记录。

清除旧临时样例后，目前只保留这两份。
