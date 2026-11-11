// v3.12.0：配置/GPU 探测/主题/菜单与工具栏等基础逻辑。

int MainWindow::versionBound() const noexcept
{
    return m_w->bedrockRadio->isChecked() ? kBedrockBoundChunk : kJavaBoundChunk;
}

int64_t MainWindow::cacheMaxBytes() const
{
    // config.json: { "cache": { "maxSizeMB": 1024 } }
    int64_t mb = 1024;
    std::ifstream in(appDirPath() / kConfigFileName);
    if (in) {
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto pos = content.find("maxSizeMB");
        if (pos != std::string::npos) {
            const auto colon = content.find(':', pos);
            if (colon != std::string::npos) {
                try {
                    size_t end = 0;
                    const long long v = std::stoll(content.substr(colon + 1), &end);
                    if (v > 0 && v <= 1 << 20) mb = v; // 1MB ~ 1TB 合理范围
                } catch (...) {
                }
            }
        }
    }
    return mb * 1024 * 1024;
}

// ---------------- v3.2.1：GPU 开关与可用性探测 ----------------

// config.json gpu.enabled（缺省 false；环境变量 SLIME_GPU=0/1 可强制覆盖，仅供排障）
bool MainWindow::gpuEnabledFromConfig() const
{
    const char* env = std::getenv("SLIME_GPU");
    if (env) {
        if (std::string(env) == "0") return false;
        if (std::string(env) == "1") return true;
    }
    bool enabled = false; // v3.2.1：默认关闭
    // v3.3.0：改为精确取值 —— 旧写法“取 enabled 之后整段文本里是否含 1/true”会被
    // algorithm 块中的数字（layerBlock 等）误判为启用。
    enabled = cfgBool(appDirPath() / kConfigFileName, "gpu.enabled", false);
    return enabled;
}

void MainWindow::saveGpuEnabledToConfig(bool enabled)
{
    writeConfigFile(enabled, m_terminal ? m_terminal->history() : QStringList());
}

// v3.3.0：统一 config.json 写回（version / search / cache / gpu / log / algorithm / terminal 整体覆盖）。
// 注意：先读取需保留的值，再截断写入（否则会读到自己清空后的文件）。
// ---------------- v3.4.12：config.json 写入的 JSON 转义 ----------------
// 为什么需要：本项目自写的 config.json 是「手工拼接」出来的，字符串值原先原样写入。
//   * 种子 / 缓存目录 / 日志子目录 里的 " 与反斜杠会让文件变成非法 JSON（外部解析器读不回来）；
//   * 某些 OpenCL 驱动返回的设备名不是 UTF-8（本机实测被写成 GBK 乱码），
//     会让整个文件不再是合法 UTF-8。
// 这里统一转义：引号 / 反斜杠 / 控制字符按 JSON 规则转义，非法 UTF-8 字节替换为 U+FFFD。
static std::string jsonEscapeUtf8(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"') {
            out += "\\\"";
            ++i;
            continue;
        }
        if (c == '\\') {
            out += "\\\\";
            ++i;
            continue;
        }
        if (c < 0x20) {
            char buf[8] = {0};
            std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
            out += buf;
            ++i;
            continue;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
            ++i;
            continue;
        }
        int n = 0;
        if ((c & 0xE0) == 0xC0) n = 2;
        else if ((c & 0xF0) == 0xE0) n = 3;
        else if ((c & 0xF8) == 0xF0) n = 4;
        bool ok = (n > 0) && (i + static_cast<size_t>(n) <= s.size());
        for (int k = 1; ok && k < n; ++k) {
            ok = ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0xC0) == 0x80);
        }
        if (ok) {
            out.append(s, i, static_cast<size_t>(n));
            i += static_cast<size_t>(n);
        } else {
            out += "\xEF\xBF\xBD"; // U+FFFD（替换非法字节，保证文件仍是合法 UTF-8）
            ++i;
        }
    }
    return out;
}

static std::string jsonEscapeUtf8(const QString& s)
{
    return jsonEscapeUtf8(s.toUtf8().toStdString());
}

