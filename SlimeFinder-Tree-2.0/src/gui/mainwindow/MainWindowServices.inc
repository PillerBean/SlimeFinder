// v3.12.0：语言、版本、预设、缓存、遥测与对话框服务。

void MainWindow::applyLanguage()
{
    const bool en = slime::ui::isEnglish();
    auto fixText = [en](QWidget* w) {
        if (!w) return;
        QString zh = w->property("zhText").toString();
        if (zh.isEmpty()) {
            zh = w->windowTitle().isEmpty() ? QString() : QString();
            // 不同控件取文本的方式不同，统一在下面按类型处理
        }
    };
    (void)fixText;

    auto applyTo = [en](QObject* o, const QString& cur, const std::function<void(const QString&)>& setter) {
        if (cur.isEmpty()) return;
        QString zh = o->property("zhText").toString();
        // v3.6.4：若控件当前文本本身就是一个已知中文键（运行时才设置的提示），以它为准
        if (slime::ui::zhToEn().contains(cur) || zh.isEmpty()) zh = cur;
        o->setProperty("zhText", zh);
        setter(en ? slime::ui::tr(zh) : zh);
    };

    const auto widgets = findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        if (auto* lb = qobject_cast<QLabel*>(w)) {
            if (lb->text().isEmpty()) continue;
            const QString zh = lb->property("zhText").toString().isEmpty() ? lb->text()
                                                                          : lb->property("zhText").toString();
            applyTo(lb, zh, [lb](const QString& t) { lb->setText(t); });
        } else if (auto* bt = qobject_cast<QAbstractButton*>(w)) {
            if (bt->text().isEmpty()) continue;
            const QString zh = bt->property("zhText").toString().isEmpty() ? bt->text()
                                                                          : bt->property("zhText").toString();
            applyTo(bt, zh, [bt](const QString& t) { bt->setText(t); });
        } else if (auto* gb = qobject_cast<QGroupBox*>(w)) {
            if (gb->title().isEmpty()) continue;
            const QString zh = gb->property("zhText").toString().isEmpty() ? gb->title()
                                                                          : gb->property("zhText").toString();
            applyTo(gb, zh, [gb](const QString& t) { gb->setTitle(t); });
        } else if (auto* le = qobject_cast<QLineEdit*>(w)) {
            const QString cur = le->placeholderText();
            if (!cur.isEmpty()) {
                QString zh = le->property("zhPlaceholder").toString();
                if (slime::ui::zhToEn().contains(cur) || zh.isEmpty()) zh = cur;
                le->setProperty("zhPlaceholder", zh);
                le->setPlaceholderText(en ? slime::ui::tr(zh) : zh);
            }
        } else if (auto* cb = qobject_cast<QComboBox*>(w)) {
            for (int i = 0; i < cb->count(); ++i) {
                const QString key = QStringLiteral("zhItem%1").arg(i);
                const QString cur = cb->itemText(i);
                QString zh = cb->property(key.toUtf8().constData()).toString();
                if (zh.isEmpty()) {
                    zh = cur;
                    cb->setProperty(key.toUtf8().constData(), zh);
                }
                cb->setItemText(i, en ? slime::ui::tr(zh) : zh);
            }
        }
    }
    // 标签页名（底部面板：进度/反馈/日志/命令）
    const auto tabs = findChildren<QTabWidget*>();
    for (QTabWidget* tw : tabs) {
        for (int i = 0; i < tw->count(); ++i) {
            const QString key = QStringLiteral("zhTab%1").arg(i);
            const QString cur = tw->tabText(i);
            QString zh = tw->property(key.toUtf8().constData()).toString();
            if (zh.isEmpty()) {
                zh = cur;
                tw->setProperty(key.toUtf8().constData(), zh);
            }
            tw->setTabText(i, en ? slime::ui::tr(zh) : zh);
        }
    }
    // 结果表头
    const auto tables = findChildren<QTableWidget*>();
    for (QTableWidget* tb : tables) {
        for (int c = 0; c < tb->columnCount(); ++c) {
            QTableWidgetItem* it = tb->horizontalHeaderItem(c);
            if (!it) continue;
            const QString key = QStringLiteral("zhHead%1").arg(c);
            const QString cur = it->text();
            QString zh = tb->property(key.toUtf8().constData()).toString();
            if (zh.isEmpty()) {
                zh = cur;
                tb->setProperty(key.toUtf8().constData(), zh);
            }
            it->setText(en ? slime::ui::tr(zh) : zh);
        }
    }
    const auto actions = findChildren<QAction*>();
    for (QAction* a : actions) {
        if (a->text().isEmpty()) continue;
        const QString zh = a->property("zhText").toString().isEmpty() ? a->text() : a->property("zhText").toString();
        a->setProperty("zhText", zh);
        a->setText(en ? slime::ui::tr(zh) : zh);
    }
    // 窗口标题单独处理（版本号是变量，不翻译）
    setWindowTitle(slime::ui::t("ui.8e056aa8f722").arg(QLatin1String(kAppVersion)));
}

void MainWindow::retranslateModeUi()
{
    if (!m_w) return;
    using slime::ui::Key;
    using slime::ui::text;
    if (m_w->strictRadio) m_w->strictRadio->setText(text(Key::ModePattern));
    if (m_w->densityRadio) m_w->densityRadio->setText(text(Key::ModeDensity));
    if (m_w->strictRadio) m_w->strictRadio->setToolTip(text(Key::ModePatternTip));
    if (m_w->densityRadio) m_w->densityRadio->setToolTip(text(Key::ModeDensityTip));
    if (m_w->shapeSquareRadio) m_w->shapeSquareRadio->setText(text(Key::ShapeSquare));
    if (m_w->shapeRingRadio) m_w->shapeRingRadio->setText(text(Key::ShapeRing));
    if (m_w->shapeSquareRadio) m_w->shapeSquareRadio->setToolTip(text(Key::ShapeSquareTip));
    if (m_w->shapeRingRadio) m_w->shapeRingRadio->setToolTip(text(Key::ShapeRingTip));
    if (m_w->ringScanCombo) {
        if (m_w->ringScanCombo->count() >= 2) {
            m_w->ringScanCombo->setItemText(0, text(Key::RingScanFast));
            m_w->ringScanCombo->setItemText(1, text(Key::RingScanExact));
        }
        m_w->ringScanCombo->setToolTip(text(Key::RingScanTip));
    }
    if (m_w->smartRange) {
        m_w->smartRange->setText(text(Key::SmartRange));
        m_w->smartRange->setToolTip(text(Key::SmartRangeTip));
    }
}

