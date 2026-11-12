// v3.12.0：搜索、构建与结果处理逻辑。

QString MainWindow::successProbabilityText(int64_t areaX, int64_t areaZ) const
{
    const double pSlime = 0.1;
    double pMatch = 0.0;
    int64_t positions = 0;
    QString basis;

    const bool patternMode = m_w->strictRadio && m_w->strictRadio->isChecked();
    if (patternMode) {
        if (!m_patternSet || m_pattern.empty()) {
            return slime::ui::t("ui.f82967ddf109");
        }
        int green = 0, white = 0;
        for (int r = 0; r < m_pattern.rows(); ++r) {
            for (int c = 0; c < m_pattern.cols(); ++c) {
                const auto st = m_pattern.at(r, c);
                if (st == slime::CellState::Green) ++green;
                else if (st == slime::CellState::White) ++white;
            }
        }
        pMatch = std::pow(pSlime, green) * std::pow(1.0 - pSlime, white);
        const bool lock = m_w->lockDir && m_w->lockDir->isChecked();
        if (lock) pMatch *= 0.25;   // 只允许 1 个方向 ⇒ 命中率降到约 1/4
        positions = (std::max)(int64_t{0}, areaX - m_pattern.cols() + 1) *
                    (std::max)(int64_t{0}, areaZ - m_pattern.rows() + 1);
        basis = slime::ui::t("ui.659103792aff")
                    .arg(green + white)
                    .arg(green)
                    .arg(white)
                    .arg(lock ? slime::ui::t("ui.aec43f954535") : QString());
    } else {
        const int g = (std::max)(1, m_w->gridSize ? m_w->gridSize->value() : 5);
        const int64_t cells = static_cast<int64_t>(g) * g;
        const double d = (std::max)(0.0, (std::min)(1.0, m_w->minDensity ? m_w->minDensity->value() : 0.0));
        const int64_t need = static_cast<int64_t>(std::ceil(d * static_cast<double>(cells)));
        if (need <= 0) {
            pMatch = 1.0;
        } else if (need > cells) {
            pMatch = 0.0;
        } else {
            const double mean = static_cast<double>(cells) * pSlime;
            const double sd = std::sqrt(static_cast<double>(cells) * pSlime * (1.0 - pSlime));
            // 连续性修正的正态近似：密度窗口一般远大于 30 格，近似足够准
            const double z = (static_cast<double>(need) - 0.5 - mean) / (sd > 1e-9 ? sd : 1.0);
            pMatch = 0.5 * std::erfc(z / std::sqrt(2.0));
        }
        positions = (std::max)(int64_t{0}, areaX - g + 1) * (std::max)(int64_t{0}, areaZ - g + 1);
        basis = slime::ui::t("ui.1acf7f8f1396").arg(g).arg(d, 0, 'f', 2);
    }

    if (positions <= 0) return slime::ui::t("ui.e448e31ab0ac");
    const double pAny = (pMatch <= 0.0)
                            ? 0.0
                            : (pMatch >= 1.0 ? 1.0
                                             : -std::expm1(static_cast<double>(positions) * std::log1p(-pMatch)));
    const double pct = 100.0 * pAny;
    QString hint;
    if (pct < 1.0) hint = slime::ui::t("ui.822cb673d8d6");
    else if (pct < 20.0) hint = slime::ui::t("ui.402c20628dcf");
    else if (pct > 99.5) hint = slime::ui::t("ui.7a0d7b3dfbd8");
    const QString pctText = (pct < 1.0) ? QStringLiteral("%1").arg(pct, 0, 'f', 2)
                                         : QStringLiteral("%1").arg(pct, 0, 'f', pct < 10.0 ? 1 : 0);
    return slime::ui::t("ui.6ae8a00a69ce") + pctText + QStringLiteral("%") + hint +
           slime::ui::t("ui.2507a5abfb92")
               .arg(static_cast<qlonglong>(positions))
               .arg(pMatch * 100.0, 0, 'f', pMatch < 0.001 ? 4 : 2)
               .arg(basis);
}