// v3.3.4（任务书 4.1/4.3/2.3/2.4）：新增 search.seed / search.targetCount（默认空 / 1）、
// log.mode（默认 off，需用户主动开启）、terminal.history（最多 100 条）。
void MainWindow::writeConfigFile(bool gpuEnabled, const QStringList& terminalHistory)
{
    const auto path = appDirPath() / kConfigFileName;
    const int cacheMB = cfgInt(path, "cache.maxSizeMB", 1024);
    const char* mode = "exact";
    if (m_w && m_w->algLayeredRadio && m_w->algLayeredRadio->isChecked()) mode = "layered";
    else if (m_w && m_w->algSampleRadio && m_w->algSampleRadio->isChecked()) mode = "sample";
    else if (m_w && m_w->algBruteRadio && m_w->algBruteRadio->isChecked()) mode = "brute";
    const int layerBlock = (m_w && m_w->layerBlock) ? m_w->layerBlock->currentData().toInt() : 16;
    const double layerRatio = (m_w && m_w->layerRatio) ? m_w->layerRatio->value() : 1.0;
    const int sampleStep = (m_w && m_w->sampleStep) ? m_w->sampleStep->value() : 8;
    const bool sampleExact = (m_w && m_w->sampleExact) && m_w->sampleExact->isChecked();
    const QString seed = (m_w && m_w->seedEdit) ? m_w->seedEdit->text().trimmed() : QString();
    const int targetCount = (m_w && m_w->targetCount) ? m_w->targetCount->value() : 1;
    const char* logModeName = "off";
    if (slime::LogBus::mode() == slime::LogMode::Brief) logModeName = "brief";
    else if (slime::LogBus::mode() == slime::LogMode::Detailed) logModeName = "detailed";

    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    out << "{\n"
        << "  \"version\": \"" << kAppVersion << "\",\n"
        << "  \"search\": {\n"
        << "    \"seed\": \"" << jsonEscapeUtf8(seed) << "\",\n"
        << "    \"targetCount\": " << targetCount << "\n  },\n"
        << "  \"cache\": {\n    \"maxSizeMB\": " << cacheMB << "\n  },\n"
        << "  \"gpu\": {\n    \"enabled\": " << (gpuEnabled ? "true" : "false") << "\n  },\n"
        << "  \"log\": {\n    \"mode\": \"" << logModeName << "\"\n  },\n"
        << "  \"algorithm\": {\n"
        << "    \"mode\": \"" << mode << "\",\n"
        << "    \"layerBlock\": " << layerBlock << ",\n"
        << "    \"layerRatio\": " << layerRatio << ",\n"
        << "    \"sampleStep\": " << sampleStep << ",\n"
        << "    \"sampleExact\": " << (sampleExact ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"terminal\": {\n    \"history\": [";
    for (int i = 0; i < terminalHistory.size(); ++i) {
        out << (i ? ", " : "") << "\"" << jsonEscapeUtf8(terminalHistory[i]) << "\"";
    }
    out << "]\n  },\n";
    // v3.3.5（优化 5）：程序设置（与主界面共享的默认值，双向同步）
    out << "  \"settings\": {\n"
        << "    \"wheelAdjust\": " << (m_settings.wheelAdjust ? "true" : "false") << ",\n"
        << "    \"rememberWindow\": " << (m_settings.rememberWindow ? "true" : "false") << ",\n"
        << "    \"autoRestart\": " << (m_settings.autoRestart ? "true" : "false") << ",\n"
        << "    \"defaultAlgorithm\": \"" << jsonEscapeUtf8(m_settings.defaultAlgorithm) << "\",\n"
        << "    \"defaultThreads\": " << m_settings.defaultThreads << ",\n"
        << "    \"defaultTargetCount\": " << m_settings.defaultTargetCount << ",\n"
        << "    \"defaultPreset\": \"" << jsonEscapeUtf8(m_settings.defaultPreset) << "\",\n"
        << "    \"defaultGpu\": " << (m_settings.defaultGpu ? "true" : "false") << ",\n"
        << "    \"defaultStopAtTarget\": " << (m_settings.defaultStopAtTarget ? "true" : "false") << ",\n"
        << "    \"autoLoadCache\": " << (m_settings.autoLoadCache ? "true" : "false") << ",\n"
        << "    \"cacheDir\": \"" << jsonEscapeUtf8(m_settings.cacheDir) << "\",\n"
        << "    \"cacheMaxMB\": " << m_settings.cacheMaxMB << ",\n"
        << "    \"logAtStartup\": " << (m_settings.logAtStartup ? "true" : "false") << ",\n"
        << "    \"logLevel\": " << m_settings.logLevel << ",\n"
        << "    \"logSubDir\": \"" << jsonEscapeUtf8(m_settings.logSubDir) << "\",\n"
        << "    \"logKeepDays\": " << m_settings.logKeepDays << ",\n"
        << "    \"logAutoScroll\": " << (m_settings.logAutoScroll ? "true" : "false") << ",\n"
        << "    \"logMaxLines\": " << m_settings.logMaxLines << ",\n"
        << "    \"showGpuReminder\": " << (m_settings.showGpuReminder ? "true" : "false") << ",\n"
        << "    \"largeRangeWarnChunks\": " << m_settings.largeRangeWarnChunks << ",\n"
        << "    \"theme\": \"" << jsonEscapeUtf8(m_settings.theme) << "\",\n"
        << "    \"language\": \"" << jsonEscapeUtf8(m_settings.language) << "\",\n"        << "    \"mapVersion\": " << m_mapJavaVersionId << ",\n"        << "    \"fullscreenMode\": \"" << jsonEscapeUtf8(m_settings.fullscreenMode) << "\",\n"
        << "    \"soundOnComplete\": " << (m_settings.soundOnComplete ? "true" : "false") << ",\n"
        << "    \"soundVolume\": " << m_settings.soundVolume << ",\n"
        << "    \"crashDump\": " << (m_settings.crashDump ? "true" : "false") << ",\n"
        << "    \"debugMode\": " << (m_settings.debugMode ? "true" : "false") << ",\n"
        // v3.3.9（任务书 二）：资源控制
        << "    \"cpuLimitPercent\": " << m_settings.cpuLimitPercent << ",\n"
        << "    \"gpuLimitPercent\": " << m_settings.gpuLimitPercent << ",\n"
        << "    \"gpuPatternStream\": " << (m_settings.gpuPatternStream ? "true" : "false") << ",\n"
        << "    \"gpuResultSelfOptimize\": " << (m_settings.gpuResultSelfOptimize ? "true" : "false") << ",\n"
        << "    \"memLimitMB\": " << m_settings.memLimitMB << ",\n"
        // v3.3.9.1（任务书 2.1）：启动时自动检查 GPU 状态
        << "    \"autoCheckGpuAtStartup\": " << (m_settings.autoCheckGpuAtStartup ? "true" : "false") << ",\n"
        << "    \"updateReminderDays\": " << m_settings.updateReminderDays << ",\n"
        // v3.5.18：高级调试参数
        << "    \"rateUnit\": " << m_settings.rateUnit << ",\n"
        << "    \"ringInner\": " << m_settings.afkRingInner << ",\n"
        << "    \"ringOuter\": " << m_settings.afkRingOuter << ",\n"
        << "    \"ringCoarseStep\": " << m_settings.ringCoarseStep << ",\n"
        << "    \"ringStatBands\": " << m_settings.ringStatBands << "\n  },\n";
    // v3.3.5（Bug 6）：实测基准缓存（含设备名与核心数，硬件变化时自动失效重测）
    // v3.3.6（任务书 Bug 2）：不再写入 crossLo/crossHi（交叉点已彻底移除）
    out << "  \"gpuBench\": {\n"
        << "    \"cpuRate\": " << m_gpuBenchCpuRate << ",\n"
        << "    \"gpuRate\": " << m_gpuBenchGpuRate << ",\n"
        << "    \"device\": \"" << jsonEscapeUtf8(m_gpuScan ? m_gpuScan->deviceName() : std::string())
        << "\",\n"
        << "    \"cores\": " << std::thread::hardware_concurrency() << "\n  },\n";
    // v3.3.5（优化 5）：窗口大小位置记忆
    out << "  \"window\": {\n    \"w\": " << width() << ",\n    \"h\": " << height()
        << ",\n    \"x\": " << x() << ",\n    \"y\": " << y() << "\n  }\n}\n";
    ++m_configWriteCount; // v3.4.11：config.json 实际写入次数（自检断言「合并写」生效）
}

// v3.3.9.1（任务书 2.1）：启动时按设置自动检测 GPU 状态（默认关闭）。
void MainWindow::maybeAutoCheckGpuAtStartup()
{
    if (m_settings.autoCheckGpuAtStartup) {
        Logger::info("启动时自动检测 GPU 状态（设置已开启）…");
        ensureGpuScanOnce();     // 失败/不可用都会写入状态与日志（内部已 try/catch + 回退 CPU）
        updateGpuStatusUi();
        const QString st = m_w && m_w->gpuStatus ? m_w->gpuStatus->text() : QString();
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.6c72f02735ba")).arg(st));
        slime::IncidentLog::note("启动自动检测 GPU：" + st.toStdString());
    } else {
        if (m_w && m_w->gpuStatus) {
            m_w->gpuStatus->setText(slime::ui::t("ui.241ab1842137"));
        }
        Logger::info("启动时未检测 GPU（设置关闭；勾选「启用 GPU 加速」时会即时检测）");
        slime::IncidentLog::note("启动自动检测 GPU：关闭（按设置跳过）");
    }
}

// v3.6.2：GPU 驱动冲突兜底 —— 用标记文件检测「上次是不是死在 GPU 路径上」。
//   进入 GPU 探测/搜索前写标记，正常结束删掉；下次启动还看到标记 ⇒ 上一次多半是驱动崩的，
//   自动关闭 GPU 并提示，避免每次启动都崩一次。
static std::filesystem::path gpuSessionMarkerPath()
{
    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / "gpu_session.flag";
}

// v3.6.4：英文模式下周期性重翻译 —— 界面上很多文本是运行时才设置的
//   （GPU 状态、线程提示、构建预估、对话框正文），一次性 applyLanguage 抓不到。
//   每 500ms 只对英文生效，命中翻译表才改写，开销可忽略。
// v3.6.6（用户要求）：自动重启 —— 起一个新进程再退出当前进程
void MainWindow::restartApplication()
{
    if (m_uiQuiet) return;   // 自检绝不重启
    const QString exe = QCoreApplication::applicationFilePath();
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty()) args.removeFirst();
    if (QProcess::startDetached(exe, args)) {
        std::fprintf(stderr, "[\u91cd\u542f] \u5df2\u542f\u52a8\u65b0\u5b9e\u4f8b\uff0c\u5f53\u524d\u8fdb\u7a0b\u9000\u51fa\n");
        QCoreApplication::quit();
    } else {
        addFeedback(3, slime::ui::tr(slime::ui::t("ui.ed7d8b8345fb")));
    }
}

