// v3.12.0：设置页/预设/更新提醒等 UI 逻辑。

void MainWindow::loadAppSettings()
{
    const auto path = appDirPath() / kConfigFileName;
    AppSettings s = SettingsDialog::factoryDefaults();
    s.wheelAdjust = cfgBool(path, "settings.wheelAdjust", s.wheelAdjust);
    s.rememberWindow = cfgBool(path, "settings.rememberWindow", s.rememberWindow);
    s.autoRestart = cfgBool(path, "settings.autoRestart", s.autoRestart);
    s.defaultAlgorithm = QString::fromStdString(cfgString(path, "settings.defaultAlgorithm",
                                                          s.defaultAlgorithm.toStdString()));
    s.defaultThreads = cfgInt(path, "settings.defaultThreads", s.defaultThreads);
    s.defaultTargetCount = cfgInt(path, "settings.defaultTargetCount", s.defaultTargetCount);
    s.defaultPreset = QString::fromStdString(cfgString(path, "settings.defaultPreset",
                                                       s.defaultPreset.toStdString()));
    s.defaultGpu = cfgBool(path, "settings.defaultGpu", s.defaultGpu);
    s.defaultStopAtTarget = cfgBool(path, "settings.defaultStopAtTarget", s.defaultStopAtTarget);
    s.autoLoadCache = cfgBool(path, "settings.autoLoadCache", s.autoLoadCache);
    s.cacheDir = QString::fromStdString(cfgString(path, "settings.cacheDir", ""));
    s.cacheMaxMB = cfgInt(path, "settings.cacheMaxMB", s.cacheMaxMB);
    s.logAtStartup = cfgBool(path, "settings.logAtStartup", s.logAtStartup);
    s.logLevel = cfgInt(path, "settings.logLevel", s.logLevel);
    s.logSubDir = QString::fromStdString(cfgString(path, "settings.logSubDir", "logs"));
    s.logKeepDays = cfgInt(path, "settings.logKeepDays", s.logKeepDays);
    // v3.3.6 新增设置项
    s.theme = QString::fromStdString(cfgString(path, "settings.theme", s.theme.toStdString()));
    // v3.5.20：语言 / 全屏方式（此前只写不读 ⇒ 语言设置不被记忆）
    s.language = QString::fromStdString(cfgString(path, "settings.language", s.language.toStdString()));
    m_mapJavaVersionId = cfgInt(path, "settings.mapVersion", m_mapJavaVersionId);
    s.fullscreenMode = QString::fromStdString(cfgString(path, "settings.fullscreenMode", s.fullscreenMode.toStdString()));
    s.soundOnComplete = cfgBool(path, "settings.soundOnComplete", s.soundOnComplete);
    s.soundVolume = cfgInt(path, "settings.soundVolume", s.soundVolume);
    s.showGpuReminder = cfgBool(path, "settings.showGpuReminder", s.showGpuReminder);
    s.largeRangeWarnChunks = cfgInt(path, "settings.largeRangeWarnChunks", s.largeRangeWarnChunks);
    s.logAutoScroll = cfgBool(path, "settings.logAutoScroll", s.logAutoScroll);
    s.logMaxLines = cfgInt(path, "settings.logMaxLines", s.logMaxLines);
    s.crashDump = cfgBool(path, "settings.crashDump", s.crashDump);
    s.debugMode = cfgBool(path, "settings.debugMode", s.debugMode);
    // v3.3.9（任务书 二）：资源控制（CPU 占用率上限 / 内存上限）
    s.cpuLimitPercent = cfgInt(path, "settings.cpuLimitPercent", s.cpuLimitPercent);
    s.gpuLimitPercent = cfgInt(path, "settings.gpuLimitPercent", s.gpuLimitPercent);
    s.gpuPatternStream = cfgBool(path, "settings.gpuPatternStream", s.gpuPatternStream);
    s.gpuResultSelfOptimize = cfgBool(path, "settings.gpuResultSelfOptimize", s.gpuResultSelfOptimize);
    s.afkRingInner = cfgInt(path, "settings.ringInner", s.afkRingInner);
    s.afkRingOuter = cfgInt(path, "settings.ringOuter", s.afkRingOuter);
    s.ringCoarseStep = cfgInt(path, "settings.ringCoarseStep", s.ringCoarseStep);
    s.ringStatBands = cfgInt(path, "settings.ringStatBands", s.ringStatBands);
    s.rateUnit = std::clamp(cfgInt(path, "rateUnit", s.rateUnit), 0, 2);
    s.memLimitMB = cfgInt(path, "settings.memLimitMB", s.memLimitMB);
    s.autoCheckGpuAtStartup = cfgBool(path, "settings.autoCheckGpuAtStartup", s.autoCheckGpuAtStartup);
    // v3.3.9（任务书 二）：资源控制
    s.cpuLimitPercent = cfgInt(path, "settings.cpuLimitPercent", s.cpuLimitPercent);
    s.memLimitMB = cfgInt(path, "settings.memLimitMB", s.memLimitMB);
    s.autoCheckGpuAtStartup = cfgBool(path, "settings.autoCheckGpuAtStartup", s.autoCheckGpuAtStartup);
    s.updateReminderDays = cfgInt(path, "settings.updateReminderDays", s.updateReminderDays);
    m_settings = s;
}

