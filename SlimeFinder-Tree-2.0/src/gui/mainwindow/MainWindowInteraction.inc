// v3.12.0：输入过滤、日志/反馈/命令等交互逻辑。

void MainWindow::installWheelFilter()
{
    const QList<QSpinBox*> spins = findChildren<QSpinBox*>();
    const QList<QDoubleSpinBox*> dspins = findChildren<QDoubleSpinBox*>();
    for (QSpinBox* s : spins) {
        s->setFocusPolicy(Qt::StrongFocus);
        s->installEventFilter(this);
    }
    for (QDoubleSpinBox* s : dspins) {
        s->setFocusPolicy(Qt::StrongFocus);
        s->installEventFilter(this);
    }
    // v3.5.1（交互手感）：下拉框与滑块**永不**吞滚轮 —— 鼠标划过时滚轮只滚页面，避免误改设置
    const QList<QComboBox*> combos = findChildren<QComboBox*>();
    const QList<QSlider*> sliders = findChildren<QSlider*>();
    for (QComboBox* cb : combos) cb->installEventFilter(this);
    for (QSlider* sl : sliders) sl->installEventFilter(this);
    std::fprintf(stderr, "[交互] 滚轮手滑防护：下拉框 %d 个 + 滑块 %d 个（滚轮只滚页面）\n",
                 static_cast<int>(combos.size()), static_cast<int>(sliders.size()));
    std::fprintf(stderr, "[设置] 滚轮过滤器已安装：%d 个 SpinBox + %d 个 DoubleSpinBox（允许滚轮=%d）\n",
                 static_cast<int>(spins.size()), static_cast<int>(dspins.size()),
                 (int)m_settings.wheelAdjust);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* ev)
{
    // v3.5.4：点击结果行 → 地图定位到该结果所在的区块（拖动选择文本时不触发）
    if (m_w && m_w->resultView && watched == m_w->resultView) {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) m_resultClickPos = me->pos();
        } else if (ev->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton &&
                (me->pos() - m_resultClickPos).manhattanLength() <= 4) {
                const QTextCursor cur = m_w->resultView->cursorForPosition(me->pos());
                const QString line = cur.block().text();
                static const QRegularExpression re(
                    QStringLiteral("chunkX\\((-?\\d+)~(-?\\d+)\\)\\s+chunkZ\\((-?\\d+)~(-?\\d+)\\)"));
                const auto m = re.match(line);
                if (m.hasMatch()) {
                    const int cx = m.captured(1).toInt() + (m.captured(2).toInt() - m.captured(1).toInt()) / 2;
                    const int cz = m.captured(3).toInt() + (m.captured(4).toInt() - m.captured(3).toInt()) / 2;
                    focusMapOnChunk(cx, cz);
                }
            }
        }
    }
    // v3.5.1：双击中间分隔条 = 恢复默认比例（只改尺寸，不改任何视觉样式）
    if (ev->type() == QEvent::MouseButtonDblClick) {
        if (m_hSplit && m_hSplit->count() > 1 && watched == m_hSplit->handle(1)) {
            const int w = m_hSplit->width();
            const int left = 380;
            const int right = 360;
            m_hSplit->setSizes(QList<int>{ left, (std::max)(360, w - left - right), right });
            setStatus(slime::ui::tr(slime::ui::t("ui.5a6665d65ea8")));
            return true;
        }
        if (m_rightSplit && m_rightSplit->count() > 1 && watched == m_rightSplit->handle(1)) {
            // v3.10.1：右列 = 结果 / 过程；双击恢复默认（过程 240）
            const int hgt = m_rightSplit->height();
            const int bottom = (std::min)(400, (std::max)(200, hgt * 2 / 5));
            m_rightSplit->setSizes(QList<int>{ (std::max)(240, hgt - bottom), bottom });
            setStatus(slime::ui::tr(slime::ui::t("ui.f245741059aa")));
            return true;
        }
    }
    if (ev->type() == QEvent::Wheel) {
        // v3.5.1：下拉框/滑块不吞滚轮（无论是否聚焦）—— 滚轮交给外层滚动区，值只能点开选/拖动改
        if (qobject_cast<QComboBox*>(watched) || qobject_cast<QSlider*>(watched)) {
            forwardWheelToScrollArea(qobject_cast<QWidget*>(watched), static_cast<QWheelEvent*>(ev));
            ev->accept();
            return true;
        }
        auto* spin = qobject_cast<QAbstractSpinBox*>(watched);
        if (spin) {
            auto* we = static_cast<QWheelEvent*>(ev);
            const int dy = we->angleDelta().y();
            if (!m_settings.wheelAdjust) {
                // 设置关闭：数值不变，且**不吞掉滚轮** —— 转交给外层滚动区，页面照常上下滚
                forwardWheelToScrollArea(spin, static_cast<QWheelEvent*>(ev));
                ev->accept();
                return true;
            }
            // v3.4.8（用户实测：设置开启后滚轮调节失效）：改为**鼠标划过即可调**，不必先点一下获得焦点。
            //   （旧实现要求 hasFocus()，用户"滚轮划过"时永远不满足 ⇒ 看起来完全没反应。）
            if (dy != 0) {
                const int steps = (dy > 0 ? 1 : -1) * (std::max)(1, std::abs(dy) / 120);
                if (auto* sb = qobject_cast<QSpinBox*>(spin)) {
                    sb->setValue(sb->value() + steps * sb->singleStep());
                } else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(spin)) {
                    dsb->setValue(dsb->value() + steps * dsb->singleStep());
                }
            }
            ev->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, ev);
}