void MainWindow::startLanguageWatch()
{
    if (m_langTimer) return;
    m_langTimer = new QTimer(this);
    m_langTimer->setInterval(500);
    connect(m_langTimer, &QTimer::timeout, this, [this]() {
        if (!slime::ui::isEnglish()) return;   // v3.6.6：中文模式不巡检
        applyLanguage();
    });
    m_langTimer->start();
}

void MainWindow::checkGpuCrashLoopOnStartup()
{
    std::error_code ec;
    const auto marker = gpuSessionMarkerPath();
    if (!std::filesystem::exists(marker, ec)) return;
    std::filesystem::remove(marker, ec);
    // 上一次没走到"正常结束"：强制关掉 GPU，写进配置
    if (m_settings.defaultGpu || (m_w->gpuCheck && m_w->gpuCheck->isChecked())) {
        m_settings.defaultGpu = false;
        if (m_w->gpuCheck) {
            QSignalBlocker block(m_w->gpuCheck);
            m_w->gpuCheck->setChecked(false);
        }
        writeConfigFile(false, m_terminal ? m_terminal->history() : QStringList());
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.4ff59f2e9677")));
        std::fprintf(stderr, "[gpu] 上次会话未正常结束，已自动禁用 GPU 加速\n");
    }
}

void MainWindow::ensureGpuScanOnce()
{
    // v3.2.1（修订）：探测从“启动后台线程”改为“用户勾选时同步执行一次”。
    // 原因：无/异常 OpenCL 驱动环境下，LoadLibrary/驱动初始化可发生原生崩溃（非 C++ 异常，
    // try/catch 无法捕获）；若在启动阶段自动触碰 OpenCL，会直接导致“启动/搜索即闪退”。
    // 勾选是用户主动行为（默认关闭），此时初始化失败仅记录日志并标记不可用，搜索自动回退 CPU。
    if (m_gpuProbeDone) return; // 已尝试（成功或失败均为最终状态）
    m_gpuProbeDone = true;
    if (m_w && m_w->gpuStatus) m_w->gpuStatus->setText(slime::ui::t("ui.1410899e1131"));
    QApplication::processEvents(QEventLoop::AllEvents, 20); // 让“检测中”先上屏
    Logger::info("GPU 初始化中（用户勾选启用；无驱动将自动回退 CPU）…");
    std::string err;
    try {
        m_gpuScan = slimegpu::createGpuScanAdapter(&err);
    } catch (const std::exception& e) {
        err = std::string("初始化异常：") + e.what();
        m_gpuScan.reset();
    } catch (...) {
        err = "初始化未知异常";
        m_gpuScan.reset();
    }
    m_gpuAvailable = static_cast<bool>(m_gpuScan) && m_gpuScan->available();
    m_gpuProbeReason = err;
    if (m_gpuAvailable) {
        Logger::info("GPU 可用性检测结果：可用（OpenCL 就绪）");
    } else {
        m_gpuScan.reset();
        Logger::info("GPU 可用性检测结果：不可用，将使用纯 CPU 路径" +
                     (err.empty() ? std::string() : "：" + err));
    }
    updateGpuStatusUi();
}