void MainWindow::applyAppSettings(const AppSettings& s)
{
    const AppSettings old = m_settings;
    m_settings = s;
    // 1) 立即生效：滚轮开关（下次事件即生效）、更新提醒天数、日志级别
    if (old.updateReminderDays != s.updateReminderDays) {
        initUpdateReminder();
    }
    // v3.4.11：日志模式改为**按设置幂等收敛**（原来只在设置值变化时才 setMode）——
    // 否则「命令/菜单把 LogBus 改了、但设置值没变」的情况下两边会漂移，
    // 界面（设置页、菜单勾选、日志面板）与实际写盘模式不一致。
    {
        const slime::LogMode want = !s.logAtStartup
                                        ? slime::LogMode::Off
                                        : (s.logLevel == 1 ? slime::LogMode::Detailed
                                                           : slime::LogMode::Brief);
        if (slime::LogBus::mode() != want) slime::LogBus::setMode(want);
        if (m_logPanel) m_logPanel->refreshFromBus();
        updateLogMenuText();
    }
    (void)old;
    // 2) 缓存目录：切换缓存文件位置（若已有缓存则迁移到新目录）
    if (old.cacheDir != s.cacheDir) {
        const std::filesystem::path base = s.cacheDir.isEmpty()
                                               ? appDirPath()
                                               : std::filesystem::path(s.cacheDir.toStdWString());
        const std::filesystem::path newFile = base / kCacheFileName;
        std::error_code ec;
        if (std::filesystem::exists(m_cacheFile, ec) && m_cacheFile != newFile) {
            std::filesystem::create_directories(base, ec);
            std::filesystem::copy_file(m_cacheFile, newFile, std::filesystem::copy_options::overwrite_existing, ec);
        }
        m_cacheFile = newFile;
        updateCacheStatusUi();
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.6533e0806676")).arg(QString::fromStdWString(base.wstring())));
    }
    // 3) v3.3.6 新增项立即生效：主题、提示音、日志面板显示参数、调试模式
    // v3.4.11（用户拍板：全局约束）——**任何设置变更都不重放 QSS，除非主题真的变了**。
    //   旧实现每次 applyAppSettings 都重放整份样式表（Qt 要重新 polish 全部控件），
    //   于是任何滑块拖动都会卡（音量条只是最明显的暴露点）。
    //   主题下拉 / 主题命令走的是「档位真的不同」这条路，因此不会漏换肤。
    if (old.theme != s.theme ||
        slime::ThemeManager::instance().mode() != slime::ThemeManager::modeFromString(s.theme)) {
        applyThemeFromSettings();
    }
    // v3.6.6（用户要求）：有些设置必须重启才生效（日志目录/保留天数/窗口记忆）。
    //   开启「自动重启」时，检测到这类改动就直接重启程序，省得用户手动关掉再开。
    {
        const bool needRestart = (old.logSubDir != s.logSubDir) || (old.logKeepDays != s.logKeepDays) ||
                                 (old.rememberWindow != s.rememberWindow);
        m_restartPending = needRestart;
        if (needRestart && s.autoRestart && !m_uiQuiet) {
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.077be65263b2")));
            QTimer::singleShot(300, this, [this]() { restartApplication(); });
        } else if (needRestart) {
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.60e5b5c99c91")));
        }
    }
    (void)old;
    // v3.5.26（用户反馈「语言切换被错误回退」）：恢复**即时切换** —— 已迁移的文案立刻变，
    //   其余老文案提示重启后彻底生效（不再假装需要重启，避免用户以为设置没保存）。
    if (!m_uiQuiet && old.language != s.language) {   // v3.5.27：自检里不切语言，保证断言稳定
        const QString lang = s.language.trimmed().toLower();
        slime::ui::setLanguage(lang == QLatin1String("en") ? slime::ui::Language::English
                                                           : (lang == QLatin1String("zh-tw") || lang == QLatin1String("tw"))
                                                                 ? slime::ui::Language::Traditional
                                                                 : slime::ui::Language::Chinese);
        retranslateModeUi();
        applyLanguage();
        setStatus(slime::ui::tr(slime::ui::t("ui.1f9090fec01f")));
        std::fprintf(stderr, "[\u8bbe\u7f6e] \u754c\u9762\u8bed\u8a00\u6539\u4e3a %s\uff08\u91cd\u542f\u540e\u751f\u6548\uff09\n",
                     s.language.toUtf8().constData());
    }
    // 全屏方式改了 → 立即按新模式切换（不需要重启）
    if (old.fullscreenMode != s.fullscreenMode) applyFullscreenMode();
    slime::SoundPlayer::setEnabled(s.soundOnComplete);
    if (m_logPanel) {
        m_logPanel->setAutoScroll(s.logAutoScroll);
        m_logPanel->setMaxLines(s.logMaxLines);
    }
    // v3.3.7（P1-6）：崩溃转储开关真正生效（无需重启）
    // v3.4.11：改为无条件调用（幂等）——只在"变化时"调用会因外部状态漂移而漏配
    slime::CrashDump::setEnabled(s.crashDump);
    if (old.crashDump != s.crashDump) {
        slime::IncidentLog::note(std::string("崩溃转储已") + (s.crashDump ? "开启" : "关闭"));
        addFeedback(2, s.crashDump ? slime::ui::t("ui.ffaefd5b8195")
                                   : slime::ui::t("ui.e83c9f99b58b"));
    }
    slime::IncidentLog::setVerbose(s.debugMode);
    if (old.debugMode != s.debugMode) {
        slime::LogBus::info(std::string("调试模式 ") + (s.debugMode ? "已开启" : "已关闭"));
        addFeedback(2, s.debugMode ? slime::ui::t("ui.6cf4e0cdf166")
                                   : slime::ui::t("ui.551cb231385d"));
    }
    // ---- v3.5.23（用户反馈：大部分设置项无法立即生效）----
    //   搜索默认值以前只在启动时套用一次。现在改成「立刻套用，但只覆盖仍是旧默认值的控件」，
    //   这样既立即生效，又不会抹掉用户在当前搜索页手改的值。
    {
        QStringList applied;
        // v3.5.26（用户反馈「选项切换仍然无法立即生效」）：设置页是显式操作 —— 改了就套用到当前搜索页，
        //   不再要求「控件还停在旧默认值」（那个启发式在用户手动改过搜索页后会静默不生效）。
        auto anyWidget = [](QWidget*) { return true; };
        if (old.defaultThreads != s.defaultThreads ) {
            m_w->threads->setValue(s.defaultThreads > 0 ? s.defaultThreads : 0);
            applied << slime::ui::t("ui.1795484c760a");
        }
        if (old.defaultTargetCount != s.defaultTargetCount ) {
            m_w->targetCount->setValue(s.defaultTargetCount > 0 ? s.defaultTargetCount : 1);
            applied << slime::ui::t("ui.41162519d223");
        }
        if (old.defaultStopAtTarget != s.defaultStopAtTarget &&
            true) {
            m_w->stopAtTarget->setChecked(s.defaultStopAtTarget);
            applied << slime::ui::t("ui.787ef54c30cd");
        }
        if (old.defaultAlgorithm != s.defaultAlgorithm) {
            if (s.defaultAlgorithm == QLatin1String("layered") && m_w->algLayeredRadio) m_w->algLayeredRadio->setChecked(true);
            else if (s.defaultAlgorithm == QLatin1String("sample") && m_w->algSampleRadio) m_w->algSampleRadio->setChecked(true);
            else if (s.defaultAlgorithm == QLatin1String("brute") && m_w->algBruteRadio) m_w->algBruteRadio->setChecked(true);
            else if (m_w->algExactRadio) m_w->algExactRadio->setChecked(true);
            applied << slime::ui::t("ui.73ccaa710a1a");
        }
        if (old.defaultPreset != s.defaultPreset && s.defaultPreset != QLatin1String("none")) {
            const QString pv = s.defaultPreset;
            if (pv == QLatin1String("near")) applyPreset(20000);
            else if (pv == QLatin1String("local")) applyPreset(200000);
            else if (pv == QLatin1String("global")) applyPreset(1000000);
            applied << slime::ui::t("ui.427110e95874");
        }
        if (old.defaultGpu != s.defaultGpu && m_w->gpuCheck) {
            m_w->gpuCheck->setChecked(s.defaultGpu);
            applied << slime::ui::t("ui.18b6202383c3");
        }
        if (!applied.isEmpty())
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.525a0adce0d1")).arg(applied.join(QStringLiteral("、"))));
    }

    // v3.10.9：设置页改动同步到搜索页「GPU 设置」展开面板
    if (m_w) {
        if (m_w->gpuStreamCheck) {
            QSignalBlocker b(m_w->gpuStreamCheck);
            m_w->gpuStreamCheck->setChecked(s.gpuPatternStream);
        }
        if (m_w->gpuSelfOptCheck) {
            QSignalBlocker b(m_w->gpuSelfOptCheck);
            m_w->gpuSelfOptCheck->setChecked(s.gpuResultSelfOptimize);
        }
        if (m_w->gpuLimitSpin) {
            QSignalBlocker b(m_w->gpuLimitSpin);
            m_w->gpuLimitSpin->setValue(std::clamp(s.gpuLimitPercent, 10, 100));
        }
        if (m_w->gpuAutoCheck) {
            QSignalBlocker b(m_w->gpuAutoCheck);
            m_w->gpuAutoCheck->setChecked(s.autoCheckGpuAtStartup);
        }
    }
    // 4) 其它项在下次搜索/启动时生效（默认值语义）：**合并写入** config.json
    //    v3.4.11（用户拍板）：这里是「设置改动 → 落盘」的唯一出口；拖动滑块时每 60ms 都同步写整份
    //    文件会卡，因此登记待写、空闲 500ms 或滑块释放时再落盘（flushPendingConfigWrite）。
    scheduleConfigWrite();
    // v3.4.11：程序化改设置（命令 / 恢复默认 / 读配置）后立刻把真实状态收敛到设置页。
    // 否则挂起的「设置自动生效」定时器 60ms 后会用**过期页面值**把刚改的设置改回去
    //（自检实测：主题被改回并多出一次 QSS 重放）。
    syncSettingsPageFromState();
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.c68f92da9c8f")));
    setStatus(slime::ui::tr(slime::ui::t("ui.c5c0ada6fbc6")));
    slime::LogBus::info("程序设置已应用");
}