// v3.5.20：两种全屏 —— borderless（无边框铺满）/ windowed（窗口最大化，有边框）
#ifdef Q_OS_WIN
// v3.5.22（用户要求）：无边框全屏下 Win11 仍会给窗口加圆角 —— 显式关掉
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
static void disableWindows11Rounding(QWidget* w)
{
    if (!w) return;
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd) return;
    // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_DONOTROUND = 1
    const DWORD pref = 1;
    DwmSetWindowAttribute(hwnd, 33, &pref, sizeof(pref));
}
#else
static void disableWindows11Rounding(QWidget*) {}
#endif

void MainWindow::applyFullscreenMode()
{
    const bool windowed = m_settings.fullscreenMode.trimmed().toLower() == QLatin1String("windowed");
    if (!isVisible()) {
        m_pendingShowMode = true;
        return;   // 还没显示：等 showWithMode() 一次性显示，避免闪烁
    }
    if (windowed) {
        if (isFullScreen()) showNormal();
        showMaximized();
    } else {
        if (isMaximized()) showNormal();
        showFullScreen();
        disableWindows11Rounding(this);   // v3.5.22：去掉 Win11 四角圆角
    }
    std::fprintf(stderr, "[\u7a97\u53e3] \u5168\u5c4f\u65b9\u5f0f\uff1a%s\n",
                 windowed ? "windowed" : "borderless");
}

// v3.5.26：群系版本持久化（config.json 的 settings.mapVersion）
void MainWindow::saveMapVersionToConfig()
{
    writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                    m_terminal ? m_terminal->history() : QStringList());
}

void MainWindow::showWithMode()
{
    m_pendingShowMode = false;
    if (m_telemetry) m_telemetry->reportAppStart(QString::fromLatin1(kAppVersion), QStringLiteral("test"));
    const bool windowed = m_settings.fullscreenMode.trimmed().toLower() == QLatin1String("windowed");
    show();
    if (windowed)
        showMaximized();
    else {
        showFullScreen();
        disableWindows11Rounding(this);   // v3.5.22：去掉 Win11 四角圆角
    }
}

void MainWindow::toggleFullscreenMode()
{
    m_settings.fullscreenMode =
        (m_settings.fullscreenMode.trimmed().toLower() == QLatin1String("windowed")) ? "borderless" : "windowed";
    writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                    m_terminal ? m_terminal->history() : QStringList());
    applyFullscreenMode();
    setStatus(slime::ui::tr(slime::ui::t("ui.e9fb9aec5e59"))
                  .arg(m_settings.fullscreenMode == QLatin1String("windowed") ? slime::ui::t("ui.37c2545a642e")
                                                                             : slime::ui::t("ui.2a0936486d4a")));
}

void MainWindow::setStatus(const QString& text)
{
    statusBar()->showMessage(text);
}

void MainWindow::setInputsEnabled(bool on)
{
    const QList<QWidget*> controls = {
        m_w->javaRadio, m_w->bedrockRadio, m_w->seedEdit,
        m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ,
        m_w->presetNear, m_w->presetLocal, m_w->presetGlobal, m_w->presetWorld, m_w->presetBedrock,
        m_w->strictRadio, m_w->densityRadio,
        m_w->lockDir, m_w->drawPattern,
        m_w->gridSize, m_w->minDensity,
        m_w->importCache, m_w->clearCache, m_w->useCache,
        m_w->stopAtTarget, m_w->targetCount, m_w->smartRange,
        m_w->startX, m_w->startZ, m_w->convX, m_w->convZ, m_w->convertBtn,
        m_w->threads, m_w->gpuCheck, m_w->copyBtn
    };
    for (QWidget* w : controls) w->setEnabled(on);
    applyVersionUi(); // 版本相关禁用状态（种子/缓存/预设仅限对应版本）
    // v3.4.8（用户重点反馈「开启缓存构建模式之后没有标灰搜索模式」的根因）：
    // 本函数会把上面所有控件无条件重新启用，而构建模式的禁用是**叠加**在版本/模式之上的；
    // 少了这一步，搜索一开始/一结束，构建模式下的标灰就被整体抹掉。此处按当前模式重新叠加。
    updateBuildHintUi();
    m_w->start->setEnabled(on);
}

void MainWindow::setRunningUi(bool running)
{
    m_w->start->setEnabled(!running);
    m_w->cancel->setEnabled(running);
    if (!running) updateStartEnabled();   // v3.6.5：结束后恢复按钮可用性
    if (running) {
        // v3.6.0（用户反馈：要等扫完才动）：未知进度时用忙碌滚动，首个真实进度再切百分比
        m_w->progress->setRange(0, 0);
        m_w->progress->setValue(0);
        m_w->progress->setStyleSheet(QString());
        if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("…"));
        m_w->progressInfo->setText(slime::ui::t("ui.455d8705421a"));
        // v3.6.1：重新搜索清空旧结果；v3.6.19：抽成 clearResultsForNewSearch()
        clearResultsForNewSearch();
        // v3.3.6（任务书 调整 3）：进度流水每 0.5 秒追加一条（仅在搜索中）
        m_streamTick = 0;
        m_task.resetLiveMatches(); // v3.3.7：实时命中计数归零（计数对象由 SearchTask 持有）
        m_streamScanned = 0;
        if (m_streamTimer) m_streamTimer->start(500);
    } else {
        if (m_streamTimer) m_streamTimer->stop();
    }
}

// ---------------- 版本/模式 ----------------
void MainWindow::applyVersionDefaults()
{
    const bool bedrock = m_w->bedrockRadio->isChecked();
    const int b = bedrock ? kBedrockBoundChunk : kJavaBoundChunk;
    m_w->minX->setValue(-b);
    m_w->maxX->setValue(b);
    m_w->minZ->setValue(-b);
    m_w->maxZ->setValue(b);
}