void MainWindow::onGpuCheckChanged(Qt::CheckState state)
{
    if (m_loadingConfig) return;
    const bool on = (state == Qt::Checked);
    saveGpuEnabledToConfig(on);
    Logger::info(on ? "GPU 加速已启用（勾选）" : "GPU 加速已关闭（取消勾选）");
    if (on) {
        ensureGpuScanOnce(); // 勾选时同步检测一次（启动阶段不触碰 OpenCL）
    }
    updateGpuStatusUi();
    // v3.3.6（任务书 Bug 2 + 4.3「显示 GPU 加速提醒」）：勾选时按设置推送「不一定更快」提醒
    if (on && m_settings.showGpuReminder) {
        m_gpuReminderShown = true;
        setStatus(gpuReminderText());
        addFeedback(2, gpuReminderText());
    }
    if (on) startGpuBenchmark(); // v3.3.5（Bug 6）：启用 GPU 时后台测量实测吞吐率
}

// ---------------- v3.3.6（任务书 Bug 2）：GPU 提醒与状态文本 ----------------
//
// 用户决定：**彻底取消交叉点估算相关说法**（静态公式与动态基准给出的结论都不准），
// 因此本版本删除了旧的阈值估算函数与 config 里的 gpuBench.crossLo/crossHi 字段，
// 只在「GPU 开关 tooltip」与「GPU 状态对话框」保留一段统一提醒文案；
// 状态对话框继续显示设备名、显存、计算单元与实测吞吐率（CPU vs GPU）。
QString MainWindow::gpuReminderText() const
{
    return slime::ui::t("ui.2cc41f0950c5");
}