void MainWindow::showProgramSettings()
{
    // v3.3.6：自检模式只记录「入口被点击」，不弹模态对话框（自动化流程不能被阻塞）
    m_settingsDialogOpened = true;
    if (m_uiQuiet) {
        setStatus(slime::ui::tr(slime::ui::t("ui.94db9de1ffcf")));
        return;
    }
    SettingsDialog dlg(m_settings, this);
    dlg.setValues(m_settings);
    dlg.exec();
    const bool openLogDir = dlg.openLogDirRequested();
    const bool clearCache = dlg.clearCacheRequested();
    if (!dlg.applied()) {
        setStatus(slime::ui::tr(slime::ui::t("ui.d75c517a8e4a")));
        return;
    }
    const AppSettings s = dlg.settings();
    applyAppSettings(s);
    if (openLogDir) {
        const auto dir = appDirPath() / (s.logSubDir.isEmpty() ? "logs" : s.logSubDir.toStdString());
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(dir.wstring())));
    }
    if (clearCache) {
        m_cache->clear();
        std::error_code ec;
        std::filesystem::remove(m_cacheFile, ec);
        std::filesystem::remove(appDirPath() / kCacheLegacyFileName, ec);
        updateCacheStatusUi();
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.71e8b41e4d10")));
    }
    // 清空缓存请求的访问器（在对话框销毁前读取，避免悬垂）
}