// v3.5.1（交互手感）：把滚轮事件转交给外层滚动区（下拉框/滑块/数值框共用）
bool MainWindow::forwardWheelToScrollArea(QWidget* from, QWheelEvent* we)
{
    if (!from || !we) return false;
    QWidget* p = from->parentWidget();
    while (p && !qobject_cast<QAbstractScrollArea*>(p)) p = p->parentWidget();
    if (auto* sa = qobject_cast<QAbstractScrollArea*>(p)) {
        QApplication::sendEvent(sa->viewport(), we);
        return true;
    }
    return false;
}

// v3.5.1（交互手感）：Esc = 取消进行中的搜索/构建（与工具栏「取消」等价，不改变任何视觉）
void MainWindow::keyPressEvent(QKeyEvent* ev)
{
    if (ev && ev->key() == Qt::Key_Escape && ev->modifiers() == Qt::NoModifier) {
        if (m_w && m_w->cancel && m_w->cancel->isEnabled()) {
            cancelSearch();
            ev->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(ev);
}

void MainWindow::highlightSeedField()
{
    if (!m_w || !m_w->seedEdit) return;
    m_w->seedEdit->setFocus(Qt::OtherFocusReason);
    m_w->seedEdit->selectAll();
    auto* timer = new QTimer(this);
    auto* count = new int(0);
    connect(timer, &QTimer::timeout, this, [this, timer, count]() {
        const bool on = (*count % 2) == 0;
        m_w->seedEdit->setStyleSheet(on ? QStringLiteral("background:#FFE4E1; border:2px solid #B03020;")
                                        : QString());
        if (++(*count) >= 6) {
            timer->stop();
            m_w->seedEdit->setStyleSheet(QString());
            timer->deleteLater();
            delete count;
        }
    });
    timer->start(180);
}

// v3.3.4（任务书 2.2）：反馈辅助
void MainWindow::addFeedback(int level, const QString& message)
{
    if (!m_feedback) return;
    const FeedbackLevel lv = static_cast<FeedbackLevel>((std::max)(0, (std::min)(3, level)));
    m_feedback->add(lv, message);
    updateNavBadges(); // v3.3.6（任务书 调整 2）：反馈计数徽章随新条目刷新
}

void MainWindow::addError(const QString& message)
{
    if (!m_feedback) return;
    m_feedback->addSevere(message); // 追加错误 + 发出 severeAdded（主窗口据此切换分类并弹窗）
    updateNavBadges();
    slime::LogBus::error(message.toStdString());
}

// v3.3.4（任务书 2.1）：进度标签页的「已用时 / 预计剩余」刷新
void MainWindow::updateElapsedUi()
{
    if (!m_w || !m_w->progressElapsed || !m_w->progressEta) return;
    if (!m_running) return;
    const double elapsed = m_searchStartMs > 0
                               ? (static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::system_clock::now().time_since_epoch())
                                                          .count() -
                                                      m_searchStartMs) /
                                  1000.0)
                               : 0.0;
    m_w->progressElapsed->setText(slime::ui::t("ui.1bb9e1587450").arg(elapsed, 0, 'f', 1));
    // v3.5.28：工具栏状态面板的阶段文字（搜索中 / 匹配中 / 排序中）
    if (m_w->searchStage) {
        const bool matching = m_stageWasMatching || m_task.livePhase() >= 2;
        m_w->searchStage->setText(matching ? slime::ui::t("ui.8f92264f67fa")
                                           : slime::ui::t("ui.bd9dbc6fc1d9"));
    }
    if (m_etaSec > 0.0 && m_etaSec < 36000.0) {
        const int s = static_cast<int>(m_etaSec + 0.5);
        m_w->progressEta->setText(slime::ui::t("ui.11f69d6c1387").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')));
    } else {
        m_w->progressEta->setText(slime::ui::t("ui.f00785313fdc"));
    }
}

// ============================================================================
// v3.3.6（任务书 调整 2/3/4.7）：底部左侧导航 / 进度流水 / 主题应用
// ============================================================================

// 切换底部面板分类：0 进度 / 1 反馈 / 2 日志 / 3 命令
// 当前项：加粗 + 高亮背景 + ● 前缀；未选中：○ 前缀（样式由 ThemeManager 的 #navList 提供）
void MainWindow::selectBottomCategory(int index)
{
    if (!m_nav || !m_stack) return;
    if (index < 0 || index >= m_nav->count()) return;
    static const char* const kNames[4] = { "ui.tab.progress", "ui.tab.feedback", "ui.tab.log", "ui.tab.command" };
    for (int i = 0; i < m_nav->count(); ++i) {
        QListWidgetItem* it = m_nav->item(i);
        if (!it) continue;
        const bool cur = (i == index);
        QString text = QString::fromUtf8(cur ? "● " : "○ ") + slime::ui::t(kNames[i]);
        if (i == 1 && m_feedback) {
            const QString badge = FeedbackPanel::badgeText(m_feedback->warningCount(),
                                                           m_feedback->errorCount());
            if (!badge.isEmpty()) text += QLatin1Char(' ') + badge; // 仅「反馈」显示计数
        }
        it->setText(text);
        QFont f = it->font();
        f.setBold(cur);
        it->setFont(f);
        it->setBackground(cur ? QBrush(QColor(0, 120, 215, 60)) : QBrush(Qt::NoBrush));
        it->setForeground(cur ? QBrush(QColor(0, 120, 215)) : QBrush(Qt::NoBrush));
    }
    if (m_dockTitle) m_dockTitle->setText(slime::ui::t("ui.cb45d50a89a3").arg(slime::ui::t(kNames[index])));
    for (int i = 0; i < static_cast<int>(m_dockHeadTabs.size()); ++i) {
        if (m_dockHeadTabs[static_cast<size_t>(i)]) m_dockHeadTabs[static_cast<size_t>(i)]->setChecked(i == index);
    }
    m_nav->setCurrentRow(index);
    m_stack->setCurrentIndex(index);
}

// v3.3.6（任务书 调整 2）：计数规则 —— 只统计警告与错误，格式「反馈 ⚠️2❌1」
void MainWindow::updateNavBadges()
{
    if (!m_nav || m_nav->count() < 2) return;
    selectBottomCategory(m_nav->currentRow() < 0 ? 0 : m_nav->currentRow());
}

// v3.3.6（任务书 调整 3）：每 0.5 秒向「进度」分类追加一条流水
void MainWindow::appendProgressStreamLine()
{
    if (!m_progressStream || !m_running) return;
    const double elapsed = m_searchStartMs > 0
                               ? (static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::system_clock::now().time_since_epoch())
                                                          .count() -
                                                      m_searchStartMs) /
                                  1000.0)
                               : 0.0;
    const int64_t found = m_task.liveMatchCount(); // v3.3.7（P0-2）：计数由 SearchTask 持有
    const int64_t scanned = m_task.liveScannedCount(); // v3.3.7（P0-3）：真实扫描计数（与进度条同源）
    // v3.3.9（任务书 1.1/1.4）：缓存构建模式的流水行 —— 四项（已记录 / 百分比 / 速度 / 已花费）+ 阶段区分
    if (m_buildActive) {
        const int64_t recorded = m_buildDoneChunks + scanned;
        const int64_t total = (std::max)(int64_t(1), buildTotalChunks());
        const double cps = elapsed > 0.05 ? static_cast<double>(recorded) / elapsed : 0.0;
        m_progressStream->appendRawLine(
            slime::ui::t("ui.86577d8e48b4")
                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                .arg(m_task.livePhase() == 1 ? slime::ui::t("ui.6347233dd7dd") : slime::ui::t("ui.a8878877a9f5"))
                .arg(recorded)
                .arg(100.0 * static_cast<double>(recorded) / static_cast<double>(total), 0, 'f', 1)
                .arg(cps / 10000.0, 0, 'f', 1)
                .arg(elapsed, 0, 'f', 1)
                .arg(m_buildSkippedTotal + m_task.liveSkippedCoveredCount()));
        m_streamScanned = scanned;
    } else {
        if (scanned > m_streamScanned) m_streamScanned = scanned;
        m_progressStream->appendProgress(elapsed, m_streamScanned, displayTotalChunks(), found); // N/M（xx%）
    }
    // v3.3.7（P1-6/P0-3）：每 10 个节拍（约 5 秒）把状态环写进关键信息日志 —— 崩溃时能还原现场
    // （调试模式下每 2 秒一条）
    const int everyTicks = slime::IncidentLog::verbose() ? 4 : 10;
    if (++m_streamTick >= everyTicks) {
        m_streamTick = 0;
        slime::IncidentLog::note("进度：已用时 " + std::to_string(static_cast<long long>(elapsed)) +
                                 "s 扫描 " + std::to_string(m_streamScanned) + "/" +
                                 std::to_string(displayTotalChunks()) + " 区块 找到 " +
                                 std::to_string(found) + " 个 | " + slime::IncidentLog::memorySummary());
    }
}

