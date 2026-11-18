# 环形搜索并入 SearchJob（flow-ring-job）

分支：`flow-ring-job`，基于 `flow-runtime`。

## 改动

原来：

```text
std::thread ringWorker
QEventLoop 轮询 ringDone
ringWorker.join()
```

现在：

```text
SearchJob m_ringJob
ringRunner(...) -> SearchOutcome
QEventLoop 轮询 ringDone / SearchJob::State::Finished
m_ringJob.join()
```

取消路径：

```text
cancelSearch / closeEvent
  → m_ringCancel = true
  → m_ringJob.requestCancel()
```

这样环形规划与普通搜索共用：

```text
SearchJob 状态机
两阶段取消
有界查询（waitFor）
join 生命周期
```

## 仍然保留的边界

环形搜索目前仍在 `runRingSearch()` 内用局部 `QEventLoop` 等待完成，
因为规划后的结果处理、地图刷新、反馈写入都与该函数栈强绑定。

所以这次完成的是：

```text
执行体 SearchJob 化
取消/关闭接入 SearchJob
```

还没有完成：

```text
完全异步化（done 回调后再处理结果，函数立即返回）
```

## 验证

```text
构建通过
slime_search_job_tests   ALL PASS
GUI 自检（GPU off）     exit=0
```

环形本身没有独立单元测试，GUI 自检也不覆盖完整环形规划；
这是本分支的验收缺口。

## 发布命名

```text
flow-ring-job-SlimeFinder-3.13.9
```