// 读取 build_date.txt（与 exe 同目录，ISO 日期）；缺失/非法时回退编译期默认日期，
// 计算距离今天的天数：> 30 天则在工具栏显示更新提醒徽章（点击打开下载页）。
void MainWindow::initUpdateReminder()
{
    const QString defDate = QStringLiteral("2026-09-20"); // 编译期默认（打包脚本会写入真实日期）
    QString dateText = defDate;
    const auto path = appDirPath() / "build_date.txt";
    std::ifstream in(path);
    if (in) {
        std::string line;
        std::getline(in, line);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) {
            const QDate d = QDate::fromString(QString::fromStdString(line).trimmed(), Qt::ISODate);
            if (d.isValid()) {
                dateText = d.toString(Qt::ISODate);
            } else {
                slime::LogBus::warning("build_date.txt 格式非法（应为 ISO 日期，如 2026-09-20），使用默认日期");
            }
        }
    }
    const QDate rel = QDate::fromString(dateText, Qt::ISODate);
    if (!rel.isValid()) {
        slime::LogBus::warning("发布日期无效，跳过更新提醒");
        return;
    }
    m_daysSinceRelease = static_cast<int>(rel.daysTo(QDate::currentDate()));
    if (m_daysSinceRelease > 30) {
        m_updateBadgeVisible = true;
        m_updateBadge->setText(slime::ui::t("ui.30476aaf4b92").arg(m_daysSinceRelease));
        m_updateBadge->setToolTip(slime::ui::t("ui.f953ab12b923").arg(releasePageUrl()));
        m_updateBadge->setVisible(true);
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.d5c607c89b26"))
                           .arg(m_daysSinceRelease)
                           .arg(dateText));
    } else {
        m_updateBadgeVisible = false;
        m_updateBadge->setVisible(false);
    }
    slime::LogBus::info("发布日期 " + dateText.toStdString() + "，已发布 " +
                        std::to_string(m_daysSinceRelease) + " 天" +
                        (m_updateBadgeVisible ? "（显示更新提醒徽章）" : ""));
}