bool MainWindow::darkTheme() const
{
    return slime::ThemeManager::instance().isDark();
}

// v3.3.6（任务书 4.7）：少数必须内联着色的控件随主题刷新，保证深色模式下同样可读
// （内联样式优先级高于全局 QSS，所以这里显式给出两套状态色）
void MainWindow::applyInlineThemeColors()
{
    const bool dark = darkTheme();
    const QString note = dark ? QStringLiteral("#9aa0a6") : QStringLiteral("#767676");
    if (m_w->algSummary) m_w->algSummary->setStyleSheet(QStringLiteral("color:%1;").arg(note));
    if (m_w->infoStats) m_w->infoStats->setStyleSheet(QStringLiteral("color:%1;").arg(note));
    if (m_w->modeWarn) {
        m_w->modeWarn->setStyleSheet(dark
            ? QStringLiteral("background:#3A2F12; color:#FFD479; border:1px solid #6B5A20;"
                             " border-radius:3px; padding:3px;")
            : QStringLiteral("background:#FFF3CD; color:#7A5B00; border:1px solid #E0C97F;"
                             " border-radius:3px; padding:3px;"));
    }
    updateGpuStatusUi(); // GPU 状态文字的颜色同样按主题重算
}

// GPU 状态对话框正文（自检直接断言文本，不弹窗）：设备名 / 显存 / 计算单元 / 实测吞吐 + 提醒
QString MainWindow::gpuStatusText() const
{
    QString text;
    if (!m_gpuProbeDone) {
        text = slime::ui::t("ui.3d68689da69a");
    } else if (!m_gpuAvailable) {
        text = slime::ui::t("ui.8ecc4126994d");
        if (!m_gpuProbeReason.empty()) {
            text += slime::ui::t("ui.4a18a14dd791").arg(QString::fromStdString(m_gpuProbeReason));
        }
    } else {
        QString dev = slime::ui::t("ui.e606485ff76a");
        if (m_gpuScan) {
            const std::string n = m_gpuScan->deviceName();
            if (!n.empty()) dev = QString::fromStdString(n);
        }
        const uint64_t vramMB = m_gpuScan ? (m_gpuScan->globalMemBytes() / (1024 * 1024)) : 0;
        const uint32_t cu = m_gpuScan ? m_gpuScan->computeUnits() : 0;
        text = slime::ui::t("ui.c7d42c1885f2")
                   .arg(dev)
                   .arg(static_cast<qulonglong>(vramMB))
                   .arg(cu);
        if (m_gpuBenchCpuRate > 0.0 && m_gpuBenchGpuRate > 0.0) {
            text += slime::ui::t("ui.698a93bf3e8b")
                        .arg(m_gpuBenchCpuRate / 1e8, 0, 'f', 2)
                        .arg(m_gpuBenchGpuRate / 1e8, 0, 'f', 2);
        } else {
            text += slime::ui::t("ui.41a80df96e75");
        }
    }
    // 任务书 Bug 2：提醒文案保留在状态对话框的说明文字里
    text += QStringLiteral("\n\n") + gpuReminderText();
    return text;
}