void MainWindow::startSearch()
{
    if (m_running) {
        // v3.3.7（P0-2）：自检/自动化模式下**不能**用模态框（GUI 线程会被永久阻塞，
        // 实测导致自检挂死 600 秒以上）；这里只提示并忽略重复启动。
        if (m_uiQuiet) {
            setStatus(slime::ui::tr(slime::ui::t("ui.9bb8bee94e7b")));
            return;
        }
        QMessageBox::warning(this, slime::ui::t("ui.ab3656a956f5"), slime::ui::t("ui.44969209326b"));
        return;
    }

    // v3.10.12.12：只有密度模式下的「环形覆盖」形状才走环形搜索。
    //   修复：从环形切到图案后，shapeRingRadio 仍保持 checked（面板只是隐藏），
    //   旧逻辑会继续调用 runRingSearch，导致图案模式搜出环形结果。
    const bool patternModeNow = m_w && m_w->strictRadio && m_w->strictRadio->isChecked();
    if (!buildModeActive() && !patternModeNow && m_w && m_w->shapeRingRadio && m_w->shapeRingRadio->isChecked()) {
        runRingSearch();
        return;
    }

    SearchOptions opt;
    opt.version = m_w->bedrockRadio->isChecked() ? Version::Bedrock : Version::Java;
    opt.minX = m_w->minX->value();
    opt.maxX = m_w->maxX->value();
    opt.minZ = m_w->minZ->value();
    opt.maxZ = m_w->maxZ->value();
    opt.mode = m_w->densityRadio->isChecked() ? Mode::Density : Mode::Strict;
    opt.directionLocked = m_w->lockDir->isChecked();
    opt.gridSize = m_w->gridSize->value();
    opt.minDensity = m_w->minDensity->value();
    opt.startX = m_w->startX->value();
    opt.startZ = m_w->startZ->value();
    opt.targetCount = m_w->targetCount->value();
    opt.stopAtTarget = m_w->stopAtTarget->isChecked();
    opt.ignoreThreshold = m_w->ignoreThreshold && m_w->ignoreThreshold->isChecked();
    if (opt.ignoreThreshold) { opt.stopAtTarget = false; opt.smartRange = false; }
    opt.smartRange = m_w->smartRange->isChecked();
    opt.threads = m_w->threads->value();
    // v3.9.6：GPU 流式匹配现已支持「提早停止 / 智能范围」（按行带分批 + 判损判停），不再需要回退提示。
    // v3.3.9（任务书 2.2/2.1）：CPU 占用率上限 + 内存上限（GUI 主搜索路径）
    {
        const int logical = (std::max)(1, QThread::idealThreadCount());
        const int pct = std::clamp(m_settings.cpuLimitPercent, 10, 100);
        if (pct < 100) {
            const int limited = (std::max)(1, static_cast<int>(std::lround(logical * pct / 100.0)));
            if (opt.threads > limited) {
                Logger::info("CPU 占用率上限 " + std::to_string(pct) + "% ⇒ 生效线程数 " +
                             std::to_string(opt.threads) + " → " + std::to_string(limited));
                opt.threads = limited;
            }
        }
    }
    opt.memBudgetBytes =
        (m_settings.memLimitMB > 0) ? static_cast<int64_t>(m_settings.memLimitMB) * 1024 * 1024 : 0;
    opt.cpuLimitPercent = std::clamp(m_settings.cpuLimitPercent, 10, 100);
    m_lastEffectiveThreads = opt.threads; // v3.3.9（二）：供自检核对「CPU 上限确实收敛了线程数」
    // v3.3.0：搜索算法（分层/估算/保守仅在密度搜索下生效；引擎侧对误配做兜底）
    opt.algorithm = (opt.mode == Mode::Density) ? currentAlgorithm() : Algorithm::Exact;
    opt.layerBlock = m_w->layerBlock->currentData().toInt();
    opt.layerRatio = m_w->layerRatio->value();
    opt.sampleStep = m_w->sampleStep->value();
    opt.sampleExact = m_w->sampleExact->isChecked();

    // 启动前边界兜底（v3.1.0）
    const int bound = versionBound();
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        if (s->value() > bound) s->setValue(bound);
        if (s->value() < -bound) s->setValue(-bound);
    }
    opt.minX = m_w->minX->value();
    opt.maxX = m_w->maxX->value();
    opt.minZ = m_w->minZ->value();
    opt.maxZ = m_w->maxZ->value();

    bool seedOA = true;
    if (!buildModeActive() && opt.version == Version::Java) {
        // v3.3.4（任务书 4.2）：Java 版必须填写合法种子 —— 种子默认空；
        // "0" 是合法种子；纯空格视为空；"123abc" 视为非法；基岩版完全不受影响。
        const QString seedText = m_w->seedEdit->text().trimmed();
        if (seedText.isEmpty()) {
            // 弹窗 [去填写] [取消] + 反馈栏 ❌ + 日志记录（如已启用）+ 输入框高亮闪烁
            addError(slime::ui::t("ui.24599e00ffb7"));
            slime::LogBus::error("Java 版未填写世界种子，搜索被阻止");
            bool goFill = true;
            if (!m_uiQuiet) {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(slime::ui::t("ui.02343987a852"));
                box.setText(slime::ui::t("ui.d33428ef9569"));
                QPushButton* fill = box.addButton(slime::ui::t("ui.919e5d650bfa"), QMessageBox::AcceptRole);
                box.addButton(slime::ui::t("ui.4d0b4688c787"), QMessageBox::RejectRole);
                box.exec();
                goFill = (box.clickedButton() == fill);
            }
            if (goFill) highlightSeedField();
            return;
        }
        opt.worldSeed = seedText.toLongLong(&seedOA);
        if (!seedOA) {
            addError(slime::ui::t("ui.2469f1297a2d").arg(seedText));
            slime::LogBus::error("世界种子非法: " + seedText.toStdString());
            if (!m_uiQuiet) {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(slime::ui::t("ui.733bc78b8835"));
                box.setText(slime::ui::t("ui.f36f9429a104").arg(seedText));
                QPushButton* fill = box.addButton(slime::ui::t("ui.919e5d650bfa"), QMessageBox::AcceptRole);
                box.addButton(slime::ui::t("ui.4d0b4688c787"), QMessageBox::RejectRole);
                box.exec();
                if (box.clickedButton() != fill) return;
            }
            highlightSeedField();
            return;
        }
        slime::LogBus::info("搜索种子: " + std::to_string(opt.worldSeed));
    }

    // v3.4.12（用户实测：缓存构建时仍被要求设置搜索模式）：
    //   构建只做史莱姆判定 + 写缓存，**不使用搜索模式/图案**，因此跳过严格模式的图案校验。
    if (!buildModeActive() && opt.mode == Mode::Strict) {
        if (!m_patternSet || m_pattern.empty()) {
            QMessageBox::warning(this, slime::ui::t("ui.05118c2a651d"), slime::ui::t("ui.bb9712c09a09"));
            return;
        }
        opt.pattern = m_pattern;
    }

    const int64_t area = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t areaZ = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    if (area > 0 && areaZ > 0) {
        const int64_t total = area * areaZ;
        // v3.4.12：构建模式的范围确认走 prepareBuildRun（预算/容量），不再叠一层搜索式整图确认
        if (total >= kWholeMapMinArea && !buildModeActive()) {
            // v3.1.1：整图级搜索 → 动态估算确认（预设确认过的会话不再重复询问）
            // v3.3.3：无界面自检（m_uiQuiet）时自动放行，避免模态框阻塞自动化流程
            if (!m_wholeMapConfirmed) {
                if (!m_uiQuiet && !confirmWholeMapSearch(false, versionBound())) return;
                m_wholeMapConfirmed = true;
            }
        } else if (total > 1'000'000) {
            if (!m_uiQuiet) {
                // v3.6.6（用户要求）：二次确认窗口重写，并给出「成功率预测」
                const QString body =
                    slime::ui::t("ui.c6ccb27bc3fa")
                        .arg(area)
                        .arg(areaZ)
                        .arg(total)
                        .arg(successProbabilityText(area, areaZ));
                if (QMessageBox::question(this, slime::ui::t("ui.4973d000fee5"), body) != QMessageBox::Yes) {
                    return;
                }
            }
        }
    }

    const bool cacheEnabled = (opt.version == Version::Bedrock) && m_w->useCache->isChecked();
    opt.useCache = cacheEnabled;
    opt.cache = cacheEnabled ? m_cache.get() : nullptr;
    // v3.3.9（任务书 1.2）：**普通搜索不再写缓存** —— 写入路径整体移除（只有缓存构建模式能写）
    opt.cacheWrite = nullptr;
    opt.cacheBudgetBytes = 0;

    // v3.3.6（任务书 4.3）：大范围搜索警告阈值 —— 超过设置值时弹窗确认（可在设置里调整）
    // v3.3.3 的「低于临界值建议关闭 GPU」提示已随 Bug 2 一并移除。
    {
        const int64_t total0 = area * areaZ;
        const int64_t warnAt = m_settings.largeRangeWarnChunks > 0 ? m_settings.largeRangeWarnChunks
                                                                   : 100000000;
        // v3.3.8.1（任务书 3.2）：大范围警告阈值改为「**预计耗时 > 5 分钟**」触发。
        // 预计耗时用引擎的 estimateSearch（本机实测速率 + 分块计划）；不可用时回退区块数阈值。
        slime::SearchEstimate est;
        bool estOk = false;
        const int estThreads = (m_w->threads && m_w->threads->value() > 0) ? m_w->threads->value() : 0;
        estOk = slime::SlimeEngine::estimateSearch(opt, estThreads, est) && est.ok;
        const bool tooLong = largeRangeNeedsConfirm(total0, estOk, est.totalSec, warnAt);
        if (tooLong) {
            m_largeRangeWarned = true; // 自检据此断言该分支确实被走到
            Logger::info(std::string("大范围搜索确认：范围 ") + std::to_string(total0) + " 区块，" +
                         (estOk ? ("预计耗时 " + std::to_string(static_cast<int64_t>(est.totalSec)) + " 秒")
                                : ("超过区块数阈值 " + std::to_string(warnAt))) +
                         (tooLong ? "（> 5 分钟）" : ""));
            if (!m_uiQuiet) {
                // v3.3.9（任务书 3.2）：弹窗内容必须写清三项 —— 即将扫描多少、超阈值多少、预计多少分钟
                const QString body =
                    largeRangeConfirmText(total0, area, areaZ, estOk, est.totalSec, warnAt) +
                    slime::ui::t("ui.f3c740185215")
                        .arg(successProbabilityText(area, areaZ));
                if (QMessageBox::question(this, slime::ui::t("ui.54f83a642d7d"), body) != QMessageBox::Yes) {
                    return;
                }
            }
        }
    }

    // v3.3.6（任务书 调整 3）：进度流水初始化（每次搜索重新计数实时命中）
    m_runTotalChunks = area * areaZ;
    m_streamScanned = 0;
    m_streamTick = 0;
    m_task.resetLiveMatches(); // v3.3.7（P0-2）：计数由 SearchTask 持有（生命周期与线程绑定）

    // v3.2.1：GPU 加速仅在「用户勾选 且 检测可用」时注入（默认关闭；绝不自动启用）。
    // 勾选但不可用/未检测 → 静默回退 CPU，仅记录日志，不弹窗不崩溃。
    opt.gpuScan = nullptr;
    opt.gpuResultSelfOptimize = m_settings.gpuResultSelfOptimize;   // v3.10.11.1：结果自优化开关
    const bool gpuWanted = m_w->gpuCheck && m_w->gpuCheck->isChecked();
    if (gpuWanted) {
        if (!m_gpuProbeDone) {
            // v3.10.3（用户反馈：启动超大范围搜索时界面卡住）：首次 GPU 检测会同步初始化
            //   OpenCL（可达数秒）。这里给出等待光标与明确状态，让用户知道在做什么。
            setStatus(slime::ui::tr(slime::ui::t("ui.85e816a88b92")));
            if (!m_uiQuiet) QApplication::setOverrideCursor(Qt::WaitCursor);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            ensureGpuScanOnce();
            if (!m_uiQuiet) QApplication::restoreOverrideCursor();
        }
        if (m_gpuScan && m_gpuAvailable) {
            opt.gpuScan = m_gpuScan.get();
            Logger::info("本次搜索启用 GPU 扫描加速（区域达到阈值时由引擎自动使用，失败自动回退 CPU）");
        } else {
            Logger::info("GPU 已勾选但不可用，本次搜索使用 CPU"
                         + (m_gpuProbeReason.empty() ? std::string() : "：" + m_gpuProbeReason));
        }
    }

    // v3.4.12（用户实测：构建时仍被要求设置搜索模式）：**必须在引擎校验之前**把构建语义归一。
    //   构建只做史莱姆判定 + 写缓存，不做图案匹配；若界面还停在「严格图案匹配」且未画图案，
    //   引擎 validate() 会判「图案为空」⇒ 用户就会被要求去设置搜索模式。
    if (buildModeActive()) {
        opt.mode = Mode::Density;
        opt.pattern = Pattern();
        opt.algorithm = Algorithm::Exact;
    }

    const std::string verr = SlimeEngine::validate(opt);
    if (!verr.empty()) {
        // v3.4.12：无界面自检（m_uiQuiet）时只记录不弹窗（此前漏了保护，自检会被模态框挡住）
        if (!m_uiQuiet) {
            QMessageBox::warning(this, slime::ui::t("ui.3e64edf2ea33"), QString::fromStdString(verr));
        } else {
            std::fprintf(stderr, "[UI自检] 参数校验失败（quiet 模式不弹窗）：%s\n", verr.c_str());
        }
        return;
    }

    RunInfo info;
    info.version = opt.version;
    info.mode = opt.mode;
    info.algorithm = opt.algorithm;
    info.layerBlock = opt.layerBlock;
    info.layerRatio = opt.layerRatio;
    info.sampleStep = opt.sampleStep;
    info.sampleExact = opt.sampleExact;
    info.gridSizeUsed = opt.gridSize;
    info.minDensityUsed = opt.minDensity;
    info.options = opt;
    info.region = opt.region();
    info.targetCount = opt.targetCount;
    info.startX = opt.startX;
    info.startZ = opt.startZ;
    info.cacheWasUsed = cacheEnabled;
    info.mergeCacheAfter = cacheEnabled && !(m_cache->covers(info.region));


    // v3.3.9（任务书 1.1）：缓存构建模式 —— 预估/上限确认/分段（用户放弃则直接返回）
    //（构建语义已在引擎校验前归一为密度，见上）
    if (buildModeActive() && !prepareBuildRun(opt, info)) return;
    // v3.3.9.1（任务书 1.3）：构建模式必须**只跑第一段**（后续段由 handleBuildBandFinished 续跑）。
    // 旧实现直接把「整个范围」交给引擎 ⇒ 第一段就把全范围扫完（进度瞬间 100%），
    // 随后每一段都发现行已被覆盖 ⇒ 只累加「已跳过」，用户看到的就是「进度 >100%、不断重复记录」。
    if (m_buildActive && !m_buildBands.empty()) {
        opt.minZ = m_buildBands.front().minZ;
        opt.maxZ = m_buildBands.front().maxZ;
        info.region = m_buildBands.front();
        m_buildBaseOpt = opt;      // 基础选项 = 第一段选项（后续段只改 Z 范围）
        m_buildRunInfo.options = opt;
    }

    m_running = true;
    setRunningUi(true);
    setInputsEnabled(false);
    // v3.3.4：结果区改为 QTextEdit，搜索开始时清空文本（等价于旧版清空结果卡片）
    if (m_w->resultView) m_w->resultView->clear();
    if (m_w->infoStats) m_w->infoStats->setText(slime::ui::t("ui.f9012afa40dc"));
    m_lastResultText.clear();
    m_hasLastOutcome = false;
    m_w->progress->setValue(0);
    m_searchStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    setStatus(slime::ui::tr(slime::ui::t("ui.455d8705421a")));
    logSearchParams(opt);
    // v3.10.8：把全部有效参数写成一行指纹，写入日志（排查预设/自定义速度差异）
    slime::LogBus::info(std::string("搜索参数指纹: ") + searchOptionsFingerprint(opt).toStdString());
    {
        // v3.10.7：明确记录本次实际使用的版本（排查「界面 Java 实际基岩」类问题）
        const bool bedrockRun = (opt.version == Version::Bedrock);
        Logger::info(std::string("本次搜索实际版本: ") + (bedrockRun ? "基岩版" : "Java 版"));
        addFeedback(3, slime::ui::t("ui.ba3a88efa949")
                           .arg(bedrockRun ? slime::ui::t("ui.618fd37c0c01") : slime::ui::t("ui.8accf0fe2eed")));
    }
    m_etaSec = 0.0; // v3.3.8（任务 1）：新搜索重置 ETA（完成后保持清空，避免误以为还在跑）
    if (m_elapsedTimer) m_elapsedTimer->start(200); // v3.3.4：进度标签页刷新已用时/预计剩余

    if (info.cacheWasUsed && !info.mergeCacheAfter) {
        Logger::info("缓存完全覆盖搜索范围，将直接使用缓存数据");
    }

    const auto postToUi = [this](auto fn) {
        QMetaObject::invokeMethod(this, [fn = std::move(fn)]() { fn(); }, Qt::QueuedConnection);
    };

    // v3.3.7（P0-2）：本次运行编号 —— 取消后立刻重新搜索时，旧运行排队的进度/结果事件
    // 必须被丢弃，否则会错误地更新新搜索的界面状态（甚至 join 到新线程造成卡死）。
    const int runId = ++m_runId;
    // v3.3.7.2（任务书 1.4）：阶段文案按**本次运行**的模式/算法分派（不用界面控件的实时值，
    // 避免用户在搜索中途切换模式导致文案与真实行为不符）。
    const slime::Mode runMode = opt.mode;
    const slime::Algorithm runAlg = opt.algorithm;
    m_runMode = runMode; // v3.3.7.2：供取消/完成/出错等后续路径复用同一口径
    m_runAlg = runAlg;

    m_task.start(
        opt,
        [this, postToUi, runId, runMode, runAlg](double frac, bool scanning, double cps, double etaSec) {
            postToUi([this, frac, scanning, cps, etaSec, runId, runMode, runAlg]() {
                if (!m_running || runId != m_runId) return; // 陈旧运行的事件：直接丢弃
                // v3.3.9（任务书 1.1/1.4）：构建模式用「四项进度」+ 缓存扫描/新区域扫描区分
                if (m_buildActive) {
                    updateBuildProgressUi(scanning);
                    return;
                }
                // v3.3.8（任务 1）：ETA 接线 —— 此前回调收到了 etaSec 却从未写入 m_etaSec，
                // 于是 updateElapsedUi() 永远走 else 分支、状态栏恒显示「预计剩余：--:--」。
                m_etaSec = etaSec;
                // v3.10.4（用户反馈：GPU 单次派发期间进度条长时间停在 0 像卡死）：
                //   GPU 阶段改为忙碌动画（range 0,0），不伪造百分比；结束后恢复真实进度条。
                if (m_w->progress) {
                    if (m_task.livePhase() >= 2) {
                        if (m_w->progress->maximum() != 0) m_w->progress->setRange(0, 0);
                    } else if (m_w->progress->maximum() == 0) {
                        m_w->progress->setRange(0, 1000);
                    }
                }
                m_w->progress->setValue(static_cast<int>(frac * 1000));
                const int pct = static_cast<int>(frac * 100.0 + 0.5);
                if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("%1%").arg(pct));
                // v3.3.3（任务书 3.2）：分阶段文字（映射逻辑见 progressStageName，自检直接断言）
                // v3.3.7（任务书 P1-5）：**单一数据源** ——
                //   进度行：进度条 + progressPercent（唯一百分比）+ progressStage（纯阶段短语）
                //   状态栏：progressInfo（统计信息）+ progressEta（唯一预计剩余）
                // v3.3.7.2（任务书 1.4）：阶段短语按模式分派（密度=匹配密度，估算=估算）
                if (m_w->progressStage) {
                    // v3.10.4（用户反馈：GPU 进度无法同步且行带派发拖慢速度）：
                    //   不再显示 GPU 阶段标注，只保留阶段短语 + 速度/统计（速度由引擎在完成时上报）。
                    m_w->progressStage->setText(
                        progressStageName(scanning ? 0 : (m_stageWasMatching ? 2 : 1), runMode, runAlg));
                }
                m_streamScanned = m_task.liveScannedCount(); // v3.3.7（P0-3）：引擎真实计数
                if (m_w->progressInfo) {
                    // v3.3.7.2（任务书 1.2）：分母改用引擎口径总量（与分子同源）→ 百分比恒 ≤100%
                    m_w->progressInfo->setText(progressStatsText(m_streamScanned, displayTotalChunks(), cps,
                                                                 m_task.liveMatchCount(), m_settings.rateUnit));
                }
                if (scanning) {
                    m_stageWasMatching = false;
                } else if (!m_stageWasMatching) {
                    m_stageWasMatching = true;
                    // v3.3.7.2（任务书 1.3）：**不再**把阶段短语塞进状态栏的临时消息 ——
                    // 状态栏是「统计信息 + 已用时/预计剩余」区，阶段短语只由进度行的
                    // progressStage 显示。旧实现把「扫描完成，正在匹配图案…」当临时消息，
                    // 它紧贴 progressInfo 的统计文字，读起来像一句话（用户截图即为此）。
                }
            });
        },
        [this, postToUi, info, runId](const SearchOutcome& out) {
            postToUi([this, out, info, runId]() { onSearchFinished(out, info, runId); });
        });

    if (m_w->threads->value() == 1) {
        setStatus(slime::ui::tr(slime::ui::t("ui.92cfd0503816")));
    }
}