void MainWindow::showAboutDialog()
{
    // v3.7.5：与「联系我们」做差异化 —— 这里只讲「这是什么、怎么算的、什么版本」；
    //   反馈渠道统一放到「联系我们」，避免两处文案几乎一样。
    const bool en = slime::ui::isEnglish();
    const QString text =
        en ? QStringLiteral(
                 "SlimeFinder %1 (C++20 / Qt 6 rebuild)\n\n"
                 "Version: %1\nBuild date file: build_date.txt\nReleased: %2 days ago\n\n"
                 "Slime chunks are derived from the world seed with the same arithmetic as the game "
                 "(Java 48-bit LCG; Bedrock MT19937), verified bit-for-bit against the reference "
                 "implementation. The Bedrock algorithm was reverse-engineered by @protolambda / @jocopa3.\n\n"
                 "Runs offline by default. Optional anonymous usage statistics are OFF unless you enable them in Help -> Anonymous statistics.\n"
                 "For feedback channels see Help -> Contact; for credits see Help -> Credits.\n\n"
                 "Download page: %3")
                 .arg(QLatin1String(kAppVersion))
                 .arg(m_daysSinceRelease)
                 .arg(releasePageUrl())
             : slime::ui::t("ui.833b150c8f74")
                 .arg(QLatin1String(kAppVersion))
                 .arg(m_daysSinceRelease)
                 .arg(releasePageUrl());
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.07efca1cd67e")));
    if (!m_uiQuiet) {
        QMessageBox box(this);
        box.setWindowTitle(slime::ui::t("ui.bed172efc953"));
        box.setTextFormat(Qt::PlainText);
        box.setText(text);
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
    }
}


void MainWindow::resetLayout()
{
    if (m_hSplit) m_hSplit->setSizes(QList<int>() << 380 << 480 << 360);   // v3.10.0：三纵列
    if (m_rightSplit) m_rightSplit->setSizes(QList<int>() << 560 << 240);   // v3.10.1：结果 / 过程
    selectBottomCategory(0); // v3.3.6：恢复默认布局回到「进度」分类
    resize(1280, 860);
    setStatus(slime::ui::tr(slime::ui::t("ui.9e6824a751ec")));
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.cab9d4b12553")));
}

// ============================================================================
// v3.3.4（任务书 2.3）：启用日志（隐蔽入口 + 两级选项）
// ============================================================================
void MainWindow::showEnableLogDialog()
{
    slime::LogMode mode = slime::LogBus::mode();
    if (mode == slime::LogMode::Off) mode = slime::LogMode::Brief;
    if (!::showEnableLogDialog(this, &mode)) return; // 用户取消
    slime::LogBus::setMode(mode);
    saveAlgorithmToConfig(); // 写回 config.json（含 log.mode）
    if (m_logPanel) m_logPanel->refreshFromBus();
    const QString name = QString::fromUtf8(slime::LogBus::modeName(mode));
    addFeedback(3, slime::ui::tr(slime::ui::t("ui.b1533230c471")).arg(name));
    setStatus(slime::ui::tr(slime::ui::t("ui.b85b877657d1")).arg(name));
    slime::LogBus::info("日志模式切换为 " + std::string(slime::LogBus::modeName(mode)));
}

// ============================================================================
// v3.3.4（任务书 7）：UI 截图验证 —— 规定尺寸 / splitter 极限位置 / 四个标签页 / 每个菜单展开
// 由 GUI 自检在 SLIME_SHOT 设置时调用；每张截图前都做布局校验（无横向滚动条）。
// ============================================================================
#include "MainWindowScreenshots.inc"

void MainWindow::adjustMapSquare()
{
    // v3.6.6（用户反馈「每点一次按钮到处跑」）：用户手动拖过分隔条后程序不再抢着改尺寸
    if (m_rightSplitUserMoved) return;
    if (!m_rightSplit || !m_mapPanel) return;
    static bool adjusting = false;
    if (adjusting) return;
    const int totalH = m_rightSplit->height();
    const int totalW = m_rightSplit->width();
    if (totalH < 160 || totalW < 160) return;
    adjusting = true;
    const bool resultVisible = m_w && m_w->resultTable && m_w->resultTable->isVisible();
    const int chrome = 2 * 32;                 // 两个标题行
    const int squareH = (std::min)(totalW - 4, totalH - chrome);
    int mapH = resultVisible ? (std::max)((std::min)(squareH, totalH * 6 / 10), 200)
                             : (std::max)(squareH, 200);
    mapH = (std::min)(mapH, totalH - chrome);
    const int rest = (std::max)(60, totalH - chrome - mapH);
    const QList<int> want = QList<int>() << mapH << rest;
    if (m_rightSplit->sizes() != want) m_rightSplit->setSizes(want);
    adjusting = false;
}

// v3.5.5：结果列表面板折叠
void MainWindow::toggleResultPane()
{
    if (!m_resultPane || !m_w || !m_w->resultTable) return;
    const bool show = !m_w->resultTable->isVisible();
    m_w->resultTable->setVisible(show);
    if (m_resultSpacer) m_resultSpacer->setVisible(!show);
    if (m_resultToggle) m_resultToggle->setArrowType(show ? Qt::DownArrow : Qt::RightArrow);
    m_rightSplitUserMoved = false;
    setStatus(show ? slime::ui::t("ui.7b68fe95d4a2") : slime::ui::t("ui.afdc9c7d9b9b"));
    // v3.6.9：折叠/展开期间把分隔条尺寸钉住，标题行（按钮）原地不动
    {
        const QList<int> keep = m_rightSplit->sizes();
        auto pin = [this, keep]() {
            if (keep.size() >= 2) m_rightSplit->setSizes(keep);
        };
        pin();
        QTimer::singleShot(0, this, pin);
    }

}

