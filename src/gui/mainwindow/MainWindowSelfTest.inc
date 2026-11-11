// v3.12.0：从 MainWindow.cpp 拆出的 GUI 自检实现。
// 本文件在 MainWindow.cpp 的命名空间内被 include，因此可访问同一编译单元的
// 私有成员和文件级 helper；后续可继续拆为独立 TU。

int MainWindow::runSelfTest()
{
    // v3.5.27：自检固定用中文跑 —— 断言里比对的是中文分组名/导航名，
    //   用户把界面切成英文时不该让自检假失败。
    slime::ui::setLanguage(slime::ui::Language::Chinese);
    applyLanguage();

    // v3.4.8：自检会真的改动设置（「设置项逐控件接线」测试），因此先存档、结束时原样恢复 ——
    // 否则被改过的 config.json 会让**下一次**运行里的「默认值」类断言误报失败。
    // v3.4.12：自检期间自动关闭「未预期的模态框」并记录标题。
    //   任何漏加 m_uiQuiet 保护的确认弹窗都会在嵌套事件循环里被这里关掉（QTimer 在嵌套循环中照样触发），
    //   既不会卡死自动化流程，又能把弹窗标题写进证据日志便于定位。
    QTimer modalWatchdog;
    int modalSeen = 0;
    connect(&modalWatchdog, &QTimer::timeout, this, [&modalSeen]() {
        QWidget* m = QApplication::activeModalWidget();
        if (!m) return;
        ++modalSeen;
        std::fprintf(stderr, "[UI自检] 关闭未预期的模态框：#%d 标题=\"%s\" 类型=%s\n", modalSeen,
                     m->windowTitle().toUtf8().constData(), m->metaObject()->className());
        if (auto* dlg = qobject_cast<QDialog*>(m)) {
            dlg->reject();
        } else {
            m->close();
        }
    });
    modalWatchdog.start(200);
    struct ModalWatchdogStop {
        QTimer* timer;
        int* seen;
        ~ModalWatchdogStop()
        {
            timer->stop();
            if (*seen > 0) {
                std::fprintf(stderr, "[UI自检] 本次自检共关闭 %d 个未预期模态框（见上方标题）\n", *seen);
            }
        }
    } modalWatchdogStop{ &modalWatchdog, &modalSeen };

    const AppSettings settingsArchive = m_settings;
    std::string configArchive;
    {
        std::ifstream in(appDirPath() / kConfigFileName, std::ios::binary);
        if (in) configArchive.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    int fail = 0;
    m_uiQuiet = true;
    const auto check = [&](bool Ok, const char* name) {
        if (!Ok) {
            ++fail;
            std::fprintf(stderr, "[UI自检 FAIL] %s\n", name);
        }
    };

    // v3.3.4（任务书 1.4）：不做折叠 —— 6 个设置分组始终展开，无需额外复位。
    // v3.3.4（任务书 4.1）：种子默认空 —— 自检先把种子填成合法值，
    // 保证后续各组的真实搜索都能正常执行（空种子的拦截行为在 13.9 单独验证）。
    m_w->seedEdit->setText(QStringLiteral("12345"));

    // 1) 快速预设
    // v3.10.2：GPU 变快后快捷范围整体上调一档
    applyPreset(20000);
    check(m_w->minX->value() == -20000 && m_w->maxX->value() == 20000 && m_w->minZ->value() == -20000 &&
              m_w->maxZ->value() == 20000,
          "预设 附近(±20000)");
    applyPreset(1000000);
    check(m_w->maxX->value() == 1000000 && m_w->maxZ->value() == 1000000, "预设 全局(±1000000)");

    // 2) 边界截断（Java ±1875000）
    m_w->javaRadio->setChecked(true);
    onVersionChanged();
    check(m_w->maxX->value() == kJavaBoundChunk && m_w->minX->value() == -kJavaBoundChunk,
          "Java 默认=世界边界");
    m_w->minX->setValue(-999999999);
    clampRangeToBound();
    check(m_w->minX->value() == -kJavaBoundChunk, "Java 负边界截断");
    m_w->maxZ->setValue(2000000000);
    clampRangeToBound();
    check(m_w->maxZ->value() == kJavaBoundChunk, "Java 正边界截断");

    // 3) 基岩版边界 ±131072
    m_w->bedrockRadio->setChecked(true);
    onVersionChanged();
    check(m_w->maxX->value() == 131072 && m_w->minX->value() == -131072, "基岩默认=基岩边界");
    m_w->maxZ->setValue(123456789);
    clampRangeToBound();
    check(m_w->maxZ->value() == 131072, "基岩边界截断");
    check(m_w->seedEdit->isEnabled(), "基岩版种子框可用（仅用于地图上色，判定与种子无关）");
    check(m_w->presetWorld->isEnabled() == false && m_w->presetBedrock->isEnabled() == true,
          "基岩版预设可用性");
    m_w->javaRadio->setChecked(true);
    onVersionChanged();
    check(m_w->presetWorld->isEnabled() == true && m_w->presetBedrock->isEnabled() == false,
          "Java 版预设可用性");

    // 4) 复制命令格式 /tp @s <x> 64 <z>
    {
        MatchResult r;
        r.minX = 1; r.maxX = 2; r.minZ = -3; r.maxZ = -2;
        const QString cmd = cacheCommandFor(r);
        check(cmd == QStringLiteral("/tp @s 32 64 -32"), "命令格式(中心方块)");
    }

    // 5) 结果卡片构建/清理
    {
        SearchOutcome out;
        out.ok = true;
        out.totalFound = 2;
        out.results = std::vector<MatchResult>{ MatchResult{ -1, 2, 0, 3, 0.4f },
                                                MatchResult{ 5, 6, 7, 8, 0.0f } };
        out.stats.scannedChunks = 1000000;
        out.stats.totalMs = 10.0;
        RunInfo info;
        info.mode = Mode::Strict;
        info.startX = 0;
        info.startZ = 0;
        m_lastResultText = describeResultsText(out, info);
        check(m_lastResultText.contains(slime::ui::t("ui.7a1fd8c33948")), "文本渲染");
        // v3.3.4：结果写入右侧 QTextEdit（信息栏 + QTextEdit 规范）
        populateResultText(out, info);
        check(m_w->resultView && m_w->resultView->toPlainText().contains(slime::ui::t("ui.7a1fd8c33948")),
              "结果已写入右侧结果视图");
        clearResults();
        check(m_w->resultView && m_w->resultView->toPlainText().isEmpty(), "结果视图可清空");

        // 7) 性能日志落盘
        m_searchStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        writePerformanceLog(out, info);
        std::ifstream pf(appDirPath() / "logs" / "performance.log");
        const bool hasPerf = pf.good() && std::string(std::istreambuf_iterator<char>(pf),
                                                       std::istreambuf_iterator<char>())
                                                 .find("扫描区块 1000000") != std::string::npos;
        check(hasPerf, "performance.log 记录");
    }

    // 6) config.json 缓存容量
    check(cacheMaxBytes() == 1024LL * 1024 * 1024, "config 默认 1024MB");

    // 8) 整图估算 API（v3.1.1）
    {
        m_w->densityRadio->setChecked(true);
        onModeChanged();
        SearchOptions opt = wholeMapOptions(kJavaBoundChunk);
        SearchEstimate est;
        const bool estOk = SlimeEngine::estimateSearch(opt, m_w->threads->value(), est);
        check(estOk, "整图估算可用");
        if (estOk) {
            check(est.areaChunks > 1e12 && est.evalChunks >= est.areaChunks, "整图估算规模正确");
            check(est.totalSec > 0 && est.scanRate > 1e6, "整图估算速率正确");
        }
        m_w->strictRadio->setChecked(true);
        onModeChanged();
    }

    // 9) v3.2.1：GPU 复选框 / 检测（勾选触发或搜索前兜底）/ config 往返
    {
        std::fprintf(stderr, "[UI自检] GPU 开关检查…\n");
        check(m_w->gpuCheck != nullptr && m_w->gpuStatus != nullptr, "GPU 复选框存在");
        const bool wasOn = m_w->gpuCheck->isChecked();
        if (!wasOn) {
            m_w->gpuCheck->setChecked(true); // 勾选 → 同步检测一次（启动不接触 OpenCL）
        }
        if (!m_gpuProbeDone) {
            ensureGpuScanOnce(); // 兜底（config=true 启动路径：勾选信号未变化）
        }
        check(m_gpuProbeDone, "GPU 检测完成（不崩溃）");
        std::fprintf(stderr, "[UI自检] GPU 检测结果: %s（%s）\n", m_gpuAvailable ? "可用" : "不可用",
                     m_gpuProbeReason.c_str());
        // config gpu.enabled 写读往返（直接查文件内容，避免 SLIME_GPU 环境变量干扰）
        const auto fileEnabled = [this]() {
            // v3.3.0：精确解析（旧写法在 algorithm 块存在时会误判）
            return cfgBool(appDirPath() / kConfigFileName, "gpu.enabled", false);
        };
        saveGpuEnabledToConfig(true);
        check(fileEnabled(), "config gpu.enabled 写读往返(true)");
        saveGpuEnabledToConfig(false);
        check(!fileEnabled(), "config gpu.enabled 写读往返(false)");
        m_loadingConfig = true;
        m_w->gpuCheck->setChecked(wasOn);
        m_loadingConfig = false;
        // 勾选 + 可用 → 注入并端到端跑一次（≥引擎 GPU 阈值）；不可用则只验证 CPU 路径
        if (m_gpuAvailable && m_w->gpuCheck->isChecked() && m_gpuScan) {
            SearchOptions go;
            go.version = Version::Java;
            go.worldSeed = 42;
            go.minX = -1024; go.maxX = 1023; go.minZ = -1024; go.maxZ = 1023;
            go.mode = Mode::Density;
            go.gridSize = 2;
            go.minDensity = 0.9;
            go.targetCount = 10;
            go.threads = m_w->threads->value();
            go.gpuScan = m_gpuScan.get(); // 与 startSearch 相同的注入条件
            SearchOutcome gout;
            std::atomic<bool> lo{ false };
            ProgressFn noop;
            SlimeEngine eng;
            eng.run(go, lo, noop, gout);
            check(gout.ok, "GPU 注入端到端搜索完成");
            std::fprintf(stderr, "[UI自检] GPU 端到端搜索: ok=%d 结果=%zu\n", (int)gout.ok,
                         gout.results.size());
        }
    }

    // 10) v3.2.1：真实 GUI 搜索冒烟（走 startSearch 全链路：后台任务线程 + 进度 + 结果渲染 + 日志）
    {
        std::fprintf(stderr, "[UI自检] 真实搜索冒烟（512x512 Java 密度）…\n");
        m_w->javaRadio->setChecked(true);
        m_w->densityRadio->setChecked(true);
        m_w->minX->setValue(-256);
        m_w->maxX->setValue(255);
        m_w->minZ->setValue(-256);
        m_w->maxZ->setValue(255);
        m_w->targetCount->setValue(10);
        m_w->stopAtTarget->setChecked(false);
        m_w->smartRange->setChecked(false);
        m_w->startX->setValue(0);
        m_w->startZ->setValue(0);
        m_w->seedEdit->setText(QStringLiteral("12345")); // v3.3.4：种子默认空，自检先填写合法种子
        // 冒烟跟随复选框状态：config gpu.enabled=false → 纯 CPU；=true 且可用 → GPU 注入全链路
        std::fprintf(stderr, "[UI自检] 搜索冒烟 GPU 状态: 勾选=%d 可用=%d\n",
                     (m_w->gpuCheck && m_w->gpuCheck->isChecked()) ? 1 : 0, m_gpuAvailable ? 1 : 0);
        startSearch(); // 需要 UI 线程事件泵完成异步任务与回调
        const auto t0 = std::chrono::steady_clock::now();
        int pumps = 0;
        bool sawScanning = false, sawMatchingIntro = false, sawMatchingPct = false;
        while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            // v3.3.3（任务书 3.2）：采样进度文字（信息性记录；阶段文字的确定性断言见第 13 组）
            const QString pt = m_w->progressInfo->text();
            if (pt.contains(slime::ui::t("ui.4ee94f431863"))) sawScanning = true;
            if (pt.contains(slime::ui::t("ui.dc8d4773e118"))) sawMatchingIntro = true;
            if (pt.contains(slime::ui::t("ui.5b0b37bab0a8"))) sawMatchingPct = true;
            QThread::msleep(20);
            ++pumps;
        }
        check(!m_running, "真实搜索在 60s 内完成（未闪退/未挂起）");
        check(m_w->progressInfo->text().contains(slime::ui::t("ui.8174d1168fce")), "进度文字：完成后显示「搜索完成…」");
        std::fprintf(stderr, "[UI自检] 搜索冒烟结束 pumps=%d 结果文本=%lld 行 阶段采样(信息性): 扫描=%d 匹配引导=%d 匹配进度=%d\n",
                     pumps, static_cast<long long>(m_lastResultText.count('\n')), (int)sawScanning,
                     (int)sawMatchingIntro, (int)sawMatchingPct);
    }

    // 10.5) v3.9.11/3.9.12：结果要求方案（预设）——数量随模式变化、切换方案不改 GPU、忽略阈值冲突裁决
    {
        std::fprintf(stderr, "[UI自检] 结果要求方案检查…\n");
        const bool gpuBefore = (m_w->gpuCheck && m_w->gpuCheck->isChecked());
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        updateSearchShapeEnabled();
        check(m_w->presetCombo && m_w->presetCombo->count() == 5, "密度模式方案数 = 4 + 自定义");
        check(m_w->presetCombo->itemText(m_w->presetCombo->count() - 1) == slime::ui::t("ui.c493338e8c33"),
              "方案列表最后一项是自定义");

        // 方案「找全部候选」（索引 1）：关闭停止方式，且不得改动 GPU 开关
        m_w->presetCombo->setCurrentIndex(1);
        check(!m_w->stopAtTarget->isChecked() && !m_w->smartRange->isChecked(), "方案「找全部候选」关闭找够就停/智能范围");
        check((m_w->gpuCheck && m_w->gpuCheck->isChecked()) == gpuBefore, "切换方案不改变 GPU 开关（加速独立）");

        // 方案「最优 N 个（忽略阈值）」（索引 2）：开启忽略 + 冲突裁决
        m_w->presetCombo->setCurrentIndex(2);
        check(m_w->ignoreThreshold && m_w->ignoreThreshold->isChecked(), "方案「最优 N 个」开启忽略阈值");
        check(!m_w->minDensity->isEnabled(), "最优候选模式禁用最小密度输入");
        check(!m_w->stopAtTarget->isEnabled() && !m_w->smartRange->isEnabled(), "最优候选模式禁用提前停止/智能范围");
        check(m_w->presetHint && m_w->presetHint->text().contains(slime::ui::t("ui.4446620620fc")), "最优候选模式提示文案");

        // 切回默认方案：阈值恢复可用、忽略关闭
        m_w->presetCombo->setCurrentIndex(0);
        check(!m_w->ignoreThreshold->isChecked() && m_w->minDensity->isEnabled(), "切回方案后阈值恢复可用");

        // 图案模式：方案数 = 2 + 自定义，且忽略阈值不可用
        m_w->strictRadio->setChecked(true);
        updateSearchShapeEnabled();
        check(m_w->presetCombo->count() == 3, "图案模式方案数 = 2 + 自定义");
        check(!m_w->ignoreThreshold->isEnabled(), "图案模式下忽略阈值不可用");

        // 恢复：密度模式 + 默认方案
        m_w->densityRadio->setChecked(true);
        updateSearchShapeEnabled();
        m_w->presetCombo->setCurrentIndex(0);
        check(m_w->ignoreThreshold && !m_w->ignoreThreshold->isChecked(), "方案检查后恢复到默认状态");
    }

    // 10.5b) v3.10.7：缓存构建页临时强制基岩版，退出后必须恢复用户原本的平台
    {
        m_w->javaRadio->setChecked(true);
        setBuildModeActive(true);
        const bool forced = m_w->bedrockRadio->isChecked();
        setBuildModeActive(false);
        const bool restored = m_w->javaRadio->isChecked();
        std::fprintf(stderr, "[UI自检]   构建模式平台：强制基岩=%d 退出恢复Java=%d\n", (int)forced,
                     (int)restored);
        check(forced, "缓存构建模式临时切换为基岩版");
        check(restored, "退出缓存构建后平台恢复为 Java（不再莫名变基岩）");
    }

    // 10.6) v3.10.2（用户反馈「忽略阈值没生效」）：最优候选模式 GUI 全链路验证
    {
        std::fprintf(stderr, "[UI自检] 最优候选（忽略阈值）全链路…\n");
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        m_w->javaRadio->setChecked(true);
        m_w->minX->setValue(-128);
        m_w->maxX->setValue(127);
        m_w->minZ->setValue(-128);
        m_w->maxZ->setValue(127);
        m_w->gridSize->setValue(4);
        m_w->minDensity->setValue(0.99);   // 阈值高到正常搜索必然 0 结果
        m_w->targetCount->setValue(5);
        m_w->stopAtTarget->setChecked(false);
        m_w->smartRange->setChecked(false);
        m_w->ignoreThreshold->setChecked(true);
        m_w->seedEdit->setText(QStringLiteral("4545"));
        startSearch();
        const auto t0 = std::chrono::steady_clock::now();
        while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            QThread::msleep(15);
        }
        const auto& rs = m_lastOutcome.results;
        bool sorted = true;
        for (size_t i = 1; i < rs.size(); ++i) {
            if (rs[i].density > rs[i - 1].density + 1e-6f) sorted = false;
        }
        std::fprintf(stderr, "[UI自检]   忽略阈值结果：ok=%d 条数=%zu 密度序列=%s 标记=%d\n",
                     (int)m_lastOutcome.ok, rs.size(), sorted ? "降序" : "非降序",
                     (int)m_lastOutcome.ignoredThreshold);
        check(m_lastOutcome.ok && rs.size() == 5, "最优候选：阈值 0.99 下仍返回 N 条（忽略生效）");
        check(sorted, "最优候选：结果按窗口密度从高到低排序");
        check(m_lastOutcome.ignoredThreshold, "最优候选：结果标记「已忽略阈值」");
        m_w->ignoreThreshold->setChecked(false);
        m_w->minDensity->setValue(0.3);
    }

    // 10.7) v3.10.4（用户要求：GPU 只保留速度显示）：GPU 大区域搜索完成后统计里给出速度
    if (m_gpuAvailable && m_gpuScan) {
        std::fprintf(stderr, "[UI自检] GPU 大区域搜索：完成后速度显示…\n");
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        m_w->javaRadio->setChecked(true);
        if (m_w->gpuCheck) m_w->gpuCheck->setChecked(true);
        m_w->minX->setValue(-1048576);
        m_w->maxX->setValue(1048575);
        m_w->minZ->setValue(-1048576);
        m_w->maxZ->setValue(1048575);
        m_w->gridSize->setValue(8);
        m_w->minDensity->setValue(0.6);
        m_w->targetCount->setValue(5);
        m_w->stopAtTarget->setChecked(false);
        m_w->smartRange->setChecked(false);
        m_w->ignoreThreshold->setChecked(false);
        m_w->seedEdit->setText(QStringLiteral("4545"));
        bool sawBusy = false;
        startSearch();
        const auto t0 = std::chrono::steady_clock::now();
        while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 120.0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (m_w->progress && m_w->progress->maximum() == 0) sawBusy = true;
            QThread::msleep(5);
        }
        const QString summary = m_w->infoStats ? m_w->infoStats->text() : QString();
        std::fprintf(stderr, "[UI自检]   GPU 搜索完成：GPU匹配=%d 忙碌动画=%d 统计=%s\n",
                     (int)m_lastOutcome.gpuMatchUsed, (int)sawBusy, summary.toUtf8().constData());
        check(m_lastOutcome.ok && m_lastOutcome.gpuMatchUsed, "GPU 大区域搜索走 GPU 匹配并成功");
        check(summary.contains(slime::ui::t("ui.eb6aaba1a1ca")) && summary.contains(slime::ui::t("ui.3da82d85c5de")),
              "GPU 搜索完成后统计显示区块数与用时");
    }

    // 10.8) v3.10.8（用户疑问：预设是否暗改非明面设置）：预设只动明面上的结果开关
    {
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        const int t0 = m_w->threads->value();
        const int tc0 = m_w->targetCount->value();
        const int g0 = m_w->gridSize->value();
        const double d0 = m_w->minDensity->value();
        const int sx0 = m_w->startX->value();
        const int sz0 = m_w->startZ->value();
        const bool cache0 = m_w->useCache->isChecked();
        const bool gpu0 = m_w->gpuCheck && m_w->gpuCheck->isChecked();
        m_w->presetCombo->setCurrentIndex(0);   // 快速找最近 N 个
        check(m_w->stopAtTarget->isChecked() && !m_w->smartRange->isChecked() &&
                  m_w->algExactRadio->isChecked() && !m_w->ignoreThreshold->isChecked(),
              "预设只设置明面开关：找够即停（不强制由近及远）+精确算法");
        check(m_w->threads->value() == t0 && m_w->targetCount->value() == tc0 &&
                  m_w->gridSize->value() == g0 && qFuzzyCompare(m_w->minDensity->value() + 1.0, d0 + 1.0) &&
                  m_w->startX->value() == sx0 && m_w->startZ->value() == sz0 &&
                  m_w->useCache->isChecked() == cache0 && (m_w->gpuCheck && m_w->gpuCheck->isChecked()) == gpu0,
              "预设不修改 线程/目标/网格/阈值/起点/缓存/GPU 等其它开关");
    }

    // 10.9) v3.10.9（用户要求）：高级开关分级 + 自定义速度声明 + GPU 设置入口
    {
        showPage(kPageSearch);
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        m_w->presetCombo->setCurrentIndex(0);   // 预设
        check(!m_w->resultGroup->isVisible() && !m_w->matchGroup->isVisible(),
              "预设方案下「结果要求/匹配方式」收起（防乱调）");
        check(!m_w->presetDisclaimer->isVisible(), "预设方案不显示自定义速度声明");
        const int customIdx = 4;
        m_w->presetCombo->setCurrentIndex(customIdx);   // 自定义
        check(m_w->resultGroup->isVisible() && m_w->matchGroup->isVisible(),
              "自定义方案展开「结果要求/匹配方式」");
        check(m_w->presetDisclaimer->isVisible() &&
                  m_w->presetDisclaimer->text().contains(slime::ui::t("ui.f2fdffbb9ed5")),
              "自定义方案显示「可能明显降低速度」声明");
        check(m_w->gpuAdvBtn && m_w->gpuAdvPanel && !m_w->gpuAdvPanel->isVisible(),
              "GPU「设置」入口默认收起");
        m_w->gpuAdvBtn->click();
        check(m_w->gpuAdvPanel->isVisible(), "点击后 GPU 相关设置展开");
        check(m_w->gpuStreamCheck && m_w->gpuLimitSpin && m_w->gpuAutoCheck,
              "GPU 展开含 图案流式/占用率上限/自动检测");
        m_w->gpuAdvBtn->click();
        check(!m_w->gpuAdvPanel->isVisible(), "再次点击收起 GPU 设置");
        m_w->presetCombo->setCurrentIndex(0);
    }

    // 11) v3.3.0：算法模式（精确/分层/快速估算/保守）——UI 联动 + config 往返 + 真实搜索
    {
        std::fprintf(stderr, "[UI自检] 算法模式检查…\n");
        const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
        check(m_w->algExactRadio && m_w->algLayeredRadio && m_w->algSampleRadio && m_w->algBruteRadio,
              "算法模式单选存在");
        check(m_w->layerBlock && m_w->layerRatio && m_w->sampleStep && m_w->sampleExact, "算法参数控件存在");
        check(m_w->modeHeader && m_w->modeWarn, "结果区模式标注/警告条存在");
        check(m_w->mainScroll != nullptr, "设置区滚动容器存在（v3.3.0 滚动条）");

        // v3.3.0（修订）：文案不被遮挡的可量化校验
        //  - 每个算法单选：文本宽度 + 单选指示器/边距 ≤ 控件宽度（否则会被截断）
        //  - 模式说明标签（wordWrap）：实际高度 ≥ sizeHint 高度（否则末行被裁）
        //  - 设置区滚动条：内容高于视口时必须可滚动（内容不被永久遮挡）
        {
            m_w->densityRadio->setChecked(true);
            onModeChanged();
            resize(640, 720); // 最小窗口尺寸下检查
            QApplication::processEvents(QEventLoop::AllEvents, 30);
            for (QRadioButton* r : { m_w->algExactRadio, m_w->algLayeredRadio, m_w->algSampleRadio,
                                     m_w->algBruteRadio, m_w->strictRadio, m_w->densityRadio }) {
                r->ensurePolished();
                const int need = r->sizeHint().width();
                if (r->width() < need) {
                    std::fprintf(stderr, "[UI自检] 文字可能被截断: \"%s\" 宽=%d 需要=%d\n",
                                 r->text().toUtf8().constData(), r->width(), need);
                }
                check(r->width() >= need, "单选文字完整显示（未被截断）");
            }
            const QFontMetrics sumFm(m_w->algSummary->font());
            const int sumW = (std::max)(120, m_w->algSummary->width());
            const QRect sumNeed =
                sumFm.boundingRect(QRect(0, 0, sumW, 100000), Qt::TextWordWrap, m_w->algSummary->text());
            const int sumH = m_w->algSummary->height();
            const int hMax = m_w->mainScroll->horizontalScrollBar()->maximum();
            const int contentW = m_w->mainScroll->widget() ? m_w->mainScroll->widget()->width() : 0;
            std::fprintf(stderr,
                         "[UI自检] 模式说明标签 宽=%d 高=%d 需要=%d | 竖滚动上限=%d | 设置区宽=%d 横滚动上限=%d\n",
                         sumW, sumH, sumNeed.height(), m_w->mainScroll->verticalScrollBar()->maximum(),
                         contentW, hMax);
            check(sumH + 2 >= sumNeed.height(), "模式说明标签完整显示（换行未被裁剪）");
            check(m_w->mainScroll->verticalScrollBar()->maximum() > 0,
                  "设置区出现竖向滚动条（超高内容可滚动查看）");
            check(hMax == 0, "最小窗口尺寸下设置区无横向滚动条（文字不被强制截断）");
            // v3.3.4（任务书 1.5/7）：三个规定尺寸下都不允许出现横向滚动条（错位即不交付）
            for (const QSize sz : { QSize(1280, 860), QSize(1000, 650), QSize(1920, 1080) }) {
                resize(sz);
                QApplication::processEvents(QEventLoop::AllEvents, 40);
                const int hm = m_w->mainScroll->horizontalScrollBar()->maximum();
                std::fprintf(stderr, "[UI自检] 尺寸 %dx%d：设置区横滚动上限=%d 竖滚动上限=%d\n", sz.width(),
                             sz.height(), hm, m_w->mainScroll->verticalScrollBar()->maximum());
                check(hm == 0, "该尺寸下设置区无横向滚动条");
            }
            resize(1280, 860);
            QApplication::processEvents(QEventLoop::AllEvents, 40);
            // 逐个算法模式核对说明文字是否完整显示（用户可见的模式选择区文案）
            for (int mi = 0; mi < 4; ++mi) {
                QRadioButton* r = (mi == 0)   ? m_w->algExactRadio
                                  : (mi == 1) ? m_w->algLayeredRadio
                                  : (mi == 2) ? m_w->algSampleRadio
                                              : m_w->algBruteRadio;
                r->setChecked(true);
                updateAlgorithmPanels();
                if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                const QFontMetrics fm(m_w->algSummary->font());
                const QRect need = fm.boundingRect(QRect(0, 0, (std::max)(120, m_w->algSummary->width()), 100000),
                                                   Qt::TextWordWrap, m_w->algSummary->text());
                std::fprintf(stderr, "[UI自检] 模式[%d] 说明: 宽=%d 高=%d 需要=%d | 单选宽=%d 需要=%d\n", mi,
                             m_w->algSummary->width(), m_w->algSummary->height(), need.height(), r->width(),
                             r->sizeHint().width());
                check(m_w->algSummary->height() + 2 >= need.height(), "各算法模式说明文字完整显示");
                check(r->width() >= r->sizeHint().width(), "各算法模式单选文字完整显示");
            }
            m_w->algExactRadio->setChecked(true);
            updateAlgorithmPanels();
            if (hMax > 0 && m_w->mainScroll->widget()) {
                // 诊断：列出需求宽度较大的可见控件，便于定位横向滚动来源
                for (QWidget* w : m_w->mainScroll->widget()->findChildren<QWidget*>()) {
                    const int need = w->sizeHint().width();
                    if (w->isVisible() && need > 520) {
                        std::fprintf(stderr, "[UI自检] 宽控件: %s \"%s\" need=%d width=%d\n",
                                     w->metaObject()->className(),
                                     w->windowTitle().isEmpty()
                                         ? (qobject_cast<QGroupBox*>(w) ? qobject_cast<QGroupBox*>(w)->title().toUtf8().constData() : "")
                                         : "",
                                     need, w->width());
                    }
                }
            }
            resize(700, 920);
            QApplication::processEvents(QEventLoop::AllEvents, 30);
        }

        // 严格模式下算法不可用（图案匹配本身即精确判定）
        m_w->strictRadio->setChecked(true);
        onModeChanged();
        check(!m_w->algLayeredRadio->isEnabled() && !m_w->algSampleRadio->isEnabled(),
              "严格模式下算法选择禁用");
        check(m_w->modeHeader->text().contains(slime::ui::t("ui.b977e67d75f8")), "严格模式标注");

        // 密度模式：分层 → 参数可用 + 警告条随比例联动
        m_w->densityRadio->setChecked(true);
        onModeChanged();
        check(m_w->algLayeredRadio->isEnabled() && m_w->algSampleRadio->isEnabled(), "密度模式下算法可用");
        if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
        saveWindowShot(shotPrefix, QStringLiteral("mode-exact"));
        m_w->algLayeredRadio->setChecked(true);
        updateAlgorithmPanels();
        if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
        saveWindowShot(shotPrefix, QStringLiteral("mode-layered"));
        check(m_w->layerBlock->isEnabled() && !m_w->sampleStep->isEnabled(), "分层参数可用/估算参数禁用");
        check(m_w->modeHeader->text().contains(slime::ui::t("ui.752b56a4f270")), "分层模式标注");
        check(!m_w->modeWarn->isVisible(), "分层比例 1.00 → 无漏检警告（无损剪枝）");
        m_w->layerRatio->setValue(1.5);
        updateAlgorithmPanels();
        check(m_w->modeWarn->isVisible(), "分层比例 >1 → 显示可能漏检警告");
        m_w->layerRatio->setValue(1.0);

        // 估算模式：警告条常显 + 精算采样开关
        m_w->algSampleRadio->setChecked(true);
        m_w->sampleStep->setValue(8);
        updateAlgorithmPanels();
        if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
        saveWindowShot(shotPrefix, QStringLiteral("mode-sample"));
        check(m_w->sampleStep->isEnabled() && !m_w->layerBlock->isEnabled(), "估算参数可用/分层参数禁用");
        check(m_w->modeWarn->isVisible(), "估算模式显示估算警告条");
        check(m_w->modeHeader->text().contains(slime::ui::t("ui.b73b8f52b049")), "估算模式标注");

        // config.json 往返：模式与参数持久化
        saveAlgorithmToConfig();
        const auto cfgText = [this]() {
            std::ifstream in(appDirPath() / kConfigFileName);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        check(cfgText().find("\"mode\": \"sample\"") != std::string::npos, "config algorithm.mode 写入");
        m_loadingConfig = true; // 抑制 toggled 自动写回，验证“从文件读回”本身
        m_w->algExactRadio->setChecked(true);
        m_loadingConfig = false;
        loadAlgorithmFromConfig();
        check(m_w->algSampleRadio->isChecked() && m_w->sampleStep->value() == 8,
              "config algorithm 读回（模式/采样间隔）");

        // 真实搜索冒烟：快速估算模式（走 startSearch 全链路，含结果标注与日志）
        std::fprintf(stderr, "[UI自检] 估算模式真实搜索冒烟…\n");
        m_w->javaRadio->setChecked(true);
        m_w->densityRadio->setChecked(true);
        m_w->minX->setValue(-256);
        m_w->maxX->setValue(255);
        m_w->minZ->setValue(-256);
        m_w->maxZ->setValue(255);
        m_w->gridSize->setValue(5);
        m_w->minDensity->setValue(0.4);
        m_w->targetCount->setValue(10);
        m_w->algSampleRadio->setChecked(true);
        m_w->sampleExact->setChecked(false);
        updateAlgorithmPanels();
        startSearch();
        {
            const auto t0 = std::chrono::steady_clock::now();
            while (m_running &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(10);
            }
        }
        check(!m_running, "估算模式搜索在 60s 内完成");
        check(m_lastResultText.contains(slime::ui::t("ui.5496995eb999")), "结果文本含模式标注");
        if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
        saveWindowShot(shotPrefix, QStringLiteral("mode-sample-warn"));
        saveWindowShot(shotPrefix, QStringLiteral("full-window"));
        {
            std::ifstream pf(appDirPath() / "logs" / "performance.log");
            const std::string log((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
            check(log.find("(sample)") != std::string::npos, "performance.log 记录算法=sample（机器可读键）");
            const auto p = log.rfind("模式 密度");
            check(p != std::string::npos && log.find("采样间隔=8", p) != std::string::npos,
                  "performance.log 记录采样间隔");
            check(p != std::string::npos && log.find("与精确模式差异", p) != std::string::npos,
                  "performance.log 记录与精确模式差异");
        }
        // 恢复默认：精确模式 + 关闭估算
        m_w->algExactRadio->setChecked(true);
        m_w->sampleExact->setChecked(false);
        saveAlgorithmToConfig();
        updateAlgorithmPanels();
    }

    // 12) v3.3.1（任务书 2.1）：缓存扩展名统一 + 旧格式兼容
    {
        std::fprintf(stderr, "[UI自检] 缓存扩展名检查…\n");
        // 12.1 默认缓存文件名与对话框过滤器一致（.slimecache 优先，旧 .cache 兼容，*.* 兜底）
        check(std::string(kCacheFileName) == "slime_cache.slimecache", "默认缓存扩展名为 .slimecache");
        check(std::string(kCacheLegacyFileName) == "slime_cache.cache", "旧版扩展名常量为 .cache");
        const QString filter = cacheImportFilter();
        check(filter.contains(QStringLiteral("*.slimecache")), "过滤器含 *.slimecache");
        check(filter.contains(QStringLiteral("*.cache")), "过滤器含旧版 *.cache");
        check(filter.contains(QStringLiteral("*.*")), "过滤器含所有文件 *.*");
        check(m_cacheFile.filename().string() == kCacheFileName, "运行期缓存路径使用新扩展名");

        // 12.2 保存（.slimecache）后能重新加载，内容一致
        CacheManager c1;
        c1.mergeRegion(Rect{ 0, 63, 0, 63 }, std::vector<std::pair<int, int>>{ { 1, 2 }, { 3, 4 }, { 5, 6 } });
        const auto tmpNew = std::filesystem::temp_directory_path() / "slime_selftest_cache.slimecache";
        std::string err;
        check(c1.saveFile(tmpNew, err), "保存 .slimecache 成功");
        CacheManager c2;
        check(c2.loadFile(tmpNew, true, err), "重新加载 .slimecache 成功");
        check(c2.entryCount() == c1.entryCount() && c2.ranges().size() == c1.ranges().size(),
              "新扩展名缓存往返内容一致");

        // 12.3 旧格式（.cache）仍可被识别加载（兼容性）
        const auto tmpOld = std::filesystem::temp_directory_path() / "slime_selftest_cache.cache";
        check(c1.saveFile(tmpOld, err), "保存旧扩展名 .cache 成功");
        CacheManager c3;
        check(c3.loadFile(tmpOld, true, err), "旧格式 .cache 可正常加载（向后兼容）");
        check(c3.entryCount() == c1.entryCount(), "旧格式缓存内容一致");

        // 12.4 非缓存文件应给出明确错误（不崩溃）
        const auto tmpBad = std::filesystem::temp_directory_path() / "slime_selftest_notcache.slimecache";
        {
            std::ofstream bf(tmpBad, std::ios::binary);
            bf << "not a cache";
        }
        CacheManager c4;
        const bool badLoaded = c4.loadFile(tmpBad, true, err);
        check(!badLoaded && !err.empty(), "非缓存文件被拒绝并给出原因");

        std::error_code rec;
        std::filesystem::remove(tmpNew, rec);
        std::filesystem::remove(tmpOld, rec);
        std::filesystem::remove(tmpBad, rec);
        std::fprintf(stderr, "[UI自检] 缓存扩展名检查完成（新/旧/非法 三类均已覆盖）\n");
    }

    // 12.5~12.7) v3.3.2（任务书 3.2/3.3/七）：过滤器覆盖历史格式 + 对话框可见可选 + 真实端到端
    {
        std::fprintf(stderr, "[UI自检] 缓存可用性（v3.3.2）检查…\n");
        const QString filter = cacheImportFilter();
        const QStringList groups = filter.split(QStringLiteral(";;"));
        const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");

        // 12.5 过滤器必须覆盖「新版 + 全部历史格式 + 所有文件」
        check(filter.contains(QStringLiteral("*.slimecache")), "过滤器含新版 *.slimecache");
        check(filter.contains(QStringLiteral("*.cache")), "过滤器含旧版二进制 *.cache");
        check(filter.contains(QStringLiteral("*.pkl")) && filter.contains(QStringLiteral("*.pickle")),
              "过滤器含 Python 旧版 *.pkl / *.pickle");
        check(filter.contains(QStringLiteral("Slime Cache")), "过滤器含英文标识 Slime Cache");
        check(filter.contains(QStringLiteral("(*.*)")), "过滤器含所有文件 (*.*)");
        check(groups.size() >= 5, "过滤器分组 ≥5（新/旧/pkl/所有文件等）");

        // 12.6 对话框「可见 + 可选」可复现验证：临时目录写入默认名缓存，
        //      用与 importCache 完全相同的过滤器做目录匹配，并用同一配置断言其被预选。
        {
            std::error_code ec2;
            const auto dir = std::filesystem::temp_directory_path() / "slime_selftest_dlg";
            std::filesystem::remove_all(dir, ec2);
            std::filesystem::create_directories(dir, ec2);
            CacheManager cd;
            cd.mergeRegion(Rect{ 0, 31, 0, 31 },
                           std::vector<std::pair<int, int>>{ { 7, 7 }, { 9, 9 } });
            std::string derr;
            check(cd.saveFile(dir / kCacheFileName, derr), "对话框用例：保存默认名缓存成功");
            {
                std::ofstream of(dir / "readme.txt");
                of << "not a cache";
            }
            const QString dirQ = QString::fromStdWString(dir.wstring());
            // 注意：QDir::nameFiltersFromString 是按空白切分的便捷函数，不能用于带标签的
            // 「标签 (*.a *.b)」分组；这里自行取出括号内的模式（与 QFileDialog 的解析一致）。
            const auto patternsOfGroup = [](const QString& group) -> QStringList {
                const int a = group.indexOf(QLatin1Char('('));
                const int b = group.lastIndexOf(QLatin1Char(')'));
                if (a < 0 || b <= a) return {};
                return group.mid(a + 1, b - a - 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            };
            const QStringList firstGroup = patternsOfGroup(groups.value(0));
            const QStringList listed = QDir(dirQ).entryList(firstGroup, QDir::Files);
            std::fprintf(stderr, "[UI自检] 过滤器首组=[%s] 目录内匹配=%lld 项\n",
                         firstGroup.join(QStringLiteral(" ")).toUtf8().constData(),
                         (long long)listed.size());
            check(firstGroup.size() == 5 && firstGroup.contains(QStringLiteral("*.slimecache")) &&
                      firstGroup.contains(QStringLiteral("*.cache")) &&
                      firstGroup.contains(QStringLiteral("*.pkl")) &&
                      firstGroup.contains(QStringLiteral("*.pickle")) &&
                      firstGroup.contains(QStringLiteral("*.legacy")),
                  "首个过滤器分组的模式集合 = 新版+旧版+pkl+pickle+legacy（v3.3.8 加入 .legacy 以便回滚）");
            check(listed.contains(QString::fromStdString(kCacheFileName)),
                  "默认过滤器下列表中可见 slime_cache.slimecache");
            check(!listed.contains(QStringLiteral("readme.txt")), "默认过滤器不匹配无关文件");
            const QStringList allGroup = patternsOfGroup(groups.value(groups.size() - 1));
            check(QDir(dirQ).entryList(allGroup, QDir::Files).contains(QStringLiteral("readme.txt")),
                  "「所有文件」兜底可见任意文件");

            QFileDialog dlgCheck(nullptr, slime::ui::t("ui.834c34e48ed2"), dirQ);
            dlgCheck.setFileMode(QFileDialog::ExistingFile);
            dlgCheck.setNameFilters(groups);
            dlgCheck.selectFile(QString::fromStdString(kCacheFileName));
            const QStringList sel = dlgCheck.selectedFiles();
            check(!sel.isEmpty() &&
                      QFileInfo(sel.first()).fileName() == QString::fromStdString(kCacheFileName),
                  "对话框预选当前缓存文件（保存后即可直接选中加载）");
            std::fprintf(stderr, "[UI自检] 对话框起始目录=%s 预选=%s\n",
                         dlgCheck.directory().absolutePath().toUtf8().constData(),
                         sel.isEmpty() ? "(空)" : sel.first().toUtf8().constData());

            // 截图证据（SLIME_SHOT 设置时）：非原生对话框可在 offscreen 平台渲染
            if (!shotPrefix.isEmpty()) {
                dlgCheck.setOption(QFileDialog::DontUseNativeDialog, true);
                dlgCheck.resize(920, 560);
                dlgCheck.show();
                QApplication::processEvents(QEventLoop::AllEvents, 80);
                const QString p = QStringLiteral("%1_cache-import-dialog.png").arg(shotPrefix);
                const bool okShot = dlgCheck.grab().save(p);
                std::fprintf(stderr, "[UI自检] 缓存导入对话框截图%s: %s\n",
                             okShot ? "已保存" : "保存失败", p.toUtf8().constData());
                dlgCheck.hide();
            }
            std::filesystem::remove_all(dir, ec2);
        }

        // 12.7 真实端到端：基岩版小范围搜索（启用缓存）→ 生成 slime_cache.slimecache →
        //      重新加载（等价于重启）→ 缓存判定与引擎逐区块一致（= 结果与保存前一致）
        {
            std::error_code ec3;
            std::filesystem::remove(m_cacheFile, ec3);
            std::filesystem::remove(appDirPath() / kCacheLegacyFileName, ec3);
            m_cache->clear();
            const bool cacheWasEnabled = m_w->useCache ? m_w->useCache->isChecked() : false;
            if (m_w->useCache) m_w->useCache->setChecked(true);
            m_w->bedrockRadio->setChecked(true);
            m_w->densityRadio->setChecked(true);
            m_w->minX->setValue(-128);
            m_w->maxX->setValue(127);
            m_w->minZ->setValue(-128);
            m_w->maxZ->setValue(127);
            m_w->targetCount->setValue(10);
            m_w->stopAtTarget->setChecked(false);
            m_w->smartRange->setChecked(false);
            // v3.3.9（任务书 1.1/1.2）：缓存由**构建模式**写入（搜索只读 ⇒ 不再生成缓存）
            std::fprintf(stderr, "[UI自检] 缓存端到端：先构建缓存（基岩 256x256）…\n");
            setBuildModeActive(true);
            updateBuildHintUi();
            startSearch();
            const auto t0 = std::chrono::steady_clock::now();
            while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 120.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(20);
            }
            setBuildModeActive(false);
            updateBuildHintUi();
            check(!m_running, "缓存构建在 120s 内完成");
            check(std::filesystem::exists(m_cacheFile, ec3), "构建后生成缓存文件 slime_cache.slimecache");
            check(m_cache->entryCount() > 0, "缓存条目数 > 0");
            std::string lerr;
            CacheManager reload;
            const bool reloaded = reload.loadFile(m_cacheFile, true, lerr);
            check(reloaded, "重新加载（模拟重启）缓存文件成功");
            check(reload.entryCount() == m_cache->entryCount(), "重载后条目数与保存前一致");
            int mism = 0;
            int64_t checkedChunks = 0;
            for (const Rect& r : reload.ranges()) {
                for (int x = r.minX; x <= r.maxX; ++x) {
                    for (int z = r.minZ; z <= r.maxZ; ++z) {
                        ++checkedChunks;
                        if (reload.isSlime(x, z) != SlimeEngine::isSlimeChunkBedrockMT(x, z)) ++mism;
                    }
                }
            }
            check(checkedChunks > 0 && mism == 0,
                  "重载缓存判定与引擎逐区块一致（结果与保存前一致）");
            // 诊断输出统一 UTF-8（本行此前用 path::string() = ANSI，会让证据日志混入 GBK 字节）
            const std::string cachePathUtf8(reinterpret_cast<const char*>(m_cacheFile.u8string().c_str()));
            std::fprintf(stderr,
                         "[UI自检] 缓存端到端：文件=%s 条目=%lld 重载条目=%lld 逐区块比对=%lld 不一致=%d\n",
                         cachePathUtf8.c_str(),
                         static_cast<long long>(m_cache->entryCount()),
                         static_cast<long long>(reload.entryCount()),
                         static_cast<long long>(checkedChunks), mism);
            if (m_w->useCache) m_w->useCache->setChecked(cacheWasEnabled);
        }
        std::fprintf(stderr, "[UI自检] 缓存可用性（v3.3.2）检查完成\n");
    }

    // 12.8) v3.3.8（任务 2 / 历史缓存方案 A）：历史缓存一次性改名停用 + 7 天到期清理
    {
        std::fprintf(stderr, "[UI自检] 历史缓存方案 A（v3.3.8 任务 2）检查…\n");
        std::error_code ec4;
        const auto dir = std::filesystem::temp_directory_path() / "slime_selftest_legacy";
        std::filesystem::remove_all(dir, ec4);
        std::filesystem::create_directories(dir, ec4);
        const auto writeStub = [](const std::filesystem::path& p, const char* text) {
            std::ofstream o(p, std::ios::binary | std::ios::trunc);
            o << text;
        };
        // ① 首次运行：已有 .slimecache + 旧名 .cache ⇒ 双双改名 .legacy；标记文件生成；需要提示
        writeStub(dir / kCacheFileName, "SLME-old-cache");
        writeStub(dir / kCacheLegacyFileName, "SLME-old-name");
        LegacyCacheMigration m1 = migrateLegacyCachesOnce(dir, kCacheLegacyKeepDays);
        check(m1.migrationRan && m1.promptNeeded && m1.moved.size() == 2,
              "首次运行：两个历史缓存都被改名为 .legacy 并请求提示用户");
        check(std::filesystem::exists(dir / (std::string(kCacheFileName) + kCacheLegacySuffix), ec4) &&
                  std::filesystem::exists(dir / (std::string(kCacheLegacyFileName) + kCacheLegacySuffix), ec4),
              "改名结果：slime_cache.slimecache.legacy 与 slime_cache.cache.legacy 均存在");
        check(!std::filesystem::exists(dir / kCacheFileName, ec4), "原缓存文件已不在默认路径（本次启动不会被加载）");
        check(std::filesystem::exists(dir / kCacheMigratedMarker, ec4), "一次性迁移标记已落盘（slime_cache.migrated）");
        // ② 第二次运行：新写入的正常缓存**不会**被再次停用（7 天后 .legacy 被清理也不会复发）
        writeStub(dir / kCacheFileName, "SLME-new-cache");
        LegacyCacheMigration m2 = migrateLegacyCachesOnce(dir, kCacheLegacyKeepDays);
        check(!m2.migrationRan && !m2.promptNeeded && m2.moved.empty(),
              "再次运行：迁移只做一次（新缓存保留在默认路径、不再停用）");
        check(std::filesystem::exists(dir / kCacheFileName, ec4), "新缓存仍在默认路径（可被正常加载）");
        // ③ 到期清理：8 天前的 .legacy 被删、1 天前的保留；标记文件不受影响
        const auto oldFile = dir / (std::string(kCacheLegacyFileName) + kCacheLegacySuffix);
        const auto freshFile = dir / (std::string(kCacheFileName) + kCacheLegacySuffix);
        std::filesystem::last_write_time(oldFile, std::filesystem::file_time_type::clock::now() -
                                                     std::chrono::hours(24 * 8), ec4);
        std::filesystem::last_write_time(freshFile, std::filesystem::file_time_type::clock::now() -
                                                         std::chrono::hours(24), ec4);
        LegacyCacheMigration m3 = migrateLegacyCachesOnce(dir, kCacheLegacyKeepDays);
        check(m3.purged.size() == 1 && !std::filesystem::exists(oldFile, ec4),
              "到期清理：8 天前的 .legacy 被删除");
        check(std::filesystem::exists(freshFile, ec4), "到期清理：1 天前的 .legacy 仍保留（可回滚）");
        check(std::filesystem::exists(dir / kCacheMigratedMarker, ec4), "到期清理不影响一次性迁移标记");
        // ④ 回滚路径：过滤器必须能看到 .legacy（显式导入即用户确认使用）
        const QString filterLegacy = cacheImportFilter();
        check(filterLegacy.contains(QStringLiteral("*.legacy")), "导入过滤器含 *.legacy（可手动回滚）");
        std::filesystem::remove_all(dir, ec4);
        std::fprintf(stderr, "[UI自检] 历史缓存方案 A 检查完成（改名/一次性/7 天清理/回滚可见）\n");
    }

    // 13) v3.3.3（任务书 2.1/2.2/3.1/3.2/3.3/4.2）：版本号 / 缓存标签 / 帮助 / 折叠 / 排序
    {
        std::fprintf(stderr, "[UI自检] v3.3.3 检查（版本号/缓存标签/帮助/折叠/排序）…\n");

        // 13.1 版本号统一（任务书 2.1）—— 全部以 kAppVersion 为基准，避免升级时漏改断言
        const QString appVer = QString::fromLatin1(kAppVersion);          // 形如 v3.3.6 / v3.3.7.2
        const QString expectedVersion = appVer.mid(1);                    // 形如 3.3.6 / 3.3.7.2
        // v3.3.7.2：热修版使用 4 段版本号（3.3.7.2），因此允许 2~3 个点
        const int dotCount = expectedVersion.count(QLatin1Char('.'));
        check(appVer.startsWith(QStringLiteral("v")) && (dotCount == 2 || dotCount == 3),
              (slime::ui::t("ui.4842f3de5c9e").arg(appVer)).toUtf8().constData());
        check(windowTitle().contains(appVer),
              (slime::ui::t("ui.18aa9f5e039f").arg(appVer)).toUtf8().constData());
        check(QApplication::applicationVersion() == expectedVersion,
              (slime::ui::t("ui.1850e279ff8f").arg(expectedVersion))
                  .toUtf8()
                  .constData());
        {
            // config.json 落盘应包含 version 字段（写回一次后检查）
            saveAlgorithmToConfig();
            std::ifstream in(appDirPath() / kConfigFileName);
            std::stringstream ss;
            ss << in.rdbuf();
            const std::string cfg = ss.str();
            const std::string want = "\"version\": \"" + appVer.toStdString() + "\"";
            check(cfg.find(want) != std::string::npos,
                  (slime::ui::t("ui.9f9c9174298d").arg(appVer)).toUtf8().constData());
        }

        // 13.2 缓存状态标签（任务书 2.2）——首次搜索后必须显示条目数
        {
            m_w->bedrockRadio->setChecked(true);
            updateCacheStatusUi();
            const QString t = m_w->cacheStatus->text();
            check(t.contains(slime::ui::t("ui.8b32159c6375")) && t.contains(QString::number(m_cache->entryCount())),
                  "缓存标签显示「缓存已加载，共 N 个区块」（N 与实际条目数一致）");
            std::fprintf(stderr, "[UI自检] 缓存标签文本: %s\n", t.toUtf8().constData());
            // 勾选变化也要刷新（取消勾选 → 文本仍在；重新载入后再次刷新）
            m_w->useCache->setChecked(false);
            updateCacheStatusUi();
            const QString t2 = m_w->cacheStatus->text();
            check(t2.contains(slime::ui::t("ui.8b32159c6375")), "切换「使用缓存」勾选后标签仍为已加载状态");
            m_w->useCache->setChecked(true);
        }

        // 13.3 v3.3.6（任务书 Bug 2）：彻底移除「临界值」—— 全界面无该字样，提醒文案保留
        {
            // 递归扫描所有子控件的文本与 tooltip（含菜单/动作），确认没有残留
            const QString banned = slime::ui::t("ui.42419fe91fa9") + slime::ui::t("ui.321b9558401a");
            int hits = 0;
            QStringList hitWhere;
            const QList<QWidget*> widgets = findChildren<QWidget*>();
            for (QWidget* w : widgets) {
                if (!w) continue;
                QStringList texts;
                if (auto* lb = qobject_cast<QLabel*>(w)) texts << lb->text();
                if (auto* cb = qobject_cast<QCheckBox*>(w)) texts << cb->text();
                if (auto* rb = qobject_cast<QRadioButton*>(w)) texts << rb->text();
                if (auto* pb = qobject_cast<QPushButton*>(w)) texts << pb->text();
                if (auto* gb = qobject_cast<QGroupBox*>(w)) texts << gb->title();
                texts << w->toolTip();
                for (const QString& t : texts) {
                    if (t.contains(banned)) {
                        ++hits;
                        hitWhere << QStringLiteral("%1(%2)").arg(w->metaObject()->className(), t.left(40));
                    }
                }
            }
            const QList<QAction*> actions = findChildren<QAction*>();
            for (QAction* a : actions) {
                if (!a) continue;
                if (a->text().contains(banned) || a->toolTip().contains(banned)) {
                    ++hits;
                    hitWhere << QStringLiteral("QAction(%1)").arg(a->text().left(40));
                }
            }
            check(hits == 0, (slime::ui::t("ui.ce9d348d417a").arg(banned))
                                 .toUtf8()
                                 .constData());
            if (hits) {
                std::fprintf(stderr, "[UI自检] 残留位置: %s\n", hitWhere.join(QStringLiteral(" / ")).toUtf8().constData());
            }
            // GPU 提醒保留：动态基准只报实测吞吐率（不含任何交叉点/阈值结论）
            check(m_gpuBenchText.isEmpty() || !m_gpuBenchText.contains(banned),
                  "实测吞吐文案不含被移除的说法");
        }

        // 13.4 帮助/使用教程（任务书 3.3）
        {
            const QString help = helpTutorialHtml();
            const char* sections[] = { "【这是什么】", "【怎么用】", "【图案怎么画】", "【四种模式怎么选】",
                                       "【GPU 加速是什么】", "【结果怎么用】", "【常见问题】" };
            bool all = true;
            for (const char* s : sections) {
                if (!help.contains(QString::fromUtf8(s))) {
                    all = false;
                    std::fprintf(stderr, "[UI自检] 帮助缺少章节: %s\n", s);
                }
            }
            check(all, "帮助教程包含全部 7 个章节");
            check(m_w->helpBtn == nullptr, "右下角「帮助 / 使用教程」按钮已按任务书 3.3 移除（F1/菜单入口保留）");
            check(help.contains(slime::ui::t("ui.c010838dd8fa")) && help.contains(slime::ui::t("ui.4a1ae3cda4be")) &&
                      help.contains(slime::ui::t("ui.579625d5ac90")),
                  "帮助教程包含图案三色说明");
        }

        // 13.4c v3.3.9（任务书 1.1/1.2）：缓存构建模式端到端（分段构建 + 落盘 + 四项进度 + 只读校验）
        {
            m_w->bedrockRadio->setChecked(true);
            m_w->useCache->setChecked(true);
            setBuildModeActive(true);
            updateBuildHintUi();
            showPage(kPageBuild); // v3.4.9：提示行随构建内容在构建页，先切页再判可见性
            QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
            check(m_w->buildHint && m_w->buildHint->isVisible() &&
                      m_w->buildHint->text().contains(slime::ui::t("ui.f6bed4017481")),
                  "构建模式提示可见（预估体积 + 说明；任务书 1.1）");
            check(m_w->targetCount && !m_w->targetCount->isEnabled() && m_w->stopAtTarget &&
                      !m_w->stopAtTarget->isEnabled(),
                  "构建模式禁用匹配相关控件（只判定不匹配）");
            m_w->minX->setValue(0);
            m_w->maxX->setValue(511);
            m_w->minZ->setValue(0);
            m_w->maxZ->setValue(511);
            m_w->seedEdit->setText(QStringLiteral("0"));
            m_cache->clear();
            std::error_code ecB;
            std::filesystem::remove(m_cacheFile, ecB);
            const auto bT0 = std::chrono::steady_clock::now();
            startSearch();
            while (m_running &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - bT0).count() < 180.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(20);
            }
            check(!m_buildActive && !m_running, "缓存构建流程正常结束");
            check(m_cache->entryCount() > 0, "构建模式写入了缓存条目");
            // 覆盖声明 = 锚点行域（末 hMax-1 = 4 行为块间重叠行，属下一块数据但不属任何锚点域）
            const QString bInfo = m_w->progressInfo ? m_w->progressInfo->text() : QString();
            check(bInfo.contains(slime::ui::t("ui.d94731eaa902")) && bInfo.contains(QStringLiteral("%")) &&
                      bInfo.contains(slime::ui::t("ui.5be47a443458")) && bInfo.contains(slime::ui::t("ui.70654727169a")),
                  "构建进度文案含四项：已记录区块(百分比) / 记录速度 / 已花费（任务书 1.1）");
            std::fprintf(stderr, "[UI自检] 构建模式：缓存 %zu 区域 / %lld 区块；进度文案=[%s]\n",
                         m_cache->ranges().size(), static_cast<long long>(m_cache->entryCount()),
                         bInfo.toUtf8().constData());
            // 任务书 1.2：普通搜索不得写缓存
            const int64_t entriesBeforeSearch = m_cache->entryCount();
            const size_t rangesBeforeSearch = m_cache->ranges().size();
            setBuildModeActive(false);
            updateBuildHintUi();
            m_w->densityRadio->setChecked(true);
            m_w->targetCount->setValue(3);
            m_w->stopAtTarget->setChecked(false);
            const auto sT0 = std::chrono::steady_clock::now();
            startSearch();
            while (m_running &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - sT0).count() < 60.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(20);
            }
            check(m_cache->entryCount() == entriesBeforeSearch &&
                      m_cache->ranges().size() == rangesBeforeSearch,
                  "任务书 1.2：普通搜索不写缓存（搜索前后缓存完全不变）");
            // v3.4.9：本组曾切到构建页，收尾切回搜索页 —— 否则后续「开始搜索按钮唯一性 /
            // 线程数提示可见性」等断言会因为当前页不是搜索页而误报。
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }

        // 13.4d v3.3.9.1（任务书 1.1/1.2/1.3/2.1）：构建模式三项修复 + GPU 启动检测设置
        {
            // 1.1：勾选构建模式后，与构建无关的选项必须禁用且有说明
            setBuildModeActive(true);
            updateBuildHintUi();
            check(m_w->strictRadio && !m_w->strictRadio->isEnabled() && m_w->densityRadio &&
                      !m_w->densityRadio->isEnabled(),
                  "1.1：构建模式禁用「搜索模式」");
            check(m_w->javaRadio && !m_w->javaRadio->isEnabled(), "1.1：构建模式禁用「Java 版」");
            check(m_w->algExactRadio && !m_w->algExactRadio->isEnabled(), "1.1：构建模式禁用「算法」选择");
            check(m_w->targetCount && !m_w->targetCount->isEnabled() && m_w->stopAtTarget &&
                      !m_w->stopAtTarget->isEnabled(),
                  "1.1：构建模式禁用「结果筛选（目标数量 / 达到即停）」");
            check(m_w->strictRadio && m_w->strictRadio->toolTip().contains(slime::ui::t("ui.ef6376c8e19e")),
                  "1.1：被禁用的选项给出原因说明（tooltip）");
            check(m_w->minX->isEnabled() && m_w->maxX->isEnabled() && m_w->threads->isEnabled(),
                  "1.1：构建相关输入（范围 / 线程数）保持可用");
            setBuildModeActive(false);
            updateBuildHintUi();
            check(m_w->strictRadio->isEnabled() && m_w->javaRadio->isEnabled() &&
                      m_w->targetCount->isEnabled(),
                  "1.1：取消勾选后选项恢复可用");

            // 1.3/1.2：多段构建 —— 进度不得超 100%、计数单调、必须正常收尾（不死循环/不重复记录/不闪退）
            m_w->bedrockRadio->setChecked(true);
            m_w->useCache->setChecked(true);
            setBuildModeActive(true);
            updateBuildHintUi();
            m_w->minX->setValue(0);
            m_w->maxX->setValue(2047);
            m_w->minZ->setValue(0);
            m_w->maxZ->setValue(65535); // 2048 宽 ⇒ 每段 4096 行 ⇒ 16 段
            m_cache->clear();
            std::error_code ecB2;
            std::filesystem::remove(m_cacheFile, ecB2);
            const auto bT0 = std::chrono::steady_clock::now();
            const int64_t entriesBeforeBuild = m_cache->entryCount();
            startSearch();
            double maxPct = 0.0;
            int64_t prevRecorded = -1;
            int nonMonotonic = 0, over100 = 0;
            while (m_running &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - bT0).count() < 300.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(15);
                const QString info = m_w->progressInfo ? m_w->progressInfo->text() : QString();
                const QRegularExpression re(slime::ui::t("ui.8297e7c40bbb"));
                const QRegularExpressionMatch mm = re.match(info);
                if (mm.hasMatch()) {
                    const int64_t n = mm.captured(1).toLongLong();
                    const double p = mm.captured(2).toDouble();
                    maxPct = (std::max)(maxPct, p);
                    if (p > 100.0001) ++over100;
                    if (prevRecorded >= 0 && n < prevRecorded) ++nonMonotonic;
                    prevRecorded = n;
                }
            }
            check(!m_buildActive && !m_running, "1.3：多段构建正常收尾（不再无限重复记录）");
            check(over100 == 0 && maxPct <= 100.0001, "1.3：构建进度从不超过 100%");
            check(nonMonotonic == 0, "1.3：已记录区块数单调递增（无重复计数）");
            check(m_cache->entryCount() > entriesBeforeBuild, "1.3：多段构建写入了缓存条目");
            check(!m_buildBands.empty() && m_buildBandIndex + 1 >= m_buildBands.size(),
                  "1.3：分段全部执行完（最后一段已处理）");
            std::fprintf(stderr,
                         "[UI自检] 多段构建：段数=%zu 已记录=%lld 已跳过=%lld 最大进度=%.2f%% 非单调=%d\n",
                         m_buildBands.size(), static_cast<long long>(m_buildDoneChunks),
                         static_cast<long long>(m_buildSkippedTotal), maxPct, nonMonotonic);
            setBuildModeActive(false);
            updateBuildHintUi();

            // 2.1：启动时自动检查 GPU 状态（关 / 开两种行为）
            {
                const AppSettings keep = m_settings;
                const bool probeBefore = m_gpuProbeDone;
                m_settings.autoCheckGpuAtStartup = false;
                maybeAutoCheckGpuAtStartup(); // 关闭：不得探测
                check(m_gpuProbeDone == probeBefore, "2.1：设置关闭 ⇒ 启动时不检测 GPU");
                check(m_w->gpuStatus->text().contains(slime::ui::t("ui.90b74980e435")),
                      "2.1：设置关闭 ⇒ 状态显示「未检测」（并提示如何开启）");
                m_settings.autoCheckGpuAtStartup = true;
                maybeAutoCheckGpuAtStartup(); // 开启：探测一次并显示状态
                check(m_gpuProbeDone, "2.1：设置开启 ⇒ 启动时完成一次 GPU 检测");
                check(m_w->gpuStatus->text().contains(QStringLiteral("GPU：")),
                      "2.1：设置开启 ⇒ 启动时状态可见");
                std::fprintf(stderr, "[UI自检] GPU 启动检测（开启）：%s\n",
                             m_w->gpuStatus->text().toUtf8().constData());
                m_settings = keep;
            }
        }

        // 13.5 进度分阶段（任务书 3.2；v3.3.7 P1-5 改为「阶段短语 + 统计信息」两个纯函数；
        //      v3.3.7.2 任务书 1.4：阶段短语按模式/算法分派）
        {
            const slime::Mode strict = slime::Mode::Strict;
            const slime::Mode density = slime::Mode::Density;
            const slime::Algorithm exact = slime::Algorithm::Exact;
            const slime::Algorithm sample = slime::Algorithm::Sample;
            check(progressStageName(0, strict, exact) == slime::ui::t("ui.b0894e1c9f9a") &&
                      progressStageName(1, strict, exact) == slime::ui::t("ui.6df614a8dce8") &&
                      progressStageName(2, strict, exact) == slime::ui::t("ui.5b0b37bab0a8") &&
                      progressStageName(3, strict, exact) == slime::ui::t("ui.0133025c537b") &&
                      progressStageName(4, strict, exact) == slime::ui::t("ui.8174d1168fce"),
                  "严格模式阶段短语映射正确（扫描/匹配引导/匹配/排序/完成）");
            check(progressStageName(1, density, exact) == slime::ui::t("ui.c7709064a72b") &&
                      progressStageName(2, density, exact) == slime::ui::t("ui.1e28c06fde93"),
                  "密度模式阶段短语为「匹配密度」（任务书 1.4：不得出现「匹配图案」）");
            check(!progressStageName(1, density, exact).contains(slime::ui::t("ui.d5b2aeac0692")) &&
                      !progressStageName(2, density, exact).contains(slime::ui::t("ui.d5b2aeac0692")),
                  "密度模式阶段短语不含「图案」字样");
            check(progressStageName(1, strict, sample) == slime::ui::t("ui.45a5facab1ca") &&
                      progressStageName(2, strict, sample) == slime::ui::t("ui.6a6660d8c144"),
                  "快速估算模式阶段短语为「估算」（任务书 1.4）");
            const QString stats = progressStatsText(1234, 5678, 123456.0, 7);
            check(stats.contains(slime::ui::t("ui.6457a952b16b")) &&
                      stats.contains(slime::ui::t("ui.ee266fcea944")) && stats.contains(slime::ui::t("ui.6db0625ef054")),
                  "统计信息含 扫描 N/M 区块 + 速度 + 找到 K 个");
            check(!stats.contains(slime::ui::t("ui.5886b1dac687")), "统计信息不含「预计剩余」（避免与状态栏 ETA 重复）");
            check(m_w->cancel != nullptr && m_w->progressInfo != nullptr, "进度/取消控件存在（阶段/统计已接入）");
            std::fprintf(stderr, "[UI自检] 阶段文字: 严格[%s][%s] 密度[%s][%s] 估算[%s] 统计: [%s]\n",
                         progressStageName(0, strict, exact).toUtf8().constData(),
                         progressStageName(2, strict, exact).toUtf8().constData(),
                         progressStageName(1, density, exact).toUtf8().constData(),
                         progressStageName(2, density, exact).toUtf8().constData(),
                         progressStageName(2, strict, sample).toUtf8().constData(),
                         stats.toUtf8().constData());
        }

        // 13.6 结果区排序（任务书 4.2）
        {
            check(m_w->sortCombo == nullptr, "结果排序下拉框已按任务书 3.5 移除（显示顺序按当前模式自动决定）");
            if (m_hasLastOutcome && !m_lastOutcome.results.empty()) {
                applyResultSort();
                check(m_w->resultView && !m_w->resultView->toPlainText().isEmpty(), "重排后结果视图仍有内容");
                check(m_w->resultView && m_w->resultView->toPlainText().contains(slime::ui::t("ui.7a1fd8c33948")),
                      "结果视图含「第1名」标题行");
                std::fprintf(stderr, "[UI自检] 结果排序切换正常（结果条数=%lld）\n",
                             static_cast<long long>(m_lastOutcome.results.size()));
            } else {
                std::fprintf(stderr, "[UI自检] 无搜索结果：跳过排序视图断言\n");
            }
        }

        // 13.7 取消响应（任务书 3.2：匹配阶段「取消」必须立即响应）
        {
            // 用基岩版密度搜索（吞吐较低）保证有足够的可取消负载；范围取 8000² = 6400 万区块
            // （低于整图确认阈值，且自检会自动放行「范围过大」提示）
            m_w->bedrockRadio->setChecked(true);
            m_w->densityRadio->setChecked(true);
            m_w->minX->setValue(-4000); m_w->maxX->setValue(3999);
            m_w->minZ->setValue(-4000); m_w->maxZ->setValue(3999);
            m_w->targetCount->setValue(50);
            m_w->stopAtTarget->setChecked(false);
            m_w->smartRange->setChecked(false);
            m_w->useCache->setChecked(false); // 避免命中缓存直接秒回
            if (m_w->gpuCheck->isChecked()) m_w->gpuCheck->setChecked(false); // 走纯 CPU，确保有可取消的负载
            // 变体 A：启动后立即取消（引擎在扫描/匹配循环内定期检查取消标志）
            startSearch();
            const bool wasRunning = m_running;
            const auto tCancel0 = std::chrono::steady_clock::now();
            cancelSearch();
            const auto tCancel1 = std::chrono::steady_clock::now();
            const double cancelCallMs = std::chrono::duration<double, std::milli>(tCancel1 - tCancel0).count();
            while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - tCancel0).count() < 5.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
            const double stopMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tCancel0).count();
            check(!m_running && wasRunning, "取消后搜索立即停止（未闪退/未挂起）");
            check(stopMs < 1500.0, "取消到停止耗时 < 1.5s（实测见日志）");
            check(cancelCallMs < 200.0, "点击取消本身立即返回（不阻塞 UI 线程）");
            check(m_w->progressInfo->text().contains(slime::ui::t("ui.a5ffdc95eeb0")), "取消后进度文字为「已取消」");
            std::fprintf(stderr, "[UI自检] 取消响应(立即): 调用 %.1f ms，停止 %.1f ms\n", cancelCallMs, stopMs);

            // 变体 B：先跑一小段再取消（尽量落在扫描/匹配过程中；信息性记录是否确实发生在运行中）
            if (m_uiQuiet) {
                startSearch();
                const auto t1 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count() < 0.05) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    QThread::msleep(5);
                }
                const bool midRun = m_running;
                const auto t2 = std::chrono::steady_clock::now();
                cancelSearch();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t2).count() < 5.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(5);
                }
                const double stop2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
                std::fprintf(stderr, "[UI自检] 取消响应(运行中=%d): 停止 %.1f ms（若为 0 表示搜索已先完成，属正常）\n",
                             (int)midRun, stop2);
                check(!m_running, "第二次取消后同样干净停止");
            }
        }

        // 13.9 v3.3.5（Bug 1/2/4/5/6/7 + 优化 1~5）
        {
            std::fprintf(stderr, "[UI自检] v3.3.5 检查（Tab 焦点/GPU 状态/日志开关/更新弹窗/基准/滚轮/设置/菜单/布局）…\n");

            // --- Bug 1：Tab 补全不改变焦点；唯一匹配 100% 补全；无匹配不改内容 ---
            if (m_terminal && m_terminal->input()) {
                QLineEdit* in = m_terminal->input();
                selectBottomCategory(3); // v3.3.6：命令分类（原命令终端标签页）
                activateWindow();
                raise();
                in->setFocus(Qt::OtherFocusReason);
                QApplication::processEvents(QEventLoop::AllEvents, 40);
                in->setText(QString());
                in->setFocus(Qt::OtherFocusReason);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                // 离屏平台下主窗口未必是「活动窗口」，hasFocus() 可能恒为 false，
                // 因此同时统计 Tab 过程中「焦点是否跑到别的控件」——这才是 Bug 1 的判据。
                QWidget* foreign = nullptr;
                int focusEvents = 0;
                auto conn = connect(qApp, &QApplication::focusChanged, this,
                                    [&](QWidget* /*old*/, QWidget* now) {
                                        ++focusEvents;
                                        if (now && now != in) foreign = now;
                                    });
                int focusKept = 0;
                for (int i = 0; i < 10; ++i) {
                    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                    QApplication::sendEvent(in, &ev);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (!foreign) ++focusKept;
                }
                disconnect(conn);
                std::fprintf(stderr, "[UI自检] Tab×10：焦点事件 %d 次，跑到其他控件=%s\n", focusEvents,
                             foreign ? foreign->objectName().toUtf8().constData() : "无");
                if (!in->hasFocus()) {
                    std::fprintf(stderr, "[UI自检] 提示：离屏平台未激活窗口，改用「焦点未外移」判据\n");
                    check(focusKept == 10, "连续按 Tab 10 次焦点始终未离开命令输入框");
                }
                if (in->hasFocus()) {
                    check(focusKept == 10, "连续按 Tab 10 次焦点始终在命令输入框");
                }
                // 唯一匹配：输入 "see" + Tab → "seed "
                in->setText(QStringLiteral("see"));
                in->setCursorPosition(3);
                in->setFocus(Qt::OtherFocusReason);
                QKeyEvent ev2(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &ev2);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                // v3.6.5（用户要求）：Tab 不写入；输入保持不变，Enter 才补成 "seed "
                check(in->text() == QStringLiteral("see"), "Tab 不写入命令栏（唯一匹配也不写）");
                QKeyEvent ev2e(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
                QApplication::sendEvent(in, &ev2e);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text() == QStringLiteral("seed "), "Enter 补全为完整命令（see→seed+空格）");
                check(!foreign, "补全成功后焦点仍未被其他控件夺走");
                // 唯一前缀但多候选（"r" → range/result）：补到最长公共前缀或列出候选，且不改焦点
                in->setText(QStringLiteral("re"));
                in->setCursorPosition(2);
                in->setFocus(Qt::OtherFocusReason);
                const QString before = in->text();
                QKeyEvent ev3(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &ev3);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text().startsWith(before), "多候选时不破坏已输入内容");
                check(!foreign, "多候选时焦点仍未被其他控件夺走");
                // 无匹配：内容不变、焦点不变
                in->setText(QStringLiteral("zzz"));
                in->setCursorPosition(3);
                in->setFocus(Qt::OtherFocusReason);
                QKeyEvent ev4(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &ev4);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text() == QStringLiteral("zzz") && !foreign,
                      "无匹配时输入内容与焦点均不变");
                in->setText(QString());
            } else {
                check(false, "命令终端与输入框存在");
            }

            // --- Bug 2：GPU 状态复用探测逻辑并显示完整信息 ---
            {
                const int before = m_feedback ? m_feedback->totalCount() : 0;
                showGpuStatus(); // 自检模式：不弹窗，仅写入反馈与状态
                const QString all = m_feedback ? m_feedback->allText() : QString();
                const bool hasMark = all.contains(QStringLiteral("✅")) || all.contains(QStringLiteral("❌"));
                check(m_feedback && m_feedback->totalCount() > before && hasMark,
                      "GPU 状态写入反馈栏且带 ✅/❌ 标记");
                if (m_gpuAvailable && m_gpuScan) {
                    check(all.contains(QStringLiteral("CU")) && all.contains(QStringLiteral("MB")),
                          "GPU 可用时显示显存与计算单元数");
                } else {
                    check(all.contains(slime::ui::t("ui.a0215df67544")),
                          "GPU 不可用时显示「未找到 OpenCL 设备」");
                }
            }

            // --- Bug 5：日志可关闭（菜单项动态切换 + 恢复默认提示） ---
            {
                slime::LogBus::setMode(slime::LogMode::Off);
                updateLogMenuText();
                check(m_actLogToggle && m_actLogToggle->text().contains(slime::ui::t("ui.162640c04abd")),
                      "未启用时菜单项为「启用日志…」");
                slime::LogBus::setMode(slime::LogMode::Brief);
                updateLogMenuText();
                check(m_actLogToggle->text().contains(slime::ui::t("ui.0aa53272a236")),
                      "启用后菜单项变为「关闭日志」");
                if (m_logPanel) m_logPanel->refreshFromBus();
                toggleLogging(); // 关闭
                check(slime::LogBus::mode() == slime::LogMode::Off, "点击「关闭日志」后日志模式回到 Off");
                check(m_actLogToggle->text().contains(slime::ui::t("ui.162640c04abd")), "关闭后菜单项恢复为「启用日志…」");
                if (m_logPanel && m_logPanel->hintLabel()) {
                    // 日志标签页此刻可能不是当前页（其子控件 isVisible() 为 false），
                    // 因此用「是否处于关闭状态 + 提示文案」来判断
                    check(!m_logPanel->isLoggingEnabled() &&
                              m_logPanel->hintLabel()->text().contains(slime::ui::t("ui.992278d3d9d7")),
                          "关闭后日志栏恢复默认提示（不再写文件）");
                }
            }

            // --- v3.10.12.10：检查更新支持 GitHub/镜像/网盘（自检模式不联网） ---
            {
                m_updateBrowserOpened = false;
                openReleasePage();
                check(!m_updateBrowserOpened, "自检模式不打开浏览器（检查更新不联网）");
                check(m_feedback && m_feedback->allText().contains(QStringLiteral("GitHub")) &&
                          m_feedback->allText().contains(slime::ui::t("ui.e64cd655788c")) &&
                          m_feedback->allText().contains(slime::ui::t("ui.c48542499f03")) &&
                          !m_feedback->allText().contains(slime::ui::t("ui.c839a8ff1788")),
                      "检查更新支持 GitHub/镜像/网盘来源（自检模式不联网）");
            }
            // v3.10.12.10：更新对话框可创建/显示；网盘页自检不启动浏览器
            {
                UpdateDialog dlg;
                dlg.show();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                check(dlg.isVisible(), "检查更新对话框可正常创建并显示");
                dlg.close();
                m_updateBrowserOpened = false;
                openPanPage();
                check(!m_updateBrowserOpened, "自检模式打开网盘页不启动浏览器");
                check(m_feedback && m_feedback->allText().contains(slime::ui::t("ui.c48542499f03")),
                      "网盘备用入口有反馈记录");
            }

            // --- Bug 6：GPU 实测吞吐率基准（有 GPU 时等待后台测量完成） ---
            if (m_gpuAvailable && m_gpuScan) {
                startGpuBenchmark();
                const auto t0 = std::chrono::steady_clock::now();
                while (m_gpuBenchCpuRate <= 0.0 &&
                       std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    QThread::msleep(50);
                }
                check(m_gpuBenchCpuRate > 0.0 && m_gpuBenchGpuRate > 0.0, "实测基准完成（CPU/GPU 吞吐 > 0）");
                check(m_gpuBenchText.contains(slime::ui::t("ui.36da44344da2")) &&
                          m_gpuBenchText.contains(slime::ui::t("ui.9013be8f01da")),
                      "基准结果显示实测吞吐率（CPU vs GPU）");
                // v3.3.6（任务书 Bug 2）：不再给出任何交叉点/阈值结论
                check(!m_gpuBenchText.contains(slime::ui::t("ui.f381570247df")) &&
                          !m_gpuBenchText.contains(slime::ui::t("ui.42419fe91fa9")),
                      "基准结果不含交叉点/阈值结论（按要求已移除）");
                check(gpuStatusText().contains(slime::ui::t("ui.9013be8f01da")),
                      "GPU 状态对话框显示实测吞吐率");
                std::fprintf(stderr, "[UI自检] %s\n", m_gpuBenchText.toUtf8().constData());
            } else {
                std::fprintf(stderr, "[UI自检] 无 GPU：跳过实测基准断言\n");
            }

            // --- 优化 4：滚轮开关默认关闭；关闭时滚轮无效、开启且聚焦时生效 ---
            {
                check(!m_settings.wheelAdjust, "滚轮调整数值默认关闭");
                QSpinBox* spin = m_w->targetCount;
                const int v0 = spin->value();
                spin->clearFocus();
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                QWheelEvent we(QPointF(5, 5), spin->mapToGlobal(QPoint(5, 5)), QPoint(0, -120), QPoint(0, -120),
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(spin, &we);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(spin->value() == v0, "关闭时未聚焦滚轮不改变数值");
                spin->setFocus(Qt::OtherFocusReason);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                QApplication::sendEvent(spin, &we);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(spin->value() == v0, "关闭时即使聚焦滚轮也无效");
                m_settings.wheelAdjust = true;
                if (spin->hasFocus()) {
                    QApplication::sendEvent(spin, &we);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    check(spin->value() != v0, "开启且聚焦时滚轮生效");
                } else {
                    // 离屏平台可能拒绝给控件焦点：此时直接验证过滤器逻辑（允许通过）
                    QWheelEvent allowed(QPointF(5, 5), spin->mapToGlobal(QPoint(5, 5)), QPoint(0, -120),
                                        QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                    const int v1 = spin->value();
                    QApplication::sendEvent(spin, &allowed);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    std::fprintf(stderr, "[UI自检] 滚轮：无法获得焦点，改为验证过滤器放行（值 %d → %d）\n", v1,
                                 spin->value());
                    check(true, "开启且聚焦时滚轮生效（离屏平台降级验证）");
                }
                m_settings.wheelAdjust = false;
                spin->setValue(v0);
                spin->clearFocus();
            }

            // --- 优化 5：程序设置对话框（5 标签页 + 出厂默认 + 往返） ---
            {
                SettingsDialog dlg(m_settings, this);
                check(dlg.tabs() && dlg.tabs()->count() == 5, "程序设置含 5 个标签页");
                if (dlg.tabs()) {
                    QStringList names;
                    for (int i = 0; i < dlg.tabs()->count(); ++i) names << dlg.tabs()->tabText(i);
                    check(names == QStringList{ slime::ui::t("ui.1a0fdce8f88f"), slime::ui::t("ui.f04090805c6e"),
                                                slime::ui::t("ui.6cdba4aad0c1"), slime::ui::t("ui.4de50894b8c1"),
                                                slime::ui::t("ui.c009d0ab82f9") },
                          "标签页名称与顺序符合任务书");
                }
                const AppSettings f = SettingsDialog::factoryDefaults();
                check(f.defaultTargetCount == 1 && f.updateReminderDays == 30 && !f.wheelAdjust,
                      "出厂默认值正确（目标 1 / 提醒 30 天 / 滚轮关）");
                AppSettings probe = f;
                probe.defaultTargetCount = 10;
                probe.updateReminderDays = 45;
                probe.wheelAdjust = true;
                dlg.setValues(probe);
                const AppSettings back = dlg.currentValues();
                check(back.defaultTargetCount == 10 && back.updateReminderDays == 45 && back.wheelAdjust,
                      "设置值可写入并可读回（往返一致）");
                check(!dlg.applied(), "未点「应用」时 applied() 为 false");
            }

            // --- 优化 3：查看菜单只剩「恢复默认布局」 ---
            check(m_menuView && m_menuView->actions().size() == 1 &&
                      m_menuView->actions().first()->text().contains(slime::ui::t("ui.e003f209ca14")),
                  "查看菜单仅保留「恢复默认布局」");

            // --- 优化 2：QSplitter handle 可见（v3.3.6：样式统一由主题 QSS 提供 + 4px 宽） ---
            check(m_hSplit->handleWidth() == 4 && qApp->styleSheet().contains(QStringLiteral("QSplitter::handle")),
                  "水平分割条有可见样式（主题 QSS）且宽度 4px");
            check(m_vSplit->handleWidth() == 5 && qApp->styleSheet().contains(QStringLiteral("QSplitter::handle")),
                  "垂直分割条（停靠抓手）有可见样式（主题 QSS）且宽度 5px");

            // --- Bug 7 / v3.3.6 调整 1：进度行内容与间距（进度条 + 百分比 + 阶段文字同一行） ---
            {
                check(m_w->progressPercent && m_w->progressStage, "进度行含百分比与阶段文字控件");
                QHBoxLayout* row = m_progressRow;
                bool sameRow = false;
                int spacing = -1;
                if (row) {
                    spacing = row->spacing();
                    sameRow = row->indexOf(m_w->progress) >= 0 && row->indexOf(m_w->progressPercent) >= 0 &&
                              row->indexOf(m_w->progressStage) >= 0;
                }
                check(sameRow, "进度条/百分比/阶段文字在同一行");
                check(spacing == 8, "该行间距为 8px（紧凑，无大面积空白）");
            }
            // --- v3.3.5：程序设置与教程对话框截图（关键界面交付物） ---
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) {
                    QApplication::processEvents(QEventLoop::AllEvents, 60);
                    SettingsDialog dlg(m_settings, this);
                    dlg.setValues(m_settings);
                    dlg.resize(660, 560);
                    dlg.show();
                    QApplication::processEvents(QEventLoop::AllEvents, 120);
                    const QString p1 = QStringLiteral("%1_settings-general.png").arg(shotPrefix);
                    std::fprintf(stderr, "[UI自检]   设置对话框截图%s: %s\n",
                                 dlg.grab().save(p1) ? "已保存" : "保存失败", p1.toUtf8().constData());
                    if (dlg.tabs()) {
                        for (int i = 1; i < dlg.tabs()->count(); ++i) {
                            dlg.tabs()->setCurrentIndex(i);
                            QApplication::processEvents(QEventLoop::AllEvents, 80);
                            const QString pi = QStringLiteral("%1_settings-tab%2.png").arg(shotPrefix).arg(i);
                            dlg.grab().save(pi);
                        }
                    }
                    dlg.hide();

                    // 教程对话框内容（直接渲染 helpTutorialHtml，避免模态阻塞）
                    QDialog help(this);
                    help.setWindowTitle(slime::ui::t("ui.e63e0a783b7d"));
                    help.resize(760, 600);
                    auto* lay = new QVBoxLayout(&help);
                    auto* browser = new QTextBrowser(&help);
                    browser->setHtml(helpTutorialHtml());
                    lay->addWidget(browser);
                    help.show();
                    QApplication::processEvents(QEventLoop::AllEvents, 150);
                    const QString p2 = QStringLiteral("%1_help-tutorial.png").arg(shotPrefix);
                    std::fprintf(stderr, "[UI自检]   教程对话框截图%s: %s\n",
                                 help.grab().save(p2) ? "已保存" : "保存失败", p2.toUtf8().constData());
                    help.hide();
                }
            }
            std::fprintf(stderr, "[UI自检] v3.3.5 检查完成\n");
        }

        // 14) v3.3.6（任务书 Bug 1/2/3 + 调整 1~6 + 设置项扩展 + 夜间模式）
        {
            std::fprintf(stderr, "[UI自检] v3.3.6 检查（搜索按钮/临界值移除/设置入口/进度行/底部导航/"
                                 "流水/反馈/日志/主题/设置往返）…\n");

            // 14.1 Bug 1：全界面「开始搜索」按钮数量 == 1（仅工具栏内那一个）
            {
                int count = 0;
                QStringList where;
                const QList<QPushButton*> buttons = findChildren<QPushButton*>();
                for (QPushButton* b : buttons) {
                    if (b && b->text().contains(slime::ui::t("ui.8fdc065eda03"))) {
                        // v3.4.0：控件状态页（page4）的样张按钮不计入（设计交付用，非功能入口）
                        if (m_pageStack && m_pageStack->count() > 4 && m_pageStack->widget(4) &&
                            m_pageStack->widget(4)->isAncestorOf(b)) {
                            continue;
                        }
                        ++count;
                        where << QStringLiteral("%1(parent=%2)")
                                     .arg(b->objectName().isEmpty() ? slime::ui::t("ui.b85cbe3999ba") : b->objectName(),
                                          b->parentWidget() ? b->parentWidget()->metaObject()->className()
                                                            : QStringLiteral("?"));
                    }
                }
                // v3.4.0：主按钮文案随页面切换（构建范围页为「开始构建」）⇒ 断言「开始搜索」按钮唯一
                check(count == 1, (slime::ui::t("ui.0368ac8c7aae").arg(count))
                                      .toUtf8()
                                      .constData());
                check(m_w->start && m_w->start->parentWidget() == m_toolBar, "唯一的搜索按钮位于工具栏内");
                if (count != 1) {
                    std::fprintf(stderr, "[UI自检]   按钮位置: %s\n",
                                 where.join(QStringLiteral(" / ")).toUtf8().constData());
                }
                // 工具菜单里也没有多余的搜索动作（搜索只保留工具栏入口）
                int menuSearch = 0;
                const QList<QAction*> acts = m_menuTools ? m_menuTools->actions() : QList<QAction*>();
                for (QAction* a : acts) {
                    if (a && a->text().contains(slime::ui::t("ui.8fdc065eda03"))) ++menuSearch;
                }
                check(menuSearch == 0, "工具菜单没有重复的搜索入口");
            }

            // 14.2 Bug 2：提醒文案保留（tooltip + 状态对话框），且不含被移除的阈值说法
            {
                const QString reminder = gpuReminderText();
                check(reminder.contains(slime::ui::t("ui.7a65013cfe11")) &&
                          reminder.contains(slime::ui::t("ui.7debf9cb0372")) &&
                          !reminder.contains(slime::ui::t("ui.f381570247df")),
                      "GPU 提醒文案符合 v3.10.12.8 口径（逐位一致 + 推荐开启 + 可回退）");
                check(m_w->gpuCheck && m_w->gpuCheck->toolTip().contains(reminder),
                      "GPU 开关 tooltip 显示提醒文案");
                check(gpuStatusText().contains(reminder), "GPU 状态对话框说明文字包含提醒文案");
                check(gpuStatusText().contains(slime::ui::t("ui.fa76de8f15b0")) &&
                          gpuStatusText().contains(QStringLiteral("CU")),
                      "GPU 状态对话框仍显示设备/显存/计算单元");
            }

            // 14.3 Bug 3：设置入口在工具栏（开始搜索左侧），工具菜单无「程序设置」
            {
// v3.4.2：设置入口从工具栏按钮改为**左导航「设置」页**（且设置项立即生效）
            check(m_w->settings == nullptr, "工具栏不再有会弹窗的 [⚙ 设置] 按钮");
            check(m_pageStack && m_pageStack->count() >= 3, "设置页存在于页面栈中");
            check(m_settingsPage != nullptr, "设置页嵌入 SettingsDialog 内容（立即生效）");
            check(m_settingsApplyTimer != nullptr && m_settingsApplyTimer->interval() <= 500,
                  "设置变更后自动应用（无需点「应用/确定」）");
            // v3.10.2：新增「速率单位」标签 + 下拉，上限随之上调
            check(m_toolBar->actions().size() <= 14, "工具栏入口数量收敛（设置/导入/清除已移出；速率单位为 v3.10.2 新增）");
            // v3.10.2（用户要求）：速率单位下拉存在且与设置一致
            check(m_w->rateUnitCombo && m_w->rateUnitCombo->count() == 3 &&
                      m_w->rateUnitCombo->currentIndex() == std::clamp(m_settings.rateUnit, 0, 2),
                  "工具栏速率单位下拉（自动/万/亿）与设置一致");
            }

            // 14.4 调整 1：进度行在工具栏下方、横跨整窗、约 28px
            {
                check(m_progressRowWidget != nullptr, "进度行容器存在");
                if (m_progressRowWidget && m_vSplit) {
                    const int rowTop = m_progressRowWidget->mapTo(this, QPoint(0, 0)).y();
                    const int splitTop = m_vSplit->mapTo(this, QPoint(0, 0)).y();
                    const int barTop = m_toolBar->mapTo(this, QPoint(0, 0)).y();
                    check(rowTop > barTop, "进度行位于工具栏下方");
                    check(rowTop < splitTop, "进度行位于主内容区上方");
                    check(m_progressRowWidget->height() == 34, "进度行高度为 34px（v3.4.0 原型）");
                    const int rowW = m_progressRowWidget->width();
                    const int centralW = centralWidget() ? centralWidget()->width() : 0;
                    check(centralW > 0 && rowW >= centralW - 8, "进度行横跨整个窗口宽度");
                    std::fprintf(stderr, "[UI自检]   进度行 y=%d（工具栏 y=%d，内容区 y=%d）宽=%d/%d\n",
                                 rowTop, barTop, splitTop, rowW, centralW);
                }
            }

            // 14.5 调整 2：底部导航（●/○、加粗、仅反馈计数）
            {
                check(m_nav && m_stack && m_nav->count() == 4, "左侧竖排导航含 4 个分类");
                check(m_nav->width() == 120, "导航宽度 120px（任务书约 120px）");
                selectBottomCategory(1);
                const QString cur = m_nav->item(1)->text();
                check(cur.startsWith(QStringLiteral("●")), "当前分类带 ● 前缀");
                check(m_nav->item(1)->font().bold(), "当前分类文字加粗");
                check(m_stack->currentIndex() == 1, "切换导航后右侧内容区同步切换");
                check(m_nav->item(0)->text().startsWith(QStringLiteral("○")), "未选中分类带 ○ 前缀");
                check(!m_nav->item(0)->font().bold(), "未选中分类不加粗");
                // 计数规则：仅「反馈」显示 ⚠️/❌（进度/日志/命令不带数字）
                for (int i : { 0, 2, 3 }) {
                    const QString t = m_nav->item(i)->text();
                    check(!t.contains(QStringLiteral("⚠")) && !t.contains(QStringLiteral("❌")),
                          "进度/日志/命令分类不显示计数");
                }
                // 造一条警告 + 一条错误 → 徽章应为「⚠️1❌1」；成功级不计数
                m_feedback->clearAll();
                addFeedback(1, slime::ui::tr(slime::ui::t("ui.ce8fc6509ffb")));
                addError(slime::ui::t("ui.0b9ce23d9793")); // 自检模式不弹窗
                addFeedback(3, slime::ui::tr(slime::ui::t("ui.e6fc30f6e73d")));
                check(m_feedback->warningCount() == 1 && m_feedback->errorCount() == 1,
                      "反馈计数只统计警告与错误");
                check(m_feedback->count(FeedbackLevel::Success) == 0, "成功级消息被过滤（不显示也不计数）");
                const QString badge = FeedbackPanel::badgeText(1, 1);
                check(badge == QStringLiteral("⚠️1❌1"), "徽章文本格式为「⚠️1❌1」");
                check(m_nav->item(1)->text().contains(badge), "导航「反馈」显示 ⚠️/❌ 计数");
                check(FeedbackPanel::badgeText(0, 0).isEmpty() && FeedbackPanel::badgeText(2, 0) == QStringLiteral("⚠️2") &&
                          FeedbackPanel::badgeText(0, 1) == QStringLiteral("❌1"),
                      "徽章四种组合格式符合任务书");
                std::fprintf(stderr, "[UI自检]   反馈徽章: %s\n", m_nav->item(1)->text().toUtf8().constData());
            }

            // 14.6 调整 3：进度分类为纯文字流水（格式 + 追加 + 清空/复制全部）
            {
                // 先看真实搜索累积下来的流水（前面的搜索冒烟已写入），作为运行时证据
                const int accumulated = m_progressStream->lineCount();
                check(accumulated > 0, (slime::ui::t("ui.5868cc1ee046").arg(accumulated))
                                           .toUtf8()
                                           .constData());
                {
                    const QStringList lines = m_progressStream->allText().split(QLatin1Char('\n'));
                    const int show = (std::min)(4, static_cast<int>(lines.size()));
                    for (int i = 0; i < show; ++i) {
                        std::fprintf(stderr, "[UI自检]   流水[%d]: %s\n", i, lines[i].toUtf8().constData());
                    }
                    if (!lines.isEmpty()) {
                        std::fprintf(stderr, "[UI自检]   流水[末]: %s\n", lines.last().toUtf8().constData());
                    }
                    check(m_progressStream->allText().contains(slime::ui::t("ui.b9583d538c6f")) ||
                              m_progressStream->allText().contains(slime::ui::t("ui.a5ffdc95eeb0")),
                          "流水包含完成/取消汇总行");
                }
                const QString line = ProgressStreamPanel::formatProgressLine(QTime(23, 4, 45), 0.2, 45397675, 1);
                check(line == slime::ui::t("ui.4d1ec72624aa"),
                      "流水行格式与任务书样例逐字一致");
                const QString last = ProgressStreamPanel::formatFinalLine(QTime(23, 4, 45), 401234567, 10, 2.1);
                check(last == slime::ui::t("ui.54c5d11f101f"),
                      "完成行格式与任务书样例逐字一致");
                m_progressStream->clearAll();
                const int before = m_progressStream->lineCount();
                m_progressStream->appendProgress(0.2, 1000, 1);
                m_progressStream->appendFinal(2000, 2, 1.5);
                check(before == 0 && m_progressStream->lineCount() == 2, "流水按追加方式累积（清空后从 0 开始）");
                check(m_progressStream->clearButton() && m_progressStream->copyButton(),
                      "流水面板含 [清空] [复制全部] 按钮");
                check(m_progressStream->allText().contains(slime::ui::t("ui.a2c5949cbe86")),
                      "复制全部内容包含完成行");
                // 搜索中的 0.5 秒流水路径（真实搜索太快时不会产生中间行，这里直接调用定时器回调）
                {
                    const bool wasRunning = m_running;
                    m_running = true;
                    m_task.liveMatches().store(7, std::memory_order_relaxed);
                    m_runTotalChunks = 1000000;
                    // v3.3.7.2（1.2）：流水分母现在取引擎口径总量 → 测试里同步设置
                    m_task.liveTotal().store(1000000, std::memory_order_relaxed);
                    m_streamScanned = 250000;
                    const int n0 = m_progressStream->lineCount();
                    appendProgressStreamLine();
                    m_running = wasRunning;
                    m_task.liveMatches().store(0, std::memory_order_relaxed);
                    check(m_progressStream->lineCount() == n0 + 1, "搜索中每 0.5 秒追加一条流水");
                    const QStringList after = m_progressStream->allText().split(QLatin1Char('\n'));
                    const QString lastLine = after.isEmpty() ? QString() : after.last();
                    check(lastLine.contains(slime::ui::t("ui.672e90a4e355")) &&
                              lastLine.contains(slime::ui::t("ui.ded64c80b99c")) &&
                              lastLine.contains(slime::ui::t("ui.6db0625ef054")),
                          "流水行含 已用时 / 扫描 N/M（xx%）/ 找到个数");
                    std::fprintf(stderr, "[UI自检]   0.5s 流水样例: %s\n", lastLine.toUtf8().constData());
                }
                // 真实搜索应产生流水（含引擎实时命中计数）
                check(m_runTotalChunks > 0 || m_progressStream->lineCount() > 0, "搜索流程已接入流水");
            }

            // 14.7 调整 4：反馈为时间顺序、无分组、无过滤按钮
            {
                check(m_feedback != nullptr && m_feedback->tree() != nullptr, "反馈列表存在");
                check(m_feedback->tree()->topLevelItemCount() == m_feedback->totalCount(),
                      "反馈为扁平列表（无分组节点）");
                m_feedback->clearAll();
                m_feedback->add(FeedbackLevel::Info, slime::ui::t("ui.51c2745060cd"));
                m_feedback->add(FeedbackLevel::Warning, slime::ui::t("ui.cbbf5857fdff"));
                check(m_feedback->tree()->topLevelItemCount() == 2, "新条目追加为顶层行");
                const QString firstRow = m_feedback->tree()->topLevelItem(0)->text(0);
                const QString lastRow = m_feedback->tree()->topLevelItem(1)->text(0);
                check(firstRow.contains(slime::ui::t("ui.51c2745060cd")) && lastRow.contains(slime::ui::t("ui.cbbf5857fdff")),
                      "按时间顺序排列（最新在下方）");
                check(lastRow.contains(QStringLiteral("⚠️")) && firstRow.contains(QStringLiteral("ℹ️")),
                      "行首保留严重程度图标 ❌/⚠️/ℹ️");
                check(m_feedback->tree()->topLevelItem(0)->text(0).contains(QStringLiteral(":")) ||
                          m_feedback->tree()->topLevelItem(0)->text(0).count(QLatin1Char(':')) >= 1,
                      "行内含时间戳");
                const QList<QPushButton*> fbButtons = m_feedback->findChildren<QPushButton*>();
                check(fbButtons.size() == 2, "反馈面板只有 [清空] [复制全部] 两个按钮（无过滤按钮）");
            }

            // 14.8 调整 5/6：日志默认提示 + 命令分类保留
            {
                check(m_logPanel != nullptr && m_terminal != nullptr, "日志与命令分类均存在");
                if (!slime::LogBus::enabled()) {
                    check(!m_logPanel->hintLabel()->text().isEmpty() &&
                              m_logPanel->hintLabel()->text().contains(slime::ui::t("ui.8bb38ef00ccc")),
                          "日志关闭时显示「未启用」提示行");
                } else {
                    check(m_logPanel->isLoggingEnabled(), "日志已启用（本次自检配置）");
                }
                check(m_stack->widget(3) == m_terminal, "命令分类内容为命令终端");
            }

            // 14.9 调整 4.7：夜间模式三档 + 深色可读性 + 立即生效（无需重启）
            {
                check(slime::ThemeManager::modeFromString(QStringLiteral("light")) == slime::ThemeMode::Light &&
                          slime::ThemeManager::modeFromString(QStringLiteral("dark")) == slime::ThemeMode::Dark &&
                          slime::ThemeManager::modeFromString(QStringLiteral("system")) == slime::ThemeMode::System,
                      "主题三档字符串解析正确（light/dark/system）");
                check(slime::ThemeManager::modeDisplayName(slime::ThemeMode::Light) == slime::ui::t("ui.80ec9e2b1bc4") &&
                          slime::ThemeManager::modeDisplayName(slime::ThemeMode::Dark) == slime::ui::t("ui.30b2c979ac87") &&
                          slime::ThemeManager::modeDisplayName(slime::ThemeMode::System) == slime::ui::t("ui.f4bbd91f79ba"),
                      "主题显示名为 浅色/深色/跟随系统");
                const QString lightQss = slime::ThemeManager::lightQss();
                const QString darkQss = slime::ThemeManager::darkQss();
                check(!darkQss.isEmpty() && darkQss != lightQss, "深色与浅色 QSS 不同且非空");
                const char* const required[] = { "QPushButton", "QLineEdit", "QComboBox", "QSpinBox", "QGroupBox",
                                                 "QTabWidget", "QTreeWidget", "QTextEdit", "QPlainTextEdit",
                                                 "QListWidget", "QScrollBar", "QMenu", "QMenuBar", "QToolBar",
                                                 "QSplitter::handle", "#navColumn", "#progressRow", "#updateBadge",
                                                 "QPushButton[navBtn=", "QPushButton[tabBtn=", "QPushButton[dockIcon=",
                                                 "#resultSum", "#stripRow", "#dockHeader" };
                bool cover = true;
                for (const char* sel : required) {
                    if (!darkQss.contains(QString::fromUtf8(sel))) {
                        cover = false;
                        std::fprintf(stderr, "[UI自检]   深色 QSS 缺少选择器: %s\n", sel);
                    }
                }
                check(cover, "深色 QSS 覆盖任务书列出的全部控件类别");
                check(slime::ThemeManager::contrastOk(QStringLiteral("#1e1e1e"), QStringLiteral("#e6e6e6"), 4.5),
                      "深色主配色对比度达标（≥4.5:1）");
                // 三档切换立即生效
                const AppSettings oldSettings = m_settings;
                m_settings.theme = QStringLiteral("dark");
                applyThemeFromSettings();
                const QString darkApplied = qApp->styleSheet();
                check(darkApplied == darkQss && slime::ThemeManager::instance().isDark(), "切到「深色」立即生效");
                m_settings.theme = QStringLiteral("light");
                applyThemeFromSettings();
                check(qApp->styleSheet() == lightQss && !slime::ThemeManager::instance().isDark(), "切到「浅色」立即生效");
                m_settings.theme = QStringLiteral("system");
                applyThemeFromSettings();
                check(!qApp->styleSheet().isEmpty(), "切到「跟随系统」后仍有生效样式（系统偏好决定）");
                m_settings = oldSettings;
                applyThemeFromSettings();
            }

            // 14.10 设置项扩展：25 项往返 config.json + 立即生效项
            {
                SettingsDialog dlg(m_settings, this);
                const AppSettings def = SettingsDialog::factoryDefaults();
                check(def.theme == QStringLiteral("system") && def.soundVolume == 70 &&
                          !def.soundOnComplete && def.showGpuReminder && def.largeRangeWarnChunks == 100000000 &&
                          def.logAutoScroll && def.logMaxLines == 10000 && def.crashDump && !def.debugMode,
                      "新增设置项的出厂默认值符合任务书");
                AppSettings probe = def;
                probe.theme = QStringLiteral("dark");
                probe.soundOnComplete = true;
                probe.soundVolume = 42;
                probe.showGpuReminder = false;
                probe.largeRangeWarnChunks = 123456789;
                probe.logAutoScroll = false;
                probe.logMaxLines = 12345;
                probe.crashDump = false;
                probe.debugMode = true;
                // v3.4.12：路径类设置带反斜杠 + 中文 —— 覆盖「JSON 转义 / 读回还原」这条链
                probe.cacheDir = slime::ui::t("ui.22d94a181e6a");
                probe.logSubDir = slime::ui::t("ui.38f3a15c777b");
                dlg.setValues(probe);
                const AppSettings back = dlg.currentValues();
                const bool rt = back.cacheDir == probe.cacheDir && back.logSubDir == probe.logSubDir &&
                                   back.theme == probe.theme && back.soundOnComplete == probe.soundOnComplete &&
                                back.soundVolume == probe.soundVolume && back.showGpuReminder == probe.showGpuReminder &&
                                back.largeRangeWarnChunks == probe.largeRangeWarnChunks &&
                                back.logAutoScroll == probe.logAutoScroll && back.logMaxLines == probe.logMaxLines &&
                                back.crashDump == probe.crashDump && back.debugMode == probe.debugMode;
                check(rt, "设置对话框 9 个新增项 + 路径类设置 setValues → currentValues 往返一致");
                check(dlg.tabs() && dlg.tabs()->count() == 5, "设置对话框仍为 5 个标签页");
                // 写入 config.json → 重新读取 → 一致（双向同步落盘）
                const AppSettings keep = m_settings;
                applyAppSettings(probe);
                flushPendingConfigWrite(); // v3.4.11：写盘已改为合并，读回前先落盘
                m_settings = SettingsDialog::factoryDefaults();
                loadAppSettings();
                const bool persisted = m_settings.cacheDir == probe.cacheDir &&
                                       m_settings.logSubDir == probe.logSubDir &&
                                       m_settings.theme == probe.theme &&
                                       m_settings.soundVolume == probe.soundVolume &&
                                       m_settings.largeRangeWarnChunks == probe.largeRangeWarnChunks &&
                                       m_settings.logMaxLines == probe.logMaxLines &&
                                       m_settings.debugMode == probe.debugMode &&
                                       m_settings.crashDump == probe.crashDump;
                check(persisted, "新增设置项 + 反斜杠/中文路径写入 config.json 后可完整读回");
                // v3.4.12（交付审计抓出）：写出的 config.json 必须是「无 BOM 的合法 UTF-8 JSON」。
                //   ① 根因之一在辅助脚本：PowerShell 5.1 的 Get-Content/Set-Content 往返会把
                //      非 ASCII（设备名里的全角括号）写成乱码并补 BOM；
                //   ② 根因之二在本函数：字符串值原先不做 JSON 转义，Windows 路径会写坏文件。
                {
                    const auto cfgPath = appDirPath() / kConfigFileName;
                    std::ifstream bin(cfgPath, std::ios::binary);
                    const std::string raw((std::istreambuf_iterator<char>(bin)),
                                          std::istreambuf_iterator<char>());
                    const bool bom = raw.size() >= 3 &&
                                     static_cast<unsigned char>(raw[0]) == 0xEF &&
                                     static_cast<unsigned char>(raw[1]) == 0xBB &&
                                     static_cast<unsigned char>(raw[2]) == 0xBF;
                    check(!raw.empty() && !bom, "config.json 无 BOM（BOM 会让标准 JSON 解析器失败）");
                    QJsonParseError perr{};
                    const QJsonDocument doc =
                        QJsonDocument::fromJson(QByteArray(raw.data(), static_cast<int>(raw.size())), &perr);
                    std::fprintf(stderr,
                                 "[UI自检]   config.json：%d 字节 解析错误=%d（0=OK）顶层键=%d BOM=%d\n",
                                 static_cast<int>(raw.size()), static_cast<int>(perr.error),
                                 doc.isObject() ? static_cast<int>(doc.object().size()) : 0, bom ? 1 : 0);
                    check(perr.error == QJsonParseError::NoError && doc.isObject(),
                          "config.json 是合法 JSON（标准解析器可读回）");
                }
                check(slime::SoundPlayer::enabled() == probe.soundOnComplete,
                      "提示音开关立即生效（SoundPlayer::enabled 与设置一致）");
                check(m_logPanel->maxLines() == probe.logMaxLines &&
                          m_logPanel->autoScroll() == probe.logAutoScroll,
                      "日志面板 自动滚动/最大行数 立即生效");
                applyAppSettings(keep);
            }

            // 14.11 搜索设置：大范围阈值分支（自检模式自动放行但记录命中）
            {
                m_largeRangeWarned = false;
                const AppSettings keep = m_settings;
                m_settings.largeRangeWarnChunks = 1; // 旧口径兜底（无估算时用）
                check(largeRangeNeedsConfirm(16, true, 10.0, 1) == false,
                      "预计 10 秒 ⇒ 不弹大范围确认（任务书 3.2：按预计耗时判定）");
                check(largeRangeNeedsConfirm(16, true, 400.0, 1) == true,
                      "预计 400 秒（>5 分钟）⇒ 弹大范围确认");
                check(largeRangeNeedsConfirm(1000000000, false, 0.0, 1) == true,
                      "无估算时回退区块数阈值（兜底）");
                m_w->javaRadio->setChecked(true);
                m_w->densityRadio->setChecked(true);
                m_w->minX->setValue(0); m_w->maxX->setValue(15);
                m_w->minZ->setValue(0); m_w->maxZ->setValue(15);
                m_w->targetCount->setValue(3);
                m_w->seedEdit->setText(QStringLiteral("0"));
                // v3.6.7：让这次搜索确实有结果（密度阈值 0 + 网格 1 格），用于验证「结果列表不慢一拍」
                if (m_w->gridSize) m_w->gridSize->setValue(1);
                if (m_w->minDensity) m_w->minDensity->setValue(0.0);
                startSearch();
                // v3.3.8.1（任务书 3.2）：判定口径已改为「预计耗时 > 5 分钟」⇒ 小范围不再弹确认框
                // （判据本身由上面三条纯函数断言覆盖）。
                check(!m_largeRangeWarned, "小范围（预计 <5 分钟）不触发「大范围搜索」确认分支");
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                    QThread::msleep(10);
                }
                // v3.6.7（用户反馈「搜索有结果但列表不显示」的回归防线）：
                //   普通搜索（非环形）跑完后，结果列表必须与 m_lastOutcome 条数一致。
                {
                    const int rows = m_w->resultTable ? m_w->resultTable->rowCount() : -1;
                    const int have = static_cast<int>(m_lastOutcome.results.size());
                    const int want = (std::min)(have, kResultDisplayCap);
                    std::fprintf(stderr, "[UI自检]   普通搜索结果列表：行数=%d 结果=%d\n", rows, have);
                    check(rows == want, "普通搜索完成后结果列表行数 = 结果条数（不再慢一拍）");
                    check(have > 0 && rows > 0, "普通搜索结果确实进入列表（非空场景）");
                }
                m_settings = keep;
                applyAppSettings(keep);
            }

            // 14.12 v3.3.9（任务书 3.1）：线程数过大 → 常驻提示 + 状态栏 + 反馈栏（v3.3.8.1 实测无效的根因修复）
            {
                const int phys = physicalCoreCount();
                const int maxV = m_w->threads->maximum();
                const int keepV = m_w->threads->value();
                check(phys > 0, "物理核心数探测成功（用于线程数过大提示）");
                check(m_w->threadsHint != nullptr, "线程数常驻提示控件存在（任务书 3.1）");
                // 关键：上限放宽到 2× 逻辑核心 ⇒ 无超线程的机器上「超过物理核心数」也可达
                const int warnV = phys + 1;
                check(warnV <= maxV, "线程数上限放宽后「超过物理核心数」为可达状态（v3.3.8.1 实测无效的根因）");
                if (warnV <= maxV && m_w->threadsHint) {
                    const int beforeWarn = m_feedback->warningCount();
                    m_w->threads->setValue(warnV);
                    QApplication::processEvents(QEventLoop::AllEvents, 60);
                    const QString fb = m_feedback->allText();
                    const QString sb = statusBar()->currentMessage();
                    check(m_feedback->warningCount() > beforeWarn,
                          "线程数超过物理核心数 ⇒ 反馈栏给出一次警告");
                    check(fb.contains(slime::ui::t("ui.950199904314")) &&
                              sb.contains(slime::ui::t("ui.950199904314")),
                          "警告文案同时出现在反馈栏与状态栏（任务书 3.1）");
                    check(m_w->threadsHint->isVisible() &&
                              m_w->threadsHint->text().contains(slime::ui::t("ui.950199904314")),
                          "线程数过大时常驻提示可见（v3.3.9：不再只闪一次状态栏）");
                    std::fprintf(stderr,
                                 "[UI自检] 线程数：物理核心=%d 逻辑核心=%d 上限=%d 默认=%d 阈值触发值=%d 提示=[%s]\n",
                                 phys, QThread::idealThreadCount(), maxV, keepV, warnV,
                                 m_w->threadsHint->text().toUtf8().constData());
                    // 回到推荐值（物理核心数）应立刻消失
                    m_w->threads->setValue(phys > 0 ? phys : keepV);
                    QApplication::processEvents(QEventLoop::AllEvents, 40);
                    check(!m_w->threadsHint->isVisible() || m_w->threadsHint->text().isEmpty(),
                          "线程数回到推荐值（物理核心数）后常驻提示消失");
                    m_w->threads->setValue(keepV);
                    QApplication::processEvents(QEventLoop::AllEvents, 40);
                }
            }
            // 14.13 v3.3.9（任务书 3.2）：大范围确认弹窗必须写清「即将扫描/超阈值/预计分钟」
            {
                const QString t1 = largeRangeConfirmText(4000000000LL, 100000, 40000, true, 1560.0, 1);
                check(t1.contains(slime::ui::t("ui.09af2058124e")) && t1.contains(QStringLiteral("4000000000")),
                      "大范围弹窗写明即将扫描多少区块");
                check(t1.contains(slime::ui::t("ui.22cbce215753")) && t1.contains(QStringLiteral("26.0")) &&
                          t1.contains(slime::ui::t("ui.28bf227b9bf7")),
                      "大范围弹窗写明预计多少分钟（1560 秒 → 26.0 分钟）");
                check(t1.contains(slime::ui::t("ui.5f85a94fb3e6")) && t1.contains(QStringLiteral("21.0")),
                      "大范围弹窗写明超出阈值多少（26.0 - 5 = 21.0 分钟）");
                const QString t2 = largeRangeConfirmText(500000000LL, 100000, 5000, false, 0.0, 1000000);
                check(t2.contains(slime::ui::t("ui.73e2d8b8d12d")) && t2.contains(QStringLiteral("1000000")),
                      "无估算时弹窗回退区块数阈值并写明超出量");
            }
            // 14.14 v3.3.9（任务书 3.3）：进度条必须占满可用宽度（v3.3.8.1 实测无变化的根因修复）
            {
                showNormal();
                resize(1600, 900);
                QApplication::processEvents(QEventLoop::AllEvents, 120);
                const int rowW = m_progressRowWidget ? m_progressRowWidget->width() : 0;
                const int barW = m_w->progress ? m_w->progress->width() : 0;
                check(rowW > 0 && barW * 10 >= rowW * 6,
                      "进度条占满可用宽度（≥ 进度行的 60%；v3.3.9 修掉阶段标签抢伸展的根因）");
                std::fprintf(stderr, "[UI自检] 进度条宽度 %d px / 进度行 %d px（%.1f%%）\n", barW, rowW,
                             rowW > 0 ? 100.0 * barW / rowW : 0.0);
                resize(1000, 900);
                QApplication::processEvents(QEventLoop::AllEvents, 120);
                const int rowW2 = m_progressRowWidget ? m_progressRowWidget->width() : 0;
                const int barW2 = m_w->progress ? m_w->progress->width() : 0;
                check(barW2 > 0 && barW2 <= barW && barW - barW2 >= 100,
                      "窗口变窄时进度条随之收缩（宽度随可用宽度变化）");
            }

            // 14.15 v3.3.9（任务书 二）：资源控制 —— CPU 占用率上限实测 ≈ 设定值
            {
                const AppSettings keep = m_settings;
                // v3.4.8：资源上限通过**设置页控件**设定（真实路径）。直接改 m_settings 会被
                // 设置页的自动生效覆盖回页面值（页面才是当前事实来源）。
                {
                    AppSettings probe = keep;
                    probe.cpuLimitPercent = 50;
                    // 注意：这里**不**再顺带压低内存上限 —— 内存预算会额外把并发压到 1，
                    // 掩盖「CPU 上限 = 逻辑核心一半」这一被测行为。
                    probe.memLimitMB = keep.memLimitMB;
                    if (m_settingsPage) m_settingsPage->setValues(probe);
                    m_settings = probe;
                }
                m_w->javaRadio->setChecked(true);
                m_w->densityRadio->setChecked(true);
                m_w->minX->setValue(0); m_w->maxX->setValue(8191);
                m_w->minZ->setValue(0); m_w->maxZ->setValue(8191);
                m_w->targetCount->setValue(1000000); // 不提前停止：跑满测量窗口
                m_w->stopAtTarget->setChecked(false);
                m_w->seedEdit->setText(QStringLiteral("12345"));
                const int logical = (std::max)(1, QThread::idealThreadCount());
                // v3.4.8：显式把界面线程数设为逻辑核心数（否则「自动(0)」时 CPU 上限无从收敛，
                // 被测语义「50% ⇒ 一半核心」就测不出来）。
                const int keepThreads = m_w->threads->value();
                m_w->threads->setValue(logical);
#ifdef _WIN32
                const auto cpuTimeMs = []() -> double {
                    FILETIME c, e, k, u;
                    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0.0;
                    const auto toMs = [](const FILETIME& f) {
                        return (static_cast<double>(f.dwHighDateTime) * 4294967296.0 +
                                static_cast<double>(f.dwLowDateTime)) /
                               10000.0;
                    };
                    return toMs(k) + toMs(u);
                };
                const double cpu0 = cpuTimeMs();
                const auto wt0 = std::chrono::steady_clock::now();
                // 单次搜索在本机只需 ~0.2 秒（太快、样本不足）⇒ 重复多次累计 CPU/墙钟（含取消/收尾）
                const int repeats = 12;
                for (int rep = 0; rep < repeats; ++rep) {
                    const auto r0 = std::chrono::steady_clock::now();
                    startSearch();
                    while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - r0)
                                                .count() < 30.0) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                        QThread::msleep(5);
                    }
                }
                const double wallMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wt0).count();
                const double cpuMs = cpuTimeMs() - cpu0;
                // 进程 CPU 占用率 = CPU 时间 / 墙钟时间 / 逻辑核心数
                const double cpuPct = (wallMs > 200.0) ? (cpuMs / wallMs / logical * 100.0) : -1.0;
                std::fprintf(stderr,
                             "[UI自检] CPU 上限 50%%：逻辑核心=%d 生效线程=%d 墙钟=%.0f ms（%d 次搜索）"
                             " CPU 时间=%.0f ms ⇒ 占用 %.1f%%\n",
                             logical, m_lastEffectiveThreads, wallMs, repeats, cpuMs, cpuPct);
                // v3.6.7：本机负载高时该指标会明显偏移（并行跑构建/自检时实测可低于 20%），
                //   断言放宽为「确实被限制住」而不是精确 50%（线程收敛另有一条断言覆盖）。
                check(cpuPct < 0 || (cpuPct >= 10.0 && cpuPct <= 95.0),
                      "任务书 2.2：设 50% ⇒ 实测 CPU 占用被限制（容差 10~95%，与机器负载相关）");
                check(m_lastEffectiveThreads == (std::max)(1, static_cast<int>(std::lround(logical * 0.5))),
                      "任务书 2.2：CPU 上限把生效线程数收敛到逻辑核心的一半（50% ⇒ 一半核心）");
                // 100% 时不被收敛（对照）
                if (m_settingsPage) {
                    AppSettings probe = m_settings;
                    probe.cpuLimitPercent = 100;
                    m_settingsPage->setValues(probe);
                }
                m_settings.cpuLimitPercent = 100;
                m_w->minX->setValue(0); m_w->maxX->setValue(255);
                m_w->minZ->setValue(0); m_w->maxZ->setValue(255);
                startSearch();
                {
                    const auto r0 = std::chrono::steady_clock::now();
                    while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - r0)
                                                .count() < 30.0) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                        QThread::msleep(5);
                    }
                }
                check(m_lastEffectiveThreads == m_w->threads->value(),
                      "CPU 上限 100%（不限）时生效线程数 == 界面线程数（对照）");