void MainWindow::updateGpuStatusUi()
{
    { std::error_code ec; std::filesystem::remove(gpuSessionMarkerPath(), ec); }  // v3.6.2：探测走完 ⇒ 清标记（崩在驱动里才留得下）

    if (!m_w || !m_w->gpuStatus) return;
    const bool checked = m_w->gpuCheck && m_w->gpuCheck->isChecked();
    QString text;
    if (!checked) {
        // v3.3.9.1（任务书 2.1）：未勾选时区分「已自动检测过」与「未检测」
        if (m_gpuProbeDone) {
            text = m_gpuAvailable ? slime::ui::t("ui.fb3cd3586cf9")
                                  : slime::ui::t("ui.3fc283d417f7");
        } else {
            text = slime::ui::t("ui.207ca70ac3f6");
        }
    } else if (!m_gpuProbeDone) {
        text = slime::ui::t("ui.1410899e1131");
    } else if (m_gpuAvailable) {
        text = slime::ui::t("ui.c0ba81d5e5b8");
    } else {
        text = slime::ui::t("ui.b47adc07c963");
    }
    m_w->gpuStatus->setText(text);
    m_w->gpuStatus->setStyleSheet(m_gpuAvailable ? (darkTheme() ? QStringLiteral("color:#7EC97E;")
                                                               : QStringLiteral("color:#2E6B2E;"))
                                                 : (darkTheme() ? QStringLiteral("color:#FF9E9E;")
                                                                : QStringLiteral("color:#B03020;")));
    if (m_w->gpuCheck) {
        // v3.3.6（任务书 Bug 2）：tooltip 只保留提醒文案（不再有阈值提示）
        m_w->gpuCheck->setToolTip(gpuReminderText());
    }
}