// v3.5.5：结果灌进列表（排名/X/Z/距离/密度或覆盖/跳转按钮）；表头点击可排序
void MainWindow::fillResultTable()
{
    if (!m_w || !m_w->resultTable) return;
    auto* t = m_w->resultTable;
    const bool wasSorting = t->isSortingEnabled();
    t->setSortingEnabled(false);
    t->setRowCount(0);
    const auto& results = m_lastOutcome.results;
    const int shown = static_cast<int>((std::min)(results.size(), static_cast<size_t>(kResultDisplayCap)));
    t->setRowCount(shown);
    const bool ring = !m_lastAfk.empty();
    t->setColumnHidden(5, !ring);   // v3.5.9：环形模式才显示「有效格 · 总权重」
    // v3.10.3（用户要求）：极窄结果列的自适应 —— 压缩列宽、按视口宽度隐藏次要列、
    //   列宽仍可由用户拖动；视口比内容更窄时出现横向滚动条。
    {
        const int vw = t->viewport() ? t->viewport()->width() : 0;
        t->setColumnWidth(0, 36);
        t->setColumnWidth(1, 52);
        t->setColumnWidth(2, 52);
        t->setColumnWidth(3, 48);
        t->setColumnWidth(4, 52);
        t->setColumnHidden(3, vw > 0 && vw < 300);   // 距离列：窄列先折叠
        t->setColumnHidden(4, vw > 0 && vw < 200);   // 密度列：更窄才折叠
        t->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        t->horizontalHeader()->setMinimumSectionSize(32);
        t->horizontalHeader()->setStretchLastSection(true);
    }
    for (int i = 0; i < shown; ++i) {
        const auto& r = results[static_cast<size_t>(i)];
        int blockX = (r.minX + r.maxX + 1) * 8;
        int blockZ = (r.minZ + r.maxZ + 1) * 8;
        double value = r.density * 100.0;
        if (ring && i < static_cast<int>(m_lastAfk.size())) {
            blockX = m_lastAfk[static_cast<size_t>(i)].x;
            blockZ = m_lastAfk[static_cast<size_t>(i)].z;
            value = m_lastAfk[static_cast<size_t>(i)].coveragePercent;
        }
        const double dist = std::hypot(static_cast<double>(blockX >> 4) - m_lastInfo.startX,
                                       static_cast<double>(blockZ >> 4) - m_lastInfo.startZ);
        auto* it0 = new QTableWidgetItem();
        it0->setData(Qt::DisplayRole, i + 1);
        auto* it1 = new QTableWidgetItem();
        it1->setData(Qt::DisplayRole, blockX);
        it1->setData(Qt::UserRole, blockX);
        auto* it2 = new QTableWidgetItem();
        it2->setData(Qt::DisplayRole, blockZ);
        it2->setData(Qt::UserRole, blockZ);
        auto* it3 = new QTableWidgetItem();
        it3->setData(Qt::DisplayRole, dist);
        auto* it4 = new QTableWidgetItem();
        it4->setData(Qt::DisplayRole, value);
        t->setItem(i, 0, it0);
        t->setItem(i, 1, it1);
        t->setItem(i, 2, it2);
        t->setItem(i, 3, it3);
        t->setItem(i, 4, it4);
        if (ring && i < static_cast<int>(m_lastAfk.size())) {
            const auto& cd = m_lastAfk[static_cast<size_t>(i)];
            auto* it5 = new QTableWidgetItem();
            it5->setText(slime::ui::t("ui.aca2c2cb22cf")
                             .arg(static_cast<qlonglong>(cd.coveredBlocks))
                             .arg(cd.coveredWeight, 0, 'f', 0));
            t->setItem(i, 5, it5);
        }
        auto* jump = new QPushButton(slime::ui::t("ui.28cd2efd8fa8"), t);
        jump->setAutoDefault(false);
        jump->setToolTip(slime::ui::t("ui.b208126e95ab"));
        const int bx = blockX;
        const int bz = blockZ;
        connect(jump, &QPushButton::clicked, this, [this, i, ring, bx, bz]() {
            if (ring && i < static_cast<int>(m_lastAfk.size())) {
                focusAfkCandidate(i);   // v3.5.10：只显示这一条候选的环
            } else {
                focusMapOnChunk(bx >= 0 ? bx >> 4 : -((-bx + 15) >> 4), bz >= 0 ? bz >> 4 : -((-bz + 15) >> 4));
            }
        });
        t->setCellWidget(i, 6, jump);
    }
    if (m_resultCountLabel) {
        m_resultCountLabel->setText(shown > 0 ? slime::ui::t("ui.1389cca29639")
                                                   .arg(results.size())
                                                   .arg(shown < static_cast<int>(results.size())
                                                            ? slime::ui::t("ui.7d5266ad5822").arg(shown)
                                                            : QString())
                                            : slime::ui::t("ui.a535edb5149b"));
    }
}