void MainWindow::applyVersionUi()
{
    const bool bedrock = m_w->bedrockRadio->isChecked();
    // v3.6.12（用户反馈：基岩版种子不可调整）：判定与种子无关，但**地图群系**需要种子，
    //   所以输入框保持可用，并用标签/提示说明「基岩版结果不受种子影响」。
    // 构建模式不需要种子（其余情况都允许填；基岩版只用于地图上色）
    m_w->seedEdit->setEnabled(!buildModeActive());
    const bool enabled = bedrock && !m_running;
    m_w->importCache->setEnabled(enabled);
    m_w->clearCache->setEnabled(enabled);
    m_w->useCache->setEnabled(enabled);
    // 预设按钮按版本可用性（世界边界仅 Java、基岩边界仅基岩）
    m_w->presetWorld->setEnabled(!bedrock);
    m_w->presetBedrock->setEnabled(bedrock);
    m_w->presetNear->setEnabled(true);
    m_w->presetLocal->setEnabled(true);
    m_w->presetGlobal->setEnabled(true);
    // v3.6.12：基岩版种子只影响地图上色，不影响判定 —— 文案要写清楚
    if (m_w->seedLabel) {
        m_w->seedLabel->setText(bedrock ? slime::ui::t("ui.bdf0619ab5e7")
                                        : slime::ui::t("ui.de5100998f74"));
    }
    if (m_w->seedEdit) {
        m_w->seedEdit->setToolTip(
            bedrock ? slime::ui::t("ui.82eec10674b6")
                    : slime::ui::t("ui.acc16bb01871"));
    }
    if (!bedrock) {
        m_w->cacheStatus->setText(slime::ui::t("ui.705f907ae91f"));
    } else {
        updateCacheStatusUi();
    }
}

void MainWindow::onVersionChanged()
{
    m_wholeMapConfirmed = false; // 切换版本后整图确认失效
    applyVersionDefaults();
    applyVersionUi();
}

// ---------------- v3.3.0：设置区滚动与界面截图 ----------------

// SLIME_SHOT=<前缀>：把当前窗口渲染为 <前缀>_<tag>.png（无界面自检时核对布局用）
void MainWindow::saveWindowShot(const QString& prefix, const QString& tag)
{
    if (prefix.isEmpty()) return;
    const QString path = QStringLiteral("%1_%2.png").arg(prefix, tag);
    const QPixmap pm = grab();
    const bool Ok = pm.save(path);
    std::fprintf(stderr, "[UI自检] 截图 %s: %s (%dx%d)\n", Ok ? "已保存" : "保存失败",
                 path.toUtf8().constData(), pm.width(), pm.height());
    std::fflush(stderr);
}

// 依文本与可用宽度显式预留换行标签高度：QLabel 在滚动容器/嵌套布局中
// 的 height-for-width 可能按旧宽度计算，导致末行被裁剪（v3.3.0 实测已复现）。
void MainWindow::reserveWrappedHeight(QLabel* label)
{
    if (!label || label->text().isEmpty()) return;
    int avail = label->width();
    if (avail < 120) {
        // 首次布局前 width() 尚未确定：用父容器宽度或窗口宽度估算
        avail = label->parentWidget() ? label->parentWidget()->width() - 40 : width() - 60;
    }
    if (avail < 120) avail = 520;
    const QFontMetrics fm(label->font());
    const QRect br = fm.boundingRect(QRect(0, 0, avail, 100000), Qt::TextWordWrap, label->text());
    label->setMinimumHeight(br.height() + 4);
}

// 窗口尺寸变化后重新预留换行标签高度（宽度变小 → 行数变多 → 需要更高）
void MainWindow::resizeEvent(QResizeEvent* ev)
{
    // v3.5.5：让地图尽量呈正方形
    if (m_rightSplit) QTimer::singleShot(0, this, [this]() { adjustMapSquare(); });
    // v3.5.5：让地图尽量呈正方形
    QMainWindow::resizeEvent(ev);
    if (!m_w) return;
    reserveWrappedHeight(m_w->algSummary);
    reserveWrappedHeight(m_w->modeWarn);
}

void MainWindow::onModeChanged(){
    // v3.6.19（用户反馈）：切换图案/密度后，列表里还留着上一种方式的结果 ⇒ 主动清场
    if (m_hasLastOutcome) clearResultsForNewSearch();
    const bool strict = m_w->strictRadio->isChecked();
    m_w->strictPanel->setVisible(strict);
    m_w->densityPanel->setVisible(!strict);
    updateAlgorithmPanels(); // v3.3.0：算法选择仅在密度搜索下可用
    updateStartEnabled();    // v3.6.6：模式/形状切换后同步按钮状态
    if (strict) {
        if (!m_patternSet) {
            m_w->patternInfo->setText(slime::ui::t("ui.15d9e07ff847"));
        } else {
            m_w->patternInfo->setText(slime::ui::t("ui.81e8a258ab88")
                                          .arg(m_pattern.activeCount())
                                          .arg(m_pattern.rows())
                                          .arg(m_pattern.cols()));
        }
    }
}

// ---------------- v3.3.0：搜索算法（精确/分层/快速估算/保守） ----------------

Algorithm MainWindow::currentAlgorithm() const
{
    if (m_w->algLayeredRadio->isChecked()) return Algorithm::Layered;
    if (m_w->algSampleRadio->isChecked()) return Algorithm::Sample;
    if (m_w->algBruteRadio->isChecked()) return Algorithm::Brute;
    return Algorithm::Exact;
}

QString MainWindow::algorithmDisplayName(Algorithm a) const
{
    switch (a) {
    case Algorithm::Layered: return slime::ui::t("ui.f18c79796e88");
    case Algorithm::Sample: return m_w->sampleExact->isChecked() ? slime::ui::t("ui.144cdf9f8727")
                                                                 : slime::ui::t("ui.dcee497c44dd");
    case Algorithm::Brute: return slime::ui::t("ui.9e6092059b8f");
    default: return slime::ui::t("ui.73b75d28b4b7");
    }
}

