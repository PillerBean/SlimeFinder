# SlimeFinder 匿名统计服务端

已部署可直连的端点：

```text
https://slimefinder-telemetry.pages.dev/v1/event
```

- Cloudflare Pages 项目：`slimefinder-telemetry`
- D1 数据库：`slimefinder-telemetry`，绑定名 `DB`
- Worker 代码：`worker.js`（Pages 通过 `pages/_worker.js` 使用同一份）
- 客户端默认关闭；用户在「帮助 → 匿名统计」主动开启后才发送

## 目录

```text
telemetry/
├─ worker.js                 # 事件接收实现
├─ schema.sql                # D1 建表
├─ pages/
│  ├─ _worker.js             # Pages 高级模式入口（worker.js 副本）
│  ├─ index.html
│  └─ wrangler.toml          # Pages + D1 绑定
├─ wrangler.toml.example     # Worker 部署模板（workers.dev，国内可能不可达）
├─ deploy_telemetry.ps1      # API Token 非交互部署脚本（Worker）
└─ README.md
```

## 重新部署 Pages 函数

```bat
cd telemetry\\pages
npx --yes wrangler@latest pages deploy . --project-name slimefinder-telemetry --branch main
```

如果修改了 `telemetry/worker.js`，先同步到 Pages 入口：

```bat
copy /Y telemetry\\worker.js telemetry\\pages\\_worker.js
```

## 更新 D1 表结构

```bat
npx --yes wrangler@latest d1 execute slimefinder-telemetry --remote --file schema.sql
```

## 验证

```bat
curl https://slimefinder-telemetry.pages.dev/v1/ping
curl -X POST https://slimefinder-telemetry.pages.dev/v1/event -H "Content-Type: application/json" --data "{\"schema\":1,\"event\":\"app_start\"}"
```

查询最近事件：

```bat
npx --yes wrangler@latest d1 execute slimefinder-telemetry --remote --command "SELECT id,event,seed,result_count,best_value FROM telemetry ORDER BY id DESC LIMIT 10"
```

## 为什么不用 workers.dev

`https://<worker>.workers.dev` 在当前网络（中国大陆联通/电信）会连接超时或被重置；  
`https://slimefinder-telemetry.pages.dev` 可正常访问，因此客户端默认端点使用 Pages。

## 客户端配置

程序默认端点已写入：

```text
https://slimefinder-telemetry.pages.dev/v1/event
```

也可以在程序目录 `telemetry.json` 覆盖：

```json
{"enabled": false, "endpoint": "https://slimefinder-telemetry.pages.dev/v1/event"}
```

默认 `enabled=false`，不会上报；在程序「帮助 → 匿名统计」勾选后才发送。

## 隐私

- 不保存 IP；
- 不保存存档路径、日志内容、账号/持久 ID；
- 接收（用户主动开启后）：版本、平台、CPU/GPU/内存、游戏平台、世界种子、搜索设置、结果摘要/坐标、耗时速度、取消/早停/GPU 标记；
- 客户端默认关闭，必须用户主动勾选。

## 备选：API Token 非交互部署

如果需要在其他账号重新部署，可创建 API Token：

- Account → Workers Scripts → Edit
- Account → D1 → Edit
- 记录 Account ID

然后运行（不要公开 Token）：

```bat
set CF_TOKEN=你的Token
set CF_ACCOUNT=你的Account ID
powershell -ExecutionPolicy Bypass -File telemetry\\deploy_telemetry.ps1
```

脚本会创建/复用 D1、执行 schema、部署 Worker；Worker 域名若不可达，按上面的 Pages 方式重新发布。