// v3.9.12：结果要求冲突裁决 —— 忽略阈值（最优候选）时阈值/早停/智能范围无效；
//   图案模式下阈值与忽略阈值都无效。只改可用性，不改值与结果语义。
void MainWindow::applyResultConflicts()
{
    if (!m_w) return;
    const bool patternMode = m_w->strictRadio && m_w->strictRadio->isChecked();
    const bool ignore = m_w->ignoreThreshold && m_w->ignoreThreshold->isChecked();
    if (m_w->ignoreThreshold) m_w->ignoreThreshold->setEnabled(!patternMode);
    if (m_w->minDensity) m_w->minDensity->setEnabled(!ignore && !patternMode);
    if (m_w->stopAtTarget) {
        m_w->stopAtTarget->setEnabled(!ignore);
        if (ignore) m_w->stopAtTarget->setChecked(false);
    }
    if (m_w->smartRange) {
        m_w->smartRange->setEnabled(!ignore);
        if (ignore) m_w->smartRange->setChecked(false);
    }
    if (m_w->presetHint && ignore && !patternMode) {
        m_w->presetHint->setText(slime::ui::t("ui.ae90086a5180"));
    }
}

// ===================== v3.9.11：结果要求方案（预设） =====================
//   原则：方案只控制**会改变结果**的开关；GPU/线程/缓存等加速项永不参与。
namespace {
struct PresetDef {
    const char* label;
    bool stop;
    bool smart;
    bool ignore;   // 最优候选（忽略阈值）
    int algorithm; // 0=精确 1=分层 2=估算 3=保守
};
const PresetDef kPresetDensity[] = {
    // v3.10.12.8：实测「找够即停」不勾由近及远更快（CPU 114.5 vs 123.1 ms；GPU 也更快），预设改为仅停止
    { "ui.preset.quick_nearest", true,  false, false, 0 },
    { "ui.preset.all",           false, false, false, 0 },
    { "ui.preset.best_ignore",   false, false, true,  0 },
    { "ui.preset.coarse",        false, false, false, 2 },
};
const PresetDef kPresetPattern[] = {
    // v3.10.10.2：图案模式下「由近及远」会禁掉 AVX2 快路径（20k² 858→93 ms），预设改为仅停止
    { "ui.preset.pattern_nearest", true,  false, false, 0 },
    { "ui.preset.pattern_all",     false, false, false, 0 },
};
} // namespace

void MainWindow::rebuildPresets()
{
    if (!m_w || !m_w->presetCombo) return;
    const bool pattern = m_w->strictRadio && m_w->strictRadio->isChecked();
    m_presetPatternMode = pattern ? 1 : 0;
    m_presetApplying = true;
    m_w->presetCombo->clear();
    if (pattern) {
        for (const auto& pd : kPresetPattern) m_w->presetCombo->addItem(slime::ui::t(pd.label));
    } else {
        for (const auto& pd : kPresetDensity) m_w->presetCombo->addItem(slime::ui::t(pd.label));
    }
    m_w->presetCombo->addItem(slime::ui::t("ui.c493338e8c33"));
    m_w->presetCombo->setCurrentIndex(0);
    m_presetApplying = false;
    applySelectedPreset();
}

void MainWindow::applySelectedPreset()
{
    if (!m_w || !m_w->presetCombo) return;
    const bool pattern = (m_presetPatternMode == 1);
    const int customIdx = pattern ? 2 : 4;
    const int idx = m_w->presetCombo->currentIndex();
    if (idx < 0 || idx > customIdx) return;
    m_presetLastIndex = idx;

    if (idx == customIdx) {
        if (m_w->presetHint) {
            m_w->presetHint->setText(slime::ui::t("ui.7e7cf39ddc4d"));
        }
        updateAdvancedVisibility();   // v3.10.9：自定义方案展开高级开关并显示速度声明
        return;
    }
    const PresetDef& pd = pattern ? kPresetPattern[idx] : kPresetDensity[idx];
    m_presetApplying = true;
    if (m_w->ignoreThreshold) m_w->ignoreThreshold->setChecked(pd.ignore && !pattern);
    if (m_w->stopAtTarget) m_w->stopAtTarget->setChecked(pd.stop);
    if (m_w->smartRange) m_w->smartRange->setChecked(pd.smart);
    switch (pd.algorithm) {
    case 1: if (m_w->algLayeredRadio) m_w->algLayeredRadio->setChecked(true); break;
    case 2: if (m_w->algSampleRadio) m_w->algSampleRadio->setChecked(true); break;
    case 3: if (m_w->algBruteRadio) m_w->algBruteRadio->setChecked(true); break;
    default: if (m_w->algExactRadio) m_w->algExactRadio->setChecked(true); break;
    }
    m_presetApplying = false;
    applyResultConflicts();

    updateAdvancedVisibility();
    if (m_w->presetHint) {
        QString desc;
        if (pattern) {
            desc = slime::ui::t("ui.5c2f010bbc8f");
        } else if (pd.ignore) {
            desc = slime::ui::t("ui.0f05e9ce3b76");
        } else if (pd.algorithm == 2) {
            desc = slime::ui::t("ui.1f8e3c9e917b");
        } else if (pd.stop) {
            desc = slime::ui::t("ui.b69e415cb8e3");
        } else {
            desc = slime::ui::t("ui.07c03132f5fe");
        }
        m_w->presetHint->setText(desc);
    }
}