void MainWindow::updateAlgorithmPanels()
{
    const bool density = m_w->densityRadio->isChecked();
    const Algorithm a = currentAlgorithm();
    for (QRadioButton* r : { m_w->algExactRadio, m_w->algLayeredRadio, m_w->algSampleRadio, m_w->algBruteRadio }) {
        r->setEnabled(density);
    }
    m_w->layerBlock->setEnabled(density && a == Algorithm::Layered);
    m_w->layerRatio->setEnabled(density && a == Algorithm::Layered);
    m_w->sampleStep->setEnabled(density && a == Algorithm::Sample);
    m_w->sampleExact->setEnabled(density && a == Algorithm::Sample);

    QString s;
    if (!density) {
        s = slime::ui::t("ui.b71ec532ab48");
        m_w->modeHeader->setText(slime::ui::t("ui.bae48f65f9fa"));
        m_w->modeWarn->setVisible(false);
        m_w->algSummary->setText(s);
        reserveWrappedHeight(m_w->algSummary);
        return;
    }
    switch (a) {
    case Algorithm::Layered: {
        const int L = m_w->layerBlock->currentData().toInt();
        const double ratio = m_w->layerRatio->value();
        s = slime::ui::t("ui.73f5c24734d4").arg(L).arg(ratio, 0, 'f', 2);
        s += ratio <= 1.0 ? slime::ui::t("ui.6ea4024321e7")
                          : slime::ui::t("ui.fd577c7fa771");
        s += slime::ui::t("ui.feadac842a8d");
        m_w->modeWarn->setText(slime::ui::t("ui.581cf6954a19")
                                   .arg(ratio, 0, 'f', 2));
        m_w->modeWarn->setVisible(ratio > 1.0);
        break;
    }
    case Algorithm::Sample: {
        const int S = m_w->sampleStep->value();
        if (m_w->sampleExact->isChecked()) {
            s = slime::ui::t("ui.fe9d0cfe9720")
                    .arg(S).arg(S * S);
        } else {
            s = slime::ui::t("ui.b44e55bc1f8b")
                    .arg(S).arg(S * S).arg(S);
        }
        m_w->modeWarn->setText(slime::ui::t("ui.c96ac97198bb"));
        m_w->modeWarn->setVisible(true);
        break;
    }
    case Algorithm::Brute:
        s = slime::ui::t("ui.c32ad66a6dd5");
        m_w->modeWarn->setVisible(false);
        break;
    default:
        s = slime::ui::t("ui.6ac84b6b4c00");
        m_w->modeWarn->setVisible(false);
        break;
    }
    m_w->algSummary->setText(s);
    reserveWrappedHeight(m_w->algSummary);
    reserveWrappedHeight(m_w->modeWarn);
    m_w->modeHeader->setText(slime::ui::t("ui.ff768c1de692").arg(algorithmDisplayName(a)));
}

// config.json 算法块读写（键名扁平，沿用本项目自写 JSON 的极简解析）
void MainWindow::loadAlgorithmFromConfig()
{
    const auto path = appDirPath() / kConfigFileName;
    const std::string mode = cfgString(path, "algorithm.mode", "exact");
    m_loadingConfig = true;
    if (mode == "layered") m_w->algLayeredRadio->setChecked(true);
    else if (mode == "sample") m_w->algSampleRadio->setChecked(true);
    else if (mode == "brute") m_w->algBruteRadio->setChecked(true);
    else m_w->algExactRadio->setChecked(true);

    const int lb = cfgInt(path, "algorithm.layerBlock", 16);
    int idx = m_w->layerBlock->findData(lb);
    m_w->layerBlock->setCurrentIndex(idx >= 0 ? idx : 0);
    m_w->layerRatio->setValue(cfgDouble(path, "algorithm.layerRatio", 1.0));
    m_w->sampleStep->setValue(cfgInt(path, "algorithm.sampleStep", 8));
    m_w->sampleExact->setChecked(cfgBool(path, "algorithm.sampleExact", false));
    m_loadingConfig = false;
    updateAlgorithmPanels();
}

void MainWindow::saveAlgorithmToConfig()
{
    if (m_loadingConfig) return;
    writeConfigFile(cfgBool(appDirPath() / kConfigFileName, "gpu.enabled", false),
                    m_terminal ? m_terminal->history() : QStringList());
}

// ---------------- v3.3.4：命令终端历史持久化（任务书 2.4：config.json 最多 100 条） ----------------
void MainWindow::saveTerminalHistoryToConfig(const QStringList& history)
{
    if (m_loadingConfig) return;
    QStringList capped = history;
    while (capped.size() > 100) capped.removeFirst();
    writeConfigFile(cfgBool(appDirPath() / kConfigFileName, "gpu.enabled", false), capped);
}

// 读取 config.json 的 terminal.history（简单 JSON 字符串数组解析；损坏/缺失时返回空表）
QStringList MainWindow::loadTerminalHistoryFromConfig()
{
    QStringList out;
    std::ifstream in(appDirPath() / kConfigFileName);
    if (!in) return out;
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto kp = content.find("\"history\"");
    if (kp == std::string::npos) return out;
    const auto lb = content.find('[', kp);
    const auto rb = content.find(']', lb == std::string::npos ? kp : lb);
    if (lb == std::string::npos || rb == std::string::npos || rb < lb) return out;
    const std::string body = content.substr(lb + 1, rb - lb - 1);
    size_t i = 0;
    while (i < body.size()) {
        const auto q0 = body.find('"', i);
        if (q0 == std::string::npos) break;
        const auto q1 = body.find('"', q0 + 1);
        if (q1 == std::string::npos) break;
        out << QString::fromStdString(body.substr(q0 + 1, q1 - q0 - 1));
        i = q1 + 1;
    }
    while (out.size() > 100) out.removeFirst();
    return out;
}

// 非精确模式的差异校验：小范围（≤256×256 区块）自动跑一次精确对照
bool MainWindow::crossCheckExact(const SearchOptions& opt, int64_t sampleFound, int64_t* exactFound) const
{
    *exactFound = -1;
    const int64_t w = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t h = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    if (w <= 0 || h <= 0 || w * h > 256LL * 256LL) return false; // 范围过大：不做自动对照
    if (opt.mode != Mode::Density || opt.algorithm == Algorithm::Exact) return false;
    SearchOptions omp = opt;
    omp.algorithm = Algorithm::Exact;
    omp.useCache = false;
    omp.cache = nullptr;
    omp.cacheWrite = nullptr;
    omp.stopAtTarget = false;
    omp.gpuScan = nullptr;
    SearchOutcome out;
    std::atomic<bool> cancel{ false };
    SlimeEngine engine;
    engine.run(omp, cancel, nullptr, out);
    if (!out.ok) return false;
    *exactFound = out.totalFound;
    (void)sampleFound;
    return true;
}