// v3.3.6（任务书 4.7）：按设置切换浅色 / 深色 / 跟随系统（运行期立即生效，无需重启）
void MainWindow::applyThemeFromSettings()
{
    const slime::ThemeMode mode = slime::ThemeManager::modeFromString(m_settings.theme);
    slime::ThemeManager::instance().apply(qApp, mode);
    applyInlineThemeColors(); // 少量必须内联的状态色随主题刷新
    slime::LogBus::info(std::string("主题已切换为 ") +
                        slime::ThemeManager::modeToString(mode).toStdString());
}

// ============================================================================
// v3.3.4（任务书 2.4）：命令终端宿主 —— 命令立即执行（可插队到正在运行的搜索）
//
// 设计：命令解析与执行全部在 GUI 线程同步完成，与搜索线程（SearchTask 的 std::thread）
// 完全独立，因此设置类/查询类命令立即生效、cancel 立即中断、不阻塞用户交互。
// 最小命令集：help / clear(终端内置) / version / seed / range / center / preset / mode /
//            search / cancel / status / gpu / target / result / log
// ============================================================================
QStringList MainWindow::commandNames() const
{
    // v3.4.11（用户拍板：扩展命令表）：新增命令均对应真实功能；help/补全都读这一张表
    return { QStringLiteral("about"),  QStringLiteral("build"),   QStringLiteral("cache"),
             QStringLiteral("cancel"), QStringLiteral("center"),  QStringLiteral("clear"),
             QStringLiteral("copy"),   QStringLiteral("density"), QStringLiteral("gpu"),
             QStringLiteral("grid"),   QStringLiteral("help"),    QStringLiteral("lock"),
             QStringLiteral("log"),    QStringLiteral("mode"),    QStringLiteral("open"),
             QStringLiteral("preset"), QStringLiteral("range"),   QStringLiteral("result"),
             QStringLiteral("search"), QStringLiteral("seed"),    QStringLiteral("smart"),
             QStringLiteral("status"), QStringLiteral("stop"),    QStringLiteral("target"),
             QStringLiteral("theme"),  QStringLiteral("threads"), QStringLiteral("version") };
}