// ============================================================================
// v3.3.9（任务书 1.1）：缓存构建模式
//   * 只有这个模式写缓存（1.2：其他模式一律只读）
//   * 只做史莱姆判定 + 写缓存，不做匹配 ⇒ 比同范围搜索快（省掉整个匹配阶段）
//   * 范围 = 界面上的 X/Z；算法 = 自动（准确优先：精确判定路径，不采样、不分层）
//   * 中断续传：按「以行为单位的分段」执行，每段结束即原子落盘；重跑同范围时已覆盖分段秒过
//   * 完成条件：跑完整个范围 **或** 达到缓存容量上限（绝不「边写边丢」）
//   * 构建前预估：按位图口径（1 bit/区块 = 0.125 B/区块）+ 旧坐标口径（10% 密度 × 8 B）双口径提示
// ============================================================================
bool MainWindow::buildModeActive() const
{
    return m_buildModeActive;
}

// v3.4.9：构建模式由「当前页面 / 构建流程」驱动（搜索页开关已按要求移除）
void MainWindow::setBuildModeActive(bool on)
{
    if (m_buildModeActive == on) {
        updateBuildHintUi();
        return;
    }
    m_buildModeActive = on;
    Logger::info(std::string("缓存构建模式 ") + (on ? "开启" : "关闭"));
    updateBuildHintUi();
}

int64_t MainWindow::buildTotalChunks() const
{
    if (m_w->minX == nullptr) return 0;
    const int64_t w = static_cast<int64_t>(m_w->maxX->value()) - m_w->minX->value() + 1;
    const int64_t h = static_cast<int64_t>(m_w->maxZ->value()) - m_w->minZ->value() + 1;
    return (w > 0 && h > 0) ? w * h : 0;
}

// 勾选状态 → 提示文字 + 控件可用性（构建模式不做匹配，因此匹配相关控件禁用）
void MainWindow::updateBuildHintUi()
{
    // v3.4.9：不再因提示行缺失而提前返回 —— 标灰逻辑必须独立生效
    //（v3.4.8 的教训：搜索页开关的连接写在控件创建之前，导致"勾了不标灰"长期存在）
    const bool on = buildModeActive();
    if (m_w->buildHint) {
        m_w->buildHint->setVisible(on);
    }
    if (on && m_w->buildHint) {
        const int64_t total = buildTotalChunks();
        const double bitmapMB = static_cast<double>(total) / 8.0 / (1024.0 * 1024.0);
        m_w->buildHint->setText(
            slime::ui::t("ui.ff92ea44ed7e")
                .arg(total)
                .arg(bitmapMB, 0, 'f', 1)
                .arg(static_cast<double>(total) * 0.1 * 8.0 / (1024.0 * 1024.0), 0, 'f', 1));
    }
    // v3.3.9.1（任务书 1.1）：构建模式下，**与「判定 + 写缓存」无关的选项一律禁用**并给出原因，
    // 避免用户以为「游戏版本 / 搜索模式 / 算法 / 目标数量」等仍然生效（它们确实不参与构建）。
    // 不改排版：只切换 enabled 状态与 tooltip 文案。
    QList<QWidget*> irrelevant = {
        static_cast<QWidget*>(m_w->javaRadio),        // 游戏版本：缓存位图语义为基岩版（两个都灰，避免"还要选版本"）
        static_cast<QWidget*>(m_w->bedrockRadio),
        static_cast<QWidget*>(m_w->strictRadio),      // 搜索模式：构建不做匹配
        static_cast<QWidget*>(m_w->densityRadio),
        static_cast<QWidget*>(m_w->stopAtTarget),     // 结果筛选：构建不产出结果
        static_cast<QWidget*>(m_w->targetCount),
        static_cast<QWidget*>(m_w->smartRange),
        static_cast<QWidget*>(m_w->algExactRadio),    // 算法：构建固定用精确判定
        static_cast<QWidget*>(m_w->algLayeredRadio),
        static_cast<QWidget*>(m_w->algSampleRadio),
        static_cast<QWidget*>(m_w->algBruteRadio),
        static_cast<QWidget*>(m_w->seedEdit),         // v3.4.8：构建不需要种子（基岩版判定与种子无关）
        static_cast<QWidget*>(m_w->lockDir)           // v3.4.8：构建不做匹配 ⇒ 方向锁定同样无关
    };
    if (m_w->gridSize) irrelevant << m_w->gridSize;          // 网格/密度：只服务匹配阶段
    if (m_w->minDensity) irrelevant << m_w->minDensity;
    if (m_w->drawPattern) irrelevant << m_w->drawPattern; // 图案绘制入口：只服务严格匹配
    for (QWidget* w : irrelevant) {
        if (!w) continue;
        if (on) {
            if (w->toolTip().isEmpty() || !w->property("v3391tip").toBool()) {
                w->setProperty("v3391tip", true);
                w->setProperty("v3391oldTip", w->toolTip());
            }
            w->setToolTip(slime::ui::t("ui.29299675eb80"));
            w->setEnabled(false);
        } else if (w->property("v3391tip").toBool()) {
            // v3.4.9：只恢复**本函数禁用过**的项。原来无条件 setEnabled(true) 会把版本规则的
            // 禁用（基岩版不用 Java 种子等）一起解掉 —— 现在交给下面的 applyVersionUi() 重新收敛。
            w->setToolTip(w->property("v3391oldTip").toString());
            w->setProperty("v3391tip", false);
            w->setEnabled(true);
        }
    }
    // 关闭构建模式后按版本/模式规则重新收敛一次（种子、缓存、预设仅限对应版本）
    applyVersionUi();
    if (on && m_w->bedrockRadio && !m_w->bedrockRadio->isChecked()) {
        // v3.10.7（用户反馈：测试中平台莫名其妙自己变成基岩版）：
        //   构建模式强制基岩版只是临时语义，记下用户原来的选择，退出构建时恢复。
        m_preBuildWasBedrock = m_w->bedrockRadio && m_w->bedrockRadio->isChecked();
        m_buildSavedVersion = true;
        m_w->bedrockRadio->setChecked(true); // 缓存语义 = 基岩版（Java 版不写缓存）
    } else if (!on && m_buildSavedVersion) {
        m_buildSavedVersion = false;
        if (m_w->bedrockRadio && m_w->bedrockRadio->isChecked() != m_preBuildWasBedrock) {
            if (m_preBuildWasBedrock) m_w->bedrockRadio->setChecked(true);
            else if (m_w->javaRadio) m_w->javaRadio->setChecked(true);
        }
    }
    // v3.4.9：构建内容就在独立的「缓存构建」页里，随页面显示，不再需要在这里控制可见性
}

// ============================================================================
// v3.4.11（用户拍板）配置落盘合并 + 设置页收敛
//   为什么：设置页任何改动都会走 60ms 自动生效；旧实现每次生效都同步写整份 config.json
//   （含终端历史），拖动滑块时约 16 次/秒的磁盘写入 + QSS 重放 ⇒ 界面卡顿（音量条最明显）。
//   现在：内存状态立即生效；磁盘写入合并到「空闲 500ms」或滑块释放时 flush。
// ============================================================================
void MainWindow::scheduleConfigWrite()
{
    if (!m_configWriteTimer) {
        m_configWriteTimer = new QTimer(this);
        m_configWriteTimer->setSingleShot(true);
        m_configWriteTimer->setInterval(500);
        connect(m_configWriteTimer, &QTimer::timeout, this, [this]() {
            m_configWritePending = false;
            writeConfigFile(m_w && m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                            m_terminal ? m_terminal->history() : QStringList());
            Logger::info("配置已合并写入 config.json");
        });
    }
    m_configWritePending = true;
    m_configWriteTimer->start();
}

void MainWindow::flushPendingConfigWrite()
{
    if (!m_configWritePending) return;
    if (m_configWriteTimer) m_configWriteTimer->stop();
    m_configWritePending = false;
    writeConfigFile(m_w && m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                    m_terminal ? m_terminal->history() : QStringList());
}

// v3.4.11（用户反馈「设置在重启后仍然生效，但设置项不是正确状态」的根因）：
//   buildUi() 用**默认值**先把设置页建出来，之后 loadAppSettings() 才把 config.json 读进
//   m_settings —— 中间没人把值推回设置页 ⇒ 页面显示默认值（而程序行为是保存值）。
//   更糟的是设置页是「立即生效」的取值来源，之后任何改动都会把过期的页面值写回磁盘。
//   本函数把「真实状态 → 设置页」单向收敛一次（同步期间用 guard 抑制自动生效，避免回写循环）。
void MainWindow::syncSettingsPageFromState()
{
    if (!m_settingsPage || m_settingsSyncGuard) return;
    m_settingsSyncGuard = true;
    m_settingsPage->setValues(m_settings);
    m_settingsSyncGuard = false;
    // 页面已经等于真实状态 ⇒ 挂起的「自动生效」没有意义，停掉以免又拿页面值回写一遍
    if (m_settingsApplyTimer) m_settingsApplyTimer->stop();
}

// v3.4.9：把搜索页的范围推到构建页（构建页只是同一份数据的镜像视图）
void MainWindow::pushSearchRangeToBuild()
{
    if (!m_w || !m_w->bMinX || !m_w->minX || m_rangeSyncGuard) return;
    m_rangeSyncGuard = true;
    m_w->bMinX->setValue(m_w->minX->value());
    m_w->bMaxX->setValue(m_w->maxX->value());
    m_w->bMinZ->setValue(m_w->minZ->value());
    m_w->bMaxZ->setValue(m_w->maxZ->value());
    m_rangeSyncGuard = false;
    updateBuildHintUi();
    updateBuildEstimateLive(); // v3.4.12：搜索页改范围也刷新构建预估
}

