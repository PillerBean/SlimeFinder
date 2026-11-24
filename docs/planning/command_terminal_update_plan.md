# 命令终端更新规划

日期：2026-11-24  
状态：规划，未实施  
目标版本：3.14.x（终端重构）

## 1. 现状盘点

当前命令表 27 条：

```text
about  build  cache  cancel  center  clear  copy  density  gpu  grid
help   lock   log    mode    open    preset range  result   search
seed   smart  status stop    target  theme  threads version
```

对应实现全部存在，没有“点开就报未实现”的幽灵命令。  
问题主要是：

```text
顶层命令过多
同义命令重复
mode 同时承担搜索模式和算法
stop 与 cancel 语义容易混淆
设置类命令散落在顶层
只有一级 Tab 补全
没有格式提示总线
```

## 2. 总体目标

```text
1. 按真实功能重建命令树，而不是继续加顶层命令
2. 设置类统一走 setting 分支
3. 删除重复/歧义入口；旧名保留一个版本的静默别名
4. 所有命令可查询、可提示、可补全
5. Tab 补全与格式提示彻底分离
```

## 3. 目标命令树

### 3.1 顶层

```text
help      [path]
version
about
status    [detail|json]
search    start|cancel|estimate|status
build     on|off|start|status
cache     status|list|import <path>|clear|open|build
result    list [n]|copy <n>|copy_all|export <path>|sort <distance|density>
preset    range <near|local|global|world|bedrock>
          result <balanced|fast|precision>
setting   <path> [value]
page      search|build|cache|settings|history|controls
open      logs|cache|crash
map       open|close|fit
pattern   clear|size <n>|rotate
afk       open
clear
```

说明：

```text
help/version/about/status 保留
search 统一搜索生命周期，不再让顶层 search 既当命令又当动作
build/cache 保留，但 build 只负责构建模式，cache 负责缓存数据
result 统一结果读取/复制/导出
setting 接管全部设置
page 接管页面跳转
```

### 3.2 命名约定

```text
命令名/子命令：小写单词，空格分层
  search start
  result copy

设置键：小写下划线
  search_mode
  result_stop
  ring_scan

不使用点号（search.mode ❌）
不使用连字符（result-stop ❌）
CLI 选项仍用 --flag（如 --yes），不属于设置键命名
```

内部注册表用 token 列表保存路径，不保存 `a.b` 字符串。

### 3.3 setting 分支

语法：

```text
setting <path> [value]
```

`path` 统一使用小写下划线命名，不再使用点号；命令层级仍用空格分隔：

```text
setting theme dark
setting search_mode density
setting result_stop on
setting ring_scan k3
```

完整路径：

```text
theme                    light|dark|system
language                 zh|zh-TW|en
search_mode              strict|density
search_algorithm         exact|layered|sample|brute
search_grid              1..512
search_density           0.0..1.0
search_ignore_threshold  on|off
search_lock_direction    on|off
result_target            1..上限
result_stop              on|off
result_smart             on|off
ring_inner               0..64
ring_outer               16..1024
ring_scan                fast|square|k3|exact
ring_sim_distance        区块数
cache_enabled            on|off
gpu_enabled              on|off
gpu_stream               on|off
gpu_self_optimize        on|off
gpu_limit                1..100
threads                  0..逻辑核心
log_mode                 off|brief|detailed
log_startup              on|off
display_rate_unit        auto|wan|yi
sound_enabled            on|off
sound_volume             0..100
window_remember          on|off
```

查询形式：

```text
setting                    # 列出分组
setting search             # 列出 search_* 前缀键
setting theme              # 当前值 + 可选值
setting theme dark         # 设置并立即生效
```

## 4. 命令合并与迁移

### 4.1 合并