QString MainWindow::commandHelp(const QString& cmd) const
{
    static const QHash<QString, QString> aHelp = {
        { QStringLiteral("help"), slime::ui::t("ui.f4f64b027abd") },
        { QStringLiteral("version"), slime::ui::t("ui.bd2fc88197ae") },
        { QStringLiteral("seed"), slime::ui::t("ui.be84b2c5322f") },
        { QStringLiteral("range"), slime::ui::t("ui.c9951d36a841") },
        { QStringLiteral("center"), slime::ui::t("ui.14074052b097") },
        { QStringLiteral("preset"), slime::ui::t("ui.d72ad001dcdf") },
        { QStringLiteral("mode"), slime::ui::t("ui.7ef8de2a05a9") },
        { QStringLiteral("search"), slime::ui::t("ui.6380f599e548") },
        { QStringLiteral("cancel"), slime::ui::t("ui.dc7e8d7b8a9f") },
        { QStringLiteral("status"), slime::ui::t("ui.9d871c556321") },
        { QStringLiteral("gpu"), slime::ui::t("ui.9fc4bd1d3a96") },
        { QStringLiteral("target"), slime::ui::t("ui.a08c4c8c3e3b") },
        { QStringLiteral("result"), slime::ui::t("ui.8d2180f88471") },
        { QStringLiteral("log"), slime::ui::t("ui.88e1078439f9") },
        { QStringLiteral("theme"), slime::ui::t("ui.4ef62fa28d91") },
        { QStringLiteral("stop"), slime::ui::t("ui.facc5e3a4732") },
        { QStringLiteral("smart"), slime::ui::t("ui.1149d8a99ff3") },
        { QStringLiteral("lock"), slime::ui::t("ui.0ed032ec9e84") },
        { QStringLiteral("grid"), slime::ui::t("ui.f119374add58") },
        { QStringLiteral("density"), slime::ui::t("ui.071d3bf971b4") },
        { QStringLiteral("threads"), slime::ui::t("ui.e7258f9fd682") },
        { QStringLiteral("build"), slime::ui::t("ui.d6769ff1ecde") },
        { QStringLiteral("cache"), slime::ui::t("ui.98918284391b") },
        { QStringLiteral("copy"), slime::ui::t("ui.60a473d46989") },
        { QStringLiteral("open"), slime::ui::t("ui.e2bc2f6e629e") },
        { QStringLiteral("about"), slime::ui::t("ui.b223e9759bc5") },
        { QStringLiteral("clear"), slime::ui::t("ui.9db4f6f902f7") },
    };
    return aHelp.value(cmd, slime::ui::t("ui.6535b7b34aa5"));
}

