# 官方缓存分发（v3.12.3）

## 主源：Cloudflare R2 + Pages 网关

```text
https://slimefinder-cache.pages.dev/v3.12.3/
├─ manifest.json
└─ local/pack-*.slmz
```

- bucket：`slimefinderchunkcache`；
- **bucket 不公开**，`r2.dev` 返回 401；
- 客户端只通过 Pages Worker 使用 GET/HEAD；
- Worker 路径白名单，无目录列表，无上传/删除接口；
- 上传只在构建机执行；
- pack 不可变，按 URL 长期缓存。

当前线上 manifest：

```text
totalPacks = 1024
pack 覆盖 = 8192 × 8192 区块
压缩后总计约 4.6 GB
```

本地基准：

```text
dist/r2-8192/manifest.json
dist/netdisk-8192/
```

## 生成与上传

R2 上传脚本：

```powershell
py -3 scripts/release/upload_r2_8192.py `
  --root dist/netdisk-8192/all `
  --manifest dist/r2-8192/manifest.json
```

凭据只放在本机：

```text
C:\Users\<用户>\.slimefinder\r2.env
```

权限只应包含 `slimefinderchunkcache` 的 Object Read & Write。

## 客户端

- 首次打开缓存页**不自动请求清单**；
- 点击「获取清单」后才请求；
- manifest 在网络上流式解析，逐条显示；
- 按 pack 下载并校验 SHA256；
- 写入 `cache/downloaded/`；
- 缓存只读，可整包删除；
- 不再提供「附近」快捷项；
- 官方源失败时保留网盘手动入口。

## 国内网盘人工备用

当前只提供三个范围：

```text
局部：https://wwben.lanzoul.com/b01d7e5jyf
较大：https://wwben.lanzoul.com/b01d7e5k0h
全部：https://wwben.lanzoul.com/b01d7e5kah
```

说明：

- 这是人工备用入口，不是 manifest JSON 直链；
- 客户端不扫描网盘，也不跟踪浏览器下载；
- 用户下载 zip 后，在缓存页打开导入窗口；
- 导入会校验 zip 路径、大小、manifest、pack SHA256，再合并到本地缓存。

## 网盘分卷

v3.12.3 当前 69 个 zip：

```text
局部：1 个 zip，4 packs
较大：4 个 zip，每卷 16 packs，共 64 packs
全部：64 个 zip，每卷 16 packs，共 1024 packs
```

目录：

```text
dist/netdisk-batches-8192/
├─ 基岩版区块缓存-局部-1.zip
├─ 基岩版区块缓存-较大-1.zip ... -4.zip
├─ 基岩版区块缓存-全部-1.zip ... -64.zip
├─ SHA256SUMS.txt
└─ upload-manifest.json
```

每份 zip 内含：

```text
manifest.json
local/*.slmz
README.txt
```

`全部` 每卷约 74 MB，低于网盘 100 MB 限制；`局部` 和 `较大` 独立完整覆盖，不依赖「全部」。

## 变更记录

- v3.12.3：从 1024×1024 旧 pack 切换为 8192×8192 pack；
- 取消「附近」分卷和入口；
- R2 线上只保留 manifest 引用的 1024 个 pack；
- 客户端清单改为手动获取、流式加载。