// ---------------- 预设与边界 ----------------

void MainWindow::applyPreset(int halfSpan)
{
    setSearchRadius(halfSpan);
    setStatus(slime::ui::tr(slime::ui::t("ui.241d40e4e9d5")).arg(halfSpan));
    Logger::info("应用预设范围 ±" + std::to_string(halfSpan) + " 区块");
}

// v3.5.16：范围只由「半径」决定 —— 半径 r ⇒ 正方形 [-r, r-1]（边长 2r），中心恒为原点。
void MainWindow::setSearchRadius(int r)
{
    if (!m_w || !m_w->minX) return;
    const int radius = (std::max)(1, r);
    const int lo = -radius, hi = radius;   // 半径 r ⇒ X/Z 各 ±r（边长 2r+1）
    m_rangeSyncGuard = true;
    m_w->minX->setValue(lo);
    m_w->maxX->setValue(hi);
    m_w->minZ->setValue(lo);
    m_w->maxZ->setValue(hi);
    if (m_w->rangeRadius && m_w->rangeRadius->value() != radius) m_w->rangeRadius->setValue(radius);
    if (m_w->bRadius && m_w->bRadius->value() != radius) m_w->bRadius->setValue(radius);
    if (m_w->rangeHint)
        m_w->rangeHint->setText(slime::ui::t("ui.d25cd4d6bfc3")
                                    .arg(2 * radius + 1)
                                    .arg(lo)
                                    .arg(hi));
    m_rangeSyncGuard = false;
    pushSearchRangeToBuild();
    updateBuildHintUi();
    updateBuildEstimateLive();
}

// 反向：范围被别人（配置/命令/整图确认/自检）改动时，把半径显示同步过来
void MainWindow::syncRadiusFromRange()
{
    if (!m_w || !m_w->minX || m_rangeSyncGuard) return;
    const int spanX = m_w->maxX->value() - m_w->minX->value() + 1;
    const int spanZ = m_w->maxZ->value() - m_w->minZ->value() + 1;
    const int radius = (std::max)(1, (std::max)(spanX, spanZ) / 2);
    m_rangeSyncGuard = true;
    if (m_w->rangeRadius && m_w->rangeRadius->value() != radius) m_w->rangeRadius->setValue(radius);
    if (m_w->bRadius && m_w->bRadius->value() != radius) m_w->bRadius->setValue(radius);
    if (m_w->rangeHint)
        m_w->rangeHint->setText(slime::ui::t("ui.871551b08684").arg(spanX).arg(spanZ).arg(radius));
    m_rangeSyncGuard = false;
}

// 整图搜索的选项构造（供估算）
SearchOptions MainWindow::wholeMapOptions(int halfSpan) const
{
    SearchOptions opt;
    opt.version = m_w->bedrockRadio->isChecked() ? Version::Bedrock : Version::Java;
    opt.minX = -halfSpan;
    opt.maxX = halfSpan;
    opt.minZ = -halfSpan;
    opt.maxZ = halfSpan;
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
    // v3.4.8：整图估算原来漏了搜索算法与分层/采样参数 ⇒ 估算永远按「精确」算，与实际要跑的算法不符
    opt.algorithm = (opt.mode == Mode::Density) ? currentAlgorithm() : Algorithm::Exact;
    opt.layerBlock = m_w->layerBlock->currentData().toInt();
    opt.layerRatio = m_w->layerRatio->value();
    opt.sampleStep = m_w->sampleStep->value();
    opt.sampleExact = m_w->sampleExact->isChecked();
    opt.threads = m_w->threads->value();
    // v3.3.9（任务书 2.2）：CPU 占用率上限（按逻辑核心折算线程数）
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
    // v3.3.9（任务书 2.1）：内存上限（分块位图预算，超出自动降并发）
    opt.memBudgetBytes =
        (m_settings.memLimitMB > 0) ? static_cast<int64_t>(m_settings.memLimitMB) * 1024 * 1024 : 0;
    opt.cpuLimitPercent = std::clamp(m_settings.cpuLimitPercent, 10, 100);
    // v3.7.1：GPU 占用率上限 → 环境变量（引擎在瓦片之间按占空比休眠）
    {
        // v3.8.11：图案流式开关 → 环境变量（引擎侧读取）
        _putenv_s("SLIME_GPU_PATTERN", m_settings.gpuPatternStream ? "1" : "0");
        const int gpct = std::clamp(m_settings.gpuLimitPercent, 10, 100);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", gpct);
        _putenv_s("SLIME_GPU_MAX_PCT", buf);
    }
    if (opt.mode == Mode::Strict) {
        if (m_patternSet && !m_pattern.empty()) opt.pattern = m_pattern;
    }
    return opt;
}