// 构建前预估 + 上限确认 + 分段；返回 false 表示用户放弃本次构建
bool MainWindow::prepareBuildRun(SearchOptions& opt, RunInfo& info)
{
    const int64_t total = buildTotalChunks();
    if (total <= 0) {
        QMessageBox::warning(this, slime::ui::t("ui.073cf8d38324"), slime::ui::t("ui.9d61509a4e27"));
        return false;
    }
    // ① 构建前预估（任务书 1.1）：范围面积 × 密度 10% × 每区块字节数（旧口径）+ 位图口径
    const double bitmapMB = static_cast<double>(total) / 8.0 / (1024.0 * 1024.0);
    const double legacyMB = static_cast<double>(total) * 0.1 * 8.0 / (1024.0 * 1024.0);
    const int64_t capMB = cacheMaxBytes() / (1024 * 1024);
    const bool overCap = cacheMaxBytes() > 0 && bitmapMB > static_cast<double>(capMB);
    if (!m_uiQuiet) {
        QString body = slime::ui::t("ui.be2e910d051e")
                           .arg(m_w->minX->value()).arg(m_w->maxX->value())
                           .arg(m_w->minZ->value()).arg(m_w->maxZ->value())
                           .arg(total)
                           .arg(bitmapMB, 0, 'f', 1)
                           .arg(legacyMB, 0, 'f', 1)
                           .arg(capMB);
        if (overCap) {
            body += slime::ui::t("ui.ad89a275f758").arg(
                static_cast<int64_t>(bitmapMB * 1.2) + 1);
            body += slime::ui::t("ui.ec13fdb97145");
            QMessageBox box(this);
            box.setWindowTitle(slime::ui::t("ui.9d43e3b73ab4"));
            box.setText(body);
            QPushButton* raise = box.addButton(slime::ui::t("ui.9a81e2e81062"), QMessageBox::AcceptRole);
            QPushButton* go = box.addButton(slime::ui::t("ui.c20257102d0b"), QMessageBox::ActionRole);
            box.addButton(slime::ui::t("ui.4d0b4688c787"), QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == raise) {
                const int64_t newMB = static_cast<int64_t>(bitmapMB * 1.2) + 64;
                m_settings.cacheMaxMB = static_cast<int>(newMB);
                applyAppSettings(m_settings); // 写回 config.json（cache.maxSizeMB）
                Logger::info("缓存上限已按构建预估提高到 " + std::to_string(newMB) + " MB");
                addFeedback(2, slime::ui::tr(slime::ui::t("ui.c3108a4edc5d")).arg(newMB));
            } else if (box.clickedButton() != go) {
                return false;
            }
        } else {
            body += slime::ui::t("ui.7f8cd0261e31");
            // v3.4.12：无界面自检（m_uiQuiet）时自动放行 —— 与其它确认弹窗同一口径。
            //（此前漏了这层保护，自检走到构建流程时会被模态框挡住 => 挂起。）
            if (!m_uiQuiet && QMessageBox::question(this, slime::ui::t("ui.9d43e3b73ab4"), body) != QMessageBox::Yes) {
                return false;
            }
        }
    }

    // ② 分段（每段 rowsPerBand 行）：段结束即落盘 ⇒ 中断续传的最小单位
    const int64_t minZ = m_w->minZ->value();
    const int64_t maxZ = m_w->maxZ->value();
    const int64_t minX = m_w->minX->value();
    const int64_t maxX = m_w->maxX->value();
    const int64_t w = maxX - minX + 1;
    // 目标：单段 ≈ 20~60 秒（按 50 万区块/秒的保守速率 + 头开销估算），并保证位图占用有界
    int64_t rowsPerBand = (std::max)(int64_t(1024), (int64_t(8) * 1024 * 1024) / (std::max)(int64_t(1), w));
    rowsPerBand = (std::min)(rowsPerBand, maxZ - minZ + 1);
    m_buildBands.clear();
    for (int64_t z = minZ; z <= maxZ; z += rowsPerBand) {
        const int64_t zEnd = (std::min)(maxZ, z + rowsPerBand - 1);
        m_buildBands.push_back(Rect{ static_cast<int>(minX), static_cast<int>(maxX),
                                     static_cast<int>(z), static_cast<int>(zEnd) });
    }
    m_buildBandIndex = 0;
    m_buildDoneChunks = 0;
    m_buildSkippedTotal = 0;
    m_buildActive = true;
    m_buildStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    // ③ 构建选项：基岩版 + 精确判定（准确优先，不采样/不分层）+ 唯一写缓存
    opt.version = Version::Bedrock;
    opt.useCache = true;
    opt.cache = m_cache.get();
    opt.cacheWrite = m_cache.get();
    opt.cacheBudgetBytes = cacheMaxBytes();
    opt.cacheWriteDisabled = false;
    opt.buildMode = true;
    opt.algorithm = Algorithm::Exact;
    opt.mode = Mode::Density;   // 判定路径与密度搜索共用填充（不做匹配）
    opt.gridSize = (std::max)(1, m_w->gridSize ? m_w->gridSize->value() : 5);
    opt.stopAtTarget = false;
    opt.smartRange = false;
    opt.targetCount = 1;
    m_buildBaseOpt = opt;
    m_buildRunInfo = info;
    m_buildRunInfo.version = Version::Bedrock;
    m_buildRunInfo.options = opt;
    m_buildRunInfo.cacheWasUsed = true;
    m_buildRunInfo.mergeCacheAfter = false;
    m_buildRunInfo.region = m_buildBands.front();
    m_buildRunInfo.mode = opt.mode;
    m_buildRunInfo.algorithm = opt.algorithm;
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.9e975f02353a"))
                       .arg(total)
                       .arg(m_buildBands.size()));
    Logger::info("缓存构建开始：范围 X(" + std::to_string(minX) + "~" + std::to_string(maxX) + ") Z(" +
                 std::to_string(minZ) + "~" + std::to_string(maxZ) + ")，共 " +
                 std::to_string(m_buildBands.size()) + " 段，预估位图 " + std::to_string(bitmapMB) +
                 " MB，上限 " + std::to_string(capMB) + " MB");
    return true;
}

// 启动当前分段（同一套 SearchTask 机制：可取消、进度回调、完成回调）
void MainWindow::launchBuildBand()
{
    if (m_buildBandIndex >= m_buildBands.size() || !m_running) return;
    SearchOptions opt = m_buildBaseOpt;
    const Rect band = m_buildBands[m_buildBandIndex];
    opt.minZ = band.minZ;
    opt.maxZ = band.maxZ;
    opt.minX = band.minX;
    opt.maxX = band.maxX;
    const RunInfo info = m_buildRunInfo;
    const auto postToUi = [this](auto fn) {
        QMetaObject::invokeMethod(this, [fn = std::move(fn)]() { fn(); }, Qt::QueuedConnection);
    };
    const int runId = ++m_runId;
    m_runMode = opt.mode;
    m_runAlg = opt.algorithm;
    m_task.resetBuildCounters();
    m_task.start(
        opt,
        [this, postToUi, runId](double frac, bool scanning, double, double) {
            postToUi([this, frac, scanning, runId]() {
                if (!m_running || runId != m_runId) return;
                updateBuildProgressUi(scanning);
                (void)frac;
            });
        },
        [this, postToUi, info, runId](const SearchOutcome& out) {
            postToUi([this, out, info, runId]() { onSearchFinished(out, info, runId); });
        });
    std::fprintf(stderr, "[构建] 段 %zu/%zu 启动：Z(%d~%d)\n", m_buildBandIndex + 1, m_buildBands.size(),
                 band.minZ, band.maxZ);
}

// 四项进度（任务书 1.1 指定顺序）：已记录区块(百分比) / 记录速度(万区块/秒) / 已花费秒数（+ 阶段区分，1.4）
void MainWindow::updateBuildProgressUi(bool scanning)
{
    const int64_t scanned = m_task.liveScannedCount();
    const int64_t recorded = m_buildDoneChunks + scanned;
    const int64_t total = (std::max)(int64_t(1), buildTotalChunks());
    const qint64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    const double secs = (m_buildStartMs > 0) ? static_cast<double>(nowMs - m_buildStartMs) / 1000.0 : 0.0;
    const double wanPerSec = secs > 0.05 ? (static_cast<double>(recorded) / 10000.0) / secs : 0.0;
    // 引擎的「已记录」含块间重叠行（与搜索扫描计数同口径）⇒ 百分比按去重口径截顶到 100%
    const double pct =
        (std::min)(100.0, 100.0 * static_cast<double>(recorded) / static_cast<double>(total));
    if (m_w->progress) m_w->progress->setValue(static_cast<int>((std::min)(100.0, pct) * 10.0));
    if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("%1%").arg(pct, 0, 'f', 1));
    if (m_w->progressStage) {
        m_w->progressStage->setText(m_task.livePhase() == 1 ? slime::ui::t("ui.4ea75e619af3")
                                                            : slime::ui::t("ui.a8878877a9f5"));
    }
    if (m_w->progressInfo) {
        m_w->progressInfo->setText(
            slime::ui::t("ui.e9f7fb6a1c88")
                .arg(recorded)
                .arg(pct, 0, 'f', 1)
                .arg(wanPerSec, 0, 'f', 1)
                .arg(secs, 0, 'f', 1)
                .arg(m_buildSkippedTotal > 0 || m_task.liveSkippedCoveredCount() > 0
                         ? slime::ui::t("ui.c82eec4ef45f")
                               .arg(m_buildSkippedTotal + m_task.liveSkippedCoveredCount())
                         : QString()));
    }
    (void)scanning;
}