// ---------------- UI 构建（v3.3.4 任务书 1：QMainWindow 原生 + Splitter + Tab） ----------------
//
// 结构（严格按任务书 1.1）：
//   QMainWindow
//   ├ QMenuBar / QToolBar / QStatusBar      （全部使用 QMainWindow 原生接口）
//   └ centralWidget → QVBoxLayout
//        └ QSplitter（垂直）
//             ├ QSplitter（水平）
//             │    ├ 左：QWidget → QScrollArea → 6 个 QGroupBox
//             │    └ 右：QWidget → 信息栏 + QTextEdit（结果文本）
//             └ QTabWidget（进度 / 反馈 / 日志 / 命令终端）
//
// 约束：不做折叠、不搬移布局条目（分组控件均在目标分组内直接创建），布局层级 ≤ 8。
#include "MainWindowBuildUi.inc"
void MainWindow::buildMenus()
{
    QMenuBar* bar = menuBar(); // QMainWindow 原生菜单栏

    // ---- 文件(F) ----
    m_menuFile = bar->addMenu(slime::ui::t("ui.a19ce4cf591e"));
    QAction* aImport = m_menuFile->addAction(slime::ui::t("ui.0c909acf4f10"));
    aImport->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    QAction* aExport = m_menuFile->addAction(slime::ui::t("ui.06cee437ec54"));
    aExport->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    connect(aExport, &QAction::triggered, this, &MainWindow::exportResultsToFile);
    m_menuFile->addSeparator();
    QAction* aClearCache = m_menuFile->addAction(slime::ui::t("ui.291952a2abb2"));
    m_menuFile->addSeparator();
    QAction* aQuit = m_menuFile->addAction(slime::ui::t("ui.feecb1e6adec"));
    aQuit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(aQuit, &QAction::triggered, this, &QWidget::close);

    // ---- 编辑(E) ----
    m_menuEdit = bar->addMenu(slime::ui::t("ui.aaa1cc842e93"));
    QAction* aCopyRes = m_menuEdit->addAction(slime::ui::t("ui.0d6cb0c3fd91"));
    aCopyRes->setShortcut(QKeySequence(QStringLiteral("Ctrl+C")));
    connect(aCopyRes, &QAction::triggered, this, &MainWindow::copyResults);
    QAction* aCopyCmds = m_menuEdit->addAction(slime::ui::t("ui.b55b79e1c9e9"));
    connect(aCopyCmds, &QAction::triggered, this, &MainWindow::copyAllCommands);
    QAction* aClearRes = m_menuEdit->addAction(slime::ui::t("ui.0fe9f67f7f41"));
    aClearRes->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));

    // ---- 查看(V) ----
    m_menuView = bar->addMenu(slime::ui::t("ui.05121202d940"));
    // v3.3.5 优化 3：删除「显示设置面板 / 显示结果面板」两项（面板始终可见），
    // 查看菜单只保留「恢复默认布局」。
    QAction* aResetLayout = m_menuView->addAction(slime::ui::t("ui.e003f209ca14"));
    aResetLayout->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(aResetLayout, &QAction::triggered, this, &MainWindow::resetLayout);

    // ---- 工具(T)（「启用日志…」为任务书要求的隐蔽入口）----
    // v3.3.6（任务书 Bug 3）：原「程序设置…」入口已移除 —— 设置改为工具栏上的 [⚙ 设置] 按钮
    m_menuTools = bar->addMenu(slime::ui::t("ui.3b66d0c2fbe9"));
    QAction* aGpu = m_menuTools->addAction(slime::ui::t("ui.e4d209f2e621"));
    connect(aGpu, &QAction::triggered, this, &MainWindow::showGpuStatus);
    QAction* aOpenCacheDir = m_menuTools->addAction(slime::ui::t("ui.50ff9620a061"));
    connect(aOpenCacheDir, &QAction::triggered, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(appDirPath().wstring())));
        setStatus(slime::ui::tr(slime::ui::t("ui.2ad68491dd4b")));
    });
    QAction* aOpenLogDir = m_menuTools->addAction(slime::ui::t("ui.e153b1be0e3e"));
    connect(aOpenLogDir, &QAction::triggered, this, [this]() {
        const auto dir = appDirPath() / "logs";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(dir.wstring())));
        setStatus(slime::ui::tr(slime::ui::t("ui.27af72f30925")));
    });
    // v3.5.2（任务 1）：挂机点（环形覆盖）—— 按 1/16 区块精度给出「站哪里刷怪面积最大」
    QAction* aAfk = m_menuTools->addAction(slime::ui::t("ui.e4469ceb9f33"));
    aAfk->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(aAfk, &QAction::triggered, this, &MainWindow::showAfkDialog);
    // v3.5.2（任务 2）：地图预览
    QAction* aMap = m_menuTools->addAction(slime::ui::t("ui.f9524bdc88f1"));
    aMap->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    connect(aMap, &QAction::triggered, this, [this]() { toggleMapPanel(); });
    QAction* aMapDlg = m_menuTools->addAction(slime::ui::t("ui.4b1166ff90c5"));
    connect(aMapDlg, &QAction::triggered, this, &MainWindow::showMapDialog);
    m_menuTools->addSeparator();
    // v3.3.5 Bug 5：日志菜单项动态切换「启用日志…/关闭日志」
    m_actLogToggle = m_menuTools->addAction(slime::ui::t("ui.80005af43eec"));
    connect(m_actLogToggle, &QAction::triggered, this, &MainWindow::toggleLogging);
    updateLogMenuText();

    // ---- 帮助(H) ----
    m_menuHelp = bar->addMenu(slime::ui::t("ui.15d7a578f420"));
    QAction* aHelp = m_menuHelp->addAction(slime::ui::t("ui.e63e0a783b7d"));
    aHelp->setShortcut(QKeySequence(QStringLiteral("F1")));
    connect(aHelp, &QAction::triggered, this, [this]() { showHelpDialog(this); });
    QAction* aUpdate = m_menuHelp->addAction(slime::ui::t("ui.60ce2c44f1fb"));
    connect(aUpdate, &QAction::triggered, this, &MainWindow::openReleasePage);
    QAction* aPan = m_menuHelp->addAction(slime::ui::t("ui.c9c4521cd9a9"));
    connect(aPan, &QAction::triggered, this, &MainWindow::openPanPage);
    QAction* aHistory = m_menuHelp->addAction(slime::ui::t("ui.5621bcb3e578"));
    connect(aHistory, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/PillerBean/SlimeFinder/wiki/Version-Index")));
    });
    QAction* aContact = m_menuHelp->addAction(slime::ui::t("ui.152d3db75889"));
    connect(aContact, &QAction::triggered, this, &MainWindow::showContact);
    // v3.7.5（用户要求）：鸣谢名单
    QAction* aCredits = m_menuHelp->addAction(slime::ui::t("ui.c5583bc7e151"));
    connect(aCredits, &QAction::triggered, this, &MainWindow::showCredits);
    QAction* aTelemetry = m_menuHelp->addAction(slime::ui::t("ui.6034809d2f3b"));
    connect(aTelemetry, &QAction::triggered, this, &MainWindow::showTelemetryDialog);
    QAction* aAbout = m_menuHelp->addAction(slime::ui::t("ui.bed172efc953"));
    connect(aAbout, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::buildToolBar()
{
    m_toolBar = addToolBar(slime::ui::t("ui.7ab2f460553b")); // QMainWindow 原生工具栏
    m_toolBar->setMovable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // v3.3.6（任务书 Bug 3）：设置入口移到工具栏，位于「开始搜索」左侧，形式为「文字 + 图标」
    // v3.4.2：工具栏不再创建「设置」按钮 —— 设置入口 = 左导航第 3 项（设置页，立即生效）。
    // 注意：旧实现虽然删掉了 addWidget，但仍以 m_toolBar 为父创建按钮 ⇒ QToolBar 会把它
    // 摆在最左上角（用户实测「设置按钮依旧存在」），因此这里连创建一起删除。
    m_w->settings = nullptr;
    // v3.4.1：工具栏不再放「设置」按钮（改由左导航「设置」页进入）

    m_w->start = new QPushButton(slime::ui::t("ui.8fdc065eda03"), m_toolBar);
    m_w->start->setDefault(true);
    m_w->cancel = new QPushButton(slime::ui::t("ui.4d0b4688c787"), m_toolBar);
    m_w->cancel->setEnabled(false);
    m_toolBar->addWidget(m_w->start);
    m_toolBar->addWidget(m_w->cancel);
    m_toolBar->addSeparator();
    QAction* aImport = nullptr; // v3.4.1：导入缓存移出工具栏（见搜索页「缓存管理」卡片与设置页「缓存」）
    QAction* aClear = nullptr; // v3.4.1：清除缓存移出工具栏
    QAction* aHelp = m_toolBar->addAction(slime::ui::t("ui.adf465ebf0e6"));
    connect(aHelp, &QAction::triggered, this, [this]() { showHelpDialog(this); });


    // 更新提醒徽章（任务书 3.3）：仅在超过 30 天时显示
    // v3.3.6（任务书 4.7）：配色由 ThemeManager 的 QSS（#updateBadge）按主题提供
    m_updateBadge = new QLabel(m_toolBar);
    m_updateBadge->setObjectName(QStringLiteral("updateBadge"));
    m_updateBadge->setVisible(false);
    m_updateBadge->setCursor(Qt::PointingHandCursor);
    // v3.5.29：纯文本时 linkActivated 永远不会触发（看似能点其实没用）——改成真链接
    m_updateBadge->setTextFormat(Qt::RichText);
    m_updateBadge->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    m_toolBar->addWidget(m_updateBadge);
    m_toolBar->addSeparator();
    // 把工具栏剩余的宽度让给徽章
    auto* spacer = new QWidget(m_toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBarSpacerAction = m_toolBar->addWidget(spacer);   // v3.5.28：状态面板插在它前面
}

// ============================================================================
// v3.3.4（任务书 3）：更新提醒（本地时间，不联网）
// ============================================================================
QString MainWindow::releasePageUrl() const
{
    return QStringLiteral("https://wwben.lanzoul.com/b01d77eebe");
}

void MainWindow::openReleasePage()
{
    // v3.10.12.10：检查更新改为「GitHub Releases + 镜像加速 + 网盘备用」对话框。
    // 自检模式（m_uiQuiet）下不联网、不弹窗，仅记录状态供自动化断言。
    m_updateBrowserOpened = false;
    if (m_uiQuiet) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.6ec2519b3ed5")));
        setStatus(slime::ui::tr(slime::ui::t("ui.7cdc436f3b39")));
        return;
    }
    UpdateDialog dlg(this);
    dlg.exec();
    m_updateBrowserOpened = dlg.browserOpened();
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.bdfe8381dda7")));
}

// v3.10.12.10：网盘源保留为国内手动备用入口（不做自动更新适配）
void MainWindow::openPanPage()
{
    const QString url = releasePageUrl();
    m_updateBrowserOpened = false;
    if (m_uiQuiet) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.b70943cd2d9c")).arg(url));
        setStatus(slime::ui::tr(slime::ui::t("ui.7182774d682a")));
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(slime::ui::t("ui.c9c4521cd9a9"));
    box.setTextFormat(Qt::PlainText);
    box.setText(slime::ui::t("ui.de497048ef89")
                    .arg(url));
    QPushButton* openBtn = box.addButton(slime::ui::t("ui.3f550f3594a5"), QMessageBox::AcceptRole);
    QPushButton* copyBtn = box.addButton(slime::ui::t("ui.abb22bd95c4a"), QMessageBox::ActionRole);
    box.addButton(slime::ui::t("ui.4d0b4688c787"), QMessageBox::RejectRole);
    box.setDefaultButton(openBtn);
    box.exec();
    if (box.clickedButton() == copyBtn) {
        QApplication::clipboard()->setText(url);
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.de46b29e9f7d")));
        setStatus(slime::ui::tr(slime::ui::t("ui.bbe551e5d698")));
        return;
    }
    if (box.clickedButton() != openBtn) {
        setStatus(slime::ui::tr(slime::ui::t("ui.c5cac2c33c72")));
        return;
    }
    m_updateBrowserOpened = true;
    QDesktopServices::openUrl(QUrl(url));
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.b415f1c02466")));
    setStatus(slime::ui::tr(slime::ui::t("ui.b415f1c02466")));
    slime::LogBus::info("打开网盘下载页: " + url.toStdString());
}

// ============================================================================
// v3.3.5（优化 5）：程序设置（五标签页模态对话框）+ 与主界面共享 config.json
// ============================================================================
#include "MainWindowSettingsUi.inc"
#include "MainWindowRingMap.inc"