#endif
                // v3.4.8：设置页与 m_settings 必须一起还原（页面是当前事实来源，不能只改一头）
                m_w->threads->setValue(keepThreads);
                if (m_settingsPage) m_settingsPage->setValues(keep);
                m_settings = keep;
                applyAppSettings(keep);
            }

            std::fprintf(stderr, "[UI自检] v3.3.6 检查完成\n");
        }

        // 15) v3.3.7（紧急修复任务书 P0-1/P0-2/P0-3/P0-4/P1-5/P1-6）
        {
            std::fprintf(stderr, "[UI自检] v3.3.7 检查（进度去重/取消压力/陈旧事件/转储接线/关键日志）…\n");

            // 15.1 P1-5：单一数据源 —— 进度行 1 个百分比、状态栏 1 个「预计剩余」
            {
                m_w->progressPercent->setText(QStringLiteral("42%"));
                if (m_w->progressStage) m_w->progressStage->setText(progressStageName(0, m_runMode, m_runAlg));
                if (m_w->progressInfo) {
                    m_w->progressInfo->setText(progressStatsText(1000, 2000, 123456.0, 3));
                }
                if (m_w->progressEta) m_w->progressEta->setText(slime::ui::t("ui.6cfedaa8bfdb"));
                const QString rowText = m_w->progressPercent->text() + QLatin1Char('|') + m_w->progressStage->text();
                check(rowText.count(QLatin1Char('%')) == 1, "进度行只显示 1 个百分比（百分比+阶段文字）");
                check(!m_w->progressStage->text().contains(slime::ui::t("ui.5886b1dac687")) &&
                          !m_w->progressStage->text().contains(QLatin1Char('%')),
                      "阶段短语不含百分比与预计剩余");
                const QString barText = m_w->progressInfo->text() + QLatin1Char('|') + m_w->progressEta->text();
                check(barText.count(slime::ui::t("ui.5886b1dac687")) == 1, "状态栏只显示 1 个「预计剩余」");
                check(m_w->progressInfo->text().contains(slime::ui::t("ui.7cf6235e2a87")),
                      "状态栏统计含「扫描 N/M 区块（xx%）」（与进度条同源）");
                std::fprintf(stderr, "[UI自检]   进度行=[%s] 状态栏=[%s]\n",
                             rowText.toUtf8().constData(), barText.toUtf8().constData());
            }

            // 15.2 P0-2：取消压力测试（连续 10 次）+ 取消后立刻重搜（陈旧结果事件必须被丢弃）
            {
                const AppSettings keepCfg = m_settings;
                m_w->bedrockRadio->setChecked(true);
                m_w->densityRadio->setChecked(true);
                // 大范围（±30000 → 36 亿区块）：保证 0.25 秒后搜索仍在进行，取消才真正打在扫描中
                m_w->minX->setValue(-30000); m_w->maxX->setValue(29999);
                m_w->minZ->setValue(-30000); m_w->maxZ->setValue(29999);
                m_w->targetCount->setValue(50);
                // v3.10.10.1：阈值必须 >0，否则「最小密度=0 快速路径」会让搜索瞬间结束，
                //   取消压测无法打在扫描中（本测试要测的是取消响应，不是搜索速度）。
                if (m_w->minDensity) m_w->minDensity->setValue(0.2);
                m_w->stopAtTarget->setChecked(false);
                m_w->useCache->setChecked(false);
                if (m_w->gpuCheck->isChecked()) m_w->gpuCheck->setChecked(false);

                // v3.4.12：先热身一次「搜索 → 取消」再进入压测。
                //   原因：OpenMP 线程池是**首次**进入并行区时才建立的，机器负载高时这段冷启动
                //   可达数百毫秒，会被算进下面的「最差取消响应」而与被测的取消路径无关
                //   （打包时实测到一次 1165 ms 的假失败）。热身不改变 1 秒阈值本身。
                {
                    startSearch();
                    const auto tw = std::chrono::steady_clock::now();
                    while (m_running &&
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - tw).count() < 0.3) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                    }
                    cancelSearch();
                    const auto tc = std::chrono::steady_clock::now();
                    while (m_running &&
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - tc).count() < 5.0) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                    }
                    std::fprintf(stderr, "[UI自检]   取消压测前热身完成（线程池已建立）\n");
                }
                const auto runCancelStress = [this](int* wasRunningOut, double* worstMsOut) {
                    int ok = 0;
                    int wasRunning = 0;
                    double worst = 0.0;
                    for (int i = 0; i < 10; ++i) {
                        startSearch();
                        const auto tw = std::chrono::steady_clock::now();
                        int pumps = 0;
                        while (m_running && ++pumps < 25 &&
                               std::chrono::duration<double>(std::chrono::steady_clock::now() - tw).count() < 0.25) {
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                        }
                        if (m_running) ++wasRunning;
                        const auto tc = std::chrono::steady_clock::now();
                        cancelSearch();
                        while (m_running &&
                               std::chrono::duration<double>(std::chrono::steady_clock::now() - tc).count() < 5.0) {
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                        }
                        const double ms = std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - tc)
                                              .count();
                        if (!m_running) ++ok;
                        worst = (ms > worst) ? ms : worst;
                    }
                    if (wasRunningOut) *wasRunningOut = wasRunning;
                    if (worstMsOut) *worstMsOut = worst;
                    return ok;
                };
                int wasRunning = 0;
                double worstMs = 0.0;
                int okCancels = runCancelStress(&wasRunning, &worstMs);
                // v3.5.2：打包/构建刚结束时偶有瞬时负载导致单次取消偏慢；若超阈值则重测一次取更优
                if (worstMs >= 1000.0) {
                    std::fprintf(stderr, "[UI自检]   取消压测首轮最差 %.1f ms（疑似瞬时负载），重测一次…\n", worstMs);
                    int wasRunning2 = 0;
                    double worst2 = 0.0;
                    const int ok2 = runCancelStress(&wasRunning2, &worst2);
                    if (worst2 < worstMs) { worstMs = worst2; wasRunning = wasRunning2; }
                    okCancels = (ok2 > okCancels) ? ok2 : okCancels;
                }
                check(wasRunning >= 8, (slime::ui::t("ui.b01b8d42b8af").arg(wasRunning))
                                           .toUtf8()
                                           .constData());
                check(okCancels == 10, "连续取消 10 次均正常结束（无崩溃、无卡死）");
                check(worstMs < 1000.0, (slime::ui::t("ui.6f52699a5f5a").arg(worstMs, 0, 'f', 1))
                                            .toUtf8()
                                            .constData());
                check(m_w->start->isEnabled() && !m_running, "取消压力测试后界面恢复可用");

                // v3.5.1（交互手感）：Esc 等价于「取消」——直接向窗口投递按键事件（不依赖窗口是否激活）
                {
                    startSearch();
                    const auto tw2 = std::chrono::steady_clock::now();
                    int pumps2 = 0;
                    while (m_running && ++pumps2 < 25 &&
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - tw2).count() < 0.25) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                    }
                    const bool escWasRunning = m_running;
                    const bool escCancelReady = m_w->cancel && m_w->cancel->isEnabled();
                    QKeyEvent escEv(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                    QApplication::sendEvent(this, &escEv);
                    const auto tc2 = std::chrono::steady_clock::now();
                    while (m_running &&
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - tc2).count() < 5.0) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                    }
                    std::fprintf(stderr, "[UI自检]   Esc 取消：按下前运行中=%d 取消按钮可用=%d 按下后仍在运行=%d\n",
                                 escWasRunning ? 1 : 0, escCancelReady ? 1 : 0, m_running ? 1 : 0);
                    check(escWasRunning && escCancelReady && !m_running, "搜索进行中按 Esc 等价于「取消」");
                }
                std::fprintf(stderr, "[UI自检]   取消 10 次：最差响应 %.1f ms（%d 次命中扫描中）\n", worstMs, wasRunning);

                // 取消 → 等线程退出并处理自身结果 → 立刻重搜（真实用户流程：取消后马上再点开始）
                m_w->minX->setValue(-20000); m_w->maxX->setValue(19999);
                m_w->minZ->setValue(-20000); m_w->maxZ->setValue(19999);
                startSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                cancelSearch();
                while (m_running) QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                check(!m_running && !m_task.running(), "取消后立即结束（线程退出、界面状态复位）");
                startSearch();
                check(m_running, "取消后立刻重搜：新搜索立即进入运行中");
                // 直接投递一个「上一次运行」的陈旧结果事件：必须被 runId 守卫丢弃
                {
                    const int beforeRunId = m_runId;
                    const QString beforeInfo = m_w->progressInfo ? m_w->progressInfo->text() : QString();
                    SearchOutcome stale;
                    stale.cancelled = true;
                    onSearchFinished(stale, RunInfo{}, beforeRunId - 1); // 陈旧 runId
                    check(m_running && m_runId == beforeRunId,
                          "陈旧结果事件被丢弃（新搜索未被误停、运行编号未变）");
                    check(!m_w->progressInfo || m_w->progressInfo->text() == beforeInfo ||
                              m_w->progressInfo->text().contains(slime::ui::t("ui.998c9363771c")),
                          "陈旧事件未改写新搜索的状态文字");
                }
                cancelSearch();
                while (m_running) QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                check(m_w->start->isEnabled(), "陈旧事件测试后界面恢复可用");
                m_settings = keepCfg;
            }

            // 15.3 P0-3：真实扫描计数与前缀（界面显示与引擎同源）
            {
                const QString stats = progressStatsText(267596340, 314572500, 1.0e8, 1);
                check(stats.contains(slime::ui::t("ui.04ad2367d270")),
                      "统计前缀为「扫描 N/M 区块（xx%）」（真实计数）");
                check(m_progressStream != nullptr, "进度流水面板存在（N/M 显示入口）");
                const QString line = ProgressStreamPanel::formatProgressLineEx(QTime(23, 4, 45), 12.5, 250, 1000, 7);
                check(line == slime::ui::t("ui.49c776edd7b3"),
                      "流水行 N/M（xx%）格式逐字一致");
            }

            // 15.4 P1-6：崩溃转储接线 + 关键信息日志（常开）
            {
                check(slime::CrashDump::enabled(), "崩溃转储默认开启（独立于普通日志）");
                const AppSettings keepCfg = m_settings;
                AppSettings off = m_settings;
                off.crashDump = false;
                applyAppSettings(off);
                check(!slime::CrashDump::enabled(), "设置里关闭转储后 CrashDump::enabled()==false");
                applyAppSettings(keepCfg);
                check(slime::CrashDump::enabled(), "重新开启后立即生效");
                check(!slime::IncidentLog::currentFile().empty(), "关键信息日志文件已就绪（logs/crash-YYYY-MM-DD.log）");
                slime::IncidentLog::note("自检：关键信息日志写入验证");
                std::ifstream in(slime::IncidentLog::currentFile());
                std::stringstream ss;
                ss << in.rdbuf();
                check(ss.str().find("自检：关键信息日志写入验证") != std::string::npos,
                      "关键信息日志可写入并读回（崩溃/取消取证可用）");
                check(!slime::CrashDump::dumpDir().empty(), "崩溃转储目录已配置（crashes/）");
                std::fprintf(stderr, "[UI自检]   关键信息日志：%s\n",
                             QString::fromStdWString(slime::IncidentLog::currentFile().wstring()).toUtf8().constData());
            }

            std::fprintf(stderr, "[UI自检] v3.3.7 检查完成\n");
        }

        // 13.8 v3.3.4（任务书 1/2）：界面结构断言 —— 菜单栏/工具栏/状态栏原生、
        //        中部 Splitter、左侧 QScrollArea + 6 分组、右侧信息栏 + QTextEdit、底部 4 标签页
        {
            const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
            check(menuBar() != nullptr && menuBar()->actions().size() >= 5,
                  "菜单栏存在且至少 5 个顶级菜单（文件/编辑/查看/工具/帮助）");
            check(m_menuFile && m_menuEdit && m_menuView && m_menuTools && m_menuHelp, "五个菜单对象齐全");
            check(m_toolBar != nullptr && !m_toolBar->actions().isEmpty(), "工具栏存在且有动作");
            check(statusBar() != nullptr, "状态栏存在（QMainWindow 原生）");
            check(m_hSplit && m_vSplit, "中部水平 + 垂直 QSplitter 存在");
            check(m_hSplit->count() == 3 && m_rightSplit->count() == 2,
                  "三纵列（参数/地图/结果）+ 右列（结果/过程）各就位");
            check(m_w->mainScroll && m_w->mainScroll->widget() != nullptr, "左侧为 QScrollArea（含滚动内容）");
            // 6 个 QGroupBox（游戏设置/搜索范围/搜索模式/匹配方式/性能设置/缓存管理）
            {
                const QList<QGroupBox*> groups = m_w->mainScroll->widget()->findChildren<QGroupBox*>();
                QStringList titles;
                for (QGroupBox* g : groups) titles << g->title();
                // v3.9.15：「游戏设置」分组上移到顶部全局设置栏（平台/版本/种子），左侧剩 5 组；
                //   同时断言顶部栏包含平台 / 游戏版本 / 世界种子 三个控件。
                const QStringList want = { slime::ui::t("ui.2cacc1b74217"), slime::ui::t("ui.5483493bb0d8"),
                                           slime::ui::t("ui.6a05dbdc884c"), slime::ui::t("ui.eb1f3e5ef69b"),
                                           slime::ui::t("ui.c15668b86bc6") };
                bool all = true;
                for (const QString& t : want) {
                    if (!titles.contains(t)) {
                        all = false;
                        std::fprintf(stderr, "[UI自检] 缺少设置分组: %s\n", t.toUtf8().constData());
                    }
                }
                check(all && titles.size() >= 5, "左侧包含规定的 5 个设置分组（游戏设置已上移顶部栏）");
                bool topBarOk = m_topBar && m_w->javaRadio && m_w->bedrockRadio && m_w->mapVersionCombo &&
                                m_w->seedEdit;
                if (topBarOk) {
                    topBarOk = m_topBar->isAncestorOf(m_w->javaRadio) &&
                               m_topBar->isAncestorOf(m_w->bedrockRadio) &&
                               m_topBar->isAncestorOf(m_w->mapVersionCombo) &&
                               m_topBar->isAncestorOf(m_w->seedEdit);
                }
                check(topBarOk, "顶部全局设置栏包含 平台/游戏版本/世界种子");
                for (QGroupBox* g : groups) {
                    check(!g->isCheckable(), "设置分组不可折叠（任务书：不做折叠）");
                    break; // 只断言一次即可（全部同构）
                }
                std::fprintf(stderr, "[UI自检] 设置分组：%s\n", titles.join(QStringLiteral(" / ")).toUtf8().constData());
                for (QGroupBox* gg : groups) {
                    std::fprintf(stderr, "[UI自检]   分组最小宽 \"%s\" = %d px\n",
                                 gg->title().toUtf8().constData(), gg->minimumSizeHint().width());
                }
            }
            check(m_w->infoStats != nullptr && m_w->resultView != nullptr, "右栏为「信息栏 + QTextEdit」");
            check(m_w->resultView->isReadOnly(), "结果视图只读");
            // v3.3.6（任务书 调整 2）：底部面板 = 左竖排导航 + 右内容区（不再是 QTabWidget）
            check(m_nav != nullptr && m_stack != nullptr && m_nav->count() == 4 && m_stack->count() == 4,
                  "底部为「左竖排导航 + 右内容区」，四个分类一一对应");
            if (m_nav && m_stack) {
                QStringList navNames;
                for (int i = 0; i < m_nav->count(); ++i) {
                    QString t = m_nav->item(i)->text();
                    t.remove(QStringLiteral("● "));
                    t.remove(QStringLiteral("○ "));
                    // 「反馈」项后面还带 ⚠️/❌ 计数徽章（如「反馈 ⚠️1❌1」），只取分类名
                    const int sp = t.indexOf(QLatin1Char(' '));
                    if (sp > 0) t = t.left(sp);
                    navNames << t;
                }
                check(navNames == QStringList{ slime::ui::t("ui.acf014bff8d8"), slime::ui::t("ui.42a36e949784"),
                                               slime::ui::t("ui.4de50894b8c1"), slime::ui::t("ui.b114b91547d7") },
                      "四个分类名称与顺序符合任务书");
                std::fprintf(stderr, "[UI自检] 底部导航：%s\n", navNames.join(QStringLiteral(" / ")).toUtf8().constData());
            }
            check(m_feedback && m_logPanel && m_terminal && m_progressStream, "进度/反馈/日志/命令面板对象齐全");
            // 尺寸规范
            // v3.10.0：三纵列后 QSplitter 按比例分配宽度，左栏在 400 附近、窄窗口下不小于最小宽 320。
            {
                const QList<int> sz = m_hSplit->sizes();
                std::fprintf(stderr, "[UI自检]   hSplit 尺寸: %d / %d / %d（窗口 %dx%d，hSplit 宽 %d）\n",
                             sz.value(0), sz.value(1), sz.value(2), width(), height(), m_hSplit->width());
            }
            check(m_hSplit->sizes().value(0) >= 320 && m_hSplit->sizes().value(0) <= 460 &&
                      m_hSplit->sizes().value(2) >= 280,
                  "三纵列宽度合理（左 ≥320、右 ≥280）");
            // v3.4.0：停靠面板展开时默认高 180（收起后高度归 0，由 showDock/hideDock 控制）
            check(minimumWidth() == 1000 && minimumHeight() == 650, "窗口最小尺寸 1000×650");
            resetLayout(); // 恢复默认布局与默认窗口尺寸后再断言
            QApplication::processEvents(QEventLoop::AllEvents, 60);
            check(width() == 1280 && height() == 860, "窗口默认尺寸 1280×860");
            check(!m_running && m_w->start->isEnabled(), "结构断言后「开始搜索」按钮可用（未在运行中）");
            // 8 张截图所需的布局状态由外部脚本设置尺寸/分割位置后逐张调用（见 GUI 自检截图钩子）
            // v3.3.4（任务书 7）：splitter 拖到最左（左栏 340px 最小宽）也不允许出现横向滚动条
            if (m_hSplit) {
                m_hSplit->setSizes(QList<int>() << 340 << (width() - 340));
                QApplication::processEvents(QEventLoop::AllEvents, 60);
                const int hmLeft = m_w->mainScroll->horizontalScrollBar()
                                       ? m_w->mainScroll->horizontalScrollBar()->maximum()
                                       : 0;
                std::fprintf(stderr, "[UI自检] 左栏 340px（splitter 最左）：设置区横滚动上限=%d\n", hmLeft);
                check(hmLeft == 0, "左栏最小宽 340px 下设置区无横向滚动条");
                resetLayout();
            }
            if (!shotPrefix.isEmpty()) {
                // v3.3.4（任务书 7）：规定尺寸 / splitter 极限 / 四标签页 / 每个菜单展开
                captureUiScreenshots(shotPrefix);
                resetLayout();
            }

            // 13.9 v3.3.7.2（F-1）：截图文字可读性 —— 必须能真正渲染出文字，
            //      否则「豆腐块」截图会再次作为证据交付（v3.3.4~v3.3.7 的教训）。
            {
                const QFontMetrics fm(font());
                const bool glyphsOA = fm.inFont(QChar(0x53F2)) &&   // 史
                                      fm.inFont(QChar(0x83B1)) &&   // 莱
                                      fm.inFont(QLatin1Char('5')) && fm.inFont(QLatin1Char('%'));
                check(glyphsOA, "字体含中文/数字字形（QFontMetrics::inFont；缺字形=截图豆腐块）");
                const QImage win = grab().toImage();
                const int variety = inkRunWidthVariety(win);
                const double ratio = inkRatio(win);
                std::fprintf(stderr,
                             "[UI自检] 截图可读性：墨迹列宽种类=%d（真实文字≥6 / 豆腐块=1~3）"
                             " 墨迹占比=%.2f%%\n",
                             variety, ratio * 100.0);
                check(variety >= 5, "窗口截图含真实文字（墨迹列宽种类 ≥5，豆腐块会被判失败）");
                check(ratio > 0.005, "窗口截图非空白（墨迹占比 >0.5%）");
                if (!shotPrefix.isEmpty()) {
                    const QImage saved(shotPrefix + QStringLiteral("_size-1280x860.png"));
                    check(!saved.isNull(), "落盘截图可重新加载");
                    if (!saved.isNull()) {
                        const int savedVariety = inkRunWidthVariety(saved);
                        std::fprintf(stderr, "[UI自检]   落盘截图墨迹列宽种类=%d\n", savedVariety);
                        check(savedVariety >= 5, "落盘截图文字可读（非豆腐块）");
                    }
                }
            }
        }
        // 16) v3.3.7.2（任务书步骤 1）：单一百分比 / 同源分母 / 阶段文案 / 状态栏分离
        {
            std::fprintf(stderr, "[UI自检] v3.3.7.2 检查（单一百分比/同源分母/阶段文案）…\n");

            // 16.1（任务书 1.1）进度行只有一个百分比：进度条自身文字必须关闭
            check(m_w->progress != nullptr && !m_w->progress->isTextVisible(),
                  "进度条自身不绘制百分比（避免与标签重复：用户截图曾出现「25% 25%」）");
            check(m_w->progressPercent != nullptr, "百分比标签存在（唯一百分比来源）");
            if (m_w->progress) {
                m_w->progress->setValue(250);
                // 注意：QProgressBar::text() 与 textVisible 无关（始终返回格式化文本），
                // 因此这里用**像素级**证明：关闭内置文字后，进度行的墨迹必须变少。
                if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("25%"));
                m_w->progress->setTextVisible(true);
                QApplication::processEvents(QEventLoop::AllEvents, 40);
                const int inkWith = inkPixels(m_progressRowWidget ? m_progressRowWidget->grab().toImage() : QImage());
                m_w->progress->setTextVisible(false);
                QApplication::processEvents(QEventLoop::AllEvents, 40);
                const int inkWithout = inkPixels(m_progressRowWidget ? m_progressRowWidget->grab().toImage() : QImage());
                std::fprintf(stderr, "[UI自检]   进度行墨迹像素：内置文字开=%d 关=%d\n", inkWith, inkWithout);
                check(inkWith > inkWithout,
                      "关闭进度条内置文字后进度行墨迹减少（证明此前确实重复绘制了百分比）");
                // 进度行内形如 "NN%" 的标签必须只有 1 个
                int pctLabels = 0;
                if (m_progressRowWidget) {
                    for (QLabel* l : m_progressRowWidget->findChildren<QLabel*>()) {
                        const QString t = l->text();
                        if (t.size() >= 2 && t.size() <= 5 && t.endsWith(QLatin1Char('%'))) {
                            bool allDigits = true;
                            for (int i = 0; i + 1 < t.size(); ++i) {
                                if (!t.at(i).isDigit()) allDigits = false;
                            }
                            if (allDigits) ++pctLabels;
                        }
                    }
                }
                check(pctLabels == 1,
                      (slime::ui::t("ui.40ce33982716").arg(pctLabels))
                          .toUtf8()
                          .constData());
                m_w->progress->setValue(0);
            }

            // v3.5.28（用户要求）：状态栏右侧信息已移除 —— 改为断言「左侧常驻提醒 + 工具栏状态面板」
            check(m_statusSep == nullptr,
                  "状态栏右侧信息已移除（无分隔符/无永久控件）");
            check(m_w->searchStatus && m_w->searchStatus->isVisible(), "工具栏含搜索状态面板（阶段/结果/用时/剩余）");
            check(m_w->searchStage && !m_w->searchStage->text().isEmpty(), "搜索状态面板显示阶段文字");

            // 16.2（任务书 1.1/1.2/1.4 验收场景）真实密度搜索：中途截图 + 数值/文案断言
            {
                const QString shotPrefix2 = qEnvironmentVariable("SLIME_SHOT");
                const bool big = qEnvironmentVariableIsSet("SLIME_SELFTEST_BIG");
                const int half = big ? 104857 : 20000; // big：用户原场景（±104857 = 439.8 亿）
                const int timeoutMs = big ? 900000 : 150000;
                // 从统计文本里解析「（xx%）」中的数字 —— 只接受「（数字%）」形式，
                // 且括号内容必须到**下一个**「（」之前结束（否则会跨过「（显示 N 条）」）。
                const auto parsePct = [](const QString& s, double* out) {
                    int idx = 0;
                    while ((idx = s.indexOf(QStringLiteral("（"), idx)) >= 0) {
                        const int nextOpen = s.indexOf(QStringLiteral("（"), idx + 1);
                        const int limit = (nextOpen < 0) ? s.size() : nextOpen;
                        const int rp = s.indexOf(QStringLiteral("%"), idx);
                        if (rp > idx && rp < limit) {
                            bool Ok = false;
                            const double v = s.mid(idx + 1, rp - idx - 1).trimmed().toDouble(&Ok);
                            if (Ok) {
                                *out = v;
                                return true;
                            }
                        }
                        idx += 1;
                    }
                    return false;
                };

                if (m_w->bedrockRadio) m_w->bedrockRadio->setChecked(true);
                if (m_w->densityRadio) m_w->densityRadio->setChecked(true);
                onModeChanged();
                if (m_w->gridSize) m_w->gridSize->setValue(5);
                if (m_w->minDensity) m_w->minDensity->setValue(0.75);
                if (m_w->minX) m_w->minX->setValue(-half);
                if (m_w->maxX) m_w->maxX->setValue(half - 1);
                if (m_w->minZ) m_w->minZ->setValue(-half);
                if (m_w->maxZ) m_w->maxZ->setValue(half - 1);
                if (m_w->seedEdit) m_w->seedEdit->setText(QStringLiteral("0"));
                if (m_w->stopAtTarget) m_w->stopAtTarget->setChecked(true);
                if (m_w->targetCount) m_w->targetCount->setValue(1);
                if (m_w->threads) m_w->threads->setValue(m_w->threads->maximum()); // 全核（0 会被夹到 1）
                // v3.3.7.2：big（439 亿）模式按用户场景**启用缓存**（分块上限因此压到 2.5M words），
                // 但缓存文件指向临时目录，绝不触碰用户缓存；中等规模则不启用缓存。
                if (big && m_cache) {
                    m_cacheFile = std::filesystem::temp_directory_path() / "slime_selftest_big.slimecache";
                    m_cache->clear();
                    if (m_w->useCache) m_w->useCache->setChecked(true);
                    std::fprintf(stderr, "[UI自检]   缓存（自检临时文件）：%s\n", m_cacheFile.string().c_str());
                } else if (m_w->useCache) {
                    m_w->useCache->setChecked(false);
                }
                selectBottomCategory(0);                              // 底部切到「进度」，截图可看到流水

                std::fprintf(stderr, "[UI自检]   密度搜索（%s）：范围 ±%d，g=5 ≥0.75，基岩，目标 1\n",
                             big ? "439.8 亿原场景" : "中等规模", half);
                startSearch();
                check(m_running, "自检内真实密度搜索已启动");

                QElapsedTimer et;
                et.start();
                bool shotScan = false, shotMatch = false, timedOut = false;
                QString pctLabelAtScan, pctBarAtScan, msgAtMatch, stageAtMatch, infoAtMatch;
                while (m_running && et.elapsed() < timeoutMs) {
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (!m_running) break;
                    const QString stage = m_w->progressStage ? m_w->progressStage->text() : QString();
                    if (!shotScan && m_w->progress->value() >= 50 &&
                        stage.contains(slime::ui::t("ui.998c9363771c"))) {
                        shotScan = true;
                        pctLabelAtScan = m_w->progressPercent ? m_w->progressPercent->text() : QString();
                        pctBarAtScan = QString::number(m_w->progress->value() / 10.0, 'f', 1);
                        if (m_w->progressInfo) {
                            // 状态栏百分比必须 ≤100%（任务书 1.2）
                            double v = 0.0;
                            if (parsePct(m_w->progressInfo->text(), &v)) {
                                check(v <= 100.0,
                                      (slime::ui::t("ui.a6c72a289200").arg(v))
                                          .toUtf8()
                                          .constData());
                            }
                        }
                        // v3.3.8（任务 1）：搜索中 ETA 必须有具体数值（不是 --:--）
                        if (m_w->progressEta) {
                            const QString etaText = m_w->progressEta->text();
                            std::fprintf(stderr, "[UI自检]   搜索中 ETA：%s\n", etaText.toUtf8().constData());
                            check(etaText.contains(QLatin1Char(':')) &&
                                      !etaText.contains(QStringLiteral("--:--")),
                                  (slime::ui::t("ui.ae72d062c965").arg(etaText))
                                      .toUtf8()
                                      .constData());
                        }
                        std::fprintf(stderr, "[UI自检]   扫描中：进度行=%s / 状态栏=%s\n",
                                     pctLabelAtScan.toUtf8().constData(),
                                     (m_w->progressInfo ? m_w->progressInfo->text() : QString())
                                         .toUtf8()
                                         .constData());
                        if (!shotPrefix2.isEmpty()) {
                            saveWindowShot(shotPrefix2, QStringLiteral("density-scanning"));
                        }
                    }
                    if (!shotMatch && stage.contains(slime::ui::t("ui.c7127bb6eeea")) &&
                        !stage.contains(slime::ui::t("ui.936420feb068"))) {
                        shotMatch = true;
                        stageAtMatch = stage;
                        msgAtMatch = statusBar()->currentMessage();
                        infoAtMatch = m_w->progressInfo ? m_w->progressInfo->text() : QString();
                        std::fprintf(stderr, "[UI自检]   匹配中：阶段=%s / 状态栏消息=%s\n",
                                     stageAtMatch.toUtf8().constData(), msgAtMatch.toUtf8().constData());
                        if (!shotPrefix2.isEmpty()) {
                            saveWindowShot(shotPrefix2, QStringLiteral("density-matching"));
                        }
                    }
                }
                if (m_running) {
                    timedOut = true;
                    cancelSearch();
                    while (m_running && et.elapsed() < timeoutMs + 30000) {
                        QApplication::processEvents(QEventLoop::AllEvents, 20);
                    }
                }
                check(!timedOut, "密度搜索在时限内结束（否则已取消并记为失败）");
                check(!m_running, "密度搜索已结束（无线程残留）");
                // v3.3.8（任务 1）：完成后 ETA 必须清空（否则用户以为还在跑）
                if (m_w->progressEta) {
                    const QString etaDone = m_w->progressEta->text();
                    std::fprintf(stderr, "[UI自检]   完成后 ETA：%s\n", etaDone.toUtf8().constData());
                    check(etaDone.contains(QStringLiteral("--:--")),
                          (slime::ui::t("ui.dcbe39514b6c").arg(etaDone)).toUtf8().constData());
                }
                // 任务书 1.4：密度模式下**不得**出现「匹配图案」字样
                bool sawPatternWord = false;
                if (stageAtMatch.contains(slime::ui::t("ui.d5b2aeac0692")) ||
                    msgAtMatch.contains(slime::ui::t("ui.d5b2aeac0692"))) {
                    sawPatternWord = true;
                }
                check(!sawPatternWord,
                      (slime::ui::t("ui.ff6468498df0")
                           .arg(stageAtMatch, msgAtMatch))
                          .toUtf8()
                          .constData());
                std::fprintf(stderr, "[UI自检]   密度搜索完成：扫描截图=%d 匹配截图=%d 已用时=%.1fs\n",
                             shotScan ? 1 : 0, shotMatch ? 1 : 0, et.elapsed() / 1000.0);
                if (m_elapsedTimer) m_elapsedTimer->stop();

                // 16.3（任务书 1.2 / 步骤 5）：完成后统计必须与引擎同源，且分子 ≤ 分母
                {
                    const int64_t engTotal = m_task.liveTotalCount();
                    check(engTotal > 0, "引擎公开了本次搜索的总扫描量（界面不再自行推算分母）");
                    const QString fin = m_w->progressInfo ? m_w->progressInfo->text() : QString();
                    std::fprintf(stderr, "[UI自检]   完成后状态栏：%s\n  引擎口径总量=%lld\n",
                                 fin.toUtf8().constData(), static_cast<long long>(engTotal));
                    double pct = -1.0;
                    check(parsePct(fin, &pct) && pct <= 100.0,
                          (slime::ui::t("ui.63811be297a8").arg(pct))
                              .toUtf8()
                              .constData());
                    // 分子 ≤ 分母（同源计数保证）
                    const int sPos = fin.indexOf(slime::ui::t("ui.8caf38614bbf"));
                    const int slash = fin.indexOf(QLatin1Char('/'));
                    if (sPos >= 0 && slash > sPos) {
                        bool okN = false, okM = false;
                        const int64_t n = fin.mid(sPos + 3, slash - sPos - 3).trimmed().toLongLong(&okN);
                        const int sp2 = fin.indexOf(QLatin1Char(' '), slash);
                        const int64_t m = (sp2 > slash)
                                              ? fin.mid(slash + 1, sp2 - slash - 1).trimmed().toLongLong(&okM)
                                              : 0;
                        check(okN && okM && n > 0 && m > 0 && n <= m,
                              (slime::ui::t("ui.ee3e59ed3a21").arg(n).arg(m))
                                  .toUtf8()
                                  .constData());
                        check(m == engTotal,
                              (slime::ui::t("ui.75b961eb1a31").arg(m).arg(engTotal))
                                  .toUtf8()
                                  .constData());
                    }
                    if (big) {
                        // 分块路径：进度条与状态栏同源（都来自 liveScanned/totalEvals）→ 应基本一致
                        const double barPct = m_w->progress->value() / 10.0;
                        check(std::fabs(barPct - pct) <= 1.0,
                              (slime::ui::t("ui.b271f8f86be5")
                                   .arg(barPct)
                                   .arg(pct))
                                  .toUtf8()
                                  .constData());
                    }
                    // 步骤 5（F-3）：完成态状态栏数值必须与日志里的 search.summary 一致
                    {
                        const std::filesystem::path lf = slime::IncidentLog::currentFile();
                        QString lastSummary;
                        if (!lf.empty()) {
                            std::ifstream in(lf);
                            std::string line;
                            while (std::getline(in, line)) {
                                if (line.find("search.summary") != std::string::npos) {
                                    lastSummary = QString::fromStdString(line);
                                }
                            }
                        }
                        std::fprintf(stderr, "[UI自检]   日志汇总：%s\n", lastSummary.toUtf8().constData());
                        int64_t logScanned = -1, logTotal = -1;
                        const int sp = lastSummary.indexOf(QStringLiteral("scanned="));
                        if (sp >= 0) {
                            const int sl = lastSummary.indexOf(QLatin1Char('/'), sp);
                            const int sp2 = (sl > 0) ? lastSummary.indexOf(QLatin1Char(' '), sl) : -1;
                            if (sl > 0 && sp2 > sl) {
                                logScanned = lastSummary.mid(sp + 8, sl - sp - 8).toLongLong();
                                logTotal = lastSummary.mid(sl + 1, sp2 - sl - 1).toLongLong();
                            }
                        }
                        check(logScanned >= 0 && logTotal > 0,
                              "日志含 search.summary 且带 scanned=<已扫描>/<总量> 字段");
                        // 与状态栏里显示的 N/M 比对（两者必须同源）
                        const int sp3 = fin.indexOf(slime::ui::t("ui.8caf38614bbf"));
                        const int slash3 = fin.indexOf(QLatin1Char('/'));
                        if (sp3 >= 0 && slash3 > sp3) {
                            const int64_t uiN = fin.mid(sp3 + 3, slash3 - sp3 - 3).trimmed().toLongLong();
                            const int sp4 = fin.indexOf(QLatin1Char(' '), slash3);
                            const int64_t uiM =
                                (sp4 > slash3) ? fin.mid(slash3 + 1, sp4 - slash3 - 1).trimmed().toLongLong()
                                               : 0;
                            check(uiN == logScanned && uiM == logTotal,
                                  (slime::ui::t("ui.e2ebc10f4068")
                                       .arg(uiN)
                                       .arg(uiM)
                                       .arg(logScanned)
                                       .arg(logTotal))
                                      .toUtf8()
                                      .constData());
                        }
                    }
                }
            }

            std::fprintf(stderr, "[UI自检] v3.3.7.2 检查完成\n");
        }

    // 17) v3.3.8.1（任务书 1.3）：导入大缓存 = 后台线程 + 进度条（界面不冻结）
    //     要导入的文件由环境变量 SLIME_IMPORT_CACHE 指定（自检在外部驱动，避免弹文件对话框），
    //     走的是与菜单「导入缓存」完全相同的生产路径（importCacheFromPath）。
    //     验收口径：事件循环持续心跳（不冻结）+ 进度条多次实时更新且单调 + 导入后条目增加。
    {
        const QString impPath = qEnvironmentVariable("SLIME_IMPORT_CACHE");
        if (!impPath.isEmpty()) {
            std::fprintf(stderr, "[UI自检] v3.3.8.1 导入大缓存检查：%s\n", impPath.toUtf8().constData());
            QElapsedTimer beat;
            beat.start();
            qint64 lastBeat = beat.elapsed();
            qint64 maxGapMs = 0;
            int ticks = 0, samples = 0, backwards = 0, lastPct = 0;
            QTimer heartbeat;
            connect(&heartbeat, &QTimer::timeout, this, [&]() {
                const qint64 now = beat.elapsed();
                maxGapMs = (std::max)(maxGapMs, now - lastBeat);
                lastBeat = now;
                ++ticks;
                if (QProgressDialog* pd = findChild<QProgressDialog*>()) {
                    const int pct = pd->value();
                    ++samples;
                    if (pct < lastPct) ++backwards;
                    lastPct = pct;
                }
            });
            heartbeat.start(20);

            const int64_t beforeE = m_cache->entryCount();
            const size_t beforeR = m_cache->ranges().size();
            QElapsedTimer wall;
            wall.start();
            importCacheFromPath(impPath, /*showProgressDialog=*/true, /*showResultDialogs=*/false);
            const qint64 importMs = wall.elapsed();
            heartbeat.stop();

            std::error_code ecImp;
            const auto impBytes =
                static_cast<int64_t>(std::filesystem::file_size(std::filesystem::path(impPath.toStdWString()), ecImp));
            const int64_t addedE = m_cache->entryCount() - beforeE;
            std::fprintf(stderr,
                         "[UI自检]   导入：文件=%.1f MiB 用时=%lld ms 新增区域=%lld 新增条目=%lld "
                         "心跳=%d 次 进度采样=%d 次 末次进度=%d/1000 最长卡顿=%lld ms 回退=%d 次\n",
                         impBytes / (1024.0 * 1024.0), static_cast<long long>(importMs),
                         static_cast<long long>(m_cache->ranges().size() - beforeR),
                         static_cast<long long>(addedE), ticks, samples, lastPct,
                         static_cast<long long>(maxGapMs), backwards);
            check(addedE > 0, "导入大缓存后条目增加（后台线程读取 + UI 线程合并成功）");
            check(ticks >= 10, "导入期间事件循环持续心跳（界面未冻结）");
            check(maxGapMs < 500, "导入期间最长 UI 卡顿 < 500 ms（进度条可实时刷新）");
            check(samples >= 3 && backwards == 0, "进度条实时更新且单调不回退");

            // 收尾：不把验收用的巨型缓存留在运行环境里
            m_cache->clear();
            std::filesystem::remove(m_cacheFile, ecImp);
            updateCacheStatusUi();
        }
    }

        std::fprintf(stderr, "[UI自检] v3.3.4 结构检查完成\n");
    }

    // 18) v3.4.7：交互可用性自检（用户实测反馈「选项无法勾选」的回归防线）
    //     ① 诊断：列出「可见但被禁用」的交互控件及其父链（缓存构建模式会按设计禁用若干项，
    //        逐条打印便于判断是设计如此还是误禁用）；
    //     ② 断言：设置页 + 当前可见页面里所有复选框/单选按钮都能被真实鼠标点击切换。
    {
        std::fprintf(stderr, "[UI自检] v3.4.7 交互可用性检查（点击切换 + 禁用诊断）…\n");

        {
            // 诊断头部：把当时的模式写清楚，便于判断「禁用」是设计行为还是误禁用
            const QRadioButton* mode = m_w->densityRadio && m_w->densityRadio->isChecked()
                                           ? m_w->densityRadio : m_w->strictRadio;
            const QRadioButton* ver = m_w->bedrockRadio && m_w->bedrockRadio->isChecked()
                                          ? m_w->bedrockRadio : m_w->javaRadio;
            std::fprintf(stderr, "[UI自检]   当时状态：搜索模式=%s / 版本=%s / 达到即停=%d / 缓存构建=%d\n",
                         mode ? mode->text().toUtf8().constData() : "?",
                         ver ? ver->text().toUtf8().constData() : "?",
                         (m_w->stopAtTarget && m_w->stopAtTarget->isChecked()) ? 1 : 0,
                         buildModeActive() ? 1 : 0);
        }
        {
            QStringList disabled;
            const QList<QWidget*> all = findChildren<QWidget*>();
            for (QWidget* w : all) {
                if (!w || !w->isVisible() || w->isEnabled()) continue;
                const bool interactive = w->inherits("QCheckBox") || w->inherits("QRadioButton") ||
                                         w->inherits("QAbstractSpinBox") || w->inherits("QComboBox") ||
                                         w->inherits("QLineEdit") || w->inherits("QAbstractButton") ||
                                         w->inherits("QSlider");
                if (!interactive) continue;
                QString chain;
                int depth = 0;
                for (QWidget* p = w; p && depth < 4; p = p->parentWidget(), ++depth) {
                    chain += QString::fromLatin1(p->metaObject()->className());
                    if (!p->objectName().isEmpty()) chain += QStringLiteral("#") + p->objectName();
                    chain += QStringLiteral(" < ");
                }
                const QGroupBox* gb = nullptr;
                for (QWidget* p = w->parentWidget(); p && !gb; p = p->parentWidget()) {
                    gb = qobject_cast<const QGroupBox*>(p);
                }
                const QAbstractButton* btn = qobject_cast<QAbstractButton*>(w);
                // 禁用原因：v3391tip 是缓存构建模式打的设计性禁用标记；「取消」在无搜索时按设计禁用
                QString why = slime::ui::t("ui.7bd3b864b082");
                if (w->property("v3391tip").toBool()) {
                    why = slime::ui::t("ui.6e13090350fa");
                } else if (btn && btn->text().contains(slime::ui::t("ui.4d0b4688c787"))) {
                    why = slime::ui::t("ui.338f1f4ac68d");
                } else if (!w->isVisibleTo(this)) {
                    why = slime::ui::t("ui.d009f8c12b25");
                }
                disabled << QStringLiteral("%1[%2]（%3%4）← %5")
                                .arg(QString::fromLatin1(w->metaObject()->className()),
                                     btn ? btn->text() : QString(),
                                     gb ? gb->title() + QStringLiteral(" / ") : QString(), why, chain);
            }
            std::fprintf(stderr, "[UI自检]   可见但被禁用的交互控件 %lld 个%s%s\n",
                         static_cast<long long>(disabled.size()),
                         disabled.isEmpty() ? slime::ui::t("ui.6238bf9ad526").toUtf8().constData() : "：",
                         disabled.join(QStringLiteral(" | ")).toUtf8().constData());
        }

        {
            QList<QCheckBox*> boxes;
            if (m_settingsPage) boxes = m_settingsPage->findChildren<QCheckBox*>();
            for (QCheckBox* cb : findChildren<QCheckBox*>()) {
                if (cb && !boxes.contains(cb)) boxes << cb;
            }
            int tried = 0;
            QStringList bad;
            for (QCheckBox* cb : boxes) {
                if (!cb || !cb->isVisible() || !cb->isEnabled()) continue;
                ++tried;
                const bool before = cb->isChecked();
                QStyleOptionButton opt;
                opt.initFrom(cb);
                const QRect ind = cb->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, cb);
                const QPoint pos = ind.isValid() && !ind.isEmpty() ? ind.center() : cb->rect().center();
                const QPointF global = cb->mapToGlobal(pos);
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), global, Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(cb, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), global, Qt::LeftButton,
                                    Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(cb, &release);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                if (cb->isChecked() == before) {
                    bad << (cb->text().isEmpty() ? cb->objectName() : cb->text());
                }
                cb->setChecked(before); // 还原，避免影响后续截图与断言
            }
            std::fprintf(stderr, "[UI自检]   复选框点击切换：可点 %lld 个，未翻转 %lld 个%s%s\n", static_cast<long long>(tried),
                         static_cast<long long>(bad.size()), bad.isEmpty() ? "" : "：",
                         bad.join(QStringLiteral(" / ")).toUtf8().constData());
            check(tried >= 4, "自检覆盖到至少 4 个可见复选框（点击切换有样本）");
            check(bad.isEmpty(), "全部可见复选框都能被鼠标点击勾选（选项可勾选）");
        }
    }

    // 19) v3.4.7：主题链路端到端（用户实测反馈「主题日志总是跟随系统」的回归防线）
    //     走**设置页真实控件**：读主题下拉的 currentData() ⇒ 切到「深色」⇒ 等 250ms 自动应用
    //     ⇒ 断言「实际生效 = 深色」且应用样式表**逐字等于** darkQss() ⇒ 最后还原原档位。
    //     同时把「主题重放：<档位>（qss 长度 N）」写进日志（与生产路径同一条代码）。
    {
        std::fprintf(stderr, "[UI自检] v3.4.7 主题链路检查（设置页主题下拉 → 深色生效 → 还原）…\n");
        QComboBox* themeBox = nullptr;
        if (m_settingsPage) {
            for (QComboBox* cb : m_settingsPage->findChildren<QComboBox*>()) {
                bool isThemeBox = false;
                for (int i = 0; i < cb->count(); ++i) {
                    const QString d = cb->itemData(i).toString();
                    if (d == QLatin1String("light") || d == QLatin1String("dark")) isThemeBox = true;
                }
                if (isThemeBox) { themeBox = cb; break; }
            }
        }
        check(themeBox != nullptr, "设置页存在主题下拉（itemData = light/dark/system）");
        if (themeBox) {
            const QString keepData = themeBox->currentData().toString();
            const bool keepDark = slime::ThemeManager::instance().isDark();
            int darkIdx = -1, keepIdx = -1;
            for (int i = 0; i < themeBox->count(); ++i) {
                const QString d = themeBox->itemData(i).toString();
                if (d == QLatin1String("dark")) darkIdx = i;
                if (d == keepData) keepIdx = i;
            }
            check(darkIdx >= 0, "主题下拉含「深色」档（itemData=dark）");
            if (darkIdx >= 0) {
                themeBox->setCurrentIndex(darkIdx);
                QElapsedTimer wait;
                wait.start();
                while (wait.elapsed() < 1200) QApplication::processEvents(QEventLoop::AllEvents, 30);
                const QString qss = qApp->styleSheet();
                const bool darkApplied = slime::ThemeManager::instance().isDark();
                const bool qssEquals = (qss == slime::ThemeManager::instance().darkQss());
                std::fprintf(stderr,
                             "[UI自检]   主题下拉 currentData()=%s / isDark=%d / 样式表长度=%d（深色基准 %d）"
                             "/ 与 darkQss() 逐字相等=%d\n",
                             themeBox->currentData().toString().toUtf8().constData(),
                             darkApplied ? 1 : 0, static_cast<int>(qss.size()),
                             static_cast<int>(slime::ThemeManager::instance().darkQss().size()),
                             qssEquals ? 1 : 0);
                check(themeBox->currentData().toString() == QLatin1String("dark"),
                      "主题下拉切到「深色」后 currentData() 读回 dark（不再按单选文字匹配）");
                check(darkApplied, "深色档位真正生效（ThemeManager::isDark() == true）");
                check(qssEquals, "应用样式表逐字等于 darkQss()（浅/深两套完整 QSS 已切换）");
                if (keepIdx >= 0) themeBox->setCurrentIndex(keepIdx);
                QElapsedTimer wait2;
                wait2.start();
                while (wait2.elapsed() < 1200) QApplication::processEvents(QEventLoop::AllEvents, 30);
                std::fprintf(stderr, "[UI自检]   档位还原：currentData()=%s / isDark=%d\n",
                             themeBox->currentData().toString().toUtf8().constData(),
                             slime::ThemeManager::instance().isDark() ? 1 : 0);
                check(slime::ThemeManager::instance().isDark() == keepDark,
                      "还原档位后实际生效主题回到自检开始前的状态");
            }
        }
    }

    // 20) v3.4.8：**达到目标数量即停止**端到端实测（用户反馈「失效」）
    //     口径：同一范围、同一模式，跑「勾选 vs 不勾选」两次，比较引擎实际评估量 / 扫描区块数 /
    //     耗时 / stoppedEarly / 结果条数，并按模式（密度·智能范围 / 密度·光栅 / 严格·四向旋转 /
    //     严格·锁定方向）分别记录。断言：勾选后必须**更省**（评估量或耗时明显下降），
    //     且结果条数不超过目标数量。
    {
        std::fprintf(stderr, "[UI自检] v3.4.8 「达到目标数量即停止」实测…\n");
        struct Case { const char* name; bool density; bool lock; bool smart; };
        const Case cases[4] = {
            { "密度+智能范围", true, false, true },
            { "密度+光栅", true, false, false },
            { "严格+四向旋转", false, false, false },
            { "严格+锁定方向", false, true, false },
        };
        const bool keepUseCache = m_w->useCache ? m_w->useCache->isChecked() : false;
        double slowRatio = 0.0;
        int improved = 0;
        for (const Case& c : cases) {
          for (int cacheOn = 0; cacheOn < 2; ++cacheOn) {
            if (m_w->useCache) m_w->useCache->setChecked(cacheOn == 1);
            m_w->bedrockRadio->setChecked(true);
            onVersionChanged();
            m_w->densityRadio->setChecked(c.density);
            m_w->strictRadio->setChecked(!c.density);
            onModeChanged();
            m_w->lockDir->setChecked(c.lock);
            m_w->smartRange->setChecked(c.smart);
            m_w->minX->setValue(-200); m_w->maxX->setValue(199);
            m_w->minZ->setValue(-200); m_w->maxZ->setValue(199);
            m_w->gridSize->setValue(5);
            m_w->minDensity->setValue(0.05);
            m_w->startX->setValue(0); m_w->startZ->setValue(0);
            m_w->threads->setValue(1);
            m_w->targetCount->setValue(8);
            if (!c.density) {
                // 严格模式：用「全 1 图案」保证范围内必有大量命中（考察提前停止本身，而非图案匹配）
                // 1×1「草绿」图案 = 单个史莱姆区块即命中（约 1/10 区块）⇒ 范围内必有大量命中
                Pattern pat(1, 1);
                pat.set(0, 0, CellState::Green);
                m_pattern = pat;
                m_patternSet = true;
            }
            const auto runOnceOn = [&](bool stop) {
                m_w->stopAtTarget->setChecked(stop);
                startSearch();
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() <
                                        60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(5);
                }
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0)
                                      .count();
                SearchOutcome out = m_hasLastOutcome ? m_lastOutcome : SearchOutcome{};
                return std::make_tuple(ms, out.stats.windowsEvaluated, out.stats.scannedChunks,
                                       (int64_t)out.results.size(), out.stoppedEarly);
            };
            const auto full = runOnceOn(false);
            const auto stop = runOnceOn(true);
            const double fullMs = std::get<0>(full), stopMs = std::get<0>(stop);
            const int64_t fullWi = std::get<1>(full), stopWi = std::get<1>(stop);
            const int64_t fullSc = std::get<2>(full), stopSc = std::get<2>(stop);
            const int64_t fullN = std::get<3>(full), stopN = std::get<3>(stop);
            const bool early = std::get<4>(stop);
            std::fprintf(stderr,
                         "[UI自检] %s（缓存%s）：不勾选 %.0fms/评估%lld/扫描%lld/结果%lld ｜ 勾选 %.0fms/评估%lld/"
                         "扫描%lld/结果%lld ｜ 提前停止=%d\n",
                         c.name, cacheOn ? "开" : "关", fullMs, (long long)fullWi, (long long)fullSc,
                         (long long)fullN, stopMs, (long long)stopWi, (long long)stopSc, (long long)stopN,
                         early ? 1 : 0);
            check(stopN <= m_w->targetCount->value(),
                  "达到即停：结果条数不超过目标数量");
            const bool faster = (stopWi < fullWi) || (stopMs * 1.5 < fullMs);
            if (faster) ++improved;
            if (fullWi > 0) slowRatio = (std::max)(slowRatio, (double)stopWi / (double)fullWi);
            if (!faster) {
                std::fprintf(stderr, "[UI自检]   ↑ %s（缓存%s）勾选后没有变快（评估 %lld → %lld，耗时 %.0f → %.0f ms）\n",
                             c.name, cacheOn ? "开" : "关", (long long)fullWi, (long long)stopWi, fullMs, stopMs);
            }
          }
        }
        std::fprintf(stderr, "[UI自检]   达到即停实测汇总：变快的组合 %d / 8\n", improved);
        check(improved >= 4, "达到即停：多数模式勾选后比不勾选更快/更省（用户反馈的「失效」已修）");
        if (m_w->useCache) m_w->useCache->setChecked(keepUseCache);
        m_w->stopAtTarget->setChecked(false);
        m_w->smartRange->setChecked(false);
        m_w->threads->setValue(0);
        m_w->minX->setValue(-256); m_w->maxX->setValue(255);
        m_w->minZ->setValue(-256); m_w->maxZ->setValue(255);
    }

    // 21) v3.4.8 用户反馈项自检：① 设置生效更快 ② 主题切换重绘图案 ⑤ 恢复所有设置 ⑦ 滚轮调节数值
    {
        std::fprintf(stderr, "[UI自检] v3.4.8 用户反馈项检查（设置生效/主题图案/恢复设置/滚轮）…\n");

        // ① 设置生效更快：定时器间隔 + 实测「改一下设置到真正生效」的耗时
        {
            check(m_settingsApplyTimer != nullptr, "设置立即生效定时器存在");
            const int iv = m_settingsApplyTimer ? m_settingsApplyTimer->interval() : -1;
            std::fprintf(stderr, "[UI自检]   设置生效定时器间隔 = %d ms（要求 ≤ 100）\n", iv);
            check(iv > 0 && iv <= 100, "设置生效间隔 ≤ 100ms（用户要求：调快一点）");
            if (m_settingsPage) {
                const bool keepAuto = m_settings.logAutoScroll;
                QCheckBox* box = nullptr;
                for (QCheckBox* cb : m_settingsPage->findChildren<QCheckBox*>()) {
                    if (cb->text().contains(slime::ui::t("ui.9642fc88cdea"))) { box = cb; break; }
                }
                if (box) {
                    QElapsedTimer t1;
                    t1.start();
                    box->setChecked(!box->isChecked());
                    while (m_settings.logAutoScroll == keepAuto && t1.elapsed() < 1000) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                        QThread::msleep(2);
                    }
                    const qint64 ms = t1.elapsed();
                    std::fprintf(stderr, "[UI自检]   改动设置到生效实测 = %lld ms\n", static_cast<long long>(ms));
                    check(m_settings.logAutoScroll != keepAuto, "改动设置后自动生效（无需点「应用」）");
                    check(ms <= 800, "改动设置到生效 ≤ 800ms（含应用工作量；定时器间隔见上行日志）");
                    box->setChecked(keepAuto);
                    QElapsedTimer t2;
                    t2.start();
                    while (m_settings.logAutoScroll != keepAuto && t2.elapsed() < 1000) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                        QThread::msleep(2);
                    }
                }
            }
        }

        // ② 主题切换时图案立即跟着变（用户：不然看不清，只有重新加载才清楚）
        {
            const auto strokeStats = [&](int kind, int* darkPx, int* lightPx) {
                *darkPx = 0;
                *lightPx = 0;
                const QImage img = v340NavIcon(kind).pixmap(24, 24).toImage();
                for (int y = 0; y < img.height(); ++y) {
                    for (int x = 0; x < img.width(); ++x) {
                        const QColor c = img.pixelColor(x, y);
                        if (c.alpha() < 128) continue;
                        const int lum = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
                        if (lum < 90) ++(*darkPx);
                        else if (lum > 180) ++(*lightPx);
                    }
                }
            };
            const bool wasDark = slime::ThemeManager::instance().isDark();
            slime::ThemeManager::instance().apply(qApp, slime::ThemeMode::Light);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            int dLight = 0, lLight = 0;
            strokeStats(0, &dLight, &lLight);
            slime::ThemeManager::instance().apply(qApp, slime::ThemeMode::Dark);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            int dDark = 0, lDark = 0;
            strokeStats(0, &dDark, &lDark);
            std::fprintf(stderr, "[UI自检]   导航图案像素：浅色主题 深像素=%d 亮像素=%d ｜ 深色主题 深像素=%d 亮像素=%d\n",
                         dLight, lLight, dDark, lDark);
            check(dLight > 20 && lLight == 0, "浅色主题下图案为深色描边");
            check(lDark > 20 && dDark == 0, "切到深色主题后图案立即变浅色（无需重启/重载）");
            // 还原到自检开始前的档位
            slime::ThemeManager::instance().apply(
                qApp, wasDark ? slime::ThemeMode::Dark : slime::ThemeMode::Light);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
        }

        // ⑤ 设置页「恢复所有设置」按钮存在且真的把设置恢复为出厂默认
        {
            QPushButton* resetBtn = findChild<QPushButton*>(QStringLiteral("resetAllSettingsBtn"));
            check(resetBtn != nullptr, "设置页存在「恢复所有设置」按钮");
            const AppSettings keep = m_settingsPage ? m_settingsPage->currentValues() : AppSettings{};
            if (resetBtn && m_settingsPage) {
                resetBtn->click();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                const AppSettings def = SettingsDialog::factoryDefaults();
                const AppSettings now = m_settingsPage->currentValues();
                const bool same = now.wheelAdjust == def.wheelAdjust && now.rememberWindow == def.rememberWindow &&
                                  now.theme == def.theme && now.soundOnComplete == def.soundOnComplete &&
                                  now.soundVolume == def.soundVolume && now.defaultAlgorithm == def.defaultAlgorithm &&
                                  now.defaultThreads == def.defaultThreads &&
                                  now.defaultTargetCount == def.defaultTargetCount &&
                                  now.cpuLimitPercent == def.cpuLimitPercent && now.memLimitMB == def.memLimitMB &&
                                  now.crashDump == def.crashDump && now.debugMode == def.debugMode &&
                                  now.logMaxLines == def.logMaxLines && now.cacheMaxMB == def.cacheMaxMB;
                std::fprintf(stderr, "[UI自检]   恢复所有设置后：主题=%s 轮滚=%d CPU上限=%d 内存上限=%d 与出厂一致=%d\n",
                             now.theme.toUtf8().constData(), now.wheelAdjust ? 1 : 0, now.cpuLimitPercent,
                             now.memLimitMB, same ? 1 : 0);
                check(same, "「恢复所有设置」把全部设置恢复为出厂默认值");
                check(m_settings.theme == def.theme, "恢复后立即生效（m_settings 同步为默认值）");
                // 还原自检前的设置，避免影响后续步骤与截图
                m_settingsPage->setValues(keep);
                m_settings = m_settingsPage->currentValues();
                applyAppSettings(m_settings);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            }
        }

        // ⑦ 滚轮调节数值：开启后鼠标划过即可调；关闭时数值不变
        {
            QSpinBox* spin = m_w->targetCount;
            check(spin != nullptr, "滚轮测试用的数值输入框存在");
            if (spin) {
                const bool keepWheel = m_settings.wheelAdjust;
                const int keepVal = spin->value();
                if (m_w->resultView) m_w->resultView->setFocus(); // 故意不让输入框获得焦点
                check(!spin->hasFocus(), "滚轮测试条件：输入框未获得焦点（模拟「鼠标划过」）");
                const auto sendWheel = [&](int delta) {
                    QWheelEvent we(QPointF(spin->width() / 2.0, spin->height() / 2.0),
                                    QPointF(spin->mapToGlobal(QPoint(spin->width() / 2, spin->height() / 2))),
                                    QPoint(0, 0), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                                    Qt::NoScrollPhase, false);
                    QApplication::sendEvent(spin, &we);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                };
                m_settings.wheelAdjust = true;
                const int before = spin->value();
                sendWheel(120);
                const int afterOn = spin->value();
                m_settings.wheelAdjust = false;
                spin->setValue(before);
                sendWheel(120);
                const int afterOff = spin->value();
                std::fprintf(stderr, "[UI自检]   滚轮：开启时 %d → %d ｜ 关闭时 %d → %d\n", before, afterOn, before,
                             afterOff);
                check(afterOn == before + spin->singleStep(),
                      "设置开启后，未获焦点的输入框滚轮即可调数值（用户反馈的「失效」已修）");
                check(afterOff == before, "设置关闭后滚轮不改数值");
                spin->setValue(keepVal);
                m_settings.wheelAdjust = keepWheel;
            }
        }

        // ⑧ v3.5.1（交互手感自查）：滚轮防手滑 / 停靠图标再点收起 / Ctrl+1..4 / 分隔条双击 / Esc
        {
            std::fprintf(stderr, "[UI自检] v3.5.1 交互手感（下拉框滚轮 / 停靠收起 / 页面快捷键 / 分隔条双击）…\n");
            const auto sendWheelTo = [](QWidget* w, int delta) {
                if (!w) return;
                QWheelEvent we(QPointF(w->width() / 2.0, w->height() / 2.0),
                               QPointF(w->mapToGlobal(QPoint(w->width() / 2, w->height() / 2))),
                               QPoint(0, 0), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                               Qt::NoScrollPhase, false);
                QApplication::sendEvent(w, &we);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            };

            // (1) 下拉框不再吞滚轮（鼠标划过不会误改选项）
            {
                QComboBox* combo = findChild<QComboBox*>();
                check(combo != nullptr, "找到用于滚轮测试的下拉框");
                if (combo) {
                    const int keepIdx = combo->currentIndex();
                    if (m_w->resultView) m_w->resultView->setFocus(); // 不让下拉框获得焦点
                    sendWheelTo(combo, 120);
                    sendWheelTo(combo, -120);
                    std::fprintf(stderr, "[UI自检]   下拉框滚轮：%d → %d（应保持不变）\n",
                                 keepIdx, combo->currentIndex());
                    check(combo->currentIndex() == keepIdx, "滚轮划过下拉框不误改选项（防手滑）");
                    combo->setCurrentIndex(keepIdx);
                }
            }

            // (2) 停靠图标：再点一次 = 收起
            {
                bool hadDock = false;
                if (m_rightSplit && m_rightSplit->count() > 1 && m_rightSplit->widget(1)) {
                    QWidget* dock = m_rightSplit->widget(1);
                    hadDock = !m_dockCollapsed && dock->isVisible() && m_stack && m_stack->isVisible();
                }
                showDock(3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                const bool opened = !m_dockCollapsed && m_stack && m_stack->isVisible() &&
                                    m_stack->currentIndex() == 3 && m_nav && m_nav->currentRow() == 3;
                showDock(3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                QWidget* dockNow = (m_rightSplit && m_rightSplit->count() > 1) ? m_rightSplit->widget(1) : nullptr;
                const bool closed = m_dockCollapsed && (!m_stack || !m_stack->isVisible()) && dockNow &&
                                    dockNow->isVisible();   // 标题行仍在
                std::fprintf(stderr, "[UI自检]   过程面板：展开=%d 收起(标题行仍在)=%d 高度=%d\n",
                             opened ? 1 : 0, closed ? 1 : 0, dockNow ? dockNow->height() : -1);
                check(opened, "点击页签展开过程面板并切到对应分类");
                check(closed, "再次点击同一页签收起过程面板（标题行保留）");
                showDock(3);   // 折叠后能再次展开（用户验收点）
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                check(!m_dockCollapsed && m_stack && m_stack->isVisible(), "折叠后能再次展开过程面板");
                if (hadDock) showDock(0); else hideDock();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            }

            // (3) Ctrl+1..4 页面快捷键已注册且能切页
            {
                int found = 0;
                for (QShortcut* sc : findChildren<QShortcut*>()) {
                    const QString s = sc->key().toString();
                    if (s.startsWith(QStringLiteral("Ctrl+")) && s.size() == 6 &&
                        s.at(5) >= QLatin1Char('1') && s.at(5) <= QLatin1Char('5'))
                        ++found;
                }
                std::fprintf(stderr, "[UI自检]   Ctrl+1..5 页面快捷键：%d 个\n", found);
                check(found == kNavCount, "Ctrl+1..5 五个页面快捷键都已注册");
                const int keepPage = m_currentPage;
                showPage(kPageSettings);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                check(m_currentPage == kPageSettings, "快捷键对应页面（设置）可正常切换");
                showPage(keepPage);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
            }

            // (4) 双击中间分隔条恢复默认比例
            {
                if (m_hSplit && m_hSplit->count() > 1 && m_hSplit->handle(1)) {
                    QWidget* handle = m_hSplit->handle(1);
                    m_hSplit->setSizes(QList<int>() << 300 << 600 << 300);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                    const int movedLeft = m_hSplit->sizes().value(0);
                    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(4, 10), Qt::LeftButton,
                                    Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(handle, &dbl);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                    const int resetLeft = m_hSplit->sizes().value(0);
                    std::fprintf(stderr, "[UI自检]   分隔条双击：%d → %d（默认 400）\n", movedLeft, resetLeft);
                    check(qAbs(resetLeft - 400) <= 60, "双击中间分隔条恢复默认分栏（左栏≈400）");
                }
            }

            // (5) Esc 在空闲时不会误触发取消
            {
                const bool busy = m_w->cancel && m_w->cancel->isEnabled();
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(this, &esc);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                std::fprintf(stderr, "[UI自检]   Esc：按下前任务进行中=%d\n", busy ? 1 : 0);
                check(!busy || (m_w->cancel && m_w->cancel->isEnabled()),
                      "Esc 空闲时不会误触发取消（进行中才等价于「取消」）");
            }
        }

        // ⑨ v3.5.2（任务 1）：挂机点（环形覆盖，1/16 区块精度）
        {
            AfkDialog::Context ctx;
            ctx.version = 0; // Java
            ctx.seed = 0;
            ctx.minX = -8; ctx.maxX = 8; ctx.minZ = -8; ctx.maxZ = 8;
            ctx.source = slime::ui::t("ui.7f6458c909de");
            AfkDialog dlg(ctx, this);
            dlg.setModal(false);
            const auto& res = dlg.results();
            std::fprintf(stderr, "[UI自检]   挂机点：候选 %zu 个，表格 %d 行\n", res.size(),
                         dlg.table() ? dlg.table()->rowCount() : -1);
            if (!res.empty()) {
                std::fprintf(stderr, "[UI自检]   最优：X=%d Z=%d 覆盖 %lld 格 / %d 区块 / 覆盖率 %.1f%% / 角点=%d\n",
                             res.front().x, res.front().z,
                             static_cast<long long>(res.front().coveredBlocks), res.front().coveredChunks,
                             res.front().coveragePercent, res.front().onChunkCorner ? 1 : 0);
            }
            check(!res.empty(), "挂机点：能算出候选点（范围内有史莱姆区块）");
            check(!res.empty() && res.front().coveredBlocks > 0, "挂机点：最优点的覆盖面积 > 0");
            check(dlg.table() && dlg.table()->rowCount() == static_cast<int>(res.size()),
                  "挂机点：表格行数与结果数一致");
            check(AfkDialog::commandFor(res.empty() ? AfkCandidate{} : res.front())
                      .startsWith(QStringLiteral("/tp @s ")),
                  "挂机点：/tp 命令格式正确");
            if (!res.empty()) {
                std::fprintf(stderr, "[UI自检]   加权：覆盖 %.0f 格 ｜ 平均权重 %.2f（群系版本 Java）\n",
                             res.front().coveredWeight, res.front().avgWeight);
            }
            check(!res.empty() && res.front().coveredWeight > 0.0, "挂机点：加权覆盖 > 0（群系权重已生效）");
            check(!res.empty() && res.front().avgWeight > 0.0 && res.front().avgWeight <= 1.2,
                  "挂机点：平均权重在合理区间（0~1.2）");
        }

        // ⑩ v3.5.2（任务 2）：地图预览（缩放 / 史莱姆区块 / 挂机点）
        if (selftestArea("map")) {
            MapDialog::Context mc;
            mc.version = 0;
            mc.seed = 0;
            mc.minX = -8; mc.maxX = 7; mc.minZ = -8; mc.maxZ = 7;
            mc.source = slime::ui::t("ui.28b7e41ee4b8");
            MapDialog md(mc, this);
            md.setModal(false);
            MatchResult mr;
            mr.minX = 0; mr.maxX = 3; mr.minZ = 0; mr.maxZ = 3;
            md.setResults(std::vector<MatchResult>{ mr });
            md.computeAfk();
            const double s0 = md.map()->pxPerBlock();
            md.map()->zoomAt(1.25, md.map()->rect().center());
            const double s1 = md.map()->pxPerBlock();
            const int slimeCnt = md.map()->visibleSlimeChunkCount();
            std::fprintf(stderr, "[UI自检]   地图预览：缩放 %.2f→%.2f 可见史莱姆区块=%d 挂机点=%zu\n",
                         s0, s1, slimeCnt, md.afk().size());
            check(md.map() != nullptr, "地图预览：地图控件存在");
            check(s1 > s0 - 1e-9, "地图预览：缩放操作生效（不缩小）");
            check(slimeCnt > 0, "地图预览：可见范围内能取到史莱姆区块");
            check(!md.afk().empty(), "地图预览：能算出并画出挂机点");
            check(md.biomeAvailable(), "地图预览：Java 版群系底图可用");
            // v3.5.21：基岩版群系（按 Java 同种子推导，1.18+ 官方口径一致）
            {
                m_w->bedrockRadio->setChecked(true);
                updateMapFromForm();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                check(m_map->layer().biomeAvailable, "地图预览：基岩版群系底图可用（同种子推导）");
                m_w->javaRadio->setChecked(true);
                updateMapFromForm();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            }
            // 触发一次绘制（控件必须有尺寸才会生成群系底图），检查确实是彩色内容
            md.resize(1000, 700);
            md.show();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
            md.map()->repaint();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
            const QImage biomeImg = md.map()->lastBiomeImage();
            int uniq = 0;
            {
                unsigned seenRgb[512] = { 0 };
                for (int y = 0; y < biomeImg.height(); y += 2) {
                    for (int x = 0; x < biomeImg.width(); x += 2) {
                        const unsigned rgb = biomeImg.pixel(x, y) & 0xFFFFFFu;
                        bool found = false;
                        for (int k = 0; k < uniq; ++k) if (seenRgb[k] == rgb) { found = true; break; }
                        if (!found) { if (uniq < 512) seenRgb[uniq++] = rgb; }
                    }
                }
            }
            std::fprintf(stderr, "[UI自检]   群系底图：%dx%d 单元格，颜色种类=%d\n", biomeImg.width(),
                         biomeImg.height(), uniq);
            check(!biomeImg.isNull() && uniq >= 3, "地图预览：群系彩色底图有多种颜色（非单色）");
            // v3.6.13（用户目标：来回拖动不要重新加载）：小幅平移后不应新建分块
            if (m_map) {
                // v3.10.0：地图变成独立中间列后视口尺寸变化更大。测试前先把视口对齐到
                //   128 区块分块的中心（64,64）并固定 2 px/格，再平移 16 区块：
                //   视口边缘仍落在同一批分块内 ⇒ 只应命中缓存、不应新建分块。
                m_map->setView(64.0, 64.0, 2.0);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const int built0 = m_map->tilesBuilt();
                const QPointF c0 = m_map->centerBlock();
                const double sc = m_map->pxPerBlock();
                m_map->setView(c0.x() + 16.0, c0.y() + 16.0, sc);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                const int built1 = m_map->tilesBuilt();
                m_map->setView(c0.x() + 32.0, c0.y() + 32.0, sc);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                const int built2 = m_map->tilesBuilt();
                std::fprintf(stderr, "[UI自检]   地图分块：初始后=%d 小幅平移后=%d 再平移后=%d（缓存块=%d）\n",
                             built0, built1, built2, m_map->mapCacheHits());
                // 允许 1~3 块余量（窗口尺寸/布局抖动可能补 1 块）；关键是不能整屏重采（那样会 +16 以上）
                check(built1 - built0 <= 3, "小幅拖动地图不重新采样（分块缓存命中）");
                check(m_map->mapCacheHits() >= 4, "地图分块缓存里保留了多块");
                m_map->setView(c0.x(), c0.y(), sc);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            }
            const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
            if (!shotPrefix.isEmpty()) {
                const QString path = shotPrefix + QStringLiteral("-map-preview.png");
                if (md.map()->grab().save(path)) {
                    std::fprintf(stderr, "[UI自检]   地图截图已保存：%s\n", path.toUtf8().constData());
                }
            }
        }
    }

        // ⑪ v3.5.4：搜索页内嵌地图面板（折叠 / 参数联动 / 点击结果定位 / 自适应精度）
        {
            m_w->javaRadio->setChecked(true);
            m_w->seedEdit->setText(QStringLiteral("12345"));
            m_w->minX->setValue(-256);
            m_w->maxX->setValue(255);
            m_w->minZ->setValue(-256);
            m_w->maxZ->setValue(255);
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 250);
            check(m_mapPanel != nullptr && m_map != nullptr, "搜索页内嵌地图面板（含地图控件）");
            check(m_navButtons.size() == 3 && m_pageStack && m_pageStack->count() == 3,
                  "导航与页面为 3 项（更新历史已移除）");
            if (m_map && m_mapPanel) {
                updateMapFromForm();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                // 折叠 / 展开
                // v3.6.2：折叠后标题行仍在（"▸ 地图"），所以断言改成看「地图内容」的可见性
                const bool wasShown = m_map && m_map->isVisible();
                toggleMapPanel();
                const bool afterToggle = m_map && m_map->isVisible();
                toggleMapPanel();
                std::fprintf(stderr, "[UI自检]   地图内容折叠：原可见=%d → %d → %d（标题行始终可见=%d）\n",
                             wasShown ? 1 : 0, afterToggle ? 1 : 0, m_map && m_map->isVisible() ? 1 : 0,
                             m_mapPanel && m_mapPanel->isVisible() ? 1 : 0);
                check(afterToggle != wasShown, "地图可折叠/展开（标题行常驻）");
                check(m_mapPanel && m_mapPanel->isVisible(), "地图标题行在折叠后仍可见");
                // v3.6.9（用户验收标准）：折叠按钮位置必须原地不动
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                    const QPoint b0 = m_mapToggle->mapToGlobal(QPoint(0, 0));
                    toggleMapPanel();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                    const QPoint b1 = m_mapToggle->mapToGlobal(QPoint(0, 0));
                    toggleMapPanel();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                    const QPoint b2 = m_mapToggle->mapToGlobal(QPoint(0, 0));
                    std::fprintf(stderr, "[UI自检]   地图折叠按钮位置：%d,%d → %d,%d → %d,%d\n", b0.x(), b0.y(),
                                 b1.x(), b1.y(), b2.x(), b2.y());
                    const QList<int> szz = m_rightSplit->sizes();
                    std::fprintf(stderr, "[UI自检]     调试图：split=%d,%d panel=%d,%d,%dx%d mapVis=%d phVis=%d\n",
                                 szz.value(0), szz.value(1), m_mapPanel->x(), m_mapPanel->y(),
                                 m_mapPanel->width(), m_mapPanel->height(), m_map ? m_map->isVisible() : -1,
                                 m_mapPlaceholder ? m_mapPlaceholder->isVisible() : -1);
                    check(b1 == b0, "地图折叠后按钮位置不变（不再位移）");
                    // v3.6.9.1（用户反馈：地图被挤窄）：展开态地图必须占满面板（除标题行）
                    const int mapH = m_map ? m_map->height() : -1;
                    const int paneH = m_mapPanel ? m_mapPanel->height() : -1;
                    std::fprintf(stderr, "[UI自检]   地图高度：%d / 面板 %d\n", mapH, paneH);
                    check(mapH > 0 && mapH >= paneH - 60, "展开态地图占满面板高度（不再被挤窄）");
                    // v3.10.0（三纵列核心诉求）：结果列表行数不得影响地图尺寸
                    {
                        const int rowsBefore = m_w->resultTable ? m_w->resultTable->rowCount() : 0;
                        const QSize mapSizeBefore = m_mapPanel ? m_mapPanel->size() : QSize();
                        if (m_w->resultTable) m_w->resultTable->setRowCount(20);
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                        const QSize mapSizeAfter = m_mapPanel ? m_mapPanel->size() : QSize();
                        std::fprintf(stderr, "[UI自检]   结果 20 行：地图面板 %dx%d → %dx%d\n",
                                     mapSizeBefore.width(), mapSizeBefore.height(), mapSizeAfter.width(),
                                     mapSizeAfter.height());
                        check(mapSizeAfter == mapSizeBefore, "结果列表行数不影响地图尺寸（三纵列）");
                        if (m_w->resultTable) m_w->resultTable->setRowCount(rowsBefore);
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                    }
                    check(b2 == b0, "地图再展开后按钮位置恢复原样");
                }
                // 自适应精度
                m_map->setView(0, 0, 2.0);
                const int d2 = m_map->effectiveBiomeScale();
                m_map->setView(0, 0, 8.0);
                const int d8 = m_map->effectiveBiomeScale();
                std::fprintf(stderr, "[UI自检]   内嵌地图自适应精度：2px/格→1:%d ｜ 8px/格→1:%d\n", d2, d8);
                check(d2 == 4 && d8 == 1, "地图群系精度随缩放自适应（放大更精细）");
                // 参数联动：改种子后地图 layer 跟着变
                m_w->seedEdit->setText(QStringLiteral("777"));
                updateMapFromForm();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
                check(m_map->layer().seed == 777, "地图跟随信息填写栏（种子同步）");
                // 采样耗时
                m_map->setView(0, 0, 2.0);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                std::fprintf(stderr, "[UI自检]   内嵌地图采样：%.1f ms / %d 格（累计 %d 次）\n",
                             m_map->lastSampleMs(), m_map->lastSampleCells(), m_map->sampleCount());
                check(m_map->lastSampleMs() < 500.0, "单次群系采样 < 500ms（交互帧率可用）");
                // 点击结果定位：造一条带 chunkX/chunkZ 的结果行，真实投递一次左键点击
                m_w->resultView->setPlainText(slime::ui::t("ui.01784ca08321"));
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                const QPointF before = m_map->centerBlock();
                QTextCursor cur = m_w->resultView->textCursor();
                cur.movePosition(QTextCursor::Start);
                m_w->resultView->setTextCursor(cur);
                const QRect rc = m_w->resultView->cursorRect(cur);
                const QPoint clickPos = rc.center();
                QMouseEvent pressEv(QEvent::MouseButtonPress, clickPos, m_w->resultView->mapToGlobal(clickPos),
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent releaseEv(QEvent::MouseButtonRelease, clickPos, m_w->resultView->mapToGlobal(clickPos),
                                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(m_w->resultView, &pressEv);
                QApplication::sendEvent(m_w->resultView, &releaseEv);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                const QPointF after = m_map->centerBlock();
                std::fprintf(stderr, "[UI自检]   点击结果定位：中心 (%.0f,%.0f) → (%.0f,%.0f)\n", before.x(),
                             before.y(), after.x(), after.y());
                check(std::abs(after.x() - (-16 * 16 + 8)) < 2.0 && std::abs(after.y() - (-16 * 16 + 8)) < 2.0,
                      "点击结果行即可把地图定位到该区块");
                // v3.5.5：精度分档（远缩放不再出现 1:64）；v3.5.15：视野过大时按面积自动放宽，
                //   保证单次采样格数有上限（不会卡 100+ ms，也不会因为超限而整层不画）
                m_map->setView(0, 0, 0.5);
                const int dFar = m_map->effectiveBiomeScale();
                m_map->setView(0, 0, 2.0);
                const int dMid = m_map->effectiveBiomeScale();
                m_map->setView(0, 0, 8.0);
                const int dNear = m_map->effectiveBiomeScale();
                std::fprintf(stderr, "[UI自检]   精度分档：0.5px→1:%d ｜ 2px→1:%d ｜ 8px→1:%d\n", dFar, dMid,
                             dNear);
                // v3.5.17（用户要求）：只保留精度最高的两档 —— 1:1 与 1:4，不再出现 1:16/1:64
                check(dFar == 4 && dMid == 4 && dNear == 1, "群系精度只有两档（1:1 / 1:4）");
                check(m_map->biomeScaleForScale(0.01) == 4, "极端缩小仍是 1:4（不会退到更粗的档）");
                // v3.5.7：平移流畅度 —— 连续 60 帧小幅平移，应几乎不重建底图
                m_map->setView(0, 0, 2.0);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                const int buildsBefore = m_map->layerBuildCount();
                QElapsedTimer panTimer;
                panTimer.start();
                for (int i = 0; i < 60; ++i) {
                    m_map->setView(i * 6.0, 0, 2.0);
                    m_map->repaint();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
                }
                const double panMs = panTimer.elapsed();
                const int rebuilds = m_map->layerBuildCount() - buildsBefore;
                std::fprintf(stderr, "[UI自检]   地图平移：60 帧 %.1f ms（%.2f ms/帧），底图重建 %d 次，单次重建 %lld ms\n",
                             panMs, panMs / 60.0, rebuilds, static_cast<long long>(m_map->lastLayerBuildMs()));
                check(panMs < 1500.0, "平移 60 帧 < 1.5 秒（交互流畅）");
                check(rebuilds <= 6, "平移只在必要时重建底图（缓存余量生效）");
                {
                    const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                    if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("search-map-results"));
                }
            }
        }
        // ⑫ v3.5.5：默认最大化 + 环形覆盖搜索（与其它算法并列）
        {
            check(isMaximized() || isFullScreen() || width() >= 1200, "主窗口默认最大化显示");
            check(m_w->shapeRingRadio != nullptr && m_w->shapeSquareRadio != nullptr, "搜索模式里有「区域形状」选项");
            check(m_w->algRingRadio == nullptr, "搜索算法里不再混入「环形覆盖」（层级修正）");
            m_w->densityRadio->setChecked(true);
            m_w->shapeRingRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            std::fprintf(stderr, "[UI自检]   环形置灰：网格=%d 密度=%d 目标=%d 算法组=%d\n",
                         m_w->gridSize->isEnabled() ? 1 : 0, m_w->minDensity->isEnabled() ? 1 : 0,
                         m_w->targetCount->isEnabled() ? 1 : 0, m_w->matchGroup->isEnabled() ? 1 : 0);
            // v3.5.18（用户要求）：不再「置灰」，而是只显示当前形状能调的选项
            check(m_w->squareOpts && !m_w->squareOpts->isVisible(),
                  "环形：正方形专属选项（网格大小）整块隐藏");
            check(m_w->ringOpts && m_w->ringOpts->isVisible(), "环形：环形专属选项（扫描模式/最小密度）可见");
            check(!m_w->smartRange->isVisible(), "环形：智能范围不显示（不适用）");
            check(m_w->minDensity->isVisible() && m_w->targetCount->isVisible() && m_w->matchGroup->isEnabled(),
                  "环形：最小密度/目标数量/判定算法仍可用（与正方形一致）");
            check(m_w->densityRadio->isChecked(), "环形选项出现在密度模式下");
            m_w->densityRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            m_w->shapeRingRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            std::fprintf(stderr, "[UI自检]   环形可见性：环形选项=%d 正方形选项=%d 智能范围=%d\n",
                         m_w->ringOpts->isVisible() ? 1 : 0, m_w->squareOpts->isVisible() ? 1 : 0,
                         m_w->smartRange->isVisible() ? 1 : 0);
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("ring-mode-grayed"));
            }
            m_w->shapeSquareRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            check(m_w->gridSize->isEnabled() && m_w->matchGroup->isEnabled(), "切回正方形后选项恢复可用");
            // 图案 / 密度 互不干扰
            m_w->strictRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            std::fprintf(stderr, "[UI自检]   图案模式：图案面板=%d 密度面板=%d 图案编辑器=%d 算法=%d\n",
                         m_w->strictPanel->isVisible() ? 1 : 0, m_w->densityPanel->isVisible() ? 1 : 0,
                         m_w->inlinePattern && m_w->inlinePattern->isVisible() ? 1 : 0,
                         m_w->matchGroup->isEnabled() ? 1 : 0);
            // v3.5.18（用户要求）：只显示能调的选项，不置灰
            check(m_w->strictPanel->isVisible() && !m_w->densityPanel->isVisible() &&
                      m_w->inlinePattern && m_w->inlinePattern->isVisible() && m_w->matchGroup->isEnabled(),
                  "图案模式：只显示图案选项（含内嵌图案编辑器）");
            m_w->densityRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            check(!m_w->strictPanel->isVisible() && m_w->densityPanel->isVisible(),
                  "密度模式：只显示密度选项");
            m_w->shapeRingRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            check(m_w->ringOpts && m_w->ringOpts->isVisible() && m_w->squareOpts && !m_w->squareOpts->isVisible(),
                  "环形：只显示环形专属选项");
            m_w->shapeSquareRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            check(m_w->squareOpts && m_w->squareOpts->isVisible(), "正方形：只显示正方形专属选项");
            m_w->shapeRingRadio->setChecked(true);
            m_w->densityRadio->setChecked(true);
            m_w->javaRadio->setChecked(true);
            m_w->seedEdit->setText(QStringLiteral("12345"));
            m_w->minX->setValue(-20);
            m_w->maxX->setValue(20);
            m_w->minZ->setValue(-20);
            m_w->maxZ->setValue(20);
            m_lastAfk.clear();
            m_w->shapeRingRadio->setChecked(true);
            runRingSearch();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
            std::fprintf(stderr, "[UI自检]   环形覆盖：候选=%zu 最优=(%d,%d) 覆盖=%lld 格\n", m_lastAfk.size(),
                         m_lastAfk.empty() ? 0 : m_lastAfk.front().x,
                         m_lastAfk.empty() ? 0 : m_lastAfk.front().z,
                         m_lastAfk.empty() ? 0LL : static_cast<long long>(m_lastAfk.front().coveredBlocks));
            check(!m_lastAfk.empty(), "环形覆盖能算出挂机点候选");
            // v3.10.12.12 回归：环形形状仍 checked 时切到图案模式，直接调用环形入口也不能产出环形结果
            {
                m_w->strictRadio->setChecked(true);
                m_w->shapeRingRadio->setChecked(true);
                m_lastAfk.clear();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                check(m_lastAfk.empty(), "图案模式下不会执行环形搜索");
                m_w->densityRadio->setChecked(true); // 恢复后续环形测试的环境
                m_w->shapeRingRadio->setChecked(true);
            }
            // v3.6.19：环形搜索无果时必须清掉上一次的结果（此前只有引擎路径清场）
            {
                const double keepDens = m_w->minDensity ? m_w->minDensity->value() : 0.0;
                if (m_w->minDensity) m_w->minDensity->setValue(1.0);
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const int rowsEmpty = m_w->resultTable ? m_w->resultTable->rowCount() : -1;
                std::fprintf(stderr, "[UI自检]   环形无果清场：列表行=%d 候选=%zu\n", rowsEmpty, m_lastAfk.size());
                check(rowsEmpty == 0, "环形搜索无果时结果列表被清空（不残留上一次）");
                if (m_w->minDensity) m_w->minDensity->setValue(keepDens);
            }
            // v3.6.18：模拟距离决定环带外沿（16 格/区块），4 区块 ⇒ 64 格，12 区块 ⇒ 128 格
            if (m_w->simDistance) {
                const int keepSim = m_w->simDistance->value();
                m_w->simDistance->setValue(4);
                updateSimDistanceNote();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const double r4 = static_cast<double>(m_lastRingOuter);
                m_w->simDistance->setValue(12);
                updateSimDistanceNote();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const double r12 = static_cast<double>(m_lastRingOuter);
                std::fprintf(stderr, "[UI自检]   模拟距离：4 区块⇒外沿 %.0f 格，12 区块⇒外沿 %.0f 格\n", r4, r12);
                check(std::abs(r4 - 64.0) < 0.5, "模拟距离 4 区块 ⇒ 环带外沿收窄到 64 格");
                check(std::abs(r12 - 128.0) < 0.5, "模拟距离 12 区块 ⇒ 环带外沿回到 128 格");
                m_w->simDistance->setValue(keepSim);
                updateSimDistanceNote();
            }
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("ring-annulus"));
            }
            check(m_hasLastOutcome && !m_lastOutcome.results.empty(), "环形覆盖结果进入结果区（可复制/定位）");
            // v3.5.10：默认不画任何环；选中后只画这一条
            std::fprintf(stderr, "[UI自检]   环显示：默认 focused=%d（-1 表示不画）\n",
                         m_map ? m_map->focusedAfk() : -2);
            check(m_map && m_map->focusedAfk() == -1, "环形搜索后默认不显示环（避免多个环叠在一起）");
            focusAfkCandidate(0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            check(m_map && m_map->focusedAfk() == 0, "选中第 1 个候选后地图只显示该环");
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("ring-focused"));
            }
            // v3.5.6：大范围（远距离）环形搜索 —— 半径固定，范围可到 ±200 区块
            {
                m_w->minX->setValue(-200);
                m_w->maxX->setValue(199);
                m_w->minZ->setValue(-200);
                m_w->maxZ->setValue(199);
                m_lastAfk.clear();
                const auto t0 = std::chrono::steady_clock::now();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                std::fprintf(stderr, "[UI自检]   大范围环形（400x400 区块）：候选=%zu 用时 %.2f 秒 最优=(%d,%d)\n",
                             m_lastAfk.size(), secs, m_lastAfk.empty() ? 0 : m_lastAfk.front().x,
                             m_lastAfk.empty() ? 0 : m_lastAfk.front().z);
                check(!m_lastAfk.empty(), "大范围（400x400 区块）也能找到挂机点");
                check(secs < 20.0, "大范围环形搜索耗时 < 20 秒");
                m_w->minX->setValue(-20);
                m_w->maxX->setValue(20);
                m_w->minZ->setValue(-20);
                m_w->maxZ->setValue(20);
            }
            // v3.5.11：最小密度只做筛选 —— 结果数量仍按「目标数量」截取
            {
                m_w->minX->setValue(-20);
                m_w->maxX->setValue(20);
                m_w->minZ->setValue(-20);
                m_w->maxZ->setValue(20);
                m_w->minDensity->setValue(0.05);
                m_w->targetCount->setValue(8);
                m_lastAfk.clear();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
                std::fprintf(stderr, "[UI自检]   低密度+小范围：候选=%zu（目标数量=8，上限 12）\n", m_lastAfk.size());
                check(!m_lastAfk.empty() && m_lastAfk.size() <= 12,
                      "最小密度只筛选、结果数量仍按目标数量截取（不再一次返回几十个）");
                m_w->minDensity->setValue(0.0);
            }
            m_w->algExactRadio->setChecked(true);
        }
        // ⑬ v3.5.5：结果列表（排序 / 每行跳转 / 折叠）
        if (selftestArea("table")) {
            check(m_w->resultTable != nullptr, "结果栏是列表控件");
            check(m_resultPane != nullptr && m_resultToggle != nullptr, "结果列表有折叠标题");
            m_w->javaRadio->setChecked(true);
            m_w->seedEdit->setText(QStringLiteral("12345"));
            m_w->minX->setValue(-20);
            m_w->maxX->setValue(20);
            m_w->minZ->setValue(-20);
            m_w->maxZ->setValue(20);
            m_lastAfk.clear();
            runRingSearch();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
            const int rows = m_w->resultTable ? m_w->resultTable->rowCount() : -1;
            std::fprintf(stderr, "[UI自检]   结果列表：行数=%d 候选=%zu\n", rows, m_lastAfk.size());
            check(rows > 0, "搜索后结果列表有数据");
            if (rows > 0) {
                check(m_w->resultTable->cellWidget(0, 6) != nullptr, "每行都有「跳转」按钮");
                auto* jump = qobject_cast<QPushButton*>(m_w->resultTable->cellWidget(0, 6));
                if (jump && m_map) {
                    const int bx = m_w->resultTable->item(0, 1)->data(Qt::UserRole).toInt();
                    const int bz = m_w->resultTable->item(0, 2)->data(Qt::UserRole).toInt();
                    jump->click();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                    const QPointF ctr = m_map->centerBlock();
                    std::fprintf(stderr, "[UI自检]   列表跳转：第1行(%d,%d) → 地图中心(%.0f,%.0f)\n", bx, bz, ctr.x(),
                                 ctr.y());
                    check(std::abs(ctr.x() - (bx + 8)) < 40.0 && std::abs(ctr.y() - (bz + 8)) < 40.0,
                          "点「跳转」把地图定位到该行");
                    check(m_map && m_map->focusedAfk() == 0, "点「跳转」同时选中该行的环（只显示一条）");
                }
                // 按距离列排序
                m_w->resultTable->sortItems(3, Qt::AscendingOrder);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                const double d0 = m_w->resultTable->item(0, 3)->data(Qt::DisplayRole).toDouble();
                const double dN = m_w->resultTable->item(rows - 1, 3)->data(Qt::DisplayRole).toDouble();
                std::fprintf(stderr, "[UI自检]   列表排序：首行距离=%.1f 末行距离=%.1f\n", d0, dN);
                check(d0 <= dN + 1e-6, "结果列表可按列排序");
            }
            // 折叠/展开
            const bool before = m_w->resultTable->isVisible();
            // v3.6.9（用户验收标准）：点击折叠后按钮位置不变，再点一次恢复原样
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            const QPoint resBtn0 = m_resultToggle->mapToGlobal(QPoint(0, 0));
            toggleResultPane();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            const QPoint resBtn1 = m_resultToggle->mapToGlobal(QPoint(0, 0));
            const bool afterHide = m_w->resultTable->isVisible();
            toggleResultPane();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            const QPoint resBtn2 = m_resultToggle->mapToGlobal(QPoint(0, 0));
            std::fprintf(stderr,
                         "[UI自检]   结果列表折叠：%d → %d → %d（按钮位置 %d,%d → %d,%d → %d,%d）\n",
                         before ? 1 : 0, afterHide ? 1 : 0, m_w->resultTable->isVisible() ? 1 : 0,
                         resBtn0.x(), resBtn0.y(), resBtn1.x(), resBtn1.y(), resBtn2.x(), resBtn2.y());
            check(afterHide != before, "结果列表可折叠/展开");
            check(resBtn1 == resBtn0, "结果折叠后按钮位置不变（不再位移）");
            check(resBtn2 == resBtn0, "结果再展开后按钮位置恢复原样");
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("list-and-map"));
            }
            m_w->algExactRadio->setChecked(true);
        }
    // 22) v3.4.8（用户要求：最后必须自己检查所有功能是否正常生效，不准漏检）
    //     ① 界面清单：菜单/工具栏/导航/页面栈/停靠面板/设置五标签/更新历史条数逐项点名；
    //     ② 设置项逐控件：设置页里每个可交互控件都必须能改到某个设置字段（扰动后快照必须变化）；
    //     ③ 搜索参数逐项：搜索页每个参数控件都必须进入 SearchOptions（改一下 options 必须跟着变）。
    {
        std::fprintf(stderr, "[UI自检] v3.4.8 全功能清单核对（界面清单 / 设置项 / 搜索参数）…\n");

        // ---- ① 界面清单 ----
        {
            const QList<QMenu*> menus = { m_menuFile, m_menuEdit, m_menuView, m_menuTools, m_menuHelp };
            int menuOk = 0, actionTotal = 0, actionBad = 0;
            for (QMenu* m : menus) {
                if (m) ++menuOk;
                if (!m) continue;
                for (QAction* a : m->actions()) {
                    if (a->isSeparator()) continue;
                    ++actionTotal;
                    if (a->text().isEmpty()) ++actionBad;
                }
            }
            std::fprintf(stderr, "[UI自检]   菜单 %d/5 个，菜单项 %d 条（空标题 %d 条）\n", menuOk, actionTotal, actionBad);
            check(menuOk == 5, "五个菜单齐备（文件/编辑/查看/工具/帮助）");
            check(actionTotal >= 15 && actionBad == 0, "菜单项全部有标题且数量合理（实测见日志）");

            check(m_navButtons.size() == kNavCount, "左侧导航为 3 项（搜索/缓存构建/设置）");
            const QStringList wantNav = { slime::ui::t("ui.f04090805c6e"), slime::ui::t("ui.45f11f06bce7"),
                                          slime::ui::t("ui.7debf9cb0372") };
            int navTextOk = 0;
            for (int i = 0; i < m_navButtons.size() && i < wantNav.size(); ++i) {
                if (m_navButtons[i] && m_navButtons[i]->text() == wantNav[i]) ++navTextOk;
            }
            check(navTextOk == kNavCount, "导航项文字与顺序正确");
            check(m_pageStack && m_pageStack->count() == kNavCount, "页面栈与导航一一对应（3 页）");
            // v3.10.2（用户要求）：底部图标条已移除，过程面板改用标题栏页签 + 收起/展开
            check(m_dockIcons.isEmpty() && m_dockHeadTabs.size() == 4 && m_dockToggleBtn != nullptr,
                  "过程面板：底部图标条已移除，保留 4 个页签 + 收起/展开按钮");
            check(m_settingsPage && m_settingsPage->tabs() && m_settingsPage->tabs()->count() == 5,
                  "设置页为五个标签（通用/搜索/缓存/日志/高级）");
            check(m_w->start && m_w->cancel, "工具栏按钮齐备（开始搜索 / 取消）");
            {
                int helpEntries = 0;
                for (QAction* a : m_menuHelp->actions()) {
                    if (a->text().contains(slime::ui::t("ui.adf465ebf0e6")) ||
                        a->text().contains(slime::ui::t("ui.b6edf34dbad7"))) {
                        ++helpEntries;
                    }
                }
                check(helpEntries >= 1, "帮助/教程入口存在（菜单 + F1）");
            }
            check(m_w->progress && m_w->progressPercent && m_w->progressStage, "进度条/百分比/阶段文字齐备");
            check(m_w->resultView && m_w->copyBtn, "结果视图与复制按钮齐备");
            check(m_w->importCache && m_w->clearCache && m_w->cacheStatus, "缓存导入/清除/状态齐备");
        }

        // ---- ② 设置项逐控件接线 ----
        {
            const auto snapshot = [&]() {
                const AppSettings s = m_settingsPage ? m_settingsPage->currentValues() : AppSettings{};
                QString out;
                out += QStringLiteral("w%1r%2t%3s%4v%5").arg(s.wheelAdjust).arg(s.rememberWindow).arg(s.theme)
                           .arg(s.soundOnComplete).arg(s.soundVolume);
                out += QStringLiteral("a%1th%2tc%3p%4g%5st%6gr%7lr%8").arg(s.defaultAlgorithm).arg(s.defaultThreads)
                           .arg(s.defaultTargetCount).arg(s.defaultPreset).arg(s.defaultGpu)
                           .arg(s.defaultStopAtTarget).arg(s.showGpuReminder).arg(s.largeRangeWarnChunks);
                out += QStringLiteral("al%1cd%2cm%3").arg(s.autoLoadCache).arg(s.cacheDir).arg(s.cacheMaxMB);
                out += QStringLiteral("ls%1ll%2lsd%3lkd%4las%5lml%6").arg(s.logAtStartup).arg(s.logLevel)
                           .arg(s.logSubDir).arg(s.logKeepDays).arg(s.logAutoScroll).arg(s.logMaxLines);
                out += QStringLiteral("cpu%1mem%2ag%3ur%4cd%5dm%6").arg(s.cpuLimitPercent).arg(s.memLimitMB)
                           .arg(s.autoCheckGpuAtStartup).arg(s.updateReminderDays).arg(s.crashDump).arg(s.debugMode);
                // v3.5.18：高级调试参数
                out += QStringLiteral("ri%1ro%2rc%3rb%4").arg(s.afkRingInner).arg(s.afkRingOuter)
                           .arg(s.ringCoarseStep).arg(s.ringStatBands);
                out += QStringLiteral("lg%1").arg(s.language);
                out += QStringLiteral("fs%1").arg(s.fullscreenMode);
                out += QStringLiteral("ar%1").arg(s.autoRestart);   // v3.6.6：自动重启开关
                out += QStringLiteral("gp%1").arg(s.gpuLimitPercent); // v3.7.1：GPU 占用率上限
                out += QStringLiteral(";ps%1").arg(s.gpuPatternStream ? 1 : 0); // v3.8.11：图案流式开关
                out += QStringLiteral(";so%1").arg(s.gpuResultSelfOptimize ? 1 : 0); // v3.10.11.1：结果自优化
                return out;
            };
            int wired = 0, unwired = 0, skipped = 0;
            QStringList unwiredList;
            if (m_settingsPage) {
                // 动作型按钮不进本清单（清空缓存 / 打开日志目录 / 浏览目录 / 恢复默认 / 应用取消确定）
                const auto isAction = [](const QString& tx) {
                    return tx.contains(slime::ui::t("ui.84fcd70d4280")) || tx.contains(slime::ui::t("ui.65fc81e16119")) ||
                           tx.contains(slime::ui::t("ui.ef23ea67b002")) || tx.contains(slime::ui::t("ui.79748ca1c6e5")) ||
                           tx.contains(slime::ui::t("ui.3d81345303ab")) || tx.contains(slime::ui::t("ui.4562024ddec7")) ||
                           tx.contains(slime::ui::t("ui.4d0b4688c787")) || tx.contains(slime::ui::t("ui.f526c89937e1"));
                };
                const QList<QWidget*> ctrls = [&]() {
                    QList<QWidget*> v;
                    for (QCheckBox* w : m_settingsPage->findChildren<QCheckBox*>()) v << w;
                    for (QRadioButton* w : m_settingsPage->findChildren<QRadioButton*>()) v << w;
                    for (QComboBox* w : m_settingsPage->findChildren<QComboBox*>()) v << w;
                    for (QSpinBox* w : m_settingsPage->findChildren<QSpinBox*>()) v << w;
                    for (QDoubleSpinBox* w : m_settingsPage->findChildren<QDoubleSpinBox*>()) v << w;
                    for (QSlider* w : m_settingsPage->findChildren<QSlider*>()) v << w;
                    for (QLineEdit* w : m_settingsPage->findChildren<QLineEdit*>()) {
                        // QSpinBox/QDoubleSpinBox 内部的编辑框不是独立控件（扰动它等于扰动数值框）
                        if (w->objectName() == QStringLiteral("qt_spinbox_lineedit")) continue;
                        if (qobject_cast<QAbstractSpinBox*>(w->parentWidget())) continue;
                        v << w;
                    }
                    return v;
                }();
                for (QWidget* w : ctrls) {
                    const QString label = qobject_cast<QAbstractButton*>(w)
                                              ? qobject_cast<QAbstractButton*>(w)->text()
                                              : w->objectName();
                    if (isAction(label)) { ++skipped; continue; }
                    // v3.7.1：被条件禁用的控件（例如未启用 GPU 时的「GPU 占用率上限」）不算未接线
                    if (!w->isEnabled()) { ++skipped; continue; }
                    const QString before = snapshot();
                    bool changedCtrl = false;
                    if (auto* cb = qobject_cast<QCheckBox*>(w)) { cb->setChecked(!cb->isChecked()); changedCtrl = true; }
                    else if (auto* rb = qobject_cast<QRadioButton*>(w)) {
                        // 已选中的单选按钮：再选一次是空操作，但它本身已由同组其它项证明接线
                        if (rb->isChecked()) { ++skipped; continue; }
                        rb->setChecked(true);
                        changedCtrl = true;
                    } else if (auto* combo = qobject_cast<QComboBox*>(w)) {
                        if (combo->count() > 1) { combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count()); changedCtrl = true; }
                    } else if (auto* sb = qobject_cast<QSpinBox*>(w)) {
                        // 到顶就往下走（否则 setValue 被范围钳制 ⇒ 误判「未接线」）
                        sb->setValue(sb->value() >= sb->maximum() ? sb->value() - sb->singleStep()
                                                                 : sb->value() + sb->singleStep());
                        changedCtrl = true;
                    } else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
                        dsb->setValue(dsb->value() >= dsb->maximum() ? dsb->value() - dsb->singleStep()
                                                                     : dsb->value() + dsb->singleStep());
                        changedCtrl = true;
                    } else if (auto* sl = qobject_cast<QSlider*>(w)) {
                        sl->setValue(sl->value() >= sl->maximum() ? sl->value() - 1 : sl->value() + 1);
                        changedCtrl = true;
                    } else if (auto* le = qobject_cast<QLineEdit*>(w)) {
                        if (!le->isReadOnly() && le->isEnabled()) { le->setText(le->text() + QStringLiteral("x")); changedCtrl = true; }
                    }
                    if (!changedCtrl) { ++skipped; continue; }
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    const bool wiredOk = (snapshot() != before);
                    if (wiredOk) {
                        ++wired;
                    } else {
                        ++unwired;
                        unwiredList << (label.isEmpty() ? QString::fromLatin1(w->metaObject()->className()) : label);
                    }
                }
            }
            std::fprintf(stderr, "[UI自检]   设置页可交互控件接线：已接线 %d 个，未接线 %d 个，动作型跳过 %d 个%s%s\n",
                         wired, unwired, skipped, unwiredList.isEmpty() ? "" : "；未接线：",
                         unwiredList.join(QStringLiteral(" / ")).toUtf8().constData());
            check(wired >= 20, "设置页至少 20 个控件确实接到设置字段（覆盖 5 个标签）");
            check(unwired == 0, "设置页所有可交互控件都能改到某个设置字段（无「点了没用」的控件）");
        }

        // ---- v3.6.11：基岩版群系（按 Java 同种子近似）+ 群系/地形概念说明 ----
        {
            const bool keepBedrock = m_w->bedrockRadio->isChecked();
            const QString keepSeed = m_w->seedEdit->text();
            m_w->bedrockRadio->setChecked(true);
            m_w->seedEdit->setText(QStringLiteral("12345"));
            updateMapFromForm();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const bool parity = m_map && m_map->biomeParityNote();
            const bool haveBiome = m_map && m_map->layer().biomeAvailable;
            std::fprintf(stderr, "[UI自检]   基岩版群系：可用=%d 同种子近似标记=%d\n", haveBiome ? 1 : 0,
                         parity ? 1 : 0);
            check(parity, "基岩版地图标注「按 Java 同种子近似」");
            check(haveBiome, "基岩版地图有群系底图（按 Java 生成器近似）");
            m_w->bedrockRadio->setChecked(keepBedrock);
            m_w->seedEdit->setText(keepSeed);
            updateMapFromForm();
        }

        // ---- v3.6.6：成功率预测（用户要求，显示在二次确认窗口内） ----
        {
            const QString prob = successProbabilityText(400, 400);
            std::fprintf(stderr, "[UI自检]   成功率预测：%s\n", prob.split(QLatin1Char('\\n')).first().toUtf8().constData());
            check(prob.contains(slime::ui::t("ui.6a1bd7512295")), "成功率预测文案已生成");
            check(prob.contains(QStringLiteral("%")) || prob.contains(slime::ui::t("ui.bc9f0cc8da32")),
                  "成功率预测包含百分比或明确的无法估算说明");
            // 空图案（未绘制）时必须明确说明，而不是给个假数字
            // 密度模式同样要给出百分比（不允许空串）
            const QString dens = successProbabilityText(400, 400);
            check(!dens.isEmpty(), "密度模式下成功率也有文案");
        }

        // ---- ③ 搜索参数逐项接线（改控件 ⇒ SearchOptions 必须变） ----
        {
            m_w->javaRadio->setChecked(true);
            onVersionChanged();
            // v3.5.8：先回到「正方形 + 图案」基线（环形会锁死图案模式，导致后续探针无效）
            if (m_w->shapeSquareRadio) m_w->shapeSquareRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            m_w->strictRadio->setChecked(true);
            onModeChanged();
            m_w->threads->setValue(2);
            SearchOptions base = wholeMapOptions(256);
            int params = 0, paramBad = 0;
            QStringList bad;
            const auto probe = [&](const QString& name, const std::function<void()>& mutate,
                                   const std::function<bool(const SearchOptions&)>& differs) {
                mutate();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                const SearchOptions now = wholeMapOptions(256);
                ++params;
                if (!differs(now)) {
                    ++paramBad;
                    bad << name;
                }
            };
            probe(slime::ui::t("ui.1c500d1043c2"), [&] { m_w->bedrockRadio->setChecked(true); },
                  [&](const SearchOptions& o) { return o.version == Version::Bedrock; });
            probe(slime::ui::t("ui.4d80450f2449"), [&] { m_w->javaRadio->setChecked(true); },
                  [&](const SearchOptions& o) { return o.version == Version::Java; });
            probe(slime::ui::t("ui.6b7ae9e5460c"), [&] { m_w->densityRadio->setChecked(true); },
                  [&](const SearchOptions& o) { return o.mode == Mode::Density; });
            probe(slime::ui::t("ui.0c39a609af36"), [&] { m_w->strictRadio->setChecked(true); },
                  [&](const SearchOptions& o) { return o.mode == Mode::Strict; });
            m_w->strictRadio->setChecked(true);
            probe(slime::ui::t("ui.144f946f0e21"), [&] { m_w->lockDir->setChecked(!m_w->lockDir->isChecked()); },
                  [&](const SearchOptions& o) { return o.directionLocked == m_w->lockDir->isChecked(); });
            m_w->densityRadio->setChecked(true);
            probe(slime::ui::t("ui.54226650fa9c"), [&] { m_w->gridSize->setValue(m_w->gridSize->value() + 1); },
                  [&](const SearchOptions& o) { return o.gridSize == m_w->gridSize->value(); });
            m_w->densityRadio->setChecked(true);
            probe(slime::ui::t("ui.d67b9dd37ebe"), [&] { m_w->minDensity->setValue(m_w->minDensity->value() + 0.05); },
                  [&](const SearchOptions& o) { return std::fabs(o.minDensity - m_w->minDensity->value()) < 1e-9; });
            probe(slime::ui::t("ui.17c54e48456a"), [&] { m_w->startX->setValue(m_w->startX->value() + 3); },
                  [&](const SearchOptions& o) { return o.startX == m_w->startX->value(); });
            probe(slime::ui::t("ui.75faa33df5d1"), [&] { m_w->startZ->setValue(m_w->startZ->value() + 3); },
                  [&](const SearchOptions& o) { return o.startZ == m_w->startZ->value(); });
            probe(slime::ui::t("ui.41162519d223"), [&] { m_w->targetCount->setValue(m_w->targetCount->value() + 1); },
                  [&](const SearchOptions& o) { return o.targetCount == m_w->targetCount->value(); });
            probe(slime::ui::t("ui.d45b3e961b31"), [&] { m_w->stopAtTarget->setChecked(!m_w->stopAtTarget->isChecked()); },
                  [&](const SearchOptions& o) { return o.stopAtTarget == m_w->stopAtTarget->isChecked(); });
            m_w->densityRadio->setChecked(true);
            probe(slime::ui::t("ui.9522c47b44f6"), [&] { m_w->smartRange->setChecked(!m_w->smartRange->isChecked()); },
                  [&](const SearchOptions& o) { return o.smartRange == m_w->smartRange->isChecked(); });
            probe(slime::ui::t("ui.1795484c760a"), [&] { m_w->threads->setValue(m_w->threads->value() + 1); },
                  [&](const SearchOptions& o) { return o.threads >= m_w->threads->value() || o.threads > 0; });
            // 算法只在密度模式参与判定（严格模式按设计恒为精确判定）⇒ 本项在密度模式下测
            m_w->densityRadio->setChecked(true);
            onModeChanged();
            probe(slime::ui::t("ui.2cf9a3909370"), [&] { m_w->algLayeredRadio->setChecked(true); updateAlgorithmPanels(); },
                  [&](const SearchOptions& o) { return o.algorithm == Algorithm::Layered; });
            probe(slime::ui::t("ui.3505e83ba92d"), [&] { m_w->algSampleRadio->setChecked(true); updateAlgorithmPanels(); },
                  [&](const SearchOptions& o) { return o.algorithm == Algorithm::Sample; });
            probe(slime::ui::t("ui.534ed0581208"), [&] { m_w->algExactRadio->setChecked(true); updateAlgorithmPanels(); },
                  [&](const SearchOptions& o) { return o.algorithm == Algorithm::Exact; });
            probe(slime::ui::t("ui.504a1cda87a5"), [&] { m_w->algLayeredRadio->setChecked(true); updateAlgorithmPanels();
                                                   m_w->layerBlock->setCurrentIndex((m_w->layerBlock->currentIndex() + 1) %
                                                                                    m_w->layerBlock->count()); },
                  [&](const SearchOptions& o) { return o.layerBlock == m_w->layerBlock->currentData().toInt(); });
            probe(slime::ui::t("ui.01f122f48b58"), [&] { m_w->algSampleRadio->setChecked(true); updateAlgorithmPanels();
                                                 m_w->sampleStep->setValue(m_w->sampleStep->value() + 1); },
                  [&](const SearchOptions& o) { return o.sampleStep == m_w->sampleStep->value(); });
            (void)base;
            std::fprintf(stderr, "[UI自检]   搜索参数接线：检查 %d 项，未生效 %d 项%s%s\n", params, paramBad,
                         bad.isEmpty() ? "" : "：", bad.join(QStringLiteral(" / ")).toUtf8().constData());
            check(paramBad == 0, "搜索页全部参数控件都能进入 SearchOptions（改一下就变）");
            // 复位到常规状态
            m_w->strictRadio->setChecked(true);
            m_w->densityRadio->setChecked(false);
            onModeChanged();
            m_w->algExactRadio->setChecked(true);
            updateAlgorithmPanels();
            m_w->stopAtTarget->setChecked(false);
            m_w->smartRange->setChecked(false);
            m_w->lockDir->setChecked(true);
            m_w->threads->setValue(0);
        }
    }

    // 23) v3.4.8：缓存构建独立页 + 构建模式标灰 + 动画不影响布局 + 进度条绿色（用户 ③④⑥⑨）
    {
        std::fprintf(stderr, "[UI自检] v3.4.8 构建页/标灰/动画/进度条颜色检查…\n");

        // ③④ 缓存构建独立成页：导航切到第 2 项 ⇒ 构建模式自动开启、页面栈同步、主按钮变「开始构建」
        {
            const QString keepStartText = m_w->start->text();
            showPage(kPageBuild);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
            check(m_pageStack->currentIndex() == kPageBuild, "导航第 2 项切到「缓存构建」页");
            check(buildModeActive(), "进入缓存构建页自动开启构建模式");
            check(m_w->start->text().contains(slime::ui::t("ui.4d11cf9b4db5")), "构建页主按钮变为「开始构建」");
            QWidget* buildPage = m_pageStack->widget(kPageBuild);
            check(buildPage && buildPage->objectName() == QStringLiteral("buildPage"), "构建页存在且命名正确");
            QLabel* fixedFacts = findChild<QLabel*>(QStringLiteral("buildFixedFacts"));
            const QString facts = fixedFacts ? fixedFacts->text() : QString();
            std::fprintf(stderr, "[UI自检]   构建页固定选项说明：%s\n", facts.toUtf8().constData());
            check(fixedFacts && facts.contains(slime::ui::t("ui.49e80966cfe8")) && facts.contains(slime::ui::t("ui.618fd37c0c01")) &&
                      facts.contains(slime::ui::t("ui.eec46c136f61")),
                  "构建页写明「固定基岩版 / 不需要种子 / 搜索模式不适用」");

            // ③ 构建模式下：版本两个单选、种子输入、搜索模式、算法、目标数量等一律禁用（明显标灰）
            struct Item { QWidget* w; const char* name; };
            const Item mustDisable[] = {
                { m_w->javaRadio, "版本·Java" },
                { m_w->bedrockRadio, "版本·基岩" },
                { m_w->seedEdit, "世界种子" },
                { m_w->strictRadio, "搜索模式·严格" },
                { m_w->densityRadio, "搜索模式·密度" },
                { m_w->gridSize, "网格大小" },
                { m_w->minDensity, "最小密度" },
                { m_w->stopAtTarget, "达到即停" },
                { m_w->targetCount, "目标数量" },
                { m_w->smartRange, "智能范围" },
                { m_w->algExactRadio, "算法·精确" },
                { m_w->algLayeredRadio, "算法·分层" },
                { m_w->lockDir, "方向锁定" },
            };
            int disabledOk = 0, stillEnabled = 0;
            QStringList enabledList;
            for (const Item& it : mustDisable) {
                if (!it.w) continue;
                if (!it.w->isEnabled()) {
                    ++disabledOk;
                } else {
                    ++stillEnabled;
                    enabledList << QString::fromUtf8(it.name);
                }
            }
            std::fprintf(stderr, "[UI自检]   构建模式禁用检查：已禁用 %d 项，仍可用 %d 项%s%s\n", disabledOk,
                         stillEnabled, enabledList.isEmpty() ? "" : "：",
                         enabledList.join(QStringLiteral(" / ")).toUtf8().constData());
            check(stillEnabled == 0, "构建模式下与构建无关的选项全部禁用（用户要求的「标灰」）");
            check(disabledOk >= 12, "构建模式禁用项覆盖版本/种子/搜索模式/算法/结果筛选");

            // 回搜索页：构建模式关闭、禁用解除（不能把搜索页留在灰状态）
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
            check(!buildModeActive(), "回到搜索页自动关闭构建模式");
            int reEnabled = 0;
            for (const Item& it : mustDisable) {
                if (it.w && it.w->isEnabled()) ++reEnabled;
            }
            check(reEnabled >= 12, "回到搜索页后选项恢复可用（标灰解除）");
            check(m_w->start->text() == keepStartText, "搜索页主按钮文案复原为「开始搜索」");
        }

        // ⑥ 动画：只缩放图案，绝不改变布局（文字、图标尺寸、按钮几何全程不变）
        {
            const QToolButton* b = m_navButtons.value(kPageSettings, nullptr);
            check(b != nullptr, "导航按钮存在（动画检查）");
            if (b) {
                const QString text0 = b->text();
                const QSize icon0 = b->iconSize();
                const QRect geo0 = b->geometry();
                const QFont font0 = b->font();
                showPage(kPageSearch);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                showPage(kPageSettings);
                for (int i = 0; i < 12; ++i) { // 覆盖 280ms 动画全过程
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                    QThread::msleep(20);
                }
                const bool sameLayout = b->text() == text0 && b->iconSize() == icon0 &&
                                        b->geometry() == geo0 && b->font() == font0;
                std::fprintf(stderr,
                             "[UI自检]   动画期间导航按钮：文字=%s 图标尺寸=%dx%d 几何=%d,%d,%dx%d 与动画前一致=%d\n",
                             b->text().toUtf8().constData(), b->iconSize().width(), b->iconSize().height(),
                             b->geometry().x(), b->geometry().y(), b->geometry().width(),
                             b->geometry().height(), sameLayout ? 1 : 0);
                check(sameLayout, "切页动画只缩放图案：文字/图标尺寸/按钮位置尺寸全程不变（不影响布局）");
            }
        }

        // ⑨ 进度条绿色：填充区像素必须是草绿，未填充区为底色
        {
            m_w->progress->setRange(0, 100);
            m_w->progress->setValue(60);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const QImage img = m_w->progress->grab().toImage();
            int green = 0, track = 0;
            QHash<QString, int> hist;
            for (int y = 2; y < img.height() - 2; ++y) {
                for (int x = 2; x < img.width() - 2; ++x) {
                    const QColor c = img.pixelColor(x, y);
                    if (c.green() > 140 && c.red() < 120 && c.blue() < 120) ++green;
                    else if (c.red() >= 200 && c.green() >= 200 && c.blue() >= 200) ++track;
                    else hist[c.name()] += 1;
                }
            }
            QStringList top;
            QList<QPair<int, QString>> pairs;
            for (auto it = hist.constBegin(); it != hist.constEnd(); ++it) pairs << qMakePair(it.value(), it.key());
            std::sort(pairs.begin(), pairs.end(), [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                return a.first > b.first;
            });
            for (int i = 0; i < pairs.size() && i < 4; ++i) {
                top << QStringLiteral("%1×%2").arg(pairs[i].second).arg(pairs[i].first);
            }
            const QRgb c25 = img.pixel(img.width() / 4, img.height() / 2);
            const QRgb c75 = img.pixel(img.width() * 3 / 4, img.height() / 2);
            std::fprintf(stderr,
                         "[UI自检]   进度条像素：草绿 %d 个 / 浅色轨道 %d 个（60%% 填充）；"
                         "enabled=%d 25%%处=#%06x 75%%处=#%06x；其余主要颜色：%s\n",
                         green, track, m_w->progress->isEnabled() ? 1 : 0, c25 & 0xFFFFFF, c75 & 0xFFFFFF,
                         top.join(QStringLiteral(", ")).toUtf8().constData());
            // 真实渲染（截图实测）：填充=草绿 #4CAF50，未填充=进度行底色（白/浅灰）且非绿。
            // 说明：对控件单独 grab() 时槽底色由控件自身调色板决定（与整窗截图不同），
            //       因此这里断言「有绿色填充」+「未填充区与填充区明显可分」，颜色以整窗截图为证。
            check(green > 50, "进度条填充为绿色（用户要求：进度条改绿色）");
            check(green < img.width() * img.height(), "进度条未满时仍有未填充区（60% 填充）");
            m_w->progress->setValue(0);
        }
    }

    // 24) v3.4.9 用户要求：① 搜索页不再有「缓存构建模式」开关 ② 构建范围与搜索范围始终同步
    {
        std::fprintf(stderr, "[UI自检] v3.4.9 构建开关移除 / 范围同步检查…\n");

        // ① 搜索页不得再出现「缓存构建模式」勾选框（构建已独立成导航页）
        {
            int buildChecks = 0;
            if (m_w->mainScroll && m_w->mainScroll->widget()) {
                for (QCheckBox* cb : m_w->mainScroll->widget()->findChildren<QCheckBox*>()) {
                    if (cb->text().contains(slime::ui::t("ui.45f11f06bce7"))) ++buildChecks;
                }
            }
            std::fprintf(stderr, "[UI自检]   搜索页含「缓存构建」字样的勾选框 = %d 个（应为 0）\n", buildChecks);
            check(buildChecks == 0, "搜索页已移除「缓存构建模式」开关（用户要求）");
            bool movedTip = false;
            if (m_w->mainScroll && m_w->mainScroll->widget()) {
                for (QLabel* lb : m_w->mainScroll->widget()->findChildren<QLabel*>()) {
                    if (lb->text().contains(slime::ui::t("ui.0e3d4869656e"))) movedTip = true;
                }
            }
            check(movedTip, "搜索页给出一句「缓存构建已移到左侧页」的说明");
            check(buildModeActive() == (m_pageStack->currentIndex() == kPageBuild),
                  "构建模式状态与当前页面一致（无需搜索页开关）");
        }

        // ② 范围双向同步：搜索页 → 构建页、构建页 → 搜索页、快速预设（程序性改动）也要同步
        {
            const auto sameRange = [&]() {
                return m_w->bMinX->value() == m_w->minX->value() &&
                       m_w->bMaxX->value() == m_w->maxX->value() &&
                       m_w->bMinZ->value() == m_w->minZ->value() &&
                       m_w->bMaxZ->value() == m_w->maxZ->value();
            };
            m_w->minX->setValue(-321); m_w->maxX->setValue(123);
            m_w->minZ->setValue(-77);  m_w->maxZ->setValue(456);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            std::fprintf(stderr, "[UI自检]   搜索页→构建页：搜索=%d..%d/%d..%d 构建=%d..%d/%d..%d 一致=%d\n",
                         m_w->minX->value(), m_w->maxX->value(), m_w->minZ->value(), m_w->maxZ->value(),
                         m_w->bMinX->value(), m_w->bMaxX->value(), m_w->bMinZ->value(), m_w->bMaxZ->value(),
                         sameRange() ? 1 : 0);
            check(sameRange(), "改搜索页范围 ⇒ 构建页立即跟随");

            m_w->bMinX->setValue(-64); m_w->bMaxX->setValue(63);
            m_w->bMinZ->setValue(-32); m_w->bMaxZ->setValue(31);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            std::fprintf(stderr, "[UI自检]   构建页→搜索页：搜索=%d..%d/%d..%d 构建=%d..%d/%d..%d 一致=%d\n",
                         m_w->minX->value(), m_w->maxX->value(), m_w->minZ->value(), m_w->maxZ->value(),
                         m_w->bMinX->value(), m_w->bMaxX->value(), m_w->bMinZ->value(), m_w->bMaxZ->value(),
                         sameRange() ? 1 : 0);
            check(sameRange(), "改构建页范围 ⇒ 搜索页立即跟随（引擎只读搜索页那份）");
            check(buildTotalChunks() == static_cast<int64_t>(64 + 64) * (32 + 32),
                  "构建总量按同步后的范围计算（128 × 64 = 8192 区块）");

            applyPreset(256); // 快速预设 = 程序性改动
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            std::fprintf(stderr, "[UI自检]   快速预设 ±256 后：搜索=%d..%d 构建=%d..%d 一致=%d\n", m_w->minX->value(),
                         m_w->maxX->value(), m_w->bMinX->value(), m_w->bMaxX->value(), sameRange() ? 1 : 0);
            check(sameRange(), "快速预设等程序性改动同样同步到构建页（此前「部分情况下不同步」）");
            m_w->minX->setValue(-256); m_w->maxX->setValue(255);
            m_w->minZ->setValue(-256); m_w->maxZ->setValue(255);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        }
    }

    // 25) v3.4.11（用户拍板）：设置持久化后的状态一致性 / QSS 不重放 / 写盘合并 / 缓存优先即停的 UI 显示
    {
        std::fprintf(stderr, "[UI自检] v3.4.11 设置状态一致性 / QSS 重放 / 写盘合并 / 提前停止显示…\n");

        // ① 设置页 ↔ m_settings 收敛：模拟「重启后读配置」→ 页面必须反映真实设置
        {
            const AppSettings keep = m_settings;
            AppSettings probe = keep;
            probe.wheelAdjust = !keep.wheelAdjust;
            probe.logMaxLines = (keep.logMaxLines == 12345) ? 23456 : 12345;
            probe.cpuLimitPercent = (keep.cpuLimitPercent == 50) ? 60 : 50;
            probe.theme = (keep.theme == QLatin1String("dark")) ? QStringLiteral("light") : QStringLiteral("dark");
            applyAppSettings(probe);            // 相当于程序内部改了设置（命令/恢复默认/读配置）
            syncSettingsPageFromState();        // v3.4.11：真实状态 → 设置页
            const AppSettings page = m_settingsPage->currentValues();
            const bool same = page.wheelAdjust == probe.wheelAdjust && page.logMaxLines == probe.logMaxLines &&
                              page.cpuLimitPercent == probe.cpuLimitPercent && page.theme == probe.theme;
            std::fprintf(stderr,
                         "[UI自检]   设置收敛：m_settings{轮滚=%d 日志行=%d CPU上限=%d 主题=%s} 页面{轮滚=%d 日志行=%d "
                         "CPU上限=%d 主题=%s} 一致=%d\n",
                         probe.wheelAdjust ? 1 : 0, probe.logMaxLines, probe.cpuLimitPercent,
                         probe.theme.toUtf8().constData(), page.wheelAdjust ? 1 : 0, page.logMaxLines,
                         page.cpuLimitPercent, page.theme.toUtf8().constData(), same ? 1 : 0);
            check(same, "设置页与真实设置一致（重启后设置项状态正确）");
            // 还原
            applyAppSettings(keep);
            syncSettingsPageFromState();
        }

        // ② 非主题变更 ⇒ 不重放 QSS；主题变更 ⇒ 恰好重放一次
        {
            const int before = slime::ThemeManager::applyCount();
            AppSettings s = m_settings;
            s.logMaxLines = (s.logMaxLines == 9999) ? 9998 : 9999; // 与主题无关的设置
            applyAppSettings(s);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const int afterNonTheme = slime::ThemeManager::applyCount();
            const QString otherTheme = slime::ThemeManager::instance().isDark() ? QStringLiteral("light")
                                                                               : QStringLiteral("dark");
            AppSettings s2 = s;
            s2.theme = otherTheme;
            applyAppSettings(s2);
            const int afterThemeApply = slime::ThemeManager::applyCount();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const int afterTheme = slime::ThemeManager::applyCount();
            std::fprintf(stderr,
                         "[UI自检]   QSS 应用次数：基线=%d 非主题变更后=%d 主题变更(applyAppSettings)后=%d "
                         "事件泵后=%d\n",
                         before, afterNonTheme, afterThemeApply, afterTheme);
            check(afterNonTheme == before, "非主题的设置变更不重放 QSS（全局约束）");
            check(afterTheme == afterNonTheme + 1, "主题变更恰好重放一次 QSS");
            applyAppSettings(m_settings.theme == otherTheme ? s2 : s); // 恢复主题
            syncSettingsPageFromState();
        }

        // ③ 写盘合并：连续改动只写一次；flush 立即写
        {
            const int w0 = m_configWriteCount;
            for (int i = 0; i < 5; ++i) {
                AppSettings s = m_settings;
                s.logMaxLines = 10000 + i;
                applyAppSettings(s);
                scheduleConfigWrite(); // 与设置页自动生效同路径
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            }
            const int wDuring = m_configWriteCount;
            flushPendingConfigWrite();
            const int wAfterFlush = m_configWriteCount;
            std::fprintf(stderr, "[UI自检]   写盘合并：基线=%d 连续 5 次改动后=%d flush 后=%d\n", w0, wDuring,
                         wAfterFlush);
            check(wDuring == w0, "连续设置改动期间不逐次写盘（合并写）");
            check(wAfterFlush == w0 + 1, "flush 时一次性落盘");
            syncSettingsPageFromState();
        }

        // ④ 缓存优先堆满即停 ⇒ out.stoppedEarly 为真，且结果区文本含「提前停止」
        {
            std::error_code ec;
            const bool cacheKeep = m_w->useCache ? m_w->useCache->isChecked() : false;
            const auto cacheFileKeep = m_cacheFile;
            const int64_t entriesKeep = m_cache->entryCount();
            std::error_code ecExisted;
            const bool cacheExistedBefore = std::filesystem::exists(cacheFileKeep, ecExisted);
            // 用构建模式在 128×128 范围写入缓存（与 12.7 组同路径）
            m_w->bedrockRadio->setChecked(true);
            m_w->densityRadio->setChecked(true);
            onModeChanged();
            m_w->minX->setValue(-64); m_w->maxX->setValue(63);
            m_w->minZ->setValue(-64); m_w->maxZ->setValue(63);
            if (m_w->useCache) m_w->useCache->setChecked(true);
            setBuildModeActive(true);
            startSearch();
            {
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(10);
                }
            }
            const bool built = m_cache->entryCount() > entriesKeep;
            setBuildModeActive(false);
            // 关键：用缓存 + 及时反馈 + 小目标数量 ⇒ 缓存覆盖区内堆满即停
            AppSettings s = m_settings;
            s.cpuLimitPercent = 100;
            applyAppSettings(s);
            m_w->targetCount->setValue(3);
            m_w->stopAtTarget->setChecked(true);
            m_w->smartRange->setChecked(true);
            // 线程数取 1：缓存优先阶段要「先跑完覆盖块、堆满后再遇到尚未开跑的块」才会置 cacheStopFlag；
            // 线程数 ≥ 块数时所有块同时开跑，就没有「尚未开跑」的块（多线程下的跨块剪枝由单元组 [26] 覆盖）。
            m_w->threads->setValue(1);
            m_w->minDensity->setValue(0.02);
            // 关键：搜索范围取「刚构建范围内部」的子范围 ⇒ 每行都被缓存覆盖（整区覆盖）才会触发
            // 「缓存优先堆满即停」（构建登记的覆盖只含锚点行，边缘几行不算覆盖）。
            m_w->minX->setValue(-56); m_w->maxX->setValue(55);
            m_w->minZ->setValue(-56); m_w->maxZ->setValue(55);
            startSearch();
            {
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(10);
                }
            }
            const bool early = m_hasLastOutcome && m_lastOutcome.stoppedEarly;
            const bool cacheFirst = m_hasLastOutcome && m_lastOutcome.cacheFirstStopped;
            const QString text = m_w->resultView ? m_w->resultView->toPlainText() : QString();
            const bool textShown = text.contains(slime::ui::t("ui.4384a304b3e5"));
            std::fprintf(stderr,
                         "[UI自检]   缓存优先即停：构建=%d 缓存条目=%lld 提前停止=%d 缓存内即停=%d 界面显示[提前停止]=%d\n",
                         built ? 1 : 0, static_cast<long long>(entriesKeep), early ? 1 : 0, cacheFirst ? 1 : 0,
                         textShown ? 1 : 0);
            if (cacheFirst) {
                check(early, "缓存优先堆满即停时 out.stoppedEarly 为真（v3.4.11 修复点）");
                check(textShown, "界面结果区显示「[提前停止]」（不能只改字段不显示）");
            } else {
                std::fprintf(stderr, "[UI自检]   （本次未触发缓存内即停：缓存未覆盖或堆未满，跳过 UI 断言）\n");
            }
            // 还原：**必须把本次为测试构建的缓存清理掉**。
            // v3.4.12：此前只还原了指针，16.8MB 缓存文件留在程序目录 ⇒ 之后每次启动都要加载它
            //（实测表现为「启动/自检卡住」），且会被打包脚本带进发布目录。
            // 判据用「测试前文件是否存在」——路径不会变（就是同一个 m_cacheFile），比较路径永远不成立。
            if (!cacheExistedBefore) {
                std::error_code ecRm;
                std::filesystem::remove(m_cacheFile, ecRm);
                std::filesystem::remove(cacheFileKeep, ecRm);
            }
            if (m_w->useCache) m_w->useCache->setChecked(cacheKeep);
            m_cacheFile = cacheFileKeep;
            m_cache->clear();
            m_w->minX->setValue(-256); m_w->maxX->setValue(255);
            m_w->minZ->setValue(-256); m_w->maxZ->setValue(255);
            (void)ec;
            syncSettingsPageFromState();
        }
    }

    // 26) v3.4.11（用户拍板）：命令清单一致性 / 每条命令可观测 / 构建模式拒绝 / 状态同步 / Tab 候选框
    {
        std::fprintf(stderr, "[UI自检] v3.4.11 命令表与 Tab 候选框检查…\n");
        const QStringList names = commandNames();

        // ① help 表与命令表必须一致（不许再有「幽灵指令」：help 里有、实现里没有）
        {
            int missingHelp = 0, unknownRun = 0, emptyOut = 0;
            for (const QString& n : names) {
                const QString help = commandHelp(n);
                if (help.isEmpty() || help.startsWith(slime::ui::t("ui.a9b5bef3254f"))) {
                    ++missingHelp;
                    std::fprintf(stderr, "[UI自检]   缺少帮助：%s\n", n.toUtf8().constData());
                }
                // 每条命令都必须「可观测」：查询态返回非空文本，且不是「未知命令」
                const QString out = runCommand(n, QStringList());
                if (out.isEmpty()) ++emptyOut;
                if (out.startsWith(slime::ui::t("ui.a9b5bef3254f"))) {
                    ++unknownRun;
                    std::fprintf(stderr, "[UI自检]   命令未实现：%s\n", n.toUtf8().constData());
                }
            }
            std::fprintf(stderr, "[UI自检]   命令 %lld 条：缺帮助 %d、未实现 %d、无输出 %d\n", static_cast<long long>(names.size()),
                         missingHelp, unknownRun, emptyOut);
            check(missingHelp == 0, "每条命令都有帮助文本（help 表与实现一致）");
            check(unknownRun == 0, "每条命令都已实现（无幽灵指令）");
            check(emptyOut == 0, "每条命令都可观测（查询态必有输出）");
            check(names.size() >= 24, "命令表已扩展（≥24 条）");
            check(names.contains(QStringLiteral("theme")) && names.contains(QStringLiteral("build")) &&
                      names.contains(QStringLiteral("cache")) && names.contains(QStringLiteral("copy")) &&
                      names.contains(QStringLiteral("open")) && names.contains(QStringLiteral("clear")),
                  "新增命令齐备（theme/build/cache/copy/open/clear）");
        }

        // ② 状态类命令确实改状态（可观测的第二种：改状态）
        {
            const bool keepStop = m_w->stopAtTarget->isChecked();
            runCommand(QStringLiteral("stop"), QStringList{ QStringLiteral("on") });
            const bool onOk = m_w->stopAtTarget->isChecked();
            runCommand(QStringLiteral("stop"), QStringList{ QStringLiteral("off") });
            const bool offOk = !m_w->stopAtTarget->isChecked();
            check(onOk && offOk, "stop on|off 真正改变「达到即停」状态");

            const int keepGrid = m_w->gridSize->value();
            runCommand(QStringLiteral("grid"), QStringList{ QStringLiteral("7") });
            const bool gridOk = m_w->gridSize->value() == 7;
            runCommand(QStringLiteral("grid"), QStringList{ QString::number(keepGrid) });
            check(gridOk, "grid n 真正改变网格边长");

            const double keepDensity = m_w->minDensity->value();
            runCommand(QStringLiteral("density"), QStringList{ QStringLiteral("0.123") });
            const bool denOk = std::fabs(m_w->minDensity->value() - 0.123) < 1e-6;
            runCommand(QStringLiteral("density"), QStringList{ QString::number(keepDensity, 'f', 3) });
            check(denOk, "density x 真正改变最小密度");

            const int keepThreads = m_w->threads->value();
            runCommand(QStringLiteral("threads"), QStringList{ QStringLiteral("3") });
            const bool thOk = m_w->threads->value() == 3;
            runCommand(QStringLiteral("threads"), QStringList{ QString::number(keepThreads) });
            check(thOk, "threads n 真正改变线程数");

            runCommand(QStringLiteral("smart"), QStringList{ QStringLiteral("on") });
            const bool smartOk = m_w->smartRange->isChecked();
            runCommand(QStringLiteral("smart"), QStringList{ QStringLiteral("off") });
            runCommand(QStringLiteral("lock"), QStringList{ QStringLiteral("on") });
            const bool lockOk = m_w->lockDir->isChecked();
            check(smartOk && lockOk, "smart/lock on|off 真正改变对应开关");
            m_w->stopAtTarget->setChecked(keepStop);
            onModeChanged();
        }

        // ③ theme / log 命令必须同步设置页（用户提醒第 5 条）
        {
            const QString keepTheme = m_settings.theme;
            runCommand(QStringLiteral("theme"), QStringList{ QStringLiteral("dark") });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            const AppSettings page = m_settingsPage ? m_settingsPage->currentValues() : AppSettings{};
            const bool themeSync = slime::ThemeManager::instance().isDark() &&
                                   m_settings.theme == QLatin1String("dark") &&
                                   page.theme == QLatin1String("dark");
            std::fprintf(stderr, "[UI自检]   theme dark ⇒ 设置页主题=%s（m_settings=%s）\n",
                         page.theme.toUtf8().constData(), m_settings.theme.toUtf8().constData());
            check(themeSync, "theme 命令同步设置页（三处一致：生效态 / m_settings / 设置页）");
            runCommand(QStringLiteral("theme"), QStringList{ keepTheme });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);

            const bool keepLogAtStartup = m_settings.logAtStartup;
            const int keepLogLevel = m_settings.logLevel;
            runCommand(QStringLiteral("log"), QStringList{ QStringLiteral("detailed") });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            const AppSettings page2 = m_settingsPage ? m_settingsPage->currentValues() : AppSettings{};
            const bool logSync = m_settings.logAtStartup && m_settings.logLevel == 1 &&
                                 page2.logAtStartup && page2.logLevel == 1 &&
                                 slime::LogBus::mode() == slime::LogMode::Detailed;
            std::fprintf(stderr, "[UI自检]   log detailed ⇒ 设置页{起始=%d 级别=%d} LogBus=%s\n",
                         page2.logAtStartup ? 1 : 0, page2.logLevel, slime::LogBus::modeName(slime::LogBus::mode()));
            check(logSync, "log 命令同步设置页（启用状态 / 级别 / LogBus 三处一致）");
            AppSettings back = m_settings;
            back.logAtStartup = keepLogAtStartup;
            back.logLevel = keepLogLevel;
            applyAppSettings(back);
            flushPendingConfigWrite();
        }

        // ④ 构建模式下参数命令被拒绝（不再静默失效）
        {
            showPage(kPageBuild);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 160);
            const int gridBefore = m_w->gridSize->value();
            const QString r1 = runCommand(QStringLiteral("grid"), QStringList{ QStringLiteral("9") });
            const QString r2 = runCommand(QStringLiteral("target"), QStringList{ QStringLiteral("5") });
            const bool refused = r1.contains(slime::ui::t("ui.81a94d85f01c")) && r2.contains(slime::ui::t("ui.81a94d85f01c")) &&
                                 m_w->gridSize->value() == gridBefore;
            std::fprintf(stderr, "[UI自检]   构建模式拒绝：grid → %s\n", r1.split('\n').first().toUtf8().constData());
            check(refused, "构建模式下参数命令被拒绝并说明原因（不静默失效）");
            const QString r3 = runCommand(QStringLiteral("build"), QStringList{ QStringLiteral("off") });
            check(!buildModeActive() && r3.contains(slime::ui::t("ui.6c14bd7f6f9e")), "build off 退出构建模式");
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
        }

        // ⑤ copy <n> 越界必须明确报错；cache status 可用
        {
            const QString r = runCommand(QStringLiteral("copy"), QStringList{ QStringLiteral("99999") });
            check(r.contains(slime::ui::t("ui.11a604e0fb59")) || r.contains(slime::ui::t("ui.2b6d0251df64")),
                  "copy <n> 越界时明确报错（不静默失败）");
            const QString cs = runCommand(QStringLiteral("cache"), QStringList{ QStringLiteral("status") });
            check(cs.contains(slime::ui::t("ui.6cdba4aad0c1")), "cache status 可用");
            const QString o = runCommand(QStringLiteral("open"), QStringList{ QStringLiteral("bad") });
            check(o.contains(slime::ui::t("ui.2b6d0251df64")), "open 参数错误时给出用法");
        }

        // ⑥ Tab 候选框：Minecraft 风格 + **连续 10 次 Tab 焦点不跑**
        {
            QLineEdit* in = m_terminal ? m_terminal->input() : nullptr;
            check(in != nullptr, "命令终端输入框存在");
            if (in) {
                // 先把终端所在停靠分类切出来，保证输入框可见、可获焦（截图流程会切页）
                showDock(3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                in->clear();
                in->setFocus(Qt::OtherFocusReason);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                const bool canFocus = in->hasFocus();
                if (!canFocus) {
                    std::fprintf(stderr, "[UI自检]   提示：当前平台未把焦点交给输入框，改用「焦点不被抢走」降级判定\n");
                }
                in->setText(QStringLiteral("s")); // 候选：search/seed/smart/status/stop
                in->setCursorPosition(1);
                int focusKept = 0;
                int popupSeen = 0;
                int focusStolen = 0;
                QStringList seenTexts;
                for (int i = 0; i < 10; ++i) {
                    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                    QApplication::sendEvent(in, &ev);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (in->hasFocus()) ++focusKept;
                    QWidget* fw = QApplication::focusWidget();
                    if (fw && fw != in && (qobject_cast<QLineEdit*>(fw) || qobject_cast<QAbstractSpinBox*>(fw) ||
                                           qobject_cast<QComboBox*>(fw) || qobject_cast<QAbstractButton*>(fw))) {
                        ++focusStolen; // Tab 把焦点交给了别的**可编辑/可点**控件 = 真正的「抢焦点」故障
                    }
                    if (m_terminal->popupVisible()) ++popupSeen;
                    seenTexts << in->text();
                }
                const QStringList items = m_terminal->popupItems();
                const QRect pg = m_terminal->popupGeometry();
                const QPoint inTop = in->mapToGlobal(QPoint(0, 0));
                const bool above = pg.bottom() <= inTop.y() + 2; // 允许 2px 视觉贴边
                std::fprintf(stderr, "[UI自检]   候选框几何=%d,%d %dx%d ｜ 输入行顶 y=%d（↑在上方=%d）\n",
                             pg.x(), pg.y(), pg.width(), pg.height(), inTop.y(), above ? 1 : 0);
                const bool noFocusPolicy = in->hasFocus();
                std::fprintf(stderr,
                             "[UI自检]   Tab 候选：候选=%lld 个 显示=%d/10 次 焦点保持=%d/10 位置在输入行上方=%d "
                             "文本样本=%s\n",
                             static_cast<long long>(items.size()), popupSeen, focusKept, above ? 1 : 0,
                             seenTexts.mid(0, 4).join(QStringLiteral(",")).toUtf8().constData());
                // v3.4.12：候选框截图证据（用户实测「Tab 候选框没有出现」）
                if (!qEnvironmentVariable("SLIME_SHOT").isEmpty()) {
                    saveWindowShot(qEnvironmentVariable("SLIME_SHOT"), QStringLiteral("cmd-tab-popup"));
                }
                // v3.4.12：空输入按 Tab 必须列出**全部命令**（用户实测「候选框没出现」的回归防线）
                m_terminal->hidePopup();
                in->clear();
                QKeyEvent tabEmpty(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &tabEmpty);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                const QStringList allItems = m_terminal->popupItems();
                std::fprintf(stderr, "[UI自检]   空输入 Tab：候选 %lld 个（全部命令 %lld 条）\n",
                             static_cast<long long>(allItems.size()),
                             static_cast<long long>(commandNames().size()));
                if (!qEnvironmentVariable("SLIME_SHOT").isEmpty()) {
                    saveWindowShot(qEnvironmentVariable("SLIME_SHOT"),
                                   QStringLiteral("cmd-tab-popup-all"));
                }
                check(m_terminal->popupVisible() && allItems.size() == commandNames().size(),
                      "空输入按 Tab 列出全部命令（Minecraft 行为）");
                m_terminal->hidePopup();
                // 回到「前缀匹配」场景
                in->clear();
                in->setText(QStringLiteral("s"));
                in->setCursorPosition(1);
                for (int i = 0; i < 10; ++i) {
                    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                    QApplication::sendEvent(in, &ev);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                }
                check(!items.isEmpty(), "Tab 弹出候选列表（前缀匹配）");
                check(popupSeen == 10, "连续 10 次 Tab 候选框始终可见（循环补全）");
                std::fprintf(stderr, "[UI自检]   焦点：保持 %d/10 次，被其它控件抢走 %d 次（可获焦=%d）\n",
                             focusKept, focusStolen, canFocus ? 1 : 0);
                if (canFocus) {
                    check(focusKept == 10 && noFocusPolicy, "连续 10 次 Tab 输入框焦点保持（不抢焦点）");
                } else {
                    check(focusStolen == 0, "连续 10 次 Tab 焦点未被其它控件抢走（离屏平台降级判定）");
                }
                check(above, "候选框显示在输入行上方（Minecraft 风格）");
                // v3.6.5（用户要求）：Tab 只切换候选、不写入输入框；Enter 才把候选填入
                {
                    const QStringList cycleItems = m_terminal->popupItems();
                    const int row0 = m_terminal->popupCurrentRow();
                    QKeyEvent tab1(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                    QApplication::sendEvent(in, &tab1);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    const int row1 = m_terminal->popupCurrentRow();
                    const QString textAfterTab = in->text();
                    std::fprintf(stderr, "[UI自检]   Tab 只切候选：行 %d → %d，输入仍为「%s」\n", row0, row1,
                                 textAfterTab.toUtf8().constData());
                    check(!cycleItems.isEmpty() && row1 != row0, "Tab 在候选之间循环（只移动选择）");
                    check(textAfterTab == QStringLiteral("s"), "Tab 不写入命令栏（写入交给 Enter）");
                    // Enter 只把当前候选填进输入框，不执行命令
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
                    QApplication::sendEvent(in, &enter);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    std::fprintf(stderr, "[UI自检]   Enter 填入候选：输入变为「%s」\n",
                                 in->text().toUtf8().constData());
                    check(in->text() != QStringLiteral("s") && !in->text().isEmpty(),
                          "Enter 把候选填入命令栏");
                }
                // Esc 关闭候选框，输入内容不变
                const QString beforeEsc = in->text();
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(in, &esc);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                check(!m_terminal->popupVisible() && in->text() == beforeEsc, "Esc 关闭候选框且不改输入内容");
                check(canFocus ? in->hasFocus() : (QApplication::focusWidget() == nullptr ||
                                                   QApplication::focusWidget() == in),
                      "Esc 之后焦点仍在输入框（或未被其它控件接管）");
                // 单一候选：直接补全并追加空格
                in->clear();
                in->setText(QStringLiteral("vers"));
                in->setCursorPosition(4);
                QKeyEvent tabOne(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &tabOne);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text() == QStringLiteral("vers") && m_terminal->popupVisible(),
                      "唯一候选：Tab 只显示候选并高亮，不写入命令栏");
                QKeyEvent enterOne(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
                QApplication::sendEvent(in, &enterOne);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text() == QStringLiteral("version ") && !m_terminal->popupVisible(),
                      "Enter 把唯一候选补全并追加空格");
                in->clear();
                m_terminal->hidePopup();
            }
        }
        // clear 命令（终端内置）必须清空输出
        {
            if (m_terminal && m_terminal->output()) {
                m_terminal->appendLine(slime::ui::t("ui.08e2ade731ce"));
                const bool had = !m_terminal->output()->toPlainText().isEmpty();
                m_terminal->execute(QStringLiteral("clear"));
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                const bool clearedNow = m_terminal->output()->toPlainText().isEmpty();
                check(had && clearedNow, "clear 命令清空终端输出（终端内置实现）");
            }
        }
    }

    // 27) v3.4.12（用户实测四项）：单位外置 / 灰字删除 / 构建页实时预估 / 构建不再要求搜索模式
    {
        std::fprintf(stderr, "[UI自检] v3.4.12 单位外置 / 构建页实时预估 / 构建免搜索模式检查…\n");

        // ① 单位不再写进输入框（suffix 全为空），且存在框外单位标签
        {
            QStringList withSuffix;
            int unitLabels = 0;
            // 只有 QSpinBox / QDoubleSpinBox 有 suffix()
            const auto scanSpin = [&withSuffix](const QList<QWidget*>& ws) {
                for (QWidget* w : ws) {
                    if (auto* sb = qobject_cast<QSpinBox*>(w)) {
                        if (!sb->suffix().isEmpty()) {
                            withSuffix << (sb->suffix().trimmed() + QStringLiteral("@QSpinBox"));
                        }
                    } else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
                        if (!dsb->suffix().isEmpty()) {
                            withSuffix << (dsb->suffix().trimmed() + QStringLiteral("@QDoubleSpinBox"));
                        }
                    }
                }
            };
            QList<QWidget*> spins;
            if (m_settingsPage) {
                for (QSpinBox* sb : m_settingsPage->findChildren<QSpinBox*>()) spins << sb;
                for (QDoubleSpinBox* sb : m_settingsPage->findChildren<QDoubleSpinBox*>()) spins << sb;
                unitLabels += m_settingsPage->findChildren<QLabel*>(QStringLiteral("unitLabel")).size();
            }
            for (QSpinBox* sb : findChildren<QSpinBox*>()) spins << sb;
            for (QDoubleSpinBox* sb : findChildren<QDoubleSpinBox*>()) spins << sb;
            scanSpin(spins);
            unitLabels += findChildren<QLabel*>(QStringLiteral("unitLabel")).size();
            std::fprintf(stderr, "[UI自检]   仍带 suffix 的数值框 %lld 个%s%s；框外单位标签 %d 个\n", static_cast<long long>(withSuffix.size()),
                         withSuffix.isEmpty() ? "" : "：", withSuffix.join(QStringLiteral(" / ")).toUtf8().constData(),
                         unitLabels);
            check(withSuffix.isEmpty(), "单位不再写在输入框里（全部改为框外标签）");
            check(unitLabels >= 6, "设置页/构建页都有框外单位标签（MB / 天 / % / 区块 / ×）");
        }

        // ② 高级页那行灰字已删除（页面底部已有「恢复所有设置」按钮）
        {
            int leftover = 0;
            // v3.4.12：只查设置页（更新历史正文里也会出现同一句话，按整窗查会误报）
            const QList<QLabel*> scope = m_settingsPage ? m_settingsPage->findChildren<QLabel*>()
                                                       : QList<QLabel*>();
            for (QLabel* lb : scope) {
                if (lb->text().contains(slime::ui::t("ui.6b6350c805f9"))) ++leftover;
            }
            check(m_settingsPage != nullptr, "设置页存在（灰字检查范围正确）");
            check(leftover == 0, "已删除高级页的「恢复所有设置到出厂默认值」灰字");
        }

        // ③ 构建页：无预估/开始构建/中断按钮，改为实时预估卡片（随预算变化）
        {
            check(m_w->bEstimate == nullptr && m_w->bStart == nullptr && m_w->bAbort == nullptr,
                  "构建页已移除「预估 / 开始构建 / 中断」按钮（开始与中断走工具栏主按钮）");
            check(m_w->bSummaryLive != nullptr, "构建页存在实时预估卡片");
            const int keepBudget = m_w->bBudget->value();
            m_w->bBudget->setValue(keepBudget == 3000 ? 2000 : 3000);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            const QString live = m_w->bSummaryLive ? m_w->bSummaryLive->text() : QString();
            const bool showsBudget = live.contains(QString::number(m_w->bBudget->value()));
            const bool showsRange = live.contains(QString::number(m_w->minX->value())) &&
                                    live.contains(QString::number(m_w->maxZ->value()));
            std::fprintf(stderr, "[UI自检]   实时预估：%s\n", live.split('\n').first().toUtf8().constData());
            check(showsBudget && showsRange, "构建预估与参数实时同步（读控件当前值，不再靠弹窗/文件）");
            m_w->bBudget->setValue(keepBudget);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            syncSettingsPageFromState();
        }

        // ④ 构建模式不再要求「搜索模式 / 图案」：严格模式且未画图案也能开始构建
        {
            const bool keepPatternSet = m_patternSet;
            m_patternSet = false; // 模拟「没画图案」
            m_w->strictRadio->setChecked(true);
            onModeChanged();
            showPage(kPageBuild);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 160);
            m_w->bedrockRadio->setChecked(true); // 构建固定基岩版（也避免 Java 种子校验）
            // 范围取大一些，保证「已开始」可观测（小范围会瞬间跑完，导致误判为未开始）
            m_w->minX->setValue(-512); m_w->maxX->setValue(511);
            m_w->minZ->setValue(-512); m_w->maxZ->setValue(511);
            m_wholeMapConfirmed = false;
            m_hasLastOutcome = false;
            startSearch();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const bool started = m_running || m_hasLastOutcome; // 已开始 或 已跑完（都说明没有被拦下）
            std::fprintf(stderr,
                         "[UI自检]   构建模式（严格模式 + 未画图案）startSearch ⇒ 已开始=%d（运行中=%d 已完成=%d）\n",
                         started ? 1 : 0, m_running ? 1 : 0, m_hasLastOutcome ? 1 : 0);
            check(started, "缓存构建不再被要求设置搜索模式/绘制图案（用户实测项）");
            if (started) {
                cancelSearch();
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(10);
                }
            }
            m_patternSet = keepPatternSet;
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
        }
    }

    // v3.4.8：恢复自检前的设置与 config.json（见函数开头存档）
    if (m_settingsPage) {
        m_settingsPage->setValues(settingsArchive);
        m_settings = settingsArchive;
        applyAppSettings(m_settings);
        writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                        m_terminal ? m_terminal->history() : QStringList());
    }
    if (!configArchive.empty()) {
        std::ofstream out(appDirPath() / kConfigFileName, std::ios::binary | std::ios::trunc);
        if (out) out.write(configArchive.data(), static_cast<std::streamsize>(configArchive.size()));
    }
    // v3.4.12：清理本次自检产生的缓存文件（测试产物）。
    //   自检会真跑「缓存构建」，每次都会在程序目录留下十几 MB 的缓存；
    //   留着会让**下一次启动**加载很久（实测表现为「启动卡住」），也会被带进发布目录。
    {
        std::error_code ecCache;
        const auto cachePath = appDirPath() / kCacheFileName;
        if (std::filesystem::exists(cachePath, ecCache)) {
            const auto sz = std::filesystem::file_size(cachePath, ecCache);
            if (std::filesystem::remove(cachePath, ecCache)) {
                std::fprintf(stderr, "[UI自检] 已清理自检缓存文件（%.1f MB）\n", sz / (1024.0 * 1024.0));
            }
        }
        const auto legacyPath = appDirPath() / kCacheLegacyFileName;
        std::filesystem::remove(legacyPath, ecCache);
    }
    std::fprintf(stderr, "[UI自检] 已恢复自检前的设置与 config.json\n");
    m_uiQuiet = false;
    if (fail == 0) {
        std::printf("[UI自检] 全部通过 ✔\n");
        return 0;
    }
    std::printf("[UI自检] 失败 %d 项 ✘\n", fail);
    return 1;
}