// 分段结束：落盘（续传点）→ 继续下一段 / 收尾
void MainWindow::handleBuildBandFinished(const SearchOutcome& out, int runId)
{
    // v3.3.9.1（任务书 1.2/1.3 根因修复）：
    //   ① 已收尾时再次进入 ⇒ 直接忽略（此前会重复累加计数 ⇒ 进度 158%、重复记录）
    //   ② 陈旧运行的完成事件（runId 不匹配）⇒ 丢弃（此前会多加一段、再启一段 ⇒ 死循环/计数翻倍）
    //   ③ 段下标越界 ⇒ 立即收尾（此前 `m_buildBands[idx]` 越界读取 = 未定义行为 ⇒ 偶发闪退；
    //      详细日志开启后时序变化，越界概率下降，这正是「开日志就不崩」的典型特征）
    if (!m_buildActive) return;
    if (runId != m_runId) {
        slime::IncidentLog::note("缓存构建：丢弃陈旧分段事件（事件 runId " + std::to_string(runId) +
                                 "，当前 " + std::to_string(m_runId) + "）");
        return;
    }
    if (m_buildBandIndex >= m_buildBands.size()) {
        slime::IncidentLog::note("缓存构建：分段下标越界（" + std::to_string(m_buildBandIndex) + "/" +
                                 std::to_string(m_buildBands.size()) + "）⇒ 立即收尾");
        m_buildStopRequested = true;
        m_buildBandIndex = m_buildBands.size();
    }
    m_task.wait();
    if (m_buildBandIndex < m_buildBands.size()) {
        m_buildDoneChunks += m_task.liveScannedCount();
        m_buildSkippedTotal += m_task.liveSkippedCoveredCount();
    }

    const bool cancelled = out.cancelled;
    const bool budgetHit = out.hitCacheBudget;
    const bool failed = !out.ok && !cancelled;

    // 落盘（原子替换；失败不影响内存内容）—— 这就是「中断续传」的检查点
    std::string serr;
    const bool selftestMode = !qEnvironmentVariableIsEmpty("SLIME_SELFTEST");
    const bool saved = selftestMode ? m_cache->saveFile(m_cacheFile, serr) : saveV3Cache();
    if (saved && selftestMode) saveV3Cache();
    updateCacheStatusUi();

    if (failed) {
        m_buildActive = false;
        m_running = false;
        setRunningUi(false);
        setInputsEnabled(true);
        setStatus(slime::ui::tr(slime::ui::t("ui.1242b3d3624e")));
        QMessageBox::critical(this, slime::ui::t("ui.1242b3d3624e"), QString::fromStdString(out.error));
        Logger::error("缓存构建出错：" + out.error);
        return;
    }

    const bool more = !cancelled && !budgetHit && (m_buildBandIndex + 1 < m_buildBands.size());
    std::fprintf(stderr, "[构建] 段 %zu/%zu 结束：cancelled=%d budget=%d 已记录累计=%lld 已跳过累计=%lld 保存=%d\n",
                 m_buildBandIndex + 1, m_buildBands.size(), cancelled ? 1 : 0, budgetHit ? 1 : 0,
                 static_cast<long long>(m_buildDoneChunks), static_cast<long long>(m_buildSkippedTotal),
                 saved ? 1 : 0);
    if (more) {
        ++m_buildBandIndex;
        if (m_progressStream) {
            m_progressStream->appendRawLine(
                slime::ui::t("ui.12f5363fe49e")
                    .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                    .arg(m_buildBandIndex)
                    .arg(m_buildBands.size()));
        }
        launchBuildBand(); // 继续下一段（m_running 保持 true，界面无需复位）
        return;
    }

    // 收尾
    m_buildActive = false;
    m_running = false;
    setRunningUi(false);
    setInputsEnabled(true);
    const int64_t total = (std::max)(int64_t(1), buildTotalChunks());
    const double secs = (std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count() -
                         m_buildStartMs) /
                        1000.0;
    QString result;
    if (cancelled) {
        result = slime::ui::t("ui.8ff8db608a0a");
        setStatus(slime::ui::tr(slime::ui::t("ui.9b7da6dcb967")));
    } else if (budgetHit) {
        result = slime::ui::t("ui.75015f29358d")
                     .arg(m_buildDoneChunks)
                     .arg(total);
        setStatus(slime::ui::tr(slime::ui::t("ui.8986e95164b7")));
    } else {
        result = slime::ui::t("ui.5a93958ec725")
                     .arg(m_buildDoneChunks)
                     .arg(secs, 0, 'f', 1);
        setStatus(slime::ui::tr(slime::ui::t("ui.68eb54822190")));
    }
    if (m_w->progressStage) m_w->progressStage->setText(slime::ui::t("ui.120cb0c8c8fa"));
    if (m_w->progressInfo) {
        m_w->progressInfo->setText(
            slime::ui::t("ui.4b7c9b14e7ad")
                .arg(m_buildDoneChunks)
                .arg((std::min)(100.0, 100.0 * static_cast<double>(m_buildDoneChunks) /
                                            static_cast<double>(total)),
                     0, 'f', 1)
                .arg(secs > 0.05 ? (static_cast<double>(m_buildDoneChunks) / 10000.0) / secs : 0.0, 0, 'f', 1)
                .arg(secs, 0, 'f', 1));
    }
    if (m_progressStream) {
        m_progressStream->appendRawLine(slime::ui::t("ui.0a1ed290251d")
                                            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                                            .arg(result));
    }
    addFeedback(2, result + slime::ui::t("ui.ed6566f75a34")
                           .arg(m_cache->ranges().size())
                           .arg(m_cache->entryCount()));
    Logger::info("缓存构建结束：" + result.toStdString() + " | 缓存 " +
                 std::to_string(m_cache->ranges().size()) + " 区域 / " +
                 std::to_string(m_cache->entryCount()) + " 区块 | " +
                 slime::IncidentLog::memorySummary());
    if (!saved) {
        addFeedback(1, slime::ui::tr(slime::ui::t("ui.37d56173be95"))
                           .arg(QString::fromStdString(serr)));
    }
    if (m_settings.soundOnComplete) slime::SoundPlayer::playCompletionChime(m_settings.soundVolume);
    updateNavBadges();
    updateCacheStatusUi();
    // 构建结束不弹「结果」对话框（构建模式不产生匹配结果）
    m_w->resultView->setPlainText(slime::ui::t("ui.27c62f740f6c"));
}

void MainWindow::cancelSearch()
{
    if (m_ringSearching) {
        m_ringCancel.store(true, std::memory_order_relaxed);
        if (m_w && m_w->cancel) m_w->cancel->setEnabled(false);
        setStatus(slime::ui::tr(slime::ui::t("ui.dd9a9f769e57")));
        return;
    }
    if (!m_running) return;
    m_task.cancel();
    m_w->cancel->setEnabled(false);
    setStatus(slime::ui::tr(slime::ui::t("ui.6bc0d687aeaa")));
    Logger::info("用户请求取消搜索");
    // v3.3.7（P1-6）：取消也写关键信息日志（不是崩溃，因此不标记 incident）
    slime::IncidentLog::note("用户取消搜索 | " + slime::IncidentLog::memorySummary());
}

// v3.10.8（用户疑问：预设是否暗改非明面设置？自定义调成相同设置会不会仍变慢）：
//   每次搜索都把「引擎实际收到的全部参数」写成一行指纹 → 预设 vs 自定义可以直接逐字比对。
//   若两行完全相同，性能差异就只可能来自运行时状态（缓存/GPU 探测/系统负载），不可能来自隐藏开关。
QString MainWindow::searchOptionsFingerprint(const SearchOptions& opt) const
{
    const bool gpuOn = m_w->gpuCheck && m_w->gpuCheck->isChecked();
    return QStringLiteral(
               "ver=%1 mode=%2 alg=%3 g=%4 thr=%5 target=%6 stop=%7 smart=%8 ignore=%9 "
               "lock=%10 layer=%11/%12 sample=%13/%14 threads=%15 cpuLimit=%16 memMB=%17 "
               "cache=%18 gpu=%19 patternStream=%20 start=(%21,%22) area=%23x%24 selfOpt=%25")
        .arg(opt.version == slime::Version::Bedrock ? QStringLiteral("bedrock") : QStringLiteral("java"))
        .arg(opt.mode == slime::Mode::Strict ? QStringLiteral("strict") : QStringLiteral("density"))
        .arg(static_cast<int>(opt.algorithm))
        .arg(opt.gridSize)
        .arg(opt.minDensity, 0, 'f', 3)
        .arg(opt.targetCount)
        .arg(opt.stopAtTarget ? 1 : 0)
        .arg(opt.smartRange ? 1 : 0)
        .arg(opt.ignoreThreshold ? 1 : 0)
        .arg(opt.directionLocked ? 1 : 0)
        .arg(opt.layerBlock)
        .arg(opt.layerRatio, 0, 'f', 3)
        .arg(opt.sampleStep)
        .arg(opt.sampleExact ? 1 : 0)
        .arg(opt.threads)
        .arg(opt.cpuLimitPercent)
        .arg(opt.memBudgetBytes / (1024 * 1024))
        .arg(opt.useCache ? 1 : 0)
        .arg(gpuOn ? 1 : 0)
        .arg(m_settings.gpuPatternStream ? 1 : 0)
        .arg(opt.startX)
        .arg(opt.startZ)
        .arg(static_cast<qint64>(opt.maxX) - opt.minX + 1)
        .arg(static_cast<qint64>(opt.maxZ) - opt.minZ + 1)
        .arg(m_settings.gpuResultSelfOptimize ? 1 : 0);
}

void MainWindow::logSearchParams(const SearchOptions& opt)
{
    std::string s = "开始搜索 | 版本=";
    s += (opt.version == Version::Java) ? "Java" : "基岩版";
    s += " 种子=" + std::to_string(opt.worldSeed);
    s += " 范围=(" + std::to_string(opt.minX) + "," + std::to_string(opt.maxX) + "," +
         std::to_string(opt.minZ) + "," + std::to_string(opt.maxZ) + ")";
    s += (opt.mode == Mode::Strict) ? " 模式=严格图案" : " 模式=密度";
    if (opt.mode == Mode::Strict) {
        s += " 图案=" + std::to_string(opt.pattern.rows()) + "x" + std::to_string(opt.pattern.cols()) +
             " 有效=" + std::to_string(opt.pattern.activeCount());
        s += opt.directionLocked ? " 方向锁定" : " 方向不锁定";
    } else {
        s += " 网格=" + std::to_string(opt.gridSize) + " 密度≥" + std::to_string(opt.minDensity);
        s += " 算法=" + algorithmDisplayName(opt.algorithm).toStdString();
        if (opt.algorithm == Algorithm::Layered) {
            s += " 分层级别=" + std::to_string(opt.layerBlock) +
                 " 阈值比例=" + std::to_string(opt.layerRatio);
        } else if (opt.algorithm == Algorithm::Sample) {
            s += " 采样间隔=" + std::to_string(opt.sampleStep);
            s += opt.sampleExact ? " 精算采样" : " 采样估算";
        }
    }
    s += " 起点=(" + std::to_string(opt.startX) + "," + std::to_string(opt.startZ) + ")";
    s += " 目标N=" + std::to_string(opt.targetCount);
    s += opt.stopAtTarget ? " 及时停止" : "";
    s += opt.smartRange ? " 智能范围" : "";
    s += " 线程=" + std::to_string(opt.threads);
    s += opt.useCache ? " 使用缓存" : "";
    s += opt.gpuScan ? " GPU扫描" : "";
    Logger::info(s);
    slime::IncidentLog::note("搜索参数：" + s); // v3.3.7：关键信息日志（常开）
}