| 旧命令 | 新位置 | 处理 |
|---|---|---|
| `theme` | `setting theme` | 保留旧名一个版本，提示弃用 |
| `log` | `setting log_mode` | 同上 |
| `stop` | `setting result_stop` | 避免与 `cancel` 混淆 |
| `smart` | `setting result_smart` | 旧名隐藏 |
| `lock` | `setting search_lock_direction` | 旧名隐藏 |
| `grid` | `setting search_grid` | 旧名隐藏 |
| `density` | `setting search_density` | 旧名隐藏 |
| `threads` | `setting threads` | 旧名隐藏 |
| `target` | `setting result_target` | 旧名隐藏 |
| `gpu` | `setting gpu_enabled` | 保留只读 `gpu` 查询？建议统一 |
| `mode` | `setting search_mode` / `setting search_algorithm` | 拆分，旧名只做迁移提示 |
| `copy` | `result copy` | 删除顶层重复入口 |
| `seed` | `search seed` | 保留短别名 |
| `range` | `search range` | 保留短别名 |
| `center` | `search center` | 保留短别名 |
| `preset` | `preset range` | 旧 `preset near` 保留别名 |
| `open` | `open` | 保留 |

### 4.2 删除策略

不直接爆破旧脚本：

```text
3.14.0：旧名可用，但 help 不展示，输出“已迁移到 …”
3.14.x：日志记录弃用次数
3.15.0：移除旧名
```

只删除**没有真实功能对应**或**完全重复**的入口：

```text
顶层 copy（与 result copy 重复）
mode 的 strict/density 与 exact/layered 混用语义
```

真实功能仍在的旧命令不做静默删除，只做迁移。

## 5. 格式提示设计

### 5.1 触发场景

```text
未知命令：set
缺少参数：setting theme
非法值：setting theme wat
缺分隔：settingtheme
路径不完整：setting search
```

### 5.2 输出示例

```text
> set
没有 set 命令。最接近的用法：
  setting <path> <value>
  setting theme <light|dark|system>
示例：
  setting theme dark
  setting search_mode density

> setting theme
当前值：dark
用法：setting theme <light|dark|system>

> setting search_mode density
已设置 search_mode = density
```

### 5.3 与 Tab 的关系

```text
Tab：只负责补全/切换候选，不写提示
提示：只读，不修改输入行，不抢焦点
```

具体：

- 格式提示放在输入行下方的独立 `QLabel`（`Qt::NoFocus`），或输出区；
- 不复用 Tab 候选 `QListWidget`；
- `textEdited` 后 250ms 防抖生成提示；
- 按 Tab 立即取消待显示提示；
- Tab 的候选循环、焦点保持、不写入等现有行为保持不变；
- Enter 执行失败时，再把完整提示写入输出区。

### 5.4 分支提示

```text
> setting se
候选：setting search_mode
      setting search_algorithm
提示：setting search_<key> <value>（如 setting search_mode density）
```

补全支持多级路径：

```text
setting <Tab>
setting search_<Tab>
setting search_mode <Tab>
```

## 6. 功能增强

### 6.1 查询

```text
status detail
status json
setting search
result list 20
cache list
```

### 6.2 结果

```text
result copy 3
result copy_all
result export results.csv
result sort density
```

### 6.3 搜索/耗时

```text
search estimate
search start
search cancel
search status
```

`search start` 仍走正常预计时间确认；命令行不绕过安全提示。

### 6.4 缓存与安全

```text
cache clear --yes
cache import <path> --yes
cache list
cache open
```

危险操作必须二次确认；`--yes` 仅用于明确脚本化调用，不默认。

### 6.5 页面/地图/图案

```text
page settings
map fit
pattern clear
afk open
```

## 7. 实现结构

新增：

```text
src/gui/terminal/CommandRegistry.h/.cpp
src/gui/terminal/CommandParser.h/.cpp
src/gui/terminal/CommandHints.h/.cpp
```

核心类型：

