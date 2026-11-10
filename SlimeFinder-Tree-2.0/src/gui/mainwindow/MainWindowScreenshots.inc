// v3.12.0：从 MainWindow.cpp 拆出的截图/证据生成实现。

void MainWindow::captureUiScreenshots(const QString& prefix)
{
    if (prefix.isEmpty()) return;
    const auto shoot = [&](const QString& tag) {
        QApplication::processEvents(QEventLoop::AllEvents, 60);
        saveWindowShot(prefix, tag);
        if (m_w->mainScroll && m_w->mainScroll->horizontalScrollBar()) {
            const int hm = m_w->mainScroll->horizontalScrollBar()->maximum();
            std::fprintf(stderr, "[UI自检]   截图 %s：设置区横滚动上限=%d（应为 0）\n",
                         tag.toUtf8().constData(), hm);
        }
    };

    // 1~3) 三个规定窗口尺寸
    for (const QSize sz : { QSize(1280, 860), QSize(1000, 650), QSize(1920, 1080) }) {
        resize(sz);
        QApplication::processEvents(QEventLoop::AllEvents, 80);
        shoot(QStringLiteral("size-%1x%2").arg(sz.width()).arg(sz.height()));
    }
    resize(1280, 860);
    QApplication::processEvents(QEventLoop::AllEvents, 80);

    // 4~5) 水平 splitter 拖到最左 / 最右
    if (m_hSplit) {
        const int w = width();
        m_hSplit->setSizes(QList<int>() << 320 << (w - 320 - 320) << 320);   // 左栏最小（拖到最左）
        shoot(QStringLiteral("splitter-left"));
        m_hSplit->setSizes(QList<int>() << (w - 320 - 320) << 320 << 320);   // 右栏最小（拖到最右）
        shoot(QStringLiteral("splitter-right"));
        m_hSplit->setSizes(QList<int>() << 380 << (w - 380 - 360) << 360);
        QApplication::processEvents(QEventLoop::AllEvents, 60);
    }

    // 6) 右列垂直 splitter：过程面板拖到最高 / 恢复默认
    if (m_rightSplit) {
        const int hgt = m_rightSplit->height();
        m_rightSplit->setSizes(QList<int>() << 200 << (std::max)(120, hgt - 200));
        shoot(QStringLiteral("splitter-bottom-max"));
        m_rightSplit->setSizes(QList<int>() << 560 << 240);
        QApplication::processEvents(QEventLoop::AllEvents, 60);
    }

    // 7) 底部四个分类各一张（v3.3.6：左竖排导航 + 右内容区）
    if (m_nav && m_stack) {
        static const char* const kShotTags[4] = { "progress", "feedback", "log", "command" };
        for (int i = 0; i < m_nav->count() && i < m_stack->count(); ++i) {
            selectBottomCategory(i);
            shoot(QStringLiteral("nav-%1-%2").arg(i).arg(QString::fromUtf8(kShotTags[i])));
        }
        selectBottomCategory(0);
    }

    // 7b) v3.4.8：左侧导航各页截图（搜索 / 缓存构建 / 设置）
    if (m_pageStack && m_navButtons.size() == kNavCount) {
        static const char* const kPageTags[kNavCount] = { "search", "build", "settings" };
        for (int i = 0; i < kNavCount; ++i) {
            showPage(i);
            shoot(QStringLiteral("page-%1-%2").arg(i).arg(QString::fromUtf8(kPageTags[i])));
        }
        showPage(kPageSearch);
    }

    // 8) 每个菜单展开（菜单为独立弹窗，单独 grab 菜单部件本身）
    const auto shootMenu = [&](QMenu* menu, const QString& tag) {
        if (!menu) return;
        menu->popup(QPoint(20, 40));
        QApplication::processEvents(QEventLoop::AllEvents, 80);
        const QPixmap pm = menu->grab();
        const QString path = QStringLiteral("%1_menu-%2.png").arg(prefix, tag);
        const bool Ok = pm.save(path);
        std::fprintf(stderr, "[UI自检]   菜单截图 %s: %s (%dx%d, %d 项)\n", Ok ? "已保存" : "保存失败",
                     path.toUtf8().constData(), pm.width(), pm.height(),
                     static_cast<int>(menu->actions().size()));
        menu->hide();
        QApplication::processEvents(QEventLoop::AllEvents, 30);
    };
    shootMenu(m_menuFile, QStringLiteral("file"));
    shootMenu(m_menuEdit, QStringLiteral("edit"));
    shootMenu(m_menuView, QStringLiteral("view"));
    shootMenu(m_menuTools, QStringLiteral("tools"));
    shootMenu(m_menuHelp, QStringLiteral("help"));

    // 9) v3.3.6（任务书 5.7）：深色主题截图（默认尺寸 + 四个分类 + 设置对话框 5 个标签页）
    {
        const QString keepTheme = m_settings.theme;
        m_settings.theme = QStringLiteral("dark");
        applyThemeFromSettings();
        QApplication::processEvents(QEventLoop::AllEvents, 90);
        resize(1280, 860);
        QApplication::processEvents(QEventLoop::AllEvents, 90);
        shoot(QStringLiteral("dark-default"));

        if (m_nav && m_stack) {
            static const char* const kDarkTags[4] = { "progress", "feedback", "log", "command" };
            for (int i = 0; i < m_nav->count() && i < m_stack->count(); ++i) {
                selectBottomCategory(i);
                shoot(QStringLiteral("dark-nav-%1-%2").arg(i).arg(QString::fromUtf8(kDarkTags[i])));
            }
            selectBottomCategory(0);
        }
        {
            SettingsDialog dlg(m_settings, this);
            dlg.setValues(m_settings);
            dlg.resize(660, 560);
            dlg.show();
            QApplication::processEvents(QEventLoop::AllEvents, 140);
            const QString p = QStringLiteral("%1_dark-settings-general.png").arg(prefix);
            std::fprintf(stderr, "[UI自检]   深色设置对话框截图%s: %s\n",
                         dlg.grab().save(p) ? "已保存" : "保存失败", p.toUtf8().constData());
            if (dlg.tabs()) {
                for (int i = 1; i < dlg.tabs()->count(); ++i) {
                    dlg.tabs()->setCurrentIndex(i);
                    QApplication::processEvents(QEventLoop::AllEvents, 90);
                    QString name = dlg.tabs()->tabText(i);
                    name.replace(QStringLiteral("/"), QStringLiteral("-"));
                    const QString pi = QStringLiteral("%1_dark-settings-tab%2-%3.png").arg(prefix).arg(i).arg(name);
                    std::fprintf(stderr, "[UI自检]   深色设置页截图%s: %s\n",
                                 dlg.grab().save(pi) ? "已保存" : "保存失败", pi.toUtf8().constData());
                }
            }
            dlg.hide();
        }
        m_settings.theme = keepTheme;
        applyThemeFromSettings();
        resize(1280, 860);
        QApplication::processEvents(QEventLoop::AllEvents, 80);
    }
}

// v3.3.4：种子输入框高亮闪烁（任务书 4.2：点「去填写」时高亮）
// ============================================================================
// v3.3.5（Bug 2）：工具 → GPU 状态 —— 复用与搜索完全相同的探测路径
//   * 未探测过则先 ensureGpuScanOnce()（与勾选 GPU 时同一条路径）
//   * 显示设备名、显存、计算单元、实测吞吐率（CPU vs GPU）+ 「不一定更快」提醒
//   * v3.3.6（任务书 Bug 2）：不再显示任何阈值/交叉点结论
// ============================================================================
// v3.5.2（任务 1）：挂机点（环形覆盖）
//   范围优先取「最近一次搜索结果的包围盒」（向外扩 2 区块），没有结果时用当前搜索范围。
// v3.5.5：让地图尽量接近正方形（宽度决定高度；结果列表折叠时地图占更多高度）