// 整图确认对话框（v3.1.1：动态耗时估算）
bool MainWindow::confirmWholeMapSearch(bool applyRange, int halfSpan)
{
    SearchOptions opt = wholeMapOptions(halfSpan);
    const bool hasPattern = (opt.mode != Mode::Strict) || (!opt.pattern.empty());
    const bool java = (opt.version == Version::Java);

    if (m_uiQuiet) { // 自检模式：直接放行
        if (applyRange) {
            applyPreset(halfSpan);
            m_wholeMapConfirmed = true;
        }
        return true;
    }

    if (!hasPattern) {
        // 未绘制图案时按严格模式仍可估算扫描部分（匹配窗口按空图案估计不可靠 → 提示先绘制）
        // 实际进入 startSearch 时会报“请先绘制图案”，此处直接放行由 startSearch 处理。
        if (applyRange) {
            QMessageBox::warning(this, slime::ui::t("ui.ab3656a956f5"),
                                 slime::ui::t("ui.719d10c91481"));
            applyPreset(halfSpan);
        }
        return false;
    }

    SearchEstimate est;
    const int threads = m_w->threads->value();
    bool estOk = SlimeEngine::estimateSearch(opt, threads, est);

    // 区块数（万亿）与耗时范围（实测速率 ±）
    const double wanYi = 1e12;
    double lo = 0.0, hi = 0.0;
    if (estOk) {
        lo = est.totalSec * 0.8;
        hi = est.totalSec * 1.35;
    }
    const QString blocks = estOk
                               ? slime::ui::t("ui.53365ea16089")
                                     .arg(static_cast<double>(est.areaChunks) / wanYi, 0, 'f', 1)
                               : slime::ui::t("ui.30d6b22e0eb7");
    QString dur;
    if (estOk) {
        dur = formatDurationRange(lo, hi);
    } else {
        dur = slime::ui::t("ui.16ffb883cad6");
    }
    QString extra;
    if (estOk) {
        extra = slime::ui::t("ui.a67212609609")
                    .arg(static_cast<long long>(est.scanRate))
                    .arg(est.matchRate > 0 ? QStringLiteral("%1").arg(static_cast<long long>(est.matchRate))
                                           : QStringLiteral("—"))
                    .arg(est.chunkConcurrency);
        if (est.matchWallSec > est.scanWallSec * 2) {
            extra += slime::ui::t("ui.b3ee7232db57");
        } else {
            extra += slime::ui::t("ui.0a5778f3f8fa")
                         .arg(formatDurationRange(est.scanWallSec * 0.8, est.scanWallSec * 1.35))
                         .arg(est.matchWallSec > 0
                                  ? formatDurationRange(est.matchWallSec * 0.8, est.matchWallSec * 1.35)
                                  : slime::ui::t("ui.c5eee9b92171"));
        }
    } else {
        extra = slime::ui::t("ui.caa562b17b3e");
    }
    const QString ver = java ? slime::ui::t("ui.8accf0fe2eed") : slime::ui::t("ui.618fd37c0c01");
    QString refNote;
    if (java) {
        refNote = slime::ui::t("ui.7658cb7ec35c");
    } else {
        refNote = slime::ui::t("ui.6262634bbb31");
    }

    const auto ans = QMessageBox::question(
        this, slime::ui::t("ui.8be5939d9ce1"),
        slime::ui::t("ui.8ea43b1d89a3")
            .arg(ver).arg(blocks).arg(dur).arg(extra).arg(refNote));
    if (ans != QMessageBox::Yes) {
        Logger::info("整图搜索被用户取消");
        return false;
    }
    if (applyRange) {
        applyPreset(halfSpan);
        m_wholeMapConfirmed = true;
    }
    Logger::info("用户确认整图搜索（±" + std::to_string(halfSpan) + "，估算 " +
                 (estOk ? std::to_string(static_cast<long long>(est.totalSec)) : std::string("n/a")) + "s）");
    return true;
}

void MainWindow::clampRangeToBound()
{
    const int bound = versionBound();
    bool clamped = false;
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        const int v = s->value();
        if (v > bound) {
            s->setValue(bound);
            clamped = true;
        } else if (v < -bound) {
            s->setValue(-bound);
            clamped = true;
        }
    }
    if (clamped) {
        const QString ver = m_w->bedrockRadio->isChecked() ? slime::ui::t("ui.618fd37c0c01") : QStringLiteral("Java");
        const int blocks = bound * 16;
        setStatus(slime::ui::tr(slime::ui::t("ui.2e12306cd3ab"))
                      .arg(ver).arg(bound).arg(blocks));
        if (!m_uiQuiet) {
            QMessageBox::information(this, slime::ui::t("ui.a76951f982d6"),
                                     slime::ui::t("ui.fb99a1d6fb34")
                                         .arg(ver).arg(bound).arg(blocks));
        }
        Logger::warning("搜索范围超出边界，已自动截断到 ±" + std::to_string(bound));
    }
}

// ---------------- 图案 ----------------
void MainWindow::editPattern()
{
    PatternEditor dlg(this);
    if (m_patternSet && !m_pattern.empty()) dlg.setInitial(m_pattern);
    if (dlg.exec() == QDialog::Accepted) {
        m_pattern = dlg.pattern();
        m_patternSet = true;
        onModeChanged();
        Logger::info("图案已更新：" + std::to_string(m_pattern.rows()) + "x" + std::to_string(m_pattern.cols()) +
                     "，有效格 " + std::to_string(m_pattern.activeCount()));
    }
}

// ---------------- 缓存 ----------------
// v3.3.3（任务书 2.2）：状态标签在「缓存创建/增量合并/启动自动加载/导入/清除/勾选变化」后
// 一律立即刷新；文案按要求统一为「缓存已加载，共 N 个区块」，并额外给出覆盖区域与体积。
// 另外区分「磁盘上已有缓存文件但尚未载入内存」的情形，便于用户理解为何要重启/导入。
void MainWindow::updateCacheStatusUi()
{
    if (!m_w || !m_w->cacheStatus) return;
    const bool bedrock = m_w->bedrockRadio->isChecked();
    if (!bedrock) {
        m_w->cacheStatus->setText(slime::ui::t("ui.705f907ae91f"));
        return;
    }
    if (m_cache->hasData()) {
        m_w->cacheStatus->setText(slime::ui::t("ui.a13898466222")
                                      .arg(m_cache->entryCount())
                                      .arg(m_cache->ranges().size())
                                      .arg(m_cache->sizeBytes() / (1024 * 1024)));
        return;
    }
    std::error_code ec;
    if (!m_cacheFile.empty() && std::filesystem::exists(m_cacheFile, ec)) {
        m_w->cacheStatus->setText(
            slime::ui::t("ui.1184ea9cb9f9")
                .arg(QString::fromStdString(m_cacheFile.filename().string())));
    } else {
        m_w->cacheStatus->setText(slime::ui::t("ui.23c0ca294841"));
    }
}