```cpp
struct CommandSpec {
    QString path;                 // "setting search_mode"
    QStringList aliases;          // 兼容旧名
    QString usageKey;             // i18n
    QVector<ArgSpec> args;        // 类型/范围/枚举
    Handler handler;
    bool hidden = false;
};

struct CommandInvocation {
    const CommandSpec* spec;
    QVariantMap values;
    QStringList raw;
};

struct Hint {
    enum Kind { None, Unknown, Incomplete, BadValue, Deprecated };
    Kind kind;
    QString usage;
    QStringList examples;
    QStringList nearest;
};
```

职责：

```text
CommandRegistry：唯一命令表，help/补全/提示共用
CommandParser：最长路径匹配、类型校验、别名迁移
CommandHints：Levenshtein + 前缀匹配，只生成文本
CommandTerminal：Tab 候选 + 提示条显示
MainWindow：实现 setting/action handler
```

## 8. 分阶段实施

### Phase 1：注册表与解析器

```text
- 建立 command tree
- 迁移 27 条旧命令为 spec
- help 改为由 registry 生成
- 保持现有行为不变
- 单测：解析/别名/参数校验
```

### Phase 2：setting 分支

```text
- 迁移 theme/log/stop/smart/lock/grid/density/threads/target
- 新增 ring_*、search_*、result_*、gpu_* 设置键
- 读写 AppSettings 与 UI 控件
- 旧名输出迁移提示
```

### Phase 3：多级补全与提示

```text
- Tab 支持 path 级补全
- 提示条 QLabel
- 防抖/取消/焦点保护
- 更新 GUI 自检
```

### Phase 4：结果/缓存/页面

```text
- result copy_all/export/sort
- cache list/open/--yes
- page/map/pattern/afk
```

### Phase 5：清理

```text
- 删除顶层 copy/mode
- 隐藏旧别名
- 更新 help/wiki
- 检查命令表与真实功能一一对应
```

## 9. 验收标准

```text
help 中每条命令都能执行
命令表没有幽灵命令
所有 setting 路径都能读回当前值
旧别名至少保留一个测试版本并输出迁移提示
Tab 连续 10 次焦点不动
Tab 不写入输入行
格式提示不抢焦点、不自动改写输入
未知命令 100% 有最近用法提示
```

新增测试：

```text
CommandParserTest
CommandHintTest
CommandCompletionTest
CommandSettingsRoundTripTest
CommandAliasDeprecationTest
```

## 10. 优先级

```text
P0：registry + setting + 提示 + 旧命令迁移
P1：多级 Tab 补全 + result/cache
P2：map/pattern/page 等低频命令
P3：删除旧别名、清理文档
```

命令终端是本轮最大改动，先做 P0；不要在没建立 registry 前继续往 `runCommand` 堆 if 分支。





## 11. P0 实施进度（2026-11-24）

已完成第一增量：

- 新增 `setting <key> [value]` 命令，键名统一 lower_snake_case
- 支持 theme/language/search_*/result_*/ring_*/gpu_*/cache_enabled/threads/log_*/display_rate_unit/sound_*/window_remember
- `setting <key>` 返回当前值与可用范围；非法值返回范围
- `setting search` 等前缀键可列组
- `CommandHost::commandFormatHint` 与终端只读提示条接入
- 250ms 防抖；Tab 时立即取消提示，不修改输入、不抢焦点
- `set`/`sett` 显示 setting 用法示例
- 旧顶层命令暂保留，后续阶段再隐藏/删除
- 仍待：CommandRegistry 独立文件、多级 Tab 语义候选、旧命令弃用提示、result/cache/page/map 新分支

## P0 进度补充（2026-11-24 18:15）

- 多级 Tab 补全：`setting <Tab>` 补键，`setting <key> <Tab>` 补枚举值
- 值补全不追加空格，命令/键补全追加空格
- 旧顶层设置命令迁移到 setting，并输出弃用提示；旧名保留
- 新增 `ui.cmd_deprecated`
- GUI 自检覆盖设置键/值补全与提示不干扰 Tab