// ---------------- 完成 ----------------
void MainWindow::onSearchFinished(const SearchOutcome& out, const RunInfo& info, int runId)
{
    // v3.3.9（任务书 1.1）：缓存构建模式的分段续跑（每段落盘 → 继续下一段 / 收尾）
    if (m_buildActive) {
        handleBuildBandFinished(out, runId);
        return;
    }
    // v3.3.7（任务书 P0-2）：**陈旧结果事件**（上一次运行的结果在取消后、用户已开始新搜索时才被
    // 处理）必须在此处直接丢弃：否则会错误地把新搜索标记为已结束、重新启用输入，
    // 并在下面的 wait() 里 join 到新搜索线程（界面冻结）。
    if (runId != m_runId) {
        slime::IncidentLog::note("丢弃陈旧结果事件（runId 不匹配；当前运行 " + std::to_string(m_runId) +
                                 "，事件来自 " + std::to_string(runId) + "）");
        return;
    }
    m_running = false;
    setRunningUi(false);
    setInputsEnabled(true);

    if (out.cancelled) {
        m_w->progress->setValue(0);
        m_w->progressInfo->setText(slime::ui::t("ui.14c0ffe017c1").arg(m_streamScanned));
        if (m_w->progressStage) m_w->progressStage->setText(progressStageName(5, m_runMode, m_runAlg));
        setStatus(slime::ui::tr(slime::ui::t("ui.a5ffdc95eeb0")));
        Logger::info("搜索已取消");
        // v3.3.6（任务书 调整 3）：取消也留一条流水记录；并按设置播放结束提示音
        if (m_progressStream) {
            m_progressStream->appendRawLine(slime::ui::t("ui.cfe9d8931fe1")
                                                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                                                .arg(m_streamScanned));
        }
        if (m_settings.soundOnComplete) {
            slime::SoundPlayer::playCompletionChime(m_settings.soundVolume);
        }
        updateNavBadges();
        slime::IncidentLog::note("搜索已取消：已扫描 " + std::to_string(m_streamScanned) + "/" +
                                 std::to_string(displayTotalChunks()) + " 区块 | " +
                                 slime::IncidentLog::memorySummary());
        m_task.wait();
        return;
    }
    if (!out.ok) {
        m_w->progressInfo->setText(slime::ui::t("ui.b859c7be7501"));
        if (m_w->progressStage) m_w->progressStage->setText(progressStageName(6, m_runMode, m_runAlg));
        setStatus(slime::ui::tr(slime::ui::t("ui.72b0bb777554")));
        if (m_progressStream) {
            m_progressStream->appendRawLine(slime::ui::t("ui.a5514476787e")
                                                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                                     QString::fromStdString(out.error)));
        }
        QMessageBox::critical(this, slime::ui::t("ui.680a503add20"), QString::fromStdString(out.error));
        Logger::error("搜索错误: " + out.error);
        updateNavBadges();
        slime::IncidentLog::note("搜索错误：" + out.error + " | " + slime::IncidentLog::memorySummary());
        m_task.wait();
        return;
    }

    // 成功后缓存处理：
    //  - 经典路径：整区合并（含容量上限）；分块路径：引擎已按块增量合并（cacheHandledIncrementally）
    //  v3.3.3（任务书 2.2）：无论走哪条分支，只要缓存发生了变化就**立即刷新状态标签**，
    //  并在状态栏给出一次性反馈（设置区可能被滚动到别处，用户仍能看到「缓存已更新」）。
    if (info.cacheWasUsed && info.mergeCacheAfter) {
        if (!out.cacheHandledIncrementally) {
            // v3.3.8（任务 1 / ④′ 修复方案 B）：**整区合并前先确认「有新增条目」**。
            // mergeRegion 会把 region 加入 m_ranges（覆盖声明）；若本次范围 0 新增条目，声明就没有
            // 条目支撑 ⇒ covers()=true 而 lookup() 恒返回 0（被判为已知非史莱姆）⇒ 漏史莱姆。
            // 代价：该区域下次会重扫（正确性优先；0 新增条目意味着本次新算的那部分确无史莱姆区块）。
            if (!out.newCacheChunks.empty()) {
                m_cache->mergeRegion(info.region, out.newCacheChunks);
            } else {
                const std::string rg = "[" + std::to_string(info.region.minX) + "," +
                                       std::to_string(info.region.maxX) + "]x[" +
                                       std::to_string(info.region.minZ) + "," +
                                       std::to_string(info.region.maxZ) + "]";
                Logger::info("本次范围无新增条目，未登记覆盖声明（避免空壳声明）：区域 " + rg);
                slime::IncidentLog::note("cache.hollow 经典路径未登记覆盖声明（0 新增条目）区域=" + rg);
            }
            m_cache->pruneToFit(cacheMaxBytes());
        }
        std::string err;
        const bool selftestMode = !qEnvironmentVariableIsEmpty("SLIME_SELFTEST");
        const bool savedNow = selftestMode ? m_cache->saveFile(m_cacheFile, err) : saveV3Cache();
        if (!savedNow) {
            Logger::warning("缓存保存失败: " + err);
        } else {
            if (selftestMode) saveV3Cache();
            // v3.3.2：日志中明确写出落盘路径与文件名，用户可直接据此在「导入缓存」对话框中定位
            Logger::info("缓存已更新保存：" + std::to_string(m_cache->ranges().size()) + " 区域 / " +
                         std::to_string(m_cache->entryCount()) + " 区块" +
                         (out.cacheHandledIncrementally ? "（分块增量合并）" : "") +
                         "（路径 " + m_cacheFile.string() + "）");
            setStatus(slime::ui::tr(slime::ui::t("ui.769f8655bcd8")).arg(m_cache->entryCount()));
        }
        updateCacheStatusUi();
    } else if (info.cacheWasUsed && out.cacheHandledIncrementally) {
        // 搜索范围原本已被缓存覆盖：无新数据，无需写盘；状态标签仍刷新一次（幂等）
        updateCacheStatusUi();
    }

    m_w->progress->setValue(1000);
    if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("100%"));
    if (m_w->searchStage) m_w->searchStage->setText(slime::ui::t("ui.2437d377fef4"));
    // v3.5.26（用户要求）：0 结果时进度条变红，一眼看出「什么都没找到」
    {
        const bool empty = out.results.empty();
        m_w->progress->setStyleSheet(empty ? QStringLiteral("QProgressBar::chunk { background-color: #d9534f; }")
                                           : QString());
        if (empty) m_w->progress->setValue(1000);
    }
    // v3.6.5（用户要求）：结果区上方显眼提醒 —— 没结果 / 少于目标
    if (m_resultsNotice) {
        const int got = static_cast<int>(out.results.size());
        const int want = (std::max)(1, m_w->targetCount ? m_w->targetCount->value() : 1);
        if (got == 0)
            m_resultsNotice->setText(slime::ui::t("ui.17533b453292"));
        else if (got < want)
            m_resultsNotice->setText(slime::ui::t("ui.a1e9abe326ce")
                                         .arg(got).arg(want));
        else
            m_resultsNotice->setText(QString());
        m_resultsNotice->setVisible(!m_resultsNotice->text().isEmpty());
    }

    // v3.3.4（任务书 2.1）：进度行/状态栏 —— 阶段短语 + 统计信息（v3.3.7：去重）
    // v3.3.7.2（任务书步骤 5 / F-3）：完成态也用**真实扫描数 / 引擎口径总数**，
    // 不再把 scannedChunks 同时当分子与分母（旧实现永远显示 100.0%，掩盖真实口径）。
    if (m_w->progressStage) m_w->progressStage->setText(progressStageName(3, m_runMode, m_runAlg));
    if (m_w->progressInfo) {
        m_w->progressInfo->setText(progressStatsText(out.stats.scannedChunks, displayTotalChunks(), 0.0,
                                                     static_cast<int64_t>(out.results.size())));
    }
    setStatus(slime::ui::tr(slime::ui::t("ui.0133025c537b")));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 15);
    m_lastResultText = describeResultsText(out, info);
    // v3.6.7（用户反馈「搜索有结果但结果列表不显示」）：**必须先写入 m_lastOutcome 再刷新列表**。
    //   populateResultText() 内部会调用 fillResultTable()，而它读的是 m_lastOutcome；
    //   旧顺序是「先 populate 再赋值」⇒ 列表永远慢一拍（首次搜索必为空、后续搜索显示上一次结果）。
    m_lastOutcome = out;
    m_lastInfo = info;
    m_hasLastOutcome = true;
    populateResultText(out, info); // v3.3.4：结果写入右侧 QTextEdit（信息栏 + QTextEdit）
    if (m_elapsedTimer) m_elapsedTimer->stop();
    if (m_w->progressEta) m_w->progressEta->setText(slime::ui::t("ui.f00785313fdc"));
    if (m_map) {
        m_map->setResults(out.results); // v3.5.4：结果同步到内嵌地图
        if (m_mapSyncTimer) m_mapSyncTimer->start();
    }
    // v3.3.7.2（任务书 1.3）：删除此处对 progressInfo 的**死写** ——
    // 旧代码先写「搜索完成（结果可在右侧按距离/密度排序）」，紧接着（下面几行）就被
    // 「搜索完成：共 N 条…」覆盖，属于无效赋值并让「最终显示什么」不可预期。
    if (m_w->progressStage) {
        // v3.3.7（P1-5）：阶段只放短语；条数放状态栏统计（避免同一信息两处显示）
        m_w->progressStage->setText(progressStageName(4, m_runMode, m_runAlg));
    }
    if (m_w->progressInfo) {
        m_w->progressInfo->setText(slime::ui::t("ui.1a9adb67ce4c")
                                       .arg(out.results.size())
                                       .arg(m_resultTextTotal)
                                       .arg(progressStatsText(out.stats.scannedChunks, displayTotalChunks(),
                                                              0.0, static_cast<int64_t>(out.results.size()))));
    }
    // v3.3.6（任务书 调整 3）：进度分类追加「已完成」汇总行（格式见任务书样例）
    if (m_progressStream) {
        if (m_streamTimer) m_streamTimer->stop(); // 保证汇总行是最后一条
        m_progressStream->appendFinal(out.stats.scannedChunks,
                                      static_cast<qint64>(out.results.size()),
                                      out.stats.totalMs / 1000.0);
        // v3.9.10（过程可视化）：阶段耗时 / GPU 使用与回退原因 / 停止语义
        const QString summary = processSummaryText(out);
        for (const QString& line : summary.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            m_progressStream->appendRawLine(QStringLiteral("[%1] %2")
                                                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), line));
        }
    }
    // v3.3.6（任务书 4.2）：搜索完成提示音（音量来自设置；失败静默）
    if (m_settings.soundOnComplete) {
        slime::SoundPlayer::playCompletionChime(m_settings.soundVolume);
    }
    updateNavBadges(); // 反馈计数徽章刷新
    // v3.3.7（P1-6）：完成时把关键结果与内存峰值写入关键信息日志（回归对比用）
    slime::IncidentLog::note("搜索完成：扫描 " + std::to_string(out.stats.scannedChunks) +
                             " 区块 找到 " + std::to_string(out.results.size()) + " 个结果 耗时 " +
                             std::to_string(out.stats.totalMs / 1000.0) + "s 线程 " +
                             std::to_string(out.stats.threadsUsed) +
                             (out.stoppedEarly ? " [提前停止]" : "") + " | " +
                             slime::IncidentLog::memorySummary());
    setStatus(slime::ui::tr(slime::ui::t("ui.8174d1168fce")));
    writePerformanceLog(out, info);
    // v3.3.4（任务书 2.2）：关键事件同时进入反馈栏与日志总线
    addFeedback(out.results.empty() ? 2 : 3,
                slime::ui::t("ui.f6cc745d6f70")
                    .arg(out.results.size())
                    .arg(out.stats.scannedChunks)
                    .arg(out.stats.totalMs / 1000.0, 0, 'f', 3));
    slime::LogBus::info("搜索完成：结果 " + std::to_string(out.results.size()) + " 条，扫描 " +
                        std::to_string(out.stats.scannedChunks) + " 区块");
    // v3.9.10：过程摘要（阶段耗时 / GPU / 停止语义）进反馈与日志，GPU 回退必须可见
    {
        const QString summary = processSummaryText(out);
        addFeedback(3, summary);
        Logger::info("过程摘要 | " + summary.toStdString());
        if (!out.gpuFallbackReason.empty()) {
            addFeedback(2, slime::ui::t("ui.b32dcdccbfa4").arg(QString::fromStdString(out.gpuFallbackReason)));
        }
    }

    const double totalS = out.stats.totalMs / 1000.0;
    const double cps = totalS > 0 ? out.stats.scannedChunks / totalS : 0.0;
    Logger::info("搜索完成 | 找到=" + std::to_string(out.totalFound) + " 显示=" +
                 std::to_string(out.results.size()) + " 扫描区块=" + std::to_string(out.stats.scannedChunks) +
                 " 耗时=" + std::to_string(totalS) + "s(扫描 " + std::to_string(out.stats.scanMs / 1000.0) +
                 "s/匹配 " + std::to_string(out.stats.matchMs / 1000.0) + "s) 速度=" + std::to_string(cps) +
                 " 区块/s 线程=" + std::to_string(out.stats.threadsUsed) +
                 (out.stoppedEarly ? " [提前停止]" : ""));
    if (m_telemetry && !info.options.buildMode) {
        const QString mode = (info.mode == Mode::Strict) ? QStringLiteral("pattern") : QStringLiteral("density");
        const QString alg = (info.algorithm == Algorithm::Layered) ? QStringLiteral("layered")
                            : (info.algorithm == Algorithm::Sample) ? QStringLiteral("sample")
                            : (info.algorithm == Algorithm::Brute) ? QStringLiteral("brute")
                                                                   : QStringLiteral("exact");
        QJsonObject st;
        st.insert(QStringLiteral("grid_size"), info.gridSizeUsed);
        st.insert(QStringLiteral("min_density"), info.minDensityUsed);
        st.insert(QStringLiteral("target"), static_cast<double>(info.targetCount));
        st.insert(QStringLiteral("stop_at_target"), info.options.stopAtTarget);
        st.insert(QStringLiteral("smart_range"), info.options.smartRange);
        st.insert(QStringLiteral("ignore_threshold"), info.options.ignoreThreshold);
        st.insert(QStringLiteral("threads"), info.options.threads);
        st.insert(QStringLiteral("layer_block"), info.options.layerBlock);
        st.insert(QStringLiteral("layer_ratio"), info.options.layerRatio);
        st.insert(QStringLiteral("sample_step"), info.options.sampleStep);
        st.insert(QStringLiteral("sample_exact"), info.options.sampleExact);
        st.insert(QStringLiteral("gpu_requested"), info.options.gpuScan != nullptr);
        const QString seedStr = (info.version == Version::Bedrock)
                                    ? QString()
                                    : QString::number(info.options.worldSeed);
        const QJsonObject search = buildTelemetrySearch(
            seedStr, info.version == Version::Bedrock, mode, alg, QString(), info.region,
            info.options.startX, info.options.startZ, st, out,
            out.gpuScanUsed || out.gpuMatchUsed);
        m_telemetry->reportSearchDetailed(QString::fromLatin1(kAppVersion), QStringLiteral("test"),
                                          m_gpuScan ? QString::fromStdString(m_gpuScan->deviceName()) : QString(),
                                          search);
    }
    m_task.wait();
}