void MainWindow::importCache()
{
    // v3.3.1（任务书 2.1）：过滤器与保存扩展名一致 —— 新版 .slimecache 优先，旧版 .cache 兼容，
    // 并提供「所有文件」兜底，避免缓存文件存在却在对话框里看不到。
    // v3.3.2（任务书 3.2/3.3）：对话框**定位到缓存目录（程序目录）并预选当前缓存文件**，
    // 保证「刚保存的缓存」不仅可见（过滤器匹配），而且已经选中（直接点“打开”即可加载）。
    // 修复前的实现（v3.1.1~v3.3.0）第三个参数为空 → 对话框落在进程工作目录/上次目录，
    // 缓存文件虽在程序目录却看不到，这正是任务书描述的「缓存文件存在但选不中」。
    QFileDialog dlg(this, slime::ui::t("ui.834c34e48ed2"),
                    QString::fromStdWString(appDirPath().wstring()));
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilters(cacheImportFilter().split(QStringLiteral(";;")));
    dlg.selectFile(QString::fromStdString(kCacheFileName)); // 预选 slime_cache.slimecache
    if (dlg.exec() != QDialog::Accepted) return;

    const QStringList chosen = dlg.selectedFiles();
    if (chosen.isEmpty()) return;
    const QString path = chosen.isEmpty() ? QString() : chosen.first();
    if (path.isEmpty()) return;

    if (path.endsWith(QStringLiteral(".pkl"), Qt::CaseInsensitive) ||
        path.endsWith(QStringLiteral(".pickle"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, slime::ui::t("ui.a042ea99a76c"),
                             slime::ui::t("ui.e4b8900b55f6"));
        return;
    }

    importCacheFromPath(path, !m_uiQuiet, !m_uiQuiet);
}

// v3.3.8.1（任务书 1.3）：大缓存导入移到**后台线程** + 进度条（界面不冻结）。
// 读取本身不可中断，「取消」= 丢弃已读结果、不合并（如实反映可取消范围）。
// showProgressDialog=false（自检静默模式）时同步读取，避免模态框阻塞自动化流程。
void MainWindow::importCacheFromPath(const QString& path, bool showProgressDialog, bool showResultDialogs)
{
    const auto srcPath = std::filesystem::path(path.toStdWString());
    std::error_code ec;
    const auto fileBytes = std::filesystem::file_size(srcPath, ec);
    auto loaded = std::make_shared<CacheManager>();
    auto progDone = std::make_shared<std::atomic<int64_t>>(0);
    auto progTotal = std::make_shared<std::atomic<int64_t>>(1);
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto loadOA = std::make_shared<std::atomic<bool>>(false);
    auto loadErr = std::make_shared<std::string>();
    auto cancelReq = std::make_shared<std::atomic<bool>>(false);
    const auto runLoad = [&]() {
        std::string e;
        const bool r = loaded->loadFile(srcPath, true, e, [progDone, progTotal](int64_t d, int64_t tt) {
            progDone->store(d, std::memory_order_relaxed);
            progTotal->store(tt > 0 ? tt : 1, std::memory_order_relaxed);
        });
        *loadErr = e;
        loadOA->store(r, std::memory_order_relaxed);
        finished->store(true, std::memory_order_relaxed);
    };
    std::thread worker;
    if (!showProgressDialog) {
        runLoad();
    } else {
        QProgressDialog* pd = new QProgressDialog(
            slime::ui::t("ui.5153c1d9f401").arg(fileBytes / (1024.0 * 1024.0), 0, 'f', 1),
            slime::ui::t("ui.4d0b4688c787"), 0, 1000, this);
        pd->setWindowTitle(slime::ui::t("ui.00e905651e85"));
        pd->setWindowModality(Qt::WindowModal);
        pd->setMinimumDuration(0);
        pd->setAutoClose(false);
        pd->setAutoReset(false);
        pd->setValue(0);
        pd->show();
        worker = std::thread(runLoad);
        QTimer poll;
        connect(&poll, &QTimer::timeout, this, [&]() {
            const int64_t tt = std::max<int64_t>(1, progTotal->load(std::memory_order_relaxed));
            const int64_t d = progDone->load(std::memory_order_relaxed);
            pd->setValue(static_cast<int>(std::min<int64_t>(1000, d * 1000 / tt)));
        });
        poll.start(100);
        while (!finished->load(std::memory_order_relaxed)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            QThread::msleep(10);
            if (pd->wasCanceled()) {
                cancelReq->store(true, std::memory_order_relaxed);
                pd->setCancelButton(nullptr);
                pd->setLabelText(slime::ui::t("ui.8a98a48be088"));
            }
        }
        poll.stop();
        worker.join();
        pd->close();
        pd->deleteLater();
    }
    if (cancelReq->load()) {
        setStatus(slime::ui::tr(slime::ui::t("ui.9d715e50a2c7")));
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.1bfb190be1fa")));
        return;
    }
    if (!loadOA->load()) {
        if (showResultDialogs) {
            QMessageBox::critical(this, slime::ui::t("ui.a01a8d53938f"), QString::fromStdString(*loadErr));
        }
        Logger::error("缓存导入失败: " + *loadErr + "（来源 " + path.toStdString() + "）");
        return;
    }
    const size_t beforeR = m_cache->ranges().size();
    const int64_t beforeE = m_cache->entryCount();
    m_cache->mergeFrom(*loaded); // v3.3.8.1：UI 线程合并（位图合并 = 一次置位遍历，速度快）
    loaded.reset();
    m_cache->pruneToFit(cacheMaxBytes()); // 容量限制
    std::string serr;
    if (!m_cache->saveFile(m_cacheFile, serr)) {
        if (showResultDialogs) {
            QMessageBox::warning(this, slime::ui::t("ui.5521e368d87e"), slime::ui::t("ui.de895e437b0e").arg(QString::fromStdString(serr)));
        }
        Logger::warning("缓存导入后保存失败: " + serr + "（目标 " + m_cacheFile.string() + "）");
    } else {
        // v3.3.2：记录落盘路径，便于用户与排障时定位「缓存文件到底在哪」
        Logger::info("缓存已保存（导入合并）: " + m_cacheFile.string());
    }
    updateCacheStatusUi();
    if (showResultDialogs) {
        QMessageBox::information(this, slime::ui::t("ui.425516b37306"),
                                 slime::ui::t("ui.be215de45cd3")
                                     .arg(m_cache->ranges().size() - beforeR)
                                     .arg(m_cache->entryCount() - beforeE)
                                     .arg(m_cache->ranges().size())
                                     .arg(m_cache->entryCount()));
    }
    Logger::info("缓存导入合并完成，共 " + std::to_string(m_cache->ranges().size()) + " 区域 / " +
                 std::to_string(m_cache->entryCount()) + " 区块（来源 " + path.toStdString() + "）");
}

void MainWindow::clearCache()
{
    if (QMessageBox::question(this, slime::ui::t("ui.b56d9ac6c5a0"),
                              slime::ui::t("ui.b41f2a3ce538")) != QMessageBox::Yes) {
        return;
    }
    m_cache->clear();
    std::error_code ec;
    std::filesystem::remove(m_cacheFile, ec);
    // v3.3.1：连同旧版扩展名文件一起清除（否则下次启动会把它当旧缓存重新加载）
    std::filesystem::remove(appDirPath() / kCacheLegacyFileName, ec);
    updateCacheStatusUi();
    Logger::info("缓存已清除");
}

// ---------------- 坐标转换 ----------------
void MainWindow::convertBlockCoords()
{
    bool okX = false, okZ = false;
    const qlonglong bx = m_w->convX->text().trimmed().toLongLong(&okX);
    const qlonglong bz = m_w->convZ->text().trimmed().toLongLong(&okZ);
    if (!okX || !okZ) {
        QMessageBox::warning(this, slime::ui::t("ui.05118c2a651d"), slime::ui::t("ui.943b5f18375d"));
        return;
    }
    auto floorDiv16 = [](qlonglong v) {
        qlonglong q = v / 16;
        if (v < 0 && v % 16 != 0) --q;
        return q;
    };
    const qlonglong cx = floorDiv16(bx);
    const qlonglong cz = floorDiv16(bz);
    if (cx < -2147483646LL || cx > 2147483646LL || cz < -2147483646LL || cz > 2147483646LL) {
        QMessageBox::warning(this, slime::ui::t("ui.05118c2a651d"), slime::ui::t("ui.84fbf99670a1"));
        return;
    }
    m_w->startX->setValue(static_cast<int>(cx));
    m_w->startZ->setValue(static_cast<int>(cz));
    setStatus(slime::ui::tr(slime::ui::t("ui.ea3440531233"))
                  .arg(bx).arg(bz).arg(cx).arg(cz));
    Logger::info("坐标转换: 方块(" + std::to_string(bx) + "," + std::to_string(bz) + ") -> 区块(" +
                 std::to_string(cx) + "," + std::to_string(cz) + ")");
}

// ---------------- 鸣谢（v3.7.5，用户要求） ----------------
void MainWindow::updateTelemetryNotice()
{
    if (!m_telemetryNotice || !m_telemetry) return;
    const QString text = m_telemetry->enabled()
                             ? slime::ui::t("ui.c3036df85648")
                             : slime::ui::t("ui.ae50bb7256dc");
    m_telemetryNotice->setText(QStringLiteral("<a href=\"telemetry\">%1</a>").arg(text));
}

void MainWindow::showTelemetryDialog()
{
    if (!m_telemetry) return;
    if (m_uiQuiet) {
        addFeedback(2, slime::ui::t("ui.6a59df88b543"));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(slime::ui::t("ui.6034809d2f3b"));
    dlg.setMinimumWidth(560);
    auto* lay = new QVBoxLayout(&dlg);

    auto* intro = new QLabel(
        slime::ui::t("ui.ca9adb9a211a"), &dlg);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto* data = new QLabel(
        slime::ui::t("ui.19e2f2997722"), &dlg);
    data->setWordWrap(true);
    lay->addWidget(data);

    auto* privacy = new QLabel(
        slime::ui::t("ui.95f20e8a62c3"), &dlg);
    privacy->setOpenExternalLinks(false);
    privacy->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(privacy, &QLabel::linkActivated, this, [](const QString&) {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/PillerBean/SlimeFinder/blob/main/docs/privacy.md")));
    });
    lay->addWidget(privacy);

    auto* enabled = new QCheckBox(slime::ui::t("ui.2ac72064e5e4"), &dlg);
    enabled->setChecked(m_telemetry->enabled());
    lay->addWidget(enabled);

    auto* epNote = new QLabel(slime::ui::t("ui.af049acfc3f2"), &dlg);
    epNote->setStyleSheet(QStringLiteral("color:#808080;font-size:11px;"));
    lay->addWidget(epNote);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;
    m_telemetry->setSettings(enabled->isChecked(), m_telemetry->endpoint());
    updateTelemetryNotice();
    addFeedback(2, enabled->isChecked() ? slime::ui::t("ui.ce83b3fe513c")
                                        : slime::ui::t("ui.b7b5afae98c6"));
    setStatus(enabled->isChecked() ? slime::ui::t("ui.3c674829def4")
                                   : slime::ui::t("ui.b7b5afae98c6"));
}

void MainWindow::showCredits()
{
    QDialog dlg(this);
    dlg.setWindowTitle(slime::ui::t("ui.c5583bc7e151"));
    dlg.setMinimumWidth(520);
    auto* lay = new QVBoxLayout(&dlg);

    auto* intro = new QLabel(slime::ui::t("ui.cb55d487c124"), &dlg);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto* list = new QTextBrowser(&dlg);
    list->setHtml(slime::ui::t("ui.b857b5d1e865"));
    list->setMinimumHeight(140);
    list->setOpenExternalLinks(false);
    lay->addWidget(list, 1);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    if (QPushButton* ok = btns->button(QDialogButtonBox::Ok)) ok->setText(slime::ui::t("ui.6c14bd7f6f9e"));
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(btns);
    dlg.exec();
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.00926703ef18")));
}

// ---------------- 联系我们（v3.7.5：只讲反馈渠道，与「关于」差异化） ----------------
void MainWindow::showContact()
{
    QDialog dlg(this);
    dlg.setWindowTitle(slime::ui::t("ui.152d3db75889"));
    dlg.setMinimumWidth(460);
    auto* lay = new QVBoxLayout(&dlg);
    auto* info = new QLabel(slime::ui::t("ui.8be6653c4033")
                                .arg(QLatin1String(kContactEmail)));
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info->setWordWrap(true);
    lay->addWidget(info);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(btns);
    dlg.exec();
}

// ---------------- 搜索 ----------------
// v3.6.6（用户要求）：成功率预测 —— 目标被找到「至少一次」的概率
//   图案模式：每个位置命中概率 = 0.1^绿格数 × 0.9^白格数（史莱姆区块约占 10%）
//   密度模式：窗口内命中数 ≥ ceil(最小密度×格数) 的概率（二项分布尾概率）
//   位置数：范围内可放置窗口的位置数（方向锁定时按 1/4 计）

