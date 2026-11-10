# SlimeFinder 匿名统计服务端部署

## 1. 依赖

- Node.js / npm
- Cloudflare 账号（免费套餐即可，D1/Worker 有免费额度）
- 安装 Wrangler：

```bat
npm install -g wrangler
wrangler login
```

## 2. 创建 D1 与表

在 `SlimeFinder/telemetry/` 目录：

```bat
copy wrangler.toml.example wrangler.toml
npx wrangler d1 create slimefinder-telemetry
```

把返回的 `database_id` 填进 `wrangler.toml`，然后：

```bat
npx wrangler d1 execute slimefinder-telemetry --remote --file schema.sql
```

## 3. 配置可选查询 token

```bat
npx wrangler secret put STATS_TOKEN
```

（不设置也可以，只是不能访问 `/v1/stats`。）

## 4. 部署

```bat
npx wrangler deploy
```

会得到类似：

```text
https://slimefinder-telemetry.<账户>.workers.dev
```

客户端事件地址为：

```text
https://slimefinder-telemetry.<账户>.workers.dev/v1/event
```

## 5. 客户端配置

把 URL 填到程序目录的 `telemetry.json`：

```json
{
  "enabled": false,
  "endpoint": "https://slimefinder-telemetry.<账户>.workers.dev/v1/event"
}
```

然后在程序里：
帮助 → 匿名统计（帮助改进）→ 勾选“匿名发送使用统计”。

也可以把默认 endpoint 写进发布版源码后再发版。

## 6. 查询

```bat
curl "https://slimefinder-telemetry.<账户>.workers.dev/v1/stats?token=<STATS_TOKEN>"
```

## 7. 隐私

- 不保存 IP；
- 不保存存档路径、日志内容、账号/持久 ID；
- 接收（用户主动开启后）：版本、平台、CPU/GPU/内存、游戏平台、世界种子、搜索设置、结果摘要/坐标、耗时速度、取消/早停/GPU 标记；
- 客户端默认关闭，必须用户主动勾选。