// v3.9.10（过程可视化）：阶段耗时 / GPU 使用与回退原因 / 停止语义 —— 单一来源，供进度流水、
//   反馈、日志、结果复制文本共用。
QString MainWindow::processSummaryText(const SearchOutcome& out) const
{
    QStringList lines;
    const double scanMs = out.stats.scanMs;
    const double matchMs = out.stats.matchMs;
    const double totalMs = out.stats.totalMs;
    const double otherMs = (std::max)(0.0, totalMs - scanMs - matchMs);
    lines << slime::ui::t("ui.7b6c64d1e526")
                 .arg(scanMs, 0, 'f', 1)
                 .arg(matchMs, 0, 'f', 1)
                 .arg(otherMs, 0, 'f', 1)
                 .arg(totalMs, 0, 'f', 1);

    const QString fb = QString::fromStdString(out.gpuFallbackReason);
    if (out.gpuScanUsed || out.gpuMatchUsed) {
        QString line = slime::ui::t("ui.9a7b9ede7a62")
                           .arg(out.gpuScanUsed ? QStringLiteral("✓") : QStringLiteral("×"))
                           .arg(out.gpuMatchUsed ? QStringLiteral("✓") : QStringLiteral("×"));
        if (!fb.isEmpty()) line += slime::ui::t("ui.126a68799ec1").arg(fb);
        lines << line;
    } else if (!fb.isEmpty()) {
        lines << slime::ui::t("ui.fa42483646f0").arg(fb);
    } else {
        lines << slime::ui::t("ui.52d1da3d64f2");
    }

    if (out.cacheFirstStopped) {
        lines << slime::ui::t("ui.c9e37c2e2f7f");
    } else if (out.chunksPruned > 0) {
        lines << slime::ui::t("ui.26956b0f822a")
                     .arg(out.chunksPruned);
    } else if (out.stoppedEarly) {
        lines << slime::ui::t("ui.4175c2419c3c");
    } else {
        lines << slime::ui::t("ui.94166c628e2d");
    }

    if (out.windowsSkipped > 0) {
        lines << slime::ui::t("ui.3ca6b3650a98").arg(out.windowsSkipped);
    }
    if (out.ignoredThreshold) {
        lines << slime::ui::t("ui.a123cba9d80e");
    }
    lines << slime::ui::t("ui.ebbfde235d3f").arg(out.chunkedPath ? slime::ui::t("ui.617f9839463e") : slime::ui::t("ui.f9e4c844a573"));
    return lines.join(QLatin1Char('\n'));
}

// 文本形式（用于复制与说明）
QString MainWindow::describeResultsText(const SearchOutcome& out, const RunInfo& info) const
{
    QString text;
    const double totalS = out.stats.totalMs / 1000.0;
    const double cps = totalS > 0 ? out.stats.scannedChunks / totalS : 0.0;

    text += slime::ui::t("ui.4588cdabce4f").arg(processSummaryText(out));
    text += slime::ui::t("ui.3773f957acba")
                .arg(out.stats.scannedChunks)
                .arg(totalS, 0, 'f', 3)
                .arg(out.stats.scanMs / 1000.0, 0, 'f', 3)
                .arg(out.stats.matchMs / 1000.0, 0, 'f', 3)
                .arg(out.stats.threadsUsed)
                .arg(cps / 10000.0, 0, 'f', 1);

    // v3.3.0（4.2）：当前使用模式 + 非精确模式提示（复制文本同步标注）
    const bool nonExact = (info.mode == Mode::Density && info.algorithm != Algorithm::Exact);
    text += slime::ui::t("ui.ffc148b2f7ea").arg(algorithmDisplayName(info.algorithm));
    if (nonExact) {
        text += slime::ui::t("ui.9fa83b53428b");
    }
    text += QLatin1Char('\n');
    if (nonExact && out.windowsSkipped > 0) {
        const double rate = 100.0 * static_cast<double>(out.windowsSkipped) /
                            static_cast<double>(out.windowsSkipped + out.stats.windowsEvaluated);
        text += slime::ui::t("ui.f1e3a7cbc2ce")
                    .arg(out.windowsSkipped)
                    .arg(rate, 0, 'f', 1);
    }

    if (out.results.empty()) {
        text += slime::ui::t("ui.967e134f508e");
        return text;
    }

    if (out.stoppedEarly) {
        text += slime::ui::t("ui.d37244ae5c61")
                    .arg(out.results.size()).arg(out.results.size());
    } else {
        text += slime::ui::t("ui.26039c5b8223").arg(out.totalFound).arg(out.results.size());
    }

    int idx = 1;
    for (const MatchResult& r : out.results) {
        const double cx = (static_cast<double>(r.minX) + r.maxX) / 2.0;
        const double cz = (static_cast<double>(r.minZ) + r.maxZ) / 2.0;
        const double dist = std::hypot(cx - static_cast<double>(info.startX), cz - static_cast<double>(info.startZ));
        text += slime::ui::t("ui.186c31a50487").arg(idx).arg(dist, 0, 'f', 2);
        text += slime::ui::t("ui.b657a9d6d6fd")
                    .arg(r.minX).arg(r.maxX).arg(r.minZ).arg(r.maxZ);
        const qlonglong bx0 = static_cast<qlonglong>(r.minX) * 16;
        const qlonglong bx1 = (static_cast<qlonglong>(r.maxX) + 1) * 16 - 1;
        const qlonglong bz0 = static_cast<qlonglong>(r.minZ) * 16;
        const qlonglong bz1 = (static_cast<qlonglong>(r.maxZ) + 1) * 16 - 1;
        text += slime::ui::t("ui.acd6c8073f08")
                    .arg(bx0).arg(bx1).arg(bz0).arg(bz1);
        if (info.mode == Mode::Density) {
            text += slime::ui::t("ui.52777be41a72").arg(r.density * 100.0, 0, 'f', 1);
        }
        text += QLatin1Char('\n');
        ++idx;
    }
    return text;
}

// /tp @s <x> 64 <z>（目标为中心方块坐标，y 固定 64）
QString MainWindow::cacheCommandFor(const MatchResult& r) const
{
    const qlonglong x = (static_cast<qlonglong>(r.minX) + r.maxX + 1) * 8;
    const qlonglong z = (static_cast<qlonglong>(r.minZ) + r.maxZ + 1) * 8;
    return QStringLiteral("/tp @s %1 64 %2").arg(x).arg(z);
}

// v3.3.4（任务书 1.1）：结果以文本形式显示在右侧 QTextEdit（信息栏 + QTextEdit 规范），
// 保留每条结果的 /tp 命令（右键菜单「复制该条命令」/「复制全部命令」）。
// 与 v3.3.3 的卡片列表相比：不再创建上千个 QWidget（更快、不会卡 UI），
// 复制命令的入口改为右键菜单 + 编辑菜单 + 命令终端（功能不减少）。
void MainWindow::populateResultText(const SearchOutcome& out, const RunInfo& info)
{
    QString text;
    const double totalS = out.stats.totalMs / 1000.0;
    const double cps = totalS > 0 ? out.stats.scannedChunks / totalS : 0.0;
    text += slime::ui::t("ui.a62aeabda435") +
            rateText(static_cast<double>(out.stats.scannedChunks) / (std::max)(1e-9, totalS),
                     m_settings.rateUnit) + QStringLiteral("\n")
                .arg(out.stats.scannedChunks)
                .arg(totalS, 0, 'f', 3)
                .arg(out.stats.threadsUsed)
                .arg(cps / 10000.0, 0, 'f', 1);
    text += slime::ui::t("ui.7db8f96b7d21").arg(algorithmDisplayName(info.algorithm));
    if (info.mode == Mode::Density && info.algorithm != Algorithm::Exact) {
        QString warn = slime::ui::t("ui.ecacdc495bdf").arg(algorithmDisplayName(info.algorithm));
        if (info.algorithm == Algorithm::Sample) {
            warn += slime::ui::t("ui.d8733f657313");
        }
        warn += slime::ui::t("ui.f6576484b989");
        if (out.windowsSkipped > 0) {
            const double rate = 100.0 * static_cast<double>(out.windowsSkipped) /
                                static_cast<double>(out.windowsSkipped + out.stats.windowsEvaluated);
            warn += slime::ui::t("ui.ea74b35a4457")
                        .arg(out.windowsSkipped)
                        .arg(rate, 0, 'f', 1);
        }
        text += warn + QLatin1Char('\n');
    }
    text += QLatin1Char('\n');

    if (out.results.empty()) {
        text += slime::ui::t("ui.967e134f508e");
        m_w->resultView->setPlainText(text);
        m_resultTextTotal = 0;
        // v3.6.19：空结果必须把列表/计数/地图标记一并清掉（此前只改了文本，列表会留着旧行）
        if (m_w->resultTable) m_w->resultTable->setRowCount(0);
        if (m_resultCountLabel) m_resultCountLabel->setText(slime::ui::t("ui.a535edb5149b"));
        if (m_map) {
            m_map->setResults({});
            m_map->setAfkCandidates({});
            m_map->setFocusedAfk(-1);
        }
        if (m_w->infoStats) {
            {
                // v3.10.4（用户要求：GPU 只保留速度显示）：无结果时也给平均速度
                const double avgCps = totalS > 1e-9 ? static_cast<double>(out.stats.scannedChunks) / totalS : 0.0;
                QString txt = slime::ui::t("ui.6fce8eadb15e")
                                  .arg(out.stats.scannedChunks)
                                  .arg(totalS, 0, 'f', 3);
                if (avgCps > 0.0) txt += slime::ui::t("ui.1b1b7e5468a9") + rateText(avgCps, m_settings.rateUnit);
                m_w->infoStats->setText(txt);
            }
        }
        return;
    }

    if (out.stoppedEarly) {
        text += slime::ui::t("ui.f2d64e212362")
                    .arg(out.results.size()).arg(out.results.size());
    } else {
        text += slime::ui::t("ui.1bcc6f2367f2")
                    .arg(out.totalFound).arg(out.results.size());
    }
    text += slime::ui::t("ui.088bf658860a");

    const size_t shown = std::min<size_t>(out.results.size(), static_cast<size_t>(kResultDisplayCap));
    int idx = 1;
    for (size_t i = 0; i < shown; ++i) {
        const MatchResult& r = out.results[i];
        const double cx = (static_cast<double>(r.minX) + r.maxX) / 2.0;
        const double cz = (static_cast<double>(r.minZ) + r.maxZ) / 2.0;
        const double dist = std::hypot(cx - static_cast<double>(info.startX), cz - static_cast<double>(info.startZ));
        text += slime::ui::t("ui.2fcd12613ecd").arg(idx).arg(dist, 0, 'f', 2);
        text += QStringLiteral("  chunkX(%1~%2) chunkZ(%3~%4)")
                    .arg(r.minX).arg(r.maxX).arg(r.minZ).arg(r.maxZ);
        text += slime::ui::t("ui.f773d8edbfc4")
                    .arg(static_cast<qlonglong>(r.minX) * 16)
                    .arg((static_cast<qlonglong>(r.maxX) + 1) * 16 - 1)
                    .arg(static_cast<qlonglong>(r.minZ) * 16)
                    .arg((static_cast<qlonglong>(r.maxZ) + 1) * 16 - 1);
        if (info.mode == Mode::Density) {
            text += slime::ui::t("ui.415d894a9fa6").arg(r.density * 100.0, 0, 'f', 1);
        }
        // v3.3.8.1（任务书 3.6）：显示行不再附 /tp 命令（「复制结果」「复制全部命令」路径不受影响）
        text += QLatin1Char('\n');
        ++idx;
    }
    if (out.results.size() > static_cast<size_t>(kResultDisplayCap)) {
        text += slime::ui::t("ui.d1dfa3adee72")
                    .arg(out.results.size()).arg(kResultDisplayCap);
    }
    m_w->resultView->setPlainText(text);
    m_resultTextTotal = static_cast<int64_t>(shown);
    fillResultTable(); // v3.5.5：同步到结果列表
    {
        // v3.10.4（用户要求：GPU 只保留速度显示）：结果统计追加平均速度
        const double avgCps = totalS > 1e-9 ? static_cast<double>(out.stats.scannedChunks) / totalS : 0.0;
        QString txt = slime::ui::t("ui.c9f1416974ec")
                          .arg(out.results.size())
                          .arg(shown)
                          .arg(out.stats.scannedChunks)
                          .arg(totalS, 0, 'f', 3)
                          .arg(info.mode == Mode::Strict ? slime::ui::tr(slime::ui::t("ui.b977e67d75f8"))
                                                         : algorithmDisplayName(info.algorithm))
                          .arg(out.ignoredThreshold
                                   ? slime::ui::t("ui.1ba425cc935b")
                                   : QString());
        if (avgCps > 0.0) txt += slime::ui::t("ui.1b1b7e5468a9") + rateText(avgCps, m_settings.rateUnit);
        m_w->infoStats->setText(txt);
    }
}