// v3.10.9（用户要求）：① 自定义方案的速度声明；② 高级开关仅在「自定义」或「环形」时显示
void MainWindow::updatePresetDisclaimer()
{
    if (!m_w || !m_w->presetCombo) return;
    const int customIdx = (m_presetPatternMode == 1) ? 2 : 4;
    const bool custom = m_w->presetCombo->currentIndex() == customIdx;
    if (m_w->presetDisclaimer) m_w->presetDisclaimer->setVisible(custom);
    if (custom && !m_customDisclaimerLogged) {
        m_customDisclaimerLogged = true;
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.75418b9be246")));
    }
}

void MainWindow::updateAdvancedVisibility()
{
    if (!m_w) return;
    const int customIdx = (m_presetPatternMode == 1) ? 2 : 4;
    const bool custom = m_w->presetCombo && m_w->presetCombo->currentIndex() == customIdx;
    const bool ring = m_w->shapeRingRadio && m_w->shapeRingRadio->isChecked();
    const bool showAdvanced = custom || ring;
    if (m_w->resultGroup) m_w->resultGroup->setVisible(showAdvanced);
    if (m_w->matchGroup) m_w->matchGroup->setVisible(showAdvanced && !ring);
    updatePresetDisclaimer();
}

void MainWindow::markPresetCustom(const QString& reason)
{
    if (m_presetApplying || !m_w || !m_w->presetCombo) return;
    const bool pattern = (m_presetPatternMode == 1);
    const int customIdx = pattern ? 2 : 4;
    if (m_w->presetCombo->currentIndex() == customIdx) return;
    m_presetApplying = true;
    m_w->presetCombo->setCurrentIndex(customIdx);
    m_presetApplying = false;
    m_presetLastIndex = customIdx;
    if (m_w->presetHint) {
        m_w->presetHint->setText(slime::ui::t("ui.fed55636b818").arg(reason));
    }
    updateAdvancedVisibility();
}

// v3.5.6：区域形状 = 环形覆盖时，把「正方形/密度搜索」相关控件全部置灰
void MainWindow::updateSearchShapeEnabled()
{
    if (!m_w || !m_w->densityPanel) return;
    // v3.5.18（用户要求）：一级二选一 → 下方只显示能调的选项，不再简单置灰。
    const bool patternMode = m_w->strictRadio && m_w->strictRadio->isChecked();
    const bool ringShape = m_w->shapeRingRadio && m_w->shapeRingRadio->isChecked();

    if (m_w->strictPanel) m_w->strictPanel->setVisible(patternMode);
    if (m_w->densityPanel) m_w->densityPanel->setVisible(!patternMode);
    // v3.9.11：模式决定方案数量与内容（仅在模式真正变化时重建，避免重置用户选择）
    if (m_w->presetCombo && m_presetPatternMode != (patternMode ? 1 : 0)) rebuildPresets();
    applyResultConflicts();
    if (m_w->squareOpts) m_w->squareOpts->setVisible(!patternMode && !ringShape);
    if (m_w->ringOpts) m_w->ringOpts->setVisible(!patternMode && ringShape);
    // v3.6.0（用户反馈：环形模式下方仍有 4 个匹配算法，和环形只有 2 种扫描不同步）
    if (m_w->matchGroup) m_w->matchGroup->setVisible(!ringShape);
    if (m_w->smartRange) m_w->smartRange->setVisible(!ringShape);
    if (m_w->smartRange) m_w->smartRange->setEnabled(!ringShape);
    if (m_w->stopAtTarget) m_w->stopAtTarget->setVisible(!ringShape);
    // 兼容：仍然按模式更新状态提示
    if (ringShape && !patternMode) {
        setStatus(slime::ui::tr(slime::ui::t("ui.9e6da9fa6c38")));
    }
    updateAdvancedVisibility();
}

// v3.5.5：环形覆盖搜索 —— 1 格精度找「半径 128 内覆盖史莱姆刷怪面积最大」的站位