QString MainWindow::runCommand(const QString& cmd, const QStringList& args)
{
    const QString c = cmd.toLower();
    const auto argAt = [&args](int i) { return i < args.size() ? args[i] : QString(); };

    // v3.4.11（用户拍板：构建模式下参数命令「拒绝并提示」，不再静默失效）：
    //   构建只做史莱姆判定 + 写缓存，不参与匹配 ⇒ 这些参数对构建毫无影响，
    //   静默"成功"会让用户以为设置生效了。要改参数请先 `build off` 回到搜索模式。
    if (buildModeActive()) {
        static const QStringList kSearchOnly = {
            QStringLiteral("mode"),    QStringLiteral("target"), QStringLiteral("grid"),
            QStringLiteral("density"), QStringLiteral("stop"),   QStringLiteral("smart"),
            QStringLiteral("lock"),    QStringLiteral("threads")
        };
        if (kSearchOnly.contains(c)) {
            return slime::ui::t("ui.2f1957578333")
                .arg(c);
        }
    }

    if (c == QLatin1String("help")) {
        QString out = slime::ui::t("ui.b43c9cc77dd4").arg(commandNames().size());
        for (const QString& n : commandNames()) {
            out += QStringLiteral("  %1  -  %2\n").arg(n, -8).arg(commandHelp(n));
        }
        out += slime::ui::t("ui.e3335507e1df");
        return out;
    }
    if (c == QLatin1String("version")) {
        return slime::ui::t("ui.6a97f78c38df")
            .arg(QLatin1String(kAppVersion))
            .arg(QString::fromStdWString(appDirPath().wstring()))
            .arg(QStringLiteral(QT_VERSION_STR))
            .arg(m_daysSinceRelease);
    }
    if (c == QLatin1String("clear")) {
        // clear 由终端自己处理（清空输出区）；这里给 API 调用方一个明确应答，
        // 也保证「命令表 ↔ 实现」一致性自检不会把它当成幽灵指令。
        return slime::ui::t("ui.5736a4b3d232");
    }
    if (c == QLatin1String("about")) {
        return slime::ui::t("ui.d29cc9aa2cb5")
            .arg(QLatin1String(kAppVersion))
            .arg(QString::fromStdWString(appDirPath().wstring()))
            .arg(QLatin1String(kContactEmail));
    }
    if (c == QLatin1String("help") && !args.isEmpty()) {
        const QString one = args[0].toLower();
        if (!commandNames().contains(one)) {
            return slime::ui::t("ui.e4f8211e4809").arg(one).arg(commandNames().size());
        }
        return QStringLiteral("%1 - %2").arg(one, commandHelp(one));
    }
    if (c == QLatin1String("theme")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.cc2fe33a16e2")
                .arg(slime::ThemeManager::modeDisplayName(slime::ThemeManager::instance().mode()),
                     slime::ThemeManager::modeToString(slime::ThemeManager::instance().mode()));
        }
        const QString v = args[0].toLower();
        if (v != QLatin1String("light") && v != QLatin1String("dark") && v != QLatin1String("system")) {
            return slime::ui::t("ui.491a4473817c");
        }
        AppSettings s = m_settings;
        s.theme = v;
        applyAppSettings(s);              // 含主题重放 + 设置页收敛
        flushPendingConfigWrite();        // 主题是显式操作，立即落盘
        return slime::ui::t("ui.7b64f65aa5cb")
            .arg(slime::ThemeManager::modeDisplayName(slime::ThemeManager::instance().mode()));
    }
    if (c == QLatin1String("stop") || c == QLatin1String("smart") || c == QLatin1String("lock")) {
        QCheckBox* box = (c == QLatin1String("stop"))    ? m_w->stopAtTarget
                          : (c == QLatin1String("smart")) ? m_w->smartRange
                                                          : m_w->lockDir;
        const QString label = (c == QLatin1String("stop"))    ? slime::ui::t("ui.166485d58cc3")
                               : (c == QLatin1String("smart")) ? slime::ui::t("ui.9522c47b44f6")
                                                               : slime::ui::t("ui.144f946f0e21");
        if (args.isEmpty()) return QStringLiteral("%1：%2").arg(label, box->isChecked() ? slime::ui::t("ui.d78cde076b67")
                                                                                        : slime::ui::t("ui.f628761bf56a"));
        const QString v = args[0].toLower();
        if (v != QLatin1String("on") && v != QLatin1String("off")) {
            return slime::ui::t("ui.f28af3ed51d6").arg(c);
        }
        box->setChecked(v == QLatin1String("on"));
        onModeChanged();
        return slime::ui::t("ui.23935bb28c3c").arg(label, box->isChecked() ? slime::ui::t("ui.256783b7d4ea") : slime::ui::t("ui.6c14bd7f6f9e"));
    }
    if (c == QLatin1String("grid")) {
        if (args.isEmpty()) return slime::ui::t("ui.3b566a78f727").arg(m_w->gridSize->value());
        bool Ok = false;
        const int n = args[0].toInt(&Ok);
        if (!Ok || n < 1 || n > 512) return slime::ui::t("ui.a81d88d7e1e9");
        m_w->gridSize->setValue(n);
        return slime::ui::t("ui.fae3f47b7eee").arg(m_w->gridSize->value());
    }
    if (c == QLatin1String("density")) {
        if (args.isEmpty()) return slime::ui::t("ui.b55699ba275b").arg(m_w->minDensity->value(), 0, 'f', 3);
        bool Ok = false;
        const double v = args[0].toDouble(&Ok);
        if (!Ok || v < 0.0 || v > 1.0) return slime::ui::t("ui.7736eaf1585d");
        m_w->minDensity->setValue(v);
        return slime::ui::t("ui.6fa36e115342").arg(m_w->minDensity->value(), 0, 'f', 3);
    }
    if (c == QLatin1String("threads")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.58beee25a300")
                .arg(m_w->threads->value()).arg(m_lastEffectiveThreads);
        }
        bool Ok = false;
        const int n = args[0].toInt(&Ok);
        if (!Ok || n < 0) return slime::ui::t("ui.a890971c8e76");
        m_w->threads->setValue(n);
        return slime::ui::t("ui.a1297902c8e4").arg(m_w->threads->value());
    }
    if (c == QLatin1String("build")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.1f501012d9cb")
                .arg(buildModeActive() ? slime::ui::t("ui.d78cde076b67") : slime::ui::t("ui.f628761bf56a"));
        }
        const QString v = args[0].toLower();
        if (v != QLatin1String("on") && v != QLatin1String("off")) {
            return slime::ui::t("ui.44b21e54a5d3");
        }
        const bool on = (v == QLatin1String("on"));
        showPage(on ? kPageBuild : kPageSearch); // 页面 = 构建模式（单一事实来源）
        return slime::ui::t("ui.41db64208732")
            .arg(on ? slime::ui::t("ui.256783b7d4ea") : slime::ui::t("ui.6c14bd7f6f9e"),
                 on ? slime::ui::t("ui.d4fe2a107b07") : QString());
    }
    if (c == QLatin1String("cache")) {
        const QString sub = argAt(0).toLower();
        if (sub.isEmpty() || sub == QLatin1String("status")) {
            return slime::ui::t("ui.ca94b4a071fe")
                .arg(m_cache->hasData() ? slime::ui::t("ui.b19bae5d1305") : slime::ui::t("ui.43e203c1de2c"))
                .arg(static_cast<qlonglong>(m_cache->entryCount()))
                .arg(static_cast<qlonglong>(m_cache->ranges().size()))
                .arg(QString::fromStdWString(m_cacheFile.wstring()));
        }
        if (sub == QLatin1String("clear")) {
            m_cache->clear();
            std::error_code ec;
            std::filesystem::remove(m_cacheFile, ec);
            updateCacheStatusUi();
            return slime::ui::t("ui.20563af38637");
        }
        if (sub == QLatin1String("import")) {
            const QString path = args.mid(1).join(QLatin1Char(' ')).trimmed();
            if (path.isEmpty()) return slime::ui::t("ui.7a449b293047");
            const int64_t before = m_cache->entryCount();
            importCacheFromPath(path, /*showProgressDialog=*/!m_uiQuiet, /*showResultDialogs=*/!m_uiQuiet);
            const int64_t added = m_cache->entryCount() - before;
            updateCacheStatusUi();
            return added > 0 ? slime::ui::t("ui.06fa0ea0e616").arg(path).arg(added)
                             : slime::ui::t("ui.f56a1563fe42").arg(path);
        }
        if (sub == QLatin1String("build")) {
            if (!buildModeActive()) showPage(kPageBuild); // 语义：切到构建模式并立即开始构建
            if (m_running) return slime::ui::t("ui.cf0237994529");
            startSearch();
            return slime::ui::t("ui.9c599c680242");
        }
        return slime::ui::t("ui.549d8b43d0eb");
    }
    if (c == QLatin1String("copy")) {
        if (args.isEmpty()) return slime::ui::t("ui.2a56e37d4df1");
        bool Ok = false;
        const int n = args[0].toInt(&Ok);
        const int have = m_hasLastOutcome ? static_cast<int>(m_lastOutcome.results.size()) : 0;
        if (!Ok || n < 1) return slime::ui::t("ui.afc8e8e9cff7");
        if (n > have) {
            // v3.4.11（用户拍板）：越界**明确报错**，不静默失败
            return slime::ui::t("ui.20327830ac39")
                .arg(n).arg(have).arg(have == 0 ? slime::ui::t("ui.4d35ab452991") : QString());
        }
        const QString cmdText = cacheCommandFor(m_lastOutcome.results[static_cast<size_t>(n - 1)]);
        QApplication::clipboard()->setText(cmdText);
        return slime::ui::t("ui.d62ce9956d42").arg(n).arg(cmdText);
    }
    if (c == QLatin1String("open")) {
        const QString what = argAt(0).toLower();
        std::filesystem::path dir;
        if (what == QLatin1String("logs")) dir = appDirPath() / m_settings.logSubDir.toStdWString();
        else if (what == QLatin1String("cache")) dir = m_cacheFile.parent_path();
        else if (what == QLatin1String("crash")) dir = slime::CrashDump::dumpDir();
        else return slime::ui::t("ui.891847cfe310");
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!m_uiQuiet) QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(dir.wstring())));
        return slime::ui::t("ui.3754bdc64ee8").arg(QString::fromStdWString(dir.wstring()));
    }
    if (c == QLatin1String("seed")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.1ce22a5622fb")
                .arg(m_w->seedEdit->text(),
                     m_w->seedEdit->text().trimmed().isEmpty() ? slime::ui::t("ui.7307b22f991b")
                                                               : slime::ui::t("ui.ab6e15ae13ad"));
        }
        m_w->seedEdit->setText(args.join(QLatin1Char(' ')).trimmed());
        saveAlgorithmToConfig();
        bool Ok = true;
        m_w->seedEdit->text().trimmed().toLongLong(&Ok);
        return slime::ui::t("ui.27a21d4c6a3f")
            .arg(m_w->seedEdit->text(),
                 Ok ? slime::ui::t("ui.e994f7174177") : slime::ui::t("ui.3d24b2f34e0a"));
    }
    if (c == QLatin1String("range")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.8285db444fc5")
                .arg(m_w->minX->value()).arg(m_w->maxX->value())
                .arg(m_w->minZ->value()).arg(m_w->maxZ->value())
                .arg(static_cast<qlonglong>(m_w->maxX->value() - m_w->minX->value() + 1) *
                     static_cast<qlonglong>(m_w->maxZ->value() - m_w->minZ->value() + 1));
        }
        if (args.size() == 1) {
            bool Ok = false;
            const int half = QString(args[0]).remove(QLatin1Char('+')).remove(QStringLiteral("±")).toInt(&Ok);
            if (!Ok || half <= 0) return slime::ui::t("ui.e6702b5c2316");
            m_wholeMapConfirmed = false;
            applyPreset(half);
            return slime::ui::t("ui.f72fd24b8e12")
                .arg(half).arg(m_w->minX->value()).arg(m_w->maxX->value())
                .arg(m_w->minZ->value()).arg(m_w->maxZ->value());
        }
        if (args.size() >= 4) {
            bool Ok[4] = { false, false, false, false };
            const int v[4] = { args[0].toInt(&Ok[0]), args[1].toInt(&Ok[1]), args[2].toInt(&Ok[2]), args[3].toInt(&Ok[3]) };
            if (!Ok[0] || !Ok[1] || !Ok[2] || !Ok[3]) return slime::ui::t("ui.d76733482870");
            m_wholeMapConfirmed = false;
            m_w->minX->setValue(v[0]);
            m_w->maxX->setValue(v[1]);
            m_w->minZ->setValue(v[2]);
            m_w->maxZ->setValue(v[3]);
            clampRangeToBound();
            return slime::ui::t("ui.494a57ad68b7")
                .arg(m_w->minX->value()).arg(m_w->maxX->value())
                .arg(m_w->minZ->value()).arg(m_w->maxZ->value());
        }
        return slime::ui::t("ui.e6702b5c2316");
    }
    if (c == QLatin1String("center")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.61dc0e802dcf").arg(m_w->startX->value()).arg(m_w->startZ->value());
        }
        if (args.size() < 2) return slime::ui::t("ui.e1046b29fcde");
        bool okx = false, okz = false;
        const int x = args[0].toInt(&okx);
        const int z = args[1].toInt(&okz);
        if (!okx || !okz) return slime::ui::t("ui.00373618f5f1");
        m_w->startX->setValue(x);
        m_w->startZ->setValue(z);
        return slime::ui::t("ui.ccc1c03a364a").arg(x).arg(z);
    }
    if (c == QLatin1String("preset")) {
        if (args.isEmpty()) return slime::ui::t("ui.998dc8cd0df2");
        const QString p = args[0].toLower();
        m_wholeMapConfirmed = false;
        if (p == QLatin1String("near")) { applyPreset(20000); }
        else if (p == QLatin1String("local")) { applyPreset(200000); }
        else if (p == QLatin1String("global")) { applyPreset(1000000); }
        else if (p == QLatin1String("world")) { confirmWholeMapSearch(true, kJavaBoundChunk); }
        else if (p == QLatin1String("bedrock")) { confirmWholeMapSearch(true, kBedrockBoundChunk); }
        else return slime::ui::t("ui.d93989638036").arg(p);
        return slime::ui::t("ui.a6ef6f616459")
            .arg(p).arg(m_w->minX->value()).arg(m_w->maxX->value())
            .arg(m_w->minZ->value()).arg(m_w->maxZ->value());
    }
    if (c == QLatin1String("mode")) {
        if (args.isEmpty()) {
            QString alg = algorithmDisplayName(currentAlgorithm());
            return slime::ui::t("ui.9b9c5e47b569")
                .arg(m_w->densityRadio->isChecked() ? slime::ui::t("ui.c1ae1cee6679") : slime::ui::t("ui.bcf402e061a7"))
                .arg(alg)
                .arg(m_w->gridSize->value())
                .arg(m_w->minDensity->value(), 0, 'f', 3)
                .arg(m_w->threads->value());
        }
        const QString m = args[0].toLower();
        if (m == QLatin1String("strict")) { m_w->strictRadio->setChecked(true); return slime::ui::t("ui.11102a93f46d"); }
        if (m == QLatin1String("density")) { m_w->densityRadio->setChecked(true); return slime::ui::t("ui.177ed434899d"); }
        if (m == QLatin1String("exact")) { m_w->algExactRadio->setChecked(true); updateAlgorithmPanels(); saveAlgorithmToConfig(); return slime::ui::t("ui.ef9cef28a00b"); }
        if (m == QLatin1String("layered")) { m_w->algLayeredRadio->setChecked(true); updateAlgorithmPanels(); saveAlgorithmToConfig(); return slime::ui::t("ui.11a64fcd4eb1"); }
        if (m == QLatin1String("sample")) { m_w->algSampleRadio->setChecked(true); updateAlgorithmPanels(); saveAlgorithmToConfig(); return slime::ui::t("ui.b0b3d366b535"); }
        if (m == QLatin1String("brute")) { m_w->algBruteRadio->setChecked(true); updateAlgorithmPanels(); saveAlgorithmToConfig(); return slime::ui::t("ui.06430fed9d13"); }
        return slime::ui::t("ui.f165abcfb21a").arg(m);
    }
    if (c == QLatin1String("search")) {
        if (m_running) return slime::ui::t("ui.c7ef04e0e586");
        startSearch(); // 立即返回（后台线程执行）
        return slime::ui::t("ui.baac3862490e");
    }
    if (c == QLatin1String("cancel")) {
        if (!m_running) return slime::ui::t("ui.38f3015a3657");
        cancelSearch(); // 立即置位取消标志（引擎在扫描/匹配循环内检查）
        return slime::ui::t("ui.304ad94d91b1");
    }
    if (c == QLatin1String("status")) {
        const double elapsed = m_running && m_searchStartMs > 0
                                   ? (static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                              std::chrono::system_clock::now().time_since_epoch())
                                                              .count() -
                                                          m_searchStartMs) /
                                      1000.0)
                                   : 0.0;
        return slime::ui::t("ui.77dc3da6e684")
            .arg(QLatin1String(kAppVersion))
            .arg(m_running ? slime::ui::t("ui.30160a21b92a") : slime::ui::t("ui.8bf5c10ad937"))
            .arg(elapsed, 0, 'f', 1)
            .arg(m_w->bedrockRadio->isChecked() ? slime::ui::t("ui.618fd37c0c01") : slime::ui::t("ui.8accf0fe2eed"))
            .arg(m_w->densityRadio->isChecked() ? slime::ui::t("ui.c1ae1cee6679") : slime::ui::t("ui.bcf402e061a7"))
            .arg(algorithmDisplayName(currentAlgorithm()))
            .arg(m_w->minX->value()).arg(m_w->maxX->value())
            .arg(m_w->minZ->value()).arg(m_w->maxZ->value())
            .arg(m_w->startX->value()).arg(m_w->startZ->value())
            .arg(m_w->targetCount->value())
            .arg(m_w->threads->value())
            .arg(m_gpuAvailable ? (m_w->gpuCheck->isChecked() ? slime::ui::t("ui.1482b669e3b5")
                                                             : slime::ui::t("ui.4cd139b66904"))
                                : slime::ui::t("ui.beff4a1cd1a6"))
            .arg(m_cache->hasData() ? slime::ui::t("ui.55b901c4820a").arg(m_cache->entryCount())
                                    : slime::ui::t("ui.43e203c1de2c"))
            .arg(m_hasLastOutcome ? static_cast<int>(m_lastOutcome.results.size()) : 0);
    }
    if (c == QLatin1String("gpu")) {
        if (args.isEmpty()) {
            const QString dev = (m_gpuScan && !m_gpuScan->deviceName().empty())
                                    ? QString::fromStdString(m_gpuScan->deviceName())
                                    : slime::ui::t("ui.5a692b8d27dd");
            return slime::ui::t("ui.24245a88eab7")
                .arg(m_gpuAvailable ? slime::ui::t("ui.e91365cf9ed9") : slime::ui::t("ui.beff4a1cd1a6"))
                .arg(dev, gpuReminderText());
        }
        const QString v = args[0].toLower();
        if (v == QLatin1String("on")) {
            if (!m_w->gpuCheck->isChecked()) m_w->gpuCheck->setChecked(true); // 触发检测（同步一次）
            return slime::ui::t("ui.2b798dc73cf0")
                .arg(m_gpuAvailable ? slime::ui::t("ui.e91365cf9ed9") : slime::ui::t("ui.b80b8286c79e"), gpuReminderText());
        }
        if (v == QLatin1String("off")) {
            if (m_w->gpuCheck->isChecked()) m_w->gpuCheck->setChecked(false);
            return slime::ui::t("ui.ae900ee401e9");
        }
        return slime::ui::t("ui.2ebe5eb911b5");
    }
    if (c == QLatin1String("target")) {
        if (args.isEmpty()) return slime::ui::t("ui.178488361336").arg(m_w->targetCount->value());
        bool Ok = false;
        const int n = args[0].toInt(&Ok);
        if (!Ok || n < 1) return slime::ui::t("ui.fcf3f575ff23");
        m_w->targetCount->setValue(n);
        saveAlgorithmToConfig();
        return slime::ui::t("ui.bb5ec70c5ba6").arg(n);
    }
    if (c == QLatin1String("result")) {
        const QString sub = argAt(0).toLower();
        if (sub == QLatin1String("copy")) {
            bool Ok = false;
            const int n = argAt(1).toInt(&Ok);
            if (!Ok || n < 1) return slime::ui::t("ui.5dfaa6c328d0");
            if (!m_hasLastOutcome || n > static_cast<int>(m_lastOutcome.results.size())) {
                return slime::ui::t("ui.c88644b97058")
                    .arg(n).arg(m_hasLastOutcome ? static_cast<int>(m_lastOutcome.results.size()) : 0);
            }
            const QString cmdText = cacheCommandFor(m_lastOutcome.results[static_cast<size_t>(n - 1)]);
            QApplication::clipboard()->setText(cmdText);
            return slime::ui::t("ui.d62ce9956d42").arg(n).arg(cmdText);
        }
        // result list [n]（默认 10 条）
        int limit = 10;
        if (argAt(0).toLower() == QLatin1String("list") && !argAt(1).isEmpty()) {
            bool Ok = false;
            const int n = argAt(1).toInt(&Ok);
            if (Ok && n > 0) limit = n;
        } else if (sub != QLatin1String("list") && !sub.isEmpty()) {
            bool Ok = false;
            const int n = sub.toInt(&Ok);
            if (Ok && n > 0) limit = n;
        }
        if (!m_hasLastOutcome || m_lastOutcome.results.empty()) return slime::ui::t("ui.ff2587f7bcb9");
        QString out = slime::ui::t("ui.e8976914d64f")
                          .arg(m_lastOutcome.results.size())
                          .arg(std::min<int>(limit, static_cast<int>(m_lastOutcome.results.size())));
        const size_t shown = std::min<size_t>(m_lastOutcome.results.size(), static_cast<size_t>(limit));
        for (size_t i = 0; i < shown; ++i) {
            const MatchResult& r = m_lastOutcome.results[i];
            out += slime::ui::t("ui.d0e18567e89d")
                       .arg(i + 1)
                       .arg(r.minX).arg(r.minZ)
                       .arg(r.maxX - r.minX + 1).arg(r.maxZ - r.minZ + 1)
                       .arg(r.density * 100.0, 0, 'f', 1)
                       .arg(cacheCommandFor(r));
        }
        return out;
    }
    if (c == QLatin1String("log")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.8efc1728fb58")
                .arg(QString::fromUtf8(slime::LogBus::modeName(slime::LogBus::mode())))
                .arg(slime::LogBus::enabled() ? QString::fromStdString(slime::LogBus::currentFile())
                                              : slime::ui::t("ui.817e809c70c7"))
                .arg(slime::LogBus::snapshot().size())
                .arg(slime::LogBus::kMaxEntries);
        }
        const QString v = args[0].toLower();
        if (v != QLatin1String("off") && v != QLatin1String("brief") && v != QLatin1String("detailed")) {
            return slime::ui::t("ui.d35ba202afd5");
        }
        // v3.4.11（用户要求：命令生效后设置状态及时更新）：日志模式同时写进设置项并同步设置页，
        // 否则设置页还显示旧值，下一次设置改动会把命令的结果覆盖掉。
        {
            AppSettings s = m_settings;
            s.logAtStartup = (v != QLatin1String("off"));
            s.logLevel = (v == QLatin1String("detailed")) ? 1 : 0;
            applyAppSettings(s); // 内部会 setMode + 刷新日志面板 + 收敛设置页
        }
        flushPendingConfigWrite();
        if (m_logPanel) m_logPanel->refreshFromBus();
        return slime::ui::t("ui.034e8f26bb37").arg(QString::fromUtf8(slime::LogBus::modeName(slime::LogBus::mode())));
    }
    return slime::ui::t("ui.8a5dc476ce19").arg(c);
}

// v3.3.7.2（任务书 1.2）：界面显示用的「总扫描量」—— 必须与引擎 liveScanned 同源。
// 分块路径下引擎的总量含块间重叠行（每块 hMax-1 行），比「去重区域区块数」略大；
// 若界面用后者当分母，扫描完成时百分比会 >100%（用户截图即为此）。
// 引擎未公开（快速估算的采样扫描）时退回区域区块数。
int64_t MainWindow::displayTotalChunks() const
{
    const int64_t t = m_task.liveTotalCount();
    return t > 0 ? t : m_runTotalChunks;
}

// v3.5.19：语言切换后重设「搜索模式」面板的文案（UiStrings 已迁移的部分）
// v3.5.27（用户反馈：英文覆盖面不够）：把主界面所有可见文本按翻译表改写一遍。
//   做法：首次记录中文原文（存到 dynamicProperty "zhText"），之后按语言在「原文/译文」之间切换，
//   这样来回切换不会丢失原文，也不需要把上千处 QStringLiteral 逐个改写。