// v3.3.3（任务书 4.2）：结果排序（只重排显示文本，不改变结果集合与「复制结果」内容）
void MainWindow::applyResultSort()
{
    if (!m_hasLastOutcome || !m_w) return;
    const QString key = (m_runMode == slime::Mode::Density) ? QStringLiteral("density") : QStringLiteral("distance");
    if (key.isEmpty() || key == QLatin1String("distance")) return; // 引擎默认即按距离（近→远）

    // 按密度（高→低）重排：复制一份结果集排序后重新生成文本，
    // 但 m_lastResultText /「复制结果」始终保持引擎原始（距离）顺序。
    SearchOutcome sorted = m_lastOutcome;
    std::stable_sort(sorted.results.begin(), sorted.results.end(),
                     [](const MatchResult& a, const MatchResult& b) { return a.density > b.density; });
    populateResultText(sorted, m_lastInfo);
    setStatus(slime::ui::tr(slime::ui::t("ui.1c58ffbc4881")).arg(sorted.results.size()));
}

// v3.3.4：结果视图右键菜单（复制选中 / 复制该条命令 / 复制全部命令 / 清空结果）
void MainWindow::showResultContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    QAction* aCopySel = menu.addAction(slime::ui::t("ui.4584c49b2881"));
    QAction* aCopyLineCmd = menu.addAction(slime::ui::t("ui.c9292f786187"));
    QAction* aCopyAllCmd = menu.addAction(slime::ui::t("ui.b55b79e1c9e9"));
    menu.addSeparator();
    QAction* aCopyAll = menu.addAction(slime::ui::t("ui.07bc78ac5aef"));
    QAction* aClear = menu.addAction(slime::ui::t("ui.0fe9f67f7f41"));
    QAction* chosen = menu.exec(m_w->resultView->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == aCopySel) {
        m_w->resultView->copy();
        setStatus(slime::ui::tr(slime::ui::t("ui.6bf48eb281e9")));
    } else if (chosen == aCopyLineCmd) {
        const QString line = m_w->resultView->textCursor().block().text();
        // v3.3.8.1（任务书 3.6）：显示行不再带 /tp 命令 ⇒ 改为从行内区块坐标推算命令
        static const QRegularExpression reChunk(
            QStringLiteral("chunkX\\((-?\\d+)~(-?\\d+)\\)\\s+chunkZ\\((-?\\d+)~(-?\\d+)\\)"));
        const QRegularExpressionMatch m = reChunk.match(line);
        QString cmd;
        if (m.hasMatch()) {
            const qlonglong x = (m.captured(1).toLongLong() + m.captured(2).toLongLong() + 1) * 8;
            const qlonglong z = (m.captured(3).toLongLong() + m.captured(4).toLongLong() + 1) * 8;
            cmd = QStringLiteral("/tp @s %1 64 %2").arg(x).arg(z);
        }
        if (cmd.isEmpty()) {
            setStatus(slime::ui::tr(slime::ui::t("ui.fa8bb54f3fbe")));
        } else {
            QApplication::clipboard()->setText(cmd);
            setStatus(slime::ui::tr(slime::ui::t("ui.0ed61f8d953a")).arg(cmd));
        }
    } else if (chosen == aCopyAllCmd) {
        copyAllCommands();
    } else if (chosen == aCopyAll) {
        copyResults();
    } else if (chosen == aClear) {
        clearResults();
    }
}

// v3.3.4（任务书 1.2）：编辑 → 复制全部命令
void MainWindow::copyAllCommands()
{
    if (!m_hasLastOutcome || m_lastOutcome.results.empty()) {
        setStatus(slime::ui::tr(slime::ui::t("ui.40866ce4c457")));
        return;
    }
    QStringList cmds;
    const size_t shown = std::min<size_t>(m_lastOutcome.results.size(), kMaxCards);
    for (size_t i = 0; i < shown; ++i) cmds << cacheCommandFor(m_lastOutcome.results[i]);
    QApplication::clipboard()->setText(cmds.join(QLatin1Char('\n')));
    setStatus(slime::ui::tr(slime::ui::t("ui.aff1069e95b5")).arg(cmds.size()));
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.0b7f489d26c3")).arg(cmds.size()));
}

// v3.3.4（任务书 1.2）：编辑 → 清空结果（Ctrl+L）
void MainWindow::clearResults()
{
    if (m_w && m_w->resultView) m_w->resultView->clear();
    m_lastResultText.clear();
    m_hasLastOutcome = false;
    m_resultTextTotal = 0;
    if (m_w && m_w->infoStats) m_w->infoStats->setText(slime::ui::t("ui.6e6523a7593c"));
    setStatus(slime::ui::tr(slime::ui::t("ui.ee1775e4f8d0")));
}

// v3.3.4（任务书 1.2）：文件 → 导出结果（Ctrl+S）
void MainWindow::exportResultsToFile()
{
    if (m_lastResultText.isEmpty()) {
        if (!m_uiQuiet) QMessageBox::information(this, slime::ui::t("ui.d733ad3e850d"), slime::ui::t("ui.72b97623bbfa"));
        return;
    }
    const QString def = QDir(QString::fromStdWString(appDirPath().wstring()))
                            .filePath(QStringLiteral("slime_results.txt"));
    const QString path = QFileDialog::getSaveFileName(this, slime::ui::t("ui.d733ad3e850d"), def,
                                                      slime::ui::t("ui.227f93d5f60d"));
    if (path.isEmpty()) return;
    std::ofstream out(std::filesystem::path(path.toStdWString()), std::ios::binary | std::ios::trunc);
    if (!out) {
        addError(slime::ui::t("ui.cfcd6b5bf187").arg(path));
        return;
    }
    const QByteArray utf8 = m_lastResultText.toUtf8();
    out.write(utf8.constData(), utf8.size());
    out.close();
    setStatus(slime::ui::tr(slime::ui::t("ui.6caccae3bf9c")).arg(path));
    addFeedback(3, slime::ui::tr(slime::ui::t("ui.10d76115f1dc")).arg(path));
    slime::LogBus::info("结果已导出: " + path.toStdString());
}

// ---------------- 性能日志（v3.1.0：logs/performance.log） ----------------
void MainWindow::writePerformanceLog(const SearchOutcome& out, const RunInfo& info) const
{
    (void)info;
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t tt = clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);

    double memMB = 0.0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        memMB = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
#endif
    const double totalS = out.stats.totalMs / 1000.0;
    const double cps = totalS > 0 ? out.stats.scannedChunks / totalS : 0.0;

    // 开始/结束时刻：以本次搜索实际启动时刻为基准
    int64_t startMs = m_searchStartMs;
    if (startMs <= 0) {
        startMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }
    const int64_t endMs = startMs + static_cast<int64_t>(out.stats.totalMs);
    const auto fmtClock = [](int64_t ms) {
        const std::time_t s = static_cast<std::time_t>(ms / 1000);
        std::tm t2{};
#ifdef _WIN32
        localtime_s(&t2, &s);
#else
        localtime_r(&s, &t2);
#endif
        char b[32];
        std::snprintf(b, sizeof(b), "%02d:%02d:%02d", t2.tm_hour, t2.tm_min, t2.tm_sec);
        return std::string(b);
    };

    std::error_code ec;
    std::filesystem::create_directories(appDirPath() / "logs", ec);
    std::ofstream log(appDirPath() / "logs" / "performance.log", std::ios::app);
    if (log) {
        // v3.3.0（4.3）：模式 / 算法参数 / 剪枝跳过率 / 与精确模式差异
        const double skipRate = (out.stats.windowsEvaluated + out.windowsSkipped) > 0
                                    ? static_cast<double>(out.windowsSkipped) /
                                          static_cast<double>(out.stats.windowsEvaluated + out.windowsSkipped)
                                    : 0.0;
        std::string modeText = (info.mode == Mode::Strict) ? "严格图案" : "密度";
        // v3.3.4：同时写出中文显示名与机器可读键（如 快速估算模式(sample)），便于脚本解析
        std::string algText = (info.mode == Mode::Strict)
                                  ? std::string("exact")
                                  : (algorithmDisplayName(info.algorithm).toStdString() + "(" +
                                     std::string(info.algorithm == Algorithm::Layered  ? "layered"
                                                 : info.algorithm == Algorithm::Sample ? "sample"
                                                 : info.algorithm == Algorithm::Brute  ? "brute"
                                                                                       : "exact") +
                                     ")");
        log << "日期 " << buf << " | 开始 " << fmtClock(startMs) << " | 结束 " << fmtClock(endMs) << " | 耗时 "
            << static_cast<long long>(out.stats.totalMs) << " ms | 扫描区块 " << out.stats.scannedChunks
            << " | 匹配结果 " << out.totalFound << " | 平均速度 " << static_cast<long long>(cps)
            << " 区块/秒 | 内存 " << static_cast<long long>(memMB + 0.5) << " MB"
            << " | 模式 " << modeText << " | 算法 " << algText;
        if (info.mode == Mode::Density) {
            log << " | 参数 网格=" << info.gridSizeUsed << " 阈值=" << info.minDensityUsed;
            if (info.algorithm == Algorithm::Layered) {
                log << " 分层级别=" << info.layerBlock << " 阈值比例=" << info.layerRatio;
            } else if (info.algorithm == Algorithm::Sample) {
                log << " 采样间隔=" << info.sampleStep << " 精算采样=" << (info.sampleExact ? 1 : 0);
            }
            log << " | 剪枝跳过率 " << static_cast<int>(skipRate * 1000.0 + 0.5) / 10.0 << "%"
                << "（跳过 " << out.windowsSkipped << " / 评估 " << out.stats.windowsEvaluated << "）";
            if (info.algorithm != Algorithm::Exact) {
                int64_t exactFound = -1;
                if (crossCheckExact(info.options, out.totalFound, &exactFound) && exactFound >= 0) {
                    const qlonglong diff = static_cast<qlonglong>(out.totalFound) - exactFound;
                    log << " | 与精确模式差异 " << (diff >= 0 ? "+" : "") << diff << "（精确 " << exactFound
                        << " / 本次 " << out.totalFound << "，同范围对照）";
                } else {
                    log << " | 与精确模式差异 未校验（范围过大或非密度模式）";
                }
            } else {
                log << " | 与精确模式差异 0（精确模式）";
            }
        }
        log << "\n";
    }
}

// ---------------- 其他 ----------------
void MainWindow::copyResults()
{
    if (m_lastResultText.isEmpty()) return;
    QApplication::clipboard()->setText(m_lastResultText);
    setStatus(slime::ui::tr(slime::ui::t("ui.a48bb5b20557")));
}


