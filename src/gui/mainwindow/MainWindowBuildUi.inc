// v3.12.0：从 MainWindow.cpp 拆出的主界面/页面构建实现。

void MainWindow::buildUi()
{
    // ---- 顶部：菜单栏 / 工具栏（QMainWindow 原生）----
    buildMenus();
    buildToolBar();

    // ---- 中央部件 ----
    auto* central = new QWidget(this);
    auto* rootLay = new QVBoxLayout(central);
    rootLay->setContentsMargins(4, 4, 4, 4);
    rootLay->setSpacing(0);
    setCentralWidget(central);

    // ================= v3.3.6（任务书 调整 1）：工具栏下方的进度行 =================
    // 位置：紧贴工具栏下方、横跨整个窗口宽度；高度约 28px；
    // 内容：进度条 + 百分比 + 当前阶段文字（已用时/预计剩余见状态栏，明细见「进度」分类流水）。
    m_progressRowWidget = new QWidget(central);
    m_progressRowWidget->setObjectName(QStringLiteral("progressRow"));
    m_progressRowWidget->setFixedHeight(34); // v3.4.0（原型）：进度行 34px
    auto* progRow = new QHBoxLayout(m_progressRowWidget);
    progRow->setContentsMargins(8, 2, 8, 2);
    progRow->setSpacing(8);
    m_progressRow = progRow; // 自检断言「同一行 + spacing==8」用
    progRow->addWidget(new QLabel(slime::ui::t("ui.8aa84f452a9b"), m_progressRowWidget));
    m_w->progress = new QProgressBar(m_progressRowWidget);
    m_w->progress->setRange(0, 1000);
    m_w->progress->setValue(0);
    m_w->progress->setMinimumWidth(320);
    m_w->progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // v3.3.7.2（任务书 1.1）：关闭进度条**自身**的文字（默认 textVisible=true、格式 %p%），
    // 否则它会与右侧 progressPercent 标签同时画出百分比（用户截图实测「25% 25%」）。
    // 单一数据源：百分比只由 progressPercent 显示。
    m_w->progress->setTextVisible(false);
    progRow->addWidget(m_w->progress, 1);
    m_w->progressPercent = new QLabel(QStringLiteral("0%"), m_progressRowWidget);
    m_w->progressPercent->setMinimumWidth(40);
    progRow->addWidget(m_w->progressPercent);
    m_w->progressStage = new QLabel(slime::ui::t("ui.b796f2d4ca85"), m_progressRowWidget);
    m_w->progressStage->setWordWrap(false);
    // v3.3.9（任务书 3.3 根因）：阶段标签此前是 Expanding + stretch=2 ⇒ 窗口变宽时它吃掉大部分
    // 新增宽度，进度条几乎不变（用户实测「没变化」）。改为 Preferred 且不参与伸展，只留一个上限
    // 宽度，剩余宽度**全部**给进度条（期望效果：进度条占满可用宽度）。
    m_w->progressStage->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_w->progressStage->setMaximumWidth(520);
    progRow->addWidget(m_w->progressStage, 0);
    // v3.10.2（用户要求）：速率单位切换（自动 / 万/秒 / 亿/秒）—— 放在进度行，避免工具栏溢出
    progRow->addSpacing(6);
    progRow->addWidget(new QLabel(slime::ui::t("ui.602285707fd7"), m_progressRowWidget));
    m_w->rateUnitCombo = new QComboBox(m_progressRowWidget);
    m_w->rateUnitCombo->addItem(slime::ui::t("ui.4afad877551a"));
    m_w->rateUnitCombo->addItem(slime::ui::t("ui.ee266fcea944"));
    m_w->rateUnitCombo->addItem(slime::ui::t("ui.2d629d28eb4c"));
    m_w->rateUnitCombo->setCurrentIndex(std::clamp(m_settings.rateUnit, 0, 2));
    m_w->rateUnitCombo->setFixedWidth(76);
    m_w->rateUnitCombo->setToolTip(slime::ui::t("ui.38a03968acf8"));
    connect(m_w->rateUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_settings.rateUnit = std::clamp(idx, 0, 2);
        if (m_w->progressInfo) m_w->progressInfo->setToolTip(m_w->progressInfo->text());
    });
    progRow->addWidget(m_w->rateUnitCombo);
    // v3.4.0（原型）：阶段尾（② 匹配　③ 排序）——与阶段短语并列展示三段式阶段
    // ================= v3.9.15：顶部全局设置栏（平台 / 游戏版本 / 世界种子） =================
    //   把这些「每次搜索都要确认」的全局项从左侧参数栏提到顶部，左侧只放结果要求与搜索参数。
    m_topBar = new QWidget(central);
    m_topBar->setObjectName(QStringLiteral("topSettingsBar"));
    m_topBar->setFixedHeight(36);
    m_topBarLay = new QHBoxLayout(m_topBar);
    m_topBarLay->setContentsMargins(8, 2, 8, 2);
    m_topBarLay->setSpacing(8);
    rootLay->addWidget(m_topBar);

    rootLay->addWidget(m_progressRowWidget);

    // v3.4.1：标签页条已移除 —— 页面切换**只依赖左导航**（下方 navColumn）
    auto* midRow = new QWidget(central);
    auto* midLay = new QHBoxLayout(midRow);
    midLay->setContentsMargins(0, 0, 0, 0);
    midLay->setSpacing(0);

    // 左导航：96px，图标 + 文字，选中 = 整块草绿（与标签页同源，见 showPage）
    m_navColumn = new QWidget(midRow);
    m_navColumn->setObjectName(QStringLiteral("navColumn"));
    m_navColumn->setFixedWidth(56);   // v3.9.15：导航收窄，让出地图宽度
    auto* navLay = new QVBoxLayout(m_navColumn);
    navLay->setContentsMargins(0, 4, 0, 0);
    navLay->setSpacing(0);
    // v3.4.8（用户要求）：导航项 —— 搜索 / 缓存构建 / 设置（更新历史已移除）
    //   「缓存构建」独立成一项后，构建页只放构建相关内容：不再要求选游戏版本、填种子。
    static_assert(kNavCount == 3, "导航项数需与图标表一致");
    for (int i = 0; i < kNavCount; ++i) {
        // v3.4.1：导航项改为「上图案下文字」，图标放大到 24
        auto* b = new QToolButton(midRow);
        b->setCheckable(true);
        b->setProperty("navBtn", true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setIcon(v340NavIcon(kNavIconKind[i]));
        b->setIconSize(QSize(24, 24));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setFixedWidth(56);                       // v3.9.15：导航收窄（原 96）
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        static const char* const kNavText[kNavCount] = { "ui.nav.search", "ui.nav.build", "ui.nav.settings" };
        b->setText(slime::ui::t(kNavText[i]));
        {
            QFont navFont = b->font();   // v3.9.15：56px 宽需更小字号才能放下 4 个字
            navFont.setPointSizeF(7.5);
            b->setFont(navFont);
        }
        connect(b, &QPushButton::clicked, this, [this, i]() { showPage(i); });
        navLay->addWidget(b);
        m_navButtons.push_back(b);
    }
    navLay->addStretch(1);   // v3.10.2：底部图标条已移除（过程面板改在右列标题栏操作）
    midLay->addWidget(m_navColumn);
    midLay->addStretch(1); // v3.4.2：midRow 只承载左列（导航 + 图标条），页面栈放进分割器

    m_pageStack = new QStackedWidget(central);

    m_vSplit = new QSplitter(Qt::Vertical, central);
    m_vSplit->setChildrenCollapsible(false);
    m_hSplit = new QSplitter(Qt::Horizontal);
    m_hSplit->setChildrenCollapsible(false);
    // v3.3.6（任务书 4.7）：分割条样式由 ThemeManager 的浅色/深色 QSS 统一提供
    // （内联样式会覆盖全局 QSS，导致深色模式下分割条仍是浅色），此处只设置宽度。
    m_vSplit->setHandleWidth(5); // v3.4.0：竖直分割条同时充当停靠面板抓手（5px）
    m_hSplit->setHandleWidth(4);
    m_vSplit->addWidget(m_pageStack); // 上半：页面栈
    // v3.4.2（参考 PyCharm）：左列（导航 + 图标条）横跨「内容 + 停靠面板」的整窗高度，
    // 因此展开/收起底部面板时，左下角的图标条不会被顶起或推下。
    {
        auto* bodyRow = new QWidget(central);
        auto* bodyLay = new QHBoxLayout(bodyRow);
        bodyLay->setContentsMargins(0, 0, 0, 0);
        bodyLay->setSpacing(0);
        bodyLay->addWidget(midRow);
        bodyLay->addWidget(m_vSplit, 1);
        rootLay->addWidget(bodyRow, 1);
    }

    // ================= 左：设置面板（QScrollArea + 6 分组） =================
    auto* leftPanel = new QWidget(m_hSplit);
    // v3.11.2：英文/繁体文案更长；窄窗下左栏加宽，避免 QScrollArea 横向滚动截断文字。
    const bool wideText = slime::ui::isEnglish() || slime::ui::isTraditional();
    leftPanel->setMinimumWidth(wideText ? 480 : 360);
    auto* leftLay = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);

    auto* mainScroll = new QScrollArea(leftPanel);
    mainScroll->setWidgetResizable(true);
    mainScroll->setFrameShape(QFrame::NoFrame);
    mainScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mainScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* groupsHost = new QWidget(mainScroll);
    auto* groupsLay = new QVBoxLayout(groupsHost);
    groupsLay->setContentsMargins(2, 8, 2, 8); // v3.4.4：参数列上下留白（缓解标题叠字）
    groupsLay->setSpacing(10);         // v3.4.4：卡片间距加大，标题带不再互相挤
    mainScroll->setWidget(groupsHost);
    leftLay->addWidget(mainScroll);
    m_w->mainScroll = mainScroll;
    m_hSplit->addWidget(leftPanel);

    // ================= v3.10.0：三纵列布局 =================
    //   左：参数/结果要求   中：地图   右：结果 + 过程
    //   地图从右列拆出来独立成列后，结果列表高度不再挤压地图。
    m_mapColumn = new QWidget(m_hSplit);
    m_mapColumn->setObjectName(QStringLiteral("mapColumn"));
    m_mapColumn->setMinimumWidth(wideText ? 220 : 260);
    m_mapColLay = new QVBoxLayout(m_mapColumn);
    m_mapColLay->setContentsMargins(0, 0, 0, 0);
    m_mapColLay->setSpacing(0);

    // ---- v3.9.15：原「游戏设置」分组已上移到顶部全局设置栏（平台 / 游戏版本 / 世界种子）----
    if (m_topBarLay) {
        // v3.10.12.13：顶栏切成左右两半：左半放平台/版本/种子，种子框右端正好到窗口中线；
        //   右半放匿名统计公告（样式与普通标签区分）。
        auto* topLeft = new QWidget(m_topBar);
        auto* topLeftLay = new QHBoxLayout(topLeft);
        topLeftLay->setContentsMargins(0, 0, 0, 0);
        topLeftLay->setSpacing(8);
        auto* topRight = new QWidget(m_topBar);
        auto* topRightLay = new QHBoxLayout(topRight);
        topRightLay->setContentsMargins(0, 0, 0, 0);
        topRightLay->setSpacing(0);
        m_topBarLay->addWidget(topLeft, 1);
        m_topBarLay->addWidget(topRight, 1);

        m_w->javaRadio = new QRadioButton(slime::ui::t("ui.8accf0fe2eed"), m_topBar);
        m_w->bedrockRadio = new QRadioButton(slime::ui::t("ui.618fd37c0c01"), m_topBar);
        m_w->javaRadio->setChecked(true);
        topLeftLay->addWidget(new QLabel(slime::ui::t("ui.4ba273d420a7"), m_topBar));
        topLeftLay->addWidget(m_w->javaRadio);
        topLeftLay->addWidget(m_w->bedrockRadio);

        topLeftLay->addSpacing(10);
        topLeftLay->addWidget(new QLabel(slime::ui::t("ui.fc1424adcb78"), m_topBar));
        m_w->mapVersionCombo = new QComboBox(m_topBar);
        for (const auto& v : slime::javaVersionOptions())
            m_w->mapVersionCombo->addItem(QString::fromUtf8(v.label), v.id);
        m_w->mapVersionCombo->setCurrentIndex(0);
        m_w->mapVersionCombo->setToolTip(slime::ui::t("ui.9820d2a24e2e"));
        topLeftLay->addWidget(m_w->mapVersionCombo);

        topLeftLay->addSpacing(10);
        m_w->seedLabel = new QLabel(slime::ui::t("ui.df62e1c428e6"), m_topBar);
        topLeftLay->addWidget(m_w->seedLabel);
        m_w->seedEdit = new QLineEdit(m_topBar);
        m_w->seedEdit->setClearButtonEnabled(true);
        m_w->seedEdit->setText(QString());
        m_w->seedEdit->setPlaceholderText(slime::ui::t("ui.c9cebe51b762"));
        m_w->seedEdit->setToolTip(slime::ui::t("ui.e8cae526ef7d"));
        topLeftLay->addWidget(m_w->seedEdit, 1);
        m_telemetryNotice = new QLabel(topRight);
        m_telemetryNotice->setObjectName(QStringLiteral("telemetryNotice"));
        m_telemetryNotice->setTextFormat(Qt::RichText);
        m_telemetryNotice->setOpenExternalLinks(false);
        m_telemetryNotice->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        m_telemetryNotice->setStyleSheet(QStringLiteral(
            "QLabel{background:#FFF4D6;color:#7A5A00;border:1px solid #E0B85C;"
            "border-radius:4px;padding:2px 8px;}"
            "QLabel a{color:#7A5A00;text-decoration:none;}"));
        connect(m_telemetryNotice, &QLabel::linkActivated, this, [this](const QString&) { showTelemetryDialog(); });
        topRightLay->addWidget(m_telemetryNotice, 0, Qt::AlignLeft | Qt::AlignVCenter);
        topRightLay->addStretch(1);
        updateTelemetryNotice();
    }


    // ---- 分组 2：搜索范围（预设 + 范围 + 查询起点 + 坐标转换）----
    auto* gRange = new QGroupBox(slime::ui::t("ui.2cacc1b74217"), groupsHost);
    auto* rangeLay = new QVBoxLayout(gRange);
    rangeLay->setContentsMargins(6, 6, 6, 6);
    const int kMinCoord = -2147483646;
    const int kMaxCoord = 2147483646;
    auto makeSpin = [&](int def, QWidget* parent) {
        auto* s = new QSpinBox(parent);
        s->setRange(kMinCoord, kMaxCoord);
        s->setValue(def);
        s->setGroupSeparatorShown(true);
        s->setKeyboardTracking(false);
        s->setMinimumWidth(68);   // v3.3.4：限制最小值，避免撑宽左栏（340px 面板也不出横向滚动条）
        s->setMaximumWidth(120);
        return s;
    };
    auto* rangeGrid = new QGridLayout();
    m_w->minX = makeSpin(-kJavaBoundChunk, gRange);
    m_w->maxX = makeSpin(kJavaBoundChunk, gRange);
    m_w->minZ = makeSpin(-kJavaBoundChunk, gRange);
    m_w->maxZ = makeSpin(kJavaBoundChunk, gRange);
    // v3.5.16（用户要求）：范围不再填 4 个数，只填半径 —— 范围 = 以原点为中心、边长 2×半径 的正方形。
    //   四个旧输入框保留为内部状态（引擎/缓存构建/命令/预估都还读它们），只是不再显示。
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) s->setVisible(false);
    m_w->rangeRadius = makeSpin(1000, gRange);
    m_w->rangeRadius->setRange(1, 15000000);
    m_w->rangeRadius->setToolTip(slime::ui::t("ui.8d4c2b8f9d14"));
    rangeGrid->addWidget(new QLabel(slime::ui::t("ui.f290ac0a5d9d"), gRange), 0, 0);
    rangeGrid->addWidget(m_w->rangeRadius, 0, 1);
    m_w->rangeHint = new QLabel(slime::ui::t("ui.7b040a370be7"), gRange);
    m_w->rangeHint->setObjectName(QStringLiteral("topSummary"));
    rangeGrid->addWidget(m_w->rangeHint, 1, 0, 1, 4);
    rangeLay->addLayout(rangeGrid);

    auto* presetRow = new QGridLayout();
    // v3.3.4：按钮只写范围名（具体数值在 tooltip 与点击后的输入框里显示），
    // 这样 340px 的最小左栏也不会出现横向滚动条
    m_w->presetNear = new QPushButton(slime::ui::t("ui.08c2940d3842"), gRange);
    m_w->presetLocal = new QPushButton(slime::ui::t("ui.d4548049015d"), gRange);
    m_w->presetGlobal = new QPushButton(slime::ui::t("ui.a5644f4bbfef"), gRange);
    m_w->presetWorld = new QPushButton(slime::ui::t("ui.3a7839501e82"), gRange);
    m_w->presetBedrock = new QPushButton(slime::ui::t("ui.c2fa11e4787c"), gRange);
    // v3.10.2（用户反馈：GPU 变快后旧快捷范围太小）：整体上调一档
    // v3.10.12.8：GPU 提速后快捷范围再上调一档（旧值按 3.10.2 的早期速度设定）
    m_w->presetNear->setToolTip(slime::ui::t("ui.ea3036c908c7"));
    m_w->presetLocal->setToolTip(slime::ui::t("ui.0807a0194761"));
    m_w->presetGlobal->setToolTip(slime::ui::t("ui.49bc40cff907"));
    m_w->presetWorld->setToolTip(slime::ui::t("ui.41fe5ad602fd"));
    m_w->presetBedrock->setToolTip(slime::ui::t("ui.d116c4a6c576"));
    // v3.3.4：预设标签独占一行，按钮每行 3 个 —— 避免撑宽设置区（否则 400px 左栏出现横向滚动条）
    presetRow->addWidget(new QLabel(slime::ui::t("ui.143e728e020a"), gRange), 0, 0, 1, 3);
    presetRow->addWidget(m_w->presetNear, 1, 0);
    presetRow->addWidget(m_w->presetLocal, 1, 1);
    presetRow->addWidget(m_w->presetGlobal, 1, 2);
    presetRow->addWidget(m_w->presetWorld, 2, 0);
    presetRow->addWidget(m_w->presetBedrock, 2, 1);
    presetRow->setColumnStretch(3, 1);
    rangeLay->addLayout(presetRow);

    auto* startRow = new QHBoxLayout();
    startRow->addWidget(new QLabel(slime::ui::t("ui.31db8e5f5036"), gRange));
    m_w->startX = makeSpin(0, gRange);
    m_w->startX->setMaximumWidth(120);
    startRow->addWidget(m_w->startX);
    startRow->addWidget(new QLabel(QStringLiteral("Z:"), gRange));
    m_w->startZ = makeSpin(0, gRange);
    m_w->startZ->setMaximumWidth(120);
    startRow->addWidget(m_w->startZ);
    startRow->addStretch();
    rangeLay->addLayout(startRow);

    // v3.3.4：分两行 + 缩短文案（原一行最小宽 ~440px，会撑出横向滚动条）
    auto* convRow = new QHBoxLayout();
    convRow->addWidget(new QLabel(slime::ui::t("ui.8f86ddd4fe10"), gRange));
    m_w->convX = new QLineEdit(gRange);
    m_w->convX->setMinimumWidth(60);
    m_w->convX->setMaximumWidth(90);
    m_w->convX->setPlaceholderText(slime::ui::t("ui.e272b0605275"));
    m_w->convZ = new QLineEdit(gRange);
    m_w->convZ->setMinimumWidth(60);
    m_w->convZ->setMaximumWidth(90);
    m_w->convZ->setPlaceholderText(slime::ui::t("ui.d3c729e2a6b7"));
    convRow->addWidget(m_w->convX);
    convRow->addWidget(m_w->convZ);
    convRow->addStretch();
    rangeLay->addLayout(convRow);
    auto* convRow2 = new QHBoxLayout();
    m_w->convertBtn = new QPushButton(slime::ui::t("ui.16d8f74f6cbf"), gRange);
    m_w->convertBtn->setToolTip(slime::ui::t("ui.15e186ad6baa"));
    convRow2->addWidget(m_w->convertBtn);
    convRow2->addStretch();
    rangeLay->addLayout(convRow2);
    groupsLay->addWidget(gRange);

    // ---- 分组 3：搜索模式（严格/密度 + 图案 + 密度参数 + 目标数量）----
    auto* gMode = new QGroupBox(slime::ui::t("ui.5483493bb0d8"), groupsHost);
    m_w->modeGroup = gMode;
    auto* modeLayout = new QVBoxLayout(gMode);
    modeLayout->setContentsMargins(6, 6, 6, 6);

    // ===== 一级：图案 / 密度（v3.5.18 用户要求）=====
    auto* modeRow = new QHBoxLayout();
    m_w->strictRadio = new QRadioButton(slime::ui::text(slime::ui::Key::ModePattern), gMode);
    m_w->densityRadio = new QRadioButton(slime::ui::text(slime::ui::Key::ModeDensity), gMode);
    {
        auto* modeBtnGroup = new QButtonGroup(gMode);
        modeBtnGroup->addButton(m_w->strictRadio, 0);
        modeBtnGroup->addButton(m_w->densityRadio, 1);
        m_w->modeButtonGroup = modeBtnGroup;
    }
    m_w->strictRadio->setChecked(true);
    m_w->strictRadio->setToolTip(slime::ui::text(slime::ui::Key::ModePatternTip));
    m_w->densityRadio->setToolTip(slime::ui::text(slime::ui::Key::ModeDensityTip));
    modeRow->addWidget(m_w->strictRadio);
    modeRow->addWidget(m_w->densityRadio);
    modeRow->addStretch();
    modeLayout->addLayout(modeRow);

    // v3.9.11（设置重构阶段 B）：先「模式」再「方案」——方案数量与内容由模式决定。
    {
        auto* presetRow = new QHBoxLayout();
        presetRow->addWidget(new QLabel(slime::ui::t("ui.c06ee9fb0a7e"), gMode));
        m_w->presetCombo = new QComboBox(gMode);
        m_w->presetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_w->presetCombo->setMinimumContentsLength(16);
        m_w->presetCombo->setToolTip(slime::ui::t("ui.7f3854b7425e"));
        presetRow->addWidget(m_w->presetCombo, 1);
        modeLayout->addLayout(presetRow);
        m_w->presetHint = new QLabel(gMode);
        m_w->presetHint->setObjectName(QStringLiteral("presetHint"));
        m_w->presetHint->setWordWrap(true);
        m_w->presetHint->setStyleSheet(QStringLiteral("color:#606874;font-size:11px;"));
        modeLayout->addWidget(m_w->presetHint);
        QObject::connect(m_w->presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                         [this](int) { applySelectedPreset(); });
    }

    // ===== 二级：图案选项（仅图案模式显示）=====
    m_w->strictPanel = new QWidget(gMode);
    auto* strictLay = new QVBoxLayout(m_w->strictPanel);
    strictLay->setContentsMargins(12, 0, 0, 0);
    strictLay->setSpacing(4);
    m_w->lockDir = new QCheckBox(slime::ui::t("ui.4c557e552df9"), m_w->strictPanel);
    m_w->lockDir->setChecked(true);
    m_w->lockDir->setToolTip(slime::ui::t("ui.d6dbb5040c42"));
    strictLay->addWidget(m_w->lockDir);
    // v3.5.18：旧「绘制图案」按钮改为隐藏（图案编辑器已内嵌），保留对象避免其它代码空指针
    m_w->drawPattern = new QPushButton(slime::ui::t("ui.dcac1e7a2e19"), m_w->strictPanel);
    m_w->drawPattern->setVisible(false);
    auto* patRow = new QHBoxLayout();
    m_w->patternInfo = new QLabel(slime::ui::t("ui.a1b385fa0140"), m_w->strictPanel);
    patRow->addWidget(m_w->patternInfo);
    patRow->addStretch();
    strictLay->addLayout(patRow);
    // v3.5.18：图案编辑器直接内嵌（不再弹出独立窗口）
    m_w->inlinePattern = new slime::PatternEditorPanel(m_w->strictPanel);
    m_w->inlinePattern->setVisible(true);
    strictLay->addWidget(m_w->inlinePattern);
    modeLayout->addWidget(m_w->strictPanel);

    // ===== 二级：密度选项（仅密度模式显示）=====
    m_w->densityPanel = new QWidget(gMode);
    auto* densityLay = new QVBoxLayout(m_w->densityPanel);
    densityLay->setContentsMargins(12, 0, 0, 0);
    densityLay->setSpacing(4);
    auto* shapeRow = new QHBoxLayout();
    shapeRow->addWidget(new QLabel(slime::ui::t("ui.34a2f8af72f5"), m_w->densityPanel));
    m_w->shapeSquareRadio = new QRadioButton(slime::ui::text(slime::ui::Key::ShapeSquare), m_w->densityPanel);
    m_w->shapeRingRadio = new QRadioButton(slime::ui::text(slime::ui::Key::ShapeRing), m_w->densityPanel);
    m_w->shapeSquareRadio->setChecked(true);
    m_w->shapeSquareRadio->setToolTip(slime::ui::text(slime::ui::Key::ShapeSquareTip));
    m_w->shapeRingRadio->setToolTip(slime::ui::text(slime::ui::Key::ShapeRingTip));
    shapeRow->addWidget(m_w->shapeSquareRadio);
    shapeRow->addWidget(m_w->shapeRingRadio);
    shapeRow->addStretch();
    densityLay->addLayout(shapeRow);
    {
        auto* shapeGroup = new QButtonGroup(m_w->densityPanel);
        shapeGroup->addButton(m_w->shapeSquareRadio, 0);
        shapeGroup->addButton(m_w->shapeRingRadio, 1);
        m_w->shapeButtonGroup = shapeGroup;
    }

    // v3.10.9：最小密度/忽略阈值已移入「结果要求」分组（仅自定义/环形显示），此处不再重复显示
    m_w->minDensity = new QDoubleSpinBox(m_w->densityPanel);
    m_w->minDensity->setRange(0.0, 1.0);
    m_w->minDensity->setDecimals(3);
    m_w->minDensity->setSingleStep(0.05);
    m_w->minDensity->setValue(0.3);
    m_w->minDensity->setToolTip(slime::ui::t("ui.9f053e5570b1"));

    m_w->ignoreThreshold = new QCheckBox(slime::ui::t("ui.1a3b758d0fda"), m_w->densityPanel);
    m_w->ignoreThreshold->setToolTip(slime::ui::t("ui.e302a43e602a"));

    // v3.10.12.11：最小密度 / 忽略阈值改为密度面板常显（此前放在「结果要求」分组里，
    //   选择任意预设方案后就整组隐藏，用户找不到密度设置）。改动会切换为「自定义方案」。
    {
        auto* densCfgRow = new QHBoxLayout();
        densCfgRow->addWidget(new QLabel(slime::ui::t("ui.97aed4fd11b2"), m_w->densityPanel));
        densCfgRow->addWidget(m_w->minDensity);
        densCfgRow->addWidget(m_w->ignoreThreshold);
        densCfgRow->addStretch();
        densityLay->addLayout(densCfgRow);
        m_minDensityCommitted = m_w->minDensity->value();
        QObject::connect(m_w->minDensity, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         this, [this](double) { applyResultConflicts(); });
        // v3.10.12.11：输入“0”过程中 valueChanged 会连续触发；只在焦点离开/回车后才切自定义方案
        QObject::connect(m_w->minDensity, &QDoubleSpinBox::editingFinished, this, [this]() {
            if (!m_w || !m_w->minDensity || m_presetApplying) return;
            const double v = m_w->minDensity->value();
            if (v == m_minDensityCommitted) return;
            m_minDensityCommitted = v;
            markPresetCustom(slime::ui::t("ui.d67b9dd37ebe"));
        });
    }

    QObject::connect(m_w->ignoreThreshold, &QCheckBox::toggled, this, [this](bool) {
        applyResultConflicts();
        markPresetCustom(slime::ui::t("ui.4256e53c8eaf"));
    });


    // 正方形专属：网格大小 + 最小密度
    m_w->squareOpts = new QWidget(m_w->densityPanel);
    auto* squareLay = new QVBoxLayout(m_w->squareOpts);
    squareLay->setContentsMargins(12, 0, 0, 0);
    squareLay->setSpacing(4);
    auto* densRow = new QHBoxLayout();
    densRow->addWidget(new QLabel(slime::ui::t("ui.28e8f6817207"), m_w->squareOpts));
    m_w->gridSize = new QSpinBox(m_w->squareOpts);
    m_w->gridSize->setRange(1, 512);
    m_w->gridSize->setValue(5);
    m_w->gridSize->setToolTip(slime::ui::t("ui.830890653be7"));
    densRow->addWidget(m_w->gridSize);
    densRow->addStretch();
    squareLay->addLayout(densRow);
    m_w->squareOpts->setVisible(true);
    densityLay->addWidget(m_w->squareOpts);

    // 环形专属：扫描模式 + 最小密度
    m_w->ringOpts = new QWidget(m_w->densityPanel);
    auto* ringLay = new QVBoxLayout(m_w->ringOpts);
    ringLay->setContentsMargins(12, 0, 0, 0);
    ringLay->setSpacing(4);
    auto* ringModeRow = new QHBoxLayout();
    ringModeRow->addWidget(new QLabel(slime::ui::t("ui.637be2399ba9"), m_w->ringOpts));
    m_w->ringScanCombo = new QComboBox(m_w->ringOpts);
    m_w->ringScanCombo->addItem(slime::ui::text(slime::ui::Key::RingScanFast), 0);
    m_w->ringScanCombo->addItem(slime::ui::t("ui.6ff11b12c236"), 2);
    m_w->ringScanCombo->addItem(slime::ui::text(slime::ui::Key::RingScanExact), 1);
    m_w->ringScanCombo->setToolTip(slime::ui::t("ui.5201f6ebf6aa"));
    ringModeRow->addWidget(m_w->ringScanCombo);
    ringModeRow->addStretch();
    ringLay->addLayout(ringModeRow);

    // v3.6.18（用户要求）：模拟距离（区块）—— 刷怪只发生在模拟距离内（16 格/区块），
    //   16×D < 128 时环带外沿会被收窄；>=8 区块（128 格）时与默认环带一致。
    auto* simRow = new QHBoxLayout();
    simRow->addWidget(new QLabel(slime::ui::t("ui.73aa02598560"), m_w->ringOpts));
    m_w->simDistance = new QSpinBox(m_w->ringOpts);
    m_w->simDistance->setRange(2, 32);
    m_w->simDistance->setValue(12);
    // 单位写在框外（项目规则：输入框内不放单位）
    m_w->simDistance->setToolTip(
        slime::ui::t("ui.4855e48a8f7a"));
    simRow->addWidget(m_w->simDistance);
    simRow->addWidget(new QLabel(slime::ui::t("ui.3da82d85c5de"), m_w->ringOpts));
    m_w->simDistanceNote = new QLabel(QString(), m_w->ringOpts);
    m_w->simDistanceNote->setObjectName(QStringLiteral("simDistanceNote"));
    simRow->addWidget(m_w->simDistanceNote, 1);
    ringLay->addLayout(simRow);
    QObject::connect(m_w->simDistance, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     [this](int) { updateSimDistanceNote(); });
    updateSimDistanceNote();
    m_w->ringOpts->setVisible(false);
    densityLay->addWidget(m_w->ringOpts);
    modeLayout->addWidget(m_w->densityPanel);

    // ===== 通用选项 =====
    auto* optRow3 = new QHBoxLayout();
    optRow3->addWidget(new QLabel(slime::ui::t("ui.1113d292c420"), gMode));
    m_w->targetCount = new QSpinBox(gMode);
    m_w->targetCount->setRange(1, 1000000);
    m_w->targetCount->setValue(1);
    m_w->targetCount->setToolTip(slime::ui::t("ui.0405eccc6f77"));
    optRow3->addWidget(m_w->targetCount);
    optRow3->addStretch();
    modeLayout->addLayout(optRow3);
    // v3.10.9：停止方式已移入「结果要求」分组（仅自定义方案显示）
    auto* optRow1 = new QHBoxLayout();
    m_w->stopAtTarget = new QCheckBox(slime::ui::t("ui.c4db3610d642"), gMode);
    m_w->stopAtTarget->setToolTip(slime::ui::t("ui.804c72b80469"));
    optRow1->addWidget(m_w->stopAtTarget);
    optRow1->addStretch();
    QObject::connect(m_w->stopAtTarget, &QCheckBox::toggled, this,
                     [this](bool) { markPresetCustom(slime::ui::t("ui.b77bf334b0d7")); });
    auto* optRow2 = new QHBoxLayout();
    m_w->smartRange = new QCheckBox(slime::ui::text(slime::ui::Key::SmartRange), gMode);
    m_w->smartRange->setToolTip(slime::ui::text(slime::ui::Key::SmartRangeTip));
    optRow2->addWidget(m_w->smartRange);
    optRow2->addStretch();
    QObject::connect(m_w->smartRange, &QCheckBox::toggled, this,
                     [this](bool) { markPresetCustom(slime::ui::t("ui.9522c47b44f6")); });
    groupsLay->addWidget(gMode);

    // ---- v3.10.9（用户要求）：声明「自定义方案可能导致明显降速」+ 高级开关分级 ----
    m_w->presetDisclaimer = new QLabel(gMode);
    m_w->presetDisclaimer->setObjectName(QStringLiteral("presetDisclaimer"));
    m_w->presetDisclaimer->setWordWrap(true);
    m_w->presetDisclaimer->setMinimumWidth(0);
    m_w->presetDisclaimer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // 不撑最小宽
    m_w->presetDisclaimer->setVisible(false);
    m_w->presetDisclaimer->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
                                             .arg(darkTheme() ? QStringLiteral("#ffb74d")
                                                              : QStringLiteral("#b26a00")));
    m_w->presetDisclaimer->setText(slime::ui::t("ui.08c668ef8483"));
    groupsLay->addWidget(m_w->presetDisclaimer);

    auto* gResult = new QGroupBox(slime::ui::t("ui.a62f39a46c98"), groupsHost);
    m_w->resultGroup = gResult;
    auto* resultReqLay = new QVBoxLayout(gResult);
    resultReqLay->setContentsMargins(6, 6, 6, 6);

    auto* stopRow = new QHBoxLayout();
    stopRow->addWidget(m_w->stopAtTarget);
    stopRow->addStretch();
    resultReqLay->addLayout(stopRow);
    auto* smartRow = new QHBoxLayout();
    smartRow->addWidget(m_w->smartRange);
    smartRow->addStretch();
    resultReqLay->addLayout(smartRow);
    // v3.10.12.11：最小密度/忽略阈值已移到密度面板常显，这里只保留停止方式/智能范围
    groupsLay->addWidget(gResult);

    // ---- 分组 4：匹配方式（搜索算法四模式与参数）----
    auto* gMatch = new QGroupBox(slime::ui::t("ui.6a05dbdc884c"), groupsHost);
    m_w->matchGroup = gMatch; // v3.5.6：环形模式下整组置灰
    auto* algLay = new QVBoxLayout(gMatch);
    algLay->setContentsMargins(6, 6, 6, 6);
    algLay->addWidget(new QLabel(slime::ui::t("ui.a3ff0ef4b1d2"), gMatch));
    // v3.6.0：算法对图案/密度两种模式都生效；环形模式有独立的快速/精确扫描（组内另有说明）
    if (auto* algLbl = qobject_cast<QLabel*>(algLay->itemAt(algLay->count() - 1)->widget()))
        algLbl->setToolTip(slime::ui::t("ui.deabcae2b7ea"));
    m_w->algExactRadio = new QRadioButton(slime::ui::t("ui.c0639e939430"), gMatch);
    m_w->algLayeredRadio = new QRadioButton(slime::ui::t("ui.d9cacedddc49"), gMatch);
    m_w->algSampleRadio = new QRadioButton(slime::ui::t("ui.8c7ae92e09a8"), gMatch);
    m_w->algBruteRadio = new QRadioButton(slime::ui::t("ui.9c3dbfa03e7d"), gMatch);
    m_w->algExactRadio->setToolTip(slime::ui::t("ui.af1eb6b5f9dc"));
    m_w->algLayeredRadio->setToolTip(slime::ui::t("ui.d423affec19f"));
    m_w->algSampleRadio->setToolTip(slime::ui::t("ui.4460ce315bf8"));
    m_w->algBruteRadio->setToolTip(slime::ui::t("ui.650ceeaab2e6"));
    m_w->algExactRadio->setChecked(true);
    algLay->addWidget(m_w->algExactRadio);
    // v3.9.11：用户手动改动「会改变结果」的开关 ⇒ 方案变自定义
    for (QRadioButton* r : { m_w->algExactRadio, m_w->algLayeredRadio, m_w->algSampleRadio, m_w->algBruteRadio }) {
        QObject::connect(r, &QRadioButton::toggled, this,
                         [this](bool on) { if (on) markPresetCustom(slime::ui::t("ui.60f424bf4ef4")); });
    }
    algLay->addWidget(m_w->algLayeredRadio);
    algLay->addWidget(m_w->algSampleRadio);
    algLay->addWidget(m_w->algBruteRadio);

    // v3.3.4：分层/采样参数各拆两行（原单行最小宽 ~400px）
    auto* layerRow = new QHBoxLayout();
    layerRow->setContentsMargins(12, 0, 0, 0);
    layerRow->addWidget(new QLabel(slime::ui::t("ui.a82ed5c3e01c"), gMatch));
    m_w->layerBlock = new QComboBox(gMatch);
    m_w->layerBlock->addItem(QStringLiteral("16×16"), 16);
    m_w->layerBlock->addItem(QStringLiteral("32×32"), 32);
    m_w->layerBlock->addItem(QStringLiteral("64×64"), 64);
    m_w->layerBlock->setCurrentIndex(0);
    layerRow->addWidget(m_w->layerBlock);
    layerRow->addStretch();
    algLay->addLayout(layerRow);
    auto* layerRow2 = new QHBoxLayout();
    layerRow2->setContentsMargins(12, 0, 0, 0);
    layerRow2->addWidget(new QLabel(slime::ui::t("ui.004c3be7d3e3"), gMatch));
    m_w->layerRatio = new QDoubleSpinBox(gMatch);
    m_w->layerRatio->setRange(0.2, 2.0);
    m_w->layerRatio->setDecimals(2);
    m_w->layerRatio->setSingleStep(0.1);
    m_w->layerRatio->setValue(1.0);
    // v3.4.12（用户要求）：单位从输入框里拿出来 ⇒ 框后单独放「×」标签
    m_w->layerRatio->setToolTip(slime::ui::t("ui.e5a8256a3033"));
    layerRow2->addWidget(m_w->layerRatio);
    layerRow2->addStretch();
    algLay->addLayout(layerRow2);

    auto* sampleRow = new QHBoxLayout();
    sampleRow->setContentsMargins(12, 0, 0, 0);
    sampleRow->addWidget(new QLabel(slime::ui::t("ui.005b17520be0"), gMatch));
    m_w->sampleStep = new QSpinBox(gMatch);
    m_w->sampleStep->setRange(2, 64);
    m_w->sampleStep->setValue(8);
    m_w->sampleStep->setToolTip(slime::ui::t("ui.e33742fb2823"));
    sampleRow->addWidget(m_w->sampleStep);
    sampleRow->addStretch();
    algLay->addLayout(sampleRow);
    auto* sampleRow2 = new QHBoxLayout();
    sampleRow2->setContentsMargins(12, 0, 0, 0);
    m_w->sampleExact = new QCheckBox(slime::ui::t("ui.64feeab104d9"), gMatch);
    m_w->sampleExact->setToolTip(slime::ui::t("ui.c8e19ae76760"));
    sampleRow2->addWidget(m_w->sampleExact);
    sampleRow2->addStretch();
    algLay->addLayout(sampleRow2);

    m_w->algSummary = new QLabel(gMatch);
    m_w->algSummary->setWordWrap(true);
    m_w->algSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_w->algSummary->setStyleSheet(QStringLiteral("color:%1;").arg(darkTheme() ? "#9aa0a6" : "#767676"));
    m_w->algSummary->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    algLay->addWidget(m_w->algSummary);
    groupsLay->addWidget(gMatch);

    // ---- 分组 5：性能设置（线程数 + GPU 加速）----
    auto* gPerf = new QGroupBox(slime::ui::t("ui.eb1f3e5ef69b"), groupsHost);
    auto* runLay = new QVBoxLayout(gPerf);
    auto* runRow = new QHBoxLayout();
    const int cores = QThread::idealThreadCount();
    runRow->addWidget(new QLabel(slime::ui::t("ui.dc3465f048b7"), gPerf));
    m_w->threads = new QSpinBox(gPerf);
    // v3.3.9（任务书 3.1 根因）：v3.3.8.1 把上限设为**逻辑核心数** ⇒ 在无超线程的机器上
    // 「超过物理核心数」永远不可达，警告实测永不出现。现放宽到 2× 逻辑核心，让「设得过大」
    // 成为可达状态并如实警告（也可让用户按需试验超线程/超订阅设置）。
    const int threadsMax = (std::max)(2, cores * 2);
    m_w->threads->setRange(1, threadsMax);
    runRow->addWidget(m_w->threads);
    // v3.3.9（任务书 3.1）：常驻提示行 —— 只在「超过物理核心数」时显示，
    // 避免警告只闪一下状态栏（反馈栏条目也可能被后续消息淹没）而用户看不到。
    m_w->threadsHint = new QLabel(QString(), gPerf);
    m_w->threadsHint->setObjectName(QStringLiteral("threadsHint"));
    m_w->threadsHint->setWordWrap(true);
    m_w->threadsHint->setVisible(false);
    runRow->addWidget(m_w->threadsHint, 1);
    // v3.3.8.1（任务书 3.1）：线程数过大（超过物理核心数）时给出警告（常驻提示 + 状态栏 + 反馈栏，不弹窗）
    const auto updateThreadsHint = [this]() {
        const int v = m_w->threads->value();
        const int phys = physicalCoreCount();
        if (!m_w->threadsHint) return;
        if (phys > 0 && v > phys) {
            m_w->threadsHint->setText(slime::ui::t("ui.84ff5ba34922")
                                          .arg(phys));
            m_w->threadsHint->setStyleSheet(slime::ui::tr(QStringLiteral("color:%1;"))
                                                .arg(darkTheme() ? QStringLiteral("#ffb74d")
                                                                 : QStringLiteral("#b26a00")));
            m_w->threadsHint->setVisible(true);
        } else if (v > 12) {
            m_w->threadsHint->setText(slime::ui::t("ui.fa3eb4b9a8c6"));
            m_w->threadsHint->setStyleSheet(QStringLiteral("color:%1;")
                                                .arg(darkTheme() ? QStringLiteral("#ffb74d")
                                                                 : QStringLiteral("#b26a00")));
            m_w->threadsHint->setVisible(true);
        } else {
            m_w->threadsHint->clear();
            m_w->threadsHint->setVisible(false);
        }
    };
    connect(m_w->threads, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, updateThreadsHint](int v) {
        updateThreadsHint();
        if (v <= 0) return;
        const int phys = physicalCoreCount();
        if (phys > 0 && v > phys) {
            setStatus(slime::ui::tr(slime::ui::t("ui.476bd74f4d20")).arg(v).arg(phys));
            addFeedback(1, slime::ui::tr(slime::ui::t("ui.fdcf43a74de7")).arg(v).arg(phys));
        } else if (v > 12) {
            setStatus(slime::ui::tr(slime::ui::t("ui.f0bde910701c")).arg(v));
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.f0bde910701c")).arg(v));
        }
    });
    // v3.3.9（任务书 3.1）：默认值保持历史行为（全部逻辑核心，与既有性能基准一致）；
    // 在超线程机器上该默认值本身就会触发「超过物理核心数」常驻提示 —— 这正是任务书要求的
    // 「过大时给出警告」，且 tooltip 给出推荐值（物理核心数）。
    m_w->threads->setValue(cores);
    updateThreadsHint();
    m_w->threads->setToolTip(slime::ui::t("ui.44f41848ac07")
                                 .arg(threadsMax)
                                 .arg(physicalCoreCount() > 0 ? physicalCoreCount() : cores)
                                 .arg(cores));
    runRow->addStretch();
    runLay->addLayout(runRow);
    auto* gpuRow = new QHBoxLayout();
    m_w->gpuCheck = new QCheckBox(slime::ui::t("ui.c903f3eb38b6"), gPerf);
    m_w->gpuCheck->setChecked(false);
    // v3.3.6（任务书 Bug 2）：不再显示阈值提示，只保留「不一定更快」提醒（tooltip 见 updateGpuStatusUi）
    m_w->gpuCheck->setToolTip(gpuReminderText());
    gpuRow->addWidget(m_w->gpuCheck);
    runLay->addLayout(gpuRow);
    m_w->gpuStatus = new QLabel(slime::ui::t("ui.f7d4441772c2"), gPerf);
    m_w->gpuStatus->setWordWrap(true);
    runLay->addWidget(m_w->gpuStatus);

    // v3.10.9（用户要求）：GPU 开关旁的「设置 ▸」，展开 GPU 相关开关（默认收起）
    {
        auto* advRow = new QHBoxLayout();
        m_w->gpuAdvBtn = new QToolButton(gPerf);
        m_w->gpuAdvBtn->setText(slime::ui::t("ui.9ce9703b1e69"));
        m_w->gpuAdvBtn->setAutoRaise(true);
        m_w->gpuAdvBtn->setCursor(Qt::PointingHandCursor);
        m_w->gpuAdvBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_w->gpuAdvBtn->setToolTip(slime::ui::t("ui.8f223956c5e5"));
        advRow->addWidget(m_w->gpuAdvBtn);
        advRow->addStretch();
        runLay->addLayout(advRow);

        m_w->gpuAdvPanel = new QWidget(gPerf);
        m_w->gpuAdvPanel->setVisible(false);
        auto* advLay = new QVBoxLayout(m_w->gpuAdvPanel);
        advLay->setContentsMargins(12, 0, 0, 0);
        advLay->setSpacing(3);
        auto* rowS = new QHBoxLayout();
        m_w->gpuStreamCheck = new QCheckBox(slime::ui::t("ui.d76a8f60e8cf"), m_w->gpuAdvPanel);
        m_w->gpuStreamCheck->setChecked(m_settings.gpuPatternStream);
        m_w->gpuStreamCheck->setToolTip(slime::ui::t("ui.86b8fbcbd4f9"));
        rowS->addWidget(m_w->gpuStreamCheck);
        rowS->addStretch();
        advLay->addLayout(rowS);
        // v3.10.11.1：结果自优化（默认关；只影响速度与"已保留数量"统计，不改变结果内容）
        auto* rowO = new QHBoxLayout();
        m_w->gpuSelfOptCheck = new QCheckBox(slime::ui::t("ui.b2a291b0c108"), m_w->gpuAdvPanel);
        m_w->gpuSelfOptCheck->setChecked(m_settings.gpuResultSelfOptimize);
        m_w->gpuSelfOptCheck->setToolTip(slime::ui::t("ui.39342fda69c7"));
        rowO->addWidget(m_w->gpuSelfOptCheck);
        rowO->addStretch();
        advLay->addLayout(rowO);
        auto* selfOptNote = new QLabel(slime::ui::t("ui.62ff4651a917"),
            m_w->gpuAdvPanel);
        selfOptNote->setWordWrap(true);
        selfOptNote->setStyleSheet(QStringLiteral("color:#888; font-size:11px;"));
        advLay->addWidget(selfOptNote);
        auto* rowL = new QHBoxLayout();
        rowL->addWidget(new QLabel(slime::ui::t("ui.8a71c973fb98"), m_w->gpuAdvPanel));
        m_w->gpuLimitSpin = new QSpinBox(m_w->gpuAdvPanel);
        m_w->gpuLimitSpin->setRange(10, 100);
        m_w->gpuLimitSpin->setValue(std::clamp(m_settings.gpuLimitPercent, 10, 100));
        m_w->gpuLimitSpin->setToolTip(slime::ui::t("ui.6f21a103a38e"));
        rowL->addWidget(m_w->gpuLimitSpin);
        rowL->addStretch();
        advLay->addLayout(rowL);
        auto* rowA = new QHBoxLayout();
        m_w->gpuAutoCheck = new QCheckBox(slime::ui::t("ui.039b97bba306"), m_w->gpuAdvPanel);
        m_w->gpuAutoCheck->setChecked(m_settings.autoCheckGpuAtStartup);
        m_w->gpuAutoCheck->setToolTip(slime::ui::t("ui.107542ecf736"));
        rowA->addWidget(m_w->gpuAutoCheck);
        rowA->addStretch();
        advLay->addLayout(rowA);
        runLay->addWidget(m_w->gpuAdvPanel);

        connect(m_w->gpuAdvBtn, &QToolButton::clicked, this, [this]() {
            const bool show = m_w->gpuAdvPanel && !m_w->gpuAdvPanel->isVisible();
            if (m_w->gpuAdvPanel) m_w->gpuAdvPanel->setVisible(show);
            if (m_w->gpuAdvBtn) m_w->gpuAdvBtn->setText(show ? slime::ui::t("ui.57441879db15")
                                                             : slime::ui::t("ui.9ce9703b1e69"));
        });
        connect(m_w->gpuStreamCheck, &QCheckBox::toggled, this, [this](bool on) {
            m_settings.gpuPatternStream = on;
            scheduleConfigWrite();
        });
        connect(m_w->gpuSelfOptCheck, &QCheckBox::toggled, this, [this](bool on) {
            m_settings.gpuResultSelfOptimize = on;
            scheduleConfigWrite();
        });
        connect(m_w->gpuLimitSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
            m_settings.gpuLimitPercent = std::clamp(v, 10, 100);
            scheduleConfigWrite();
        });
        connect(m_w->gpuAutoCheck, &QCheckBox::toggled, this, [this](bool on) {
            m_settings.autoCheckGpuAtStartup = on;
            scheduleConfigWrite();
        });
    }
    groupsLay->addWidget(gPerf);

    // ---- 分组 6：缓存管理 ----
    auto* gCache = new QGroupBox(slime::ui::t("ui.c15668b86bc6"), groupsHost);
    auto* cacheLay = new QVBoxLayout(gCache);
    auto* cacheRow = new QHBoxLayout();
    m_w->importCache = new QPushButton(slime::ui::t("ui.00e905651e85"), gCache);
    m_w->clearCache = new QPushButton(slime::ui::t("ui.291952a2abb2"), gCache);
    cacheRow->addWidget(m_w->importCache);
    cacheRow->addWidget(m_w->clearCache);
    cacheRow->addStretch();
    cacheLay->addLayout(cacheRow);
    m_w->useCache = new QCheckBox(slime::ui::t("ui.ba695727a2f2"), gCache);
    m_w->useCache->setToolTip(slime::ui::t("ui.72d08625eb17"));
    m_w->useCache->setChecked(true);
    cacheLay->addWidget(m_w->useCache);
    // v3.3.9（任务书 1.1）：**缓存构建模式** —— 唯一可以写缓存的模式（其他模式一律只读）。
    // v3.4.9（用户要求）：搜索页**不再**放「缓存构建模式」开关 —— 缓存构建已独立成左侧导航页，
    // 「是否处于构建模式」由当前页面决定（进构建页=开，回搜索页=关），状态保存在 m_buildModeActive。
    {
        auto* moved = new QLabel(slime::ui::t("ui.2949f64a2809"), gCache);
        moved->setObjectName(QStringLiteral("topSummary"));
        moved->setWordWrap(true);
        cacheLay->addWidget(moved);
    }
    // v3.4.9：构建提示行随构建内容一起搬到「缓存构建」页（见 buildV340Pages），此处不再创建
    m_w->cacheStatus = new QLabel(slime::ui::t("ui.23c0ca294841"), gCache);
    m_w->cacheStatus->setWordWrap(true);
    cacheLay->addWidget(m_w->cacheStatus);
    groupsLay->addWidget(gCache);

    m_paramLay = groupsLay; // v3.4.3：缓存构建「额外选项」挂到搜索页参数列
    groupsLay->addStretch();

    // ================= 右：信息栏 + QTextEdit（结果文本） =================
    auto* rightPanel = new QWidget(m_hSplit);
    rightPanel->setMinimumWidth(wideText ? 280 : 300); // v3.4.4（R1/P0）：结果栏最小宽 380，窄窗下自动出滚动条而非叠字
    auto* rightLay = new QVBoxLayout(rightPanel);
    rightLay->setContentsMargins(4, 0, 0, 0);
    rightLay->setSpacing(4);

    auto* infoBar = new QHBoxLayout();
    m_w->modeHeader = new QLabel(slime::ui::t("ui.014a215707f0"), rightPanel);
    m_w->modeHeader->setStyleSheet(QStringLiteral("font-weight:bold;"));
    infoBar->addWidget(m_w->modeHeader);
    infoBar->addStretch();
    m_w->copyBtn = new QPushButton(slime::ui::t("ui.0d6cb0c3fd91"), rightPanel);
    infoBar->addWidget(m_w->copyBtn);
    rightLay->addLayout(infoBar);
    // v3.5.5（修复）：infoStats / modeWarn 的创建在右栏重写时丢失 ⇒ 布局里塞 nullptr 崩溃
    m_w->infoStats = new QLabel(rightPanel);
    m_w->infoStats->setObjectName(QStringLiteral("resultCaption"));
    m_w->infoStats->setWordWrap(true);
    m_w->modeWarn = new QLabel(rightPanel);
    m_w->modeWarn->setObjectName(QStringLiteral("modeWarn"));
    m_w->modeWarn->setWordWrap(true);
    m_w->modeWarn->setVisible(false);
    rightLay->addWidget(m_w->infoStats);
    rightLay->addWidget(m_w->modeWarn);
    m_rightSplit = new QSplitter(Qt::Vertical, rightPanel);
    m_rightSplit->setObjectName(QStringLiteral("rightSplit"));
    m_rightSplit->setChildrenCollapsible(false);

    m_mapPanel = new QWidget(m_mapColumn);
    m_mapPanel->setObjectName(QStringLiteral("mapPanel"));
    auto* mapLay = new QVBoxLayout(m_mapPanel);
    mapLay->setContentsMargins(0, 0, 0, 0);
    mapLay->setSpacing(2);
    {
        auto* head = new QWidget(m_mapPanel);
        // v3.6.9：标题行固定高度 —— 折叠后多余空间不能分给标题行，否则按钮会被垂直居中（用户看到的「位移」）
        head->setFixedHeight(30);
        head->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* hl = new QHBoxLayout(head);
        hl->setContentsMargins(2, 2, 2, 0);
        hl->setSpacing(6);
        m_mapToggle = new QToolButton(head);
        m_mapToggle->setObjectName(QStringLiteral("mapPanelToggle"));
        m_mapToggle->setText(slime::ui::t("ui.46e1efeb268a"));
        m_mapToggle->setArrowType(Qt::DownArrow);
        m_mapToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_mapToggle->setAutoRaise(true);
        m_mapToggle->setCursor(Qt::PointingHandCursor);
        m_mapToggle->setToolTip(slime::ui::t("ui.3e25cac4426c"));
        m_mapToggle->setStyleSheet(QStringLiteral(
            "QToolButton{border:none;padding:3px 8px;border-radius:0px;font-weight:bold;}"
            "QToolButton:hover{background:#4CAF50;color:#ffffff;}"));
        hl->addWidget(m_mapToggle);
        auto* bFit = new QPushButton(slime::ui::t("ui.e9562f6fa7af"), head);
        bFit->setAutoDefault(false);
        hl->addWidget(bFit);
        hl->addStretch();
        mapLay->addWidget(head);
        m_map = new MapPreviewWidget(m_mapPanel);
        m_map->setObjectName(QStringLiteral("searchMap"));
        m_map->setMinimumHeight(160);
        mapLay->addWidget(m_map, 1);
        m_mapPlaceholder = new QLabel(slime::ui::t("ui.7cb53520e436"), m_mapPanel);
        m_mapPlaceholder->setObjectName(QStringLiteral("mapPanelPlaceholder"));
        m_mapPlaceholder->setVisible(false);
        mapLay->addWidget(m_mapPlaceholder);
        // v3.6.9.1（用户反馈：地图被挤窄且拉不动）：末尾 stretch 会和地图平分剩余空间（各 1 份），
        //   展开时地图只剩一半高度。改成「只在折叠状态显示的占位控件」：
        //   展开时它隐藏 ⇒ 剩余空间全部归地图；折叠时它显示 ⇒ 标题行贴顶不会被居中。
        m_mapSpacer = new QWidget(m_mapPanel);
        m_mapSpacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        m_mapSpacer->setVisible(false);
        mapLay->addWidget(m_mapSpacer, 1);
        connect(m_mapToggle, &QToolButton::clicked, this, [this]() { toggleMapPanel(); });
        connect(bFit, &QPushButton::clicked, this, [this]() { if (m_map) m_map->fitToContent(); });
    }
    m_mapColLay->addWidget(m_mapPanel, 1);
    // v3.6.5（用户要求）：地图下方、结果列表上方的显眼提醒
    // v3.6.8（用户反馈：折叠按钮点了会位移）：提醒**不能**放进 QSplitter —— 它是第 3 个子控件，
    //   显示/隐藏会改变分隔条的子项数量与像素分配，导致地图/结果整体位移。
    //   现在改成放进结果面板内部（结果标题行正上方）。
    m_resultsNotice = new QLabel(m_rightSplit);
    m_resultsNotice->setObjectName(QStringLiteral("resultsNotice"));
    m_resultsNotice->setVisible(false);
    m_resultsNotice->setWordWrap(true);
    // v3.6.6：用户一旦手动拖动右栏分隔条，就不再自动改尺寸
    connect(m_rightSplit, &QSplitter::splitterMoved, this, [this](int, int) { m_rightSplitUserMoved = true; });
    // v3.5.5（修复）：resultView 的创建曾丢失，导致布局里塞入 nullptr ⇒ 启动崩溃
    m_w->resultView = new QTextEdit(rightPanel);
    m_w->resultView->setObjectName(QStringLiteral("resultView"));
    m_w->resultView->setReadOnly(true);
    m_w->resultView->setPlaceholderText(slime::ui::t("ui.31bb4ff9f568"));
    m_w->resultView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_w->resultView, &QTextEdit::customContextMenuRequested, this, &MainWindow::showResultContextMenu);
    // v3.5.5：结果面板 = 可折叠标题 + 列表（表头排序、每行跳转）；resultView 保留在面板内并隐藏
    m_resultPane = new QWidget(m_rightSplit);
    m_resultPane->setObjectName(QStringLiteral("resultPane"));
    auto* resLay = new QVBoxLayout(m_resultPane);
    resLay->setContentsMargins(0, 0, 0, 0);
    resLay->setSpacing(2);
    {
        auto* head = new QWidget(m_resultPane);
        head->setFixedHeight(30);   // v3.6.9：同上，折叠后标题行位置不动
        head->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* hl = new QHBoxLayout(head);
        hl->setContentsMargins(2, 2, 2, 0);
        hl->setSpacing(6);
        m_resultToggle = new QToolButton(head);
        m_resultToggle->setObjectName(QStringLiteral("resultPaneToggle"));
        m_resultToggle->setText(slime::ui::t("ui.0a2c91cec6c8"));
        m_resultToggle->setArrowType(Qt::DownArrow);
        m_resultToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_resultToggle->setAutoRaise(true);
        m_resultToggle->setCursor(Qt::PointingHandCursor);
        m_resultToggle->setToolTip(slime::ui::t("ui.34e02c74cfb6"));
        m_resultToggle->setStyleSheet(QStringLiteral(
            "QToolButton{border:none;padding:3px 8px;border-radius:0px;font-weight:bold;}"
            "QToolButton:hover{background:#4CAF50;color:#ffffff;}"));
        hl->addWidget(m_resultToggle);
        m_resultCountLabel = new QLabel(slime::ui::t("ui.a535edb5149b"), head);
        m_resultCountLabel->setObjectName(QStringLiteral("resultCaption"));
        hl->addWidget(m_resultCountLabel);
        hl->addStretch();
        resLay->addWidget(head);
        if (m_resultsNotice) {
            m_resultsNotice->setParent(m_resultPane);
            resLay->insertWidget(1, m_resultsNotice);   // v3.6.8：提醒放在结果标题行下面
        }

        m_w->resultTable = new QTableWidget(m_resultPane);
        auto* tb = m_w->resultTable;
        tb->setObjectName(QStringLiteral("resultTable"));
        tb->setColumnCount(7);
        // v3.10.0：结果独立成第三列（约 340px）⇒ 表头缩短、列宽固定，避免窄列裁字
        tb->setHorizontalHeaderLabels({ slime::ui::t("ui.db669ff2ab99"), QStringLiteral("X"), QStringLiteral("Z"),
                                        slime::ui::t("ui.b3480678b68e"), slime::ui::t("ui.f90554a29e42"),
                                        slime::ui::t("ui.3592ccaf66db"), QStringLiteral("") });
        tb->setColumnWidth(0, 40);
        tb->setColumnWidth(1, 56);
        tb->setColumnWidth(2, 56);
        tb->setColumnWidth(3, 50);
        tb->setColumnWidth(4, 62);
        tb->setColumnWidth(5, 62);
        tb->setColumnHidden(5, true);   // v3.5.9：仅环形模式显示「有效格 · 总权重」
        tb->verticalHeader()->setVisible(false);
        tb->setAlternatingRowColors(true);
        tb->setSelectionBehavior(QAbstractItemView::SelectRows);
        tb->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tb->horizontalHeader()->setSectionsClickable(true);
        tb->horizontalHeader()->setSortIndicatorShown(true);
        tb->horizontalHeader()->setStretchLastSection(true);
        static OneDecimalDelegate oneDecimal;
        tb->setItemDelegateForColumn(3, &oneDecimal);
        tb->setItemDelegateForColumn(4, &oneDecimal);
        tb->setItemDelegateForColumn(5, &oneDecimal);
        resLay->addWidget(tb, 1);
        resLay->addWidget(m_w->resultView, 0);
        m_w->resultView->hide();
        // v3.6.9.1：同上 —— 折叠时才显示占位控件，展开时表格吃掉全部剩余空间
        m_resultSpacer = new QWidget(m_resultPane);
        m_resultSpacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        m_resultSpacer->setVisible(false);
        resLay->addWidget(m_resultSpacer, 1);

        connect(m_resultToggle, &QToolButton::clicked, this, [this]() { toggleResultPane(); });
        connect(tb, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
            if (!m_w || !m_w->resultTable) return;
            const auto* xi = m_w->resultTable->item(row, 1);
            const auto* zi = m_w->resultTable->item(row, 2);
            if (!xi || !zi) return;
            const int bx = xi->data(Qt::UserRole).toInt();
            const int bz = zi->data(Qt::UserRole).toInt();
            focusMapOnChunk(bx >= 0 ? bx >> 4 : -((-bx + 15) >> 4), bz >= 0 ? bz >> 4 : -((-bz + 15) >> 4));
        });
        // v3.5.10：点结果行 = 选中该环形候选（地图只画这一条环，并联动脉动选中行）
        connect(tb, &QTableWidget::cellClicked, this, [this](int row, int) {
            if (!m_lastAfk.empty() && row >= 0 && row < static_cast<int>(m_lastAfk.size()))
                focusAfkCandidate(row);
        });
    }
    m_rightSplit->addWidget(m_resultPane);
    m_rightSplit->setStretchFactor(0, 1);
    m_rightSplit->setStretchFactor(1, 1);
    m_rightSplit->setSizes(QList<int>() << 880);   // v3.10.0：右列只剩结果面板（地图已独立成列）
    rightLay->addWidget(m_rightSplit, 1);
    m_hSplit->addWidget(rightPanel);

    // v3.5.4：地图与信息填写栏联动（去抖刷新）+ 点击结果定位
    m_mapSyncTimer = new QTimer(this);
    m_mapSyncTimer->setSingleShot(true);
    m_mapSyncTimer->setInterval(250);
    connect(m_mapSyncTimer, &QTimer::timeout, this, [this]() { updateMapFromForm(); });
    const auto scheduleMap = [this]() { if (m_mapSyncTimer) m_mapSyncTimer->start(); };
    if (m_w->bedrockRadio) connect(m_w->bedrockRadio, &QRadioButton::toggled, this, [scheduleMap](bool) { scheduleMap(); });
    if (m_w->javaRadio) connect(m_w->javaRadio, &QRadioButton::toggled, this, [scheduleMap](bool) { scheduleMap(); });
    if (m_w->seedEdit) connect(m_w->seedEdit, &QLineEdit::textChanged, this, [scheduleMap](const QString&) { scheduleMap(); });
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        if (s) connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [scheduleMap](int) { scheduleMap(); });
    }
    if (m_w->resultView) m_w->resultView->installEventFilter(this);
    m_hSplit->setStretchFactor(0, 0);
    m_hSplit->setStretchFactor(1, 1);

    // ================= v3.3.6（任务书 调整 2）：底部面板 = 左竖排导航 + 右内容区 =================
    // v3.10.1：过程面板并入右列（结果上 / 过程下），不再横跨整窗
    auto* bottomPanel = new QWidget(m_rightSplit);
    bottomPanel->setObjectName(QStringLiteral("bottomPanel"));
    bottomPanel->setMinimumHeight(120); // 任务书 1.5：底部最小高 120
    auto* bottomLay = new QHBoxLayout(bottomPanel);
    bottomLay->setContentsMargins(0, 0, 0, 0);
    bottomLay->setSpacing(0);

    // ---- 左侧竖排导航（宽约 120px；当前项 ● + 加粗 + 高亮；仅「反馈」显示 ⚠️/❌ 计数）----
    m_nav = new QListWidget(bottomPanel);
    m_nav->setObjectName(QStringLiteral("navList"));
    m_nav->setFixedWidth(120);
    m_nav->setWordWrap(true); // 「反馈 ⚠️2❌1」在 120px 内自动折行显示
    m_nav->setFocusPolicy(Qt::NoFocus);
    m_nav->setSelectionMode(QAbstractItemView::SingleSelection);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setEditTriggers(QAbstractItemView::NoEditTriggers);
    static const char* const kNavNames[4] = { "ui.tab.progress", "ui.tab.feedback", "ui.tab.log", "ui.tab.command" };
    for (int i = 0; i < 4; ++i) {
        auto* it = new QListWidgetItem(m_nav);
        it->setText(QStringLiteral("○ ") + slime::ui::t(kNavNames[i]));
        it->setToolTip(i == 0 ? slime::ui::t("ui.774421d89d3b")
                              : i == 1 ? slime::ui::t("ui.2024c0e4a2cf")
                                       : i == 2 ? slime::ui::t("ui.418a1fb7c7d4")
                                                : slime::ui::t("ui.b4fa25c4081b"));
        it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    bottomLay->addWidget(m_nav);

    // ---- 右侧内容区（QStackedWidget 四页，与导航一一对应）----
    m_stack = new QStackedWidget(bottomPanel);
    m_stack->setObjectName(QStringLiteral("bottomStack"));

    // 页 1：进度 —— v3.3.6（调整 3）：累积式纯文字流水
    m_progressStream = new ProgressStreamPanel(m_stack);
    m_stack->addWidget(m_progressStream);

    // 页 2：反馈 —— v3.3.6（调整 4）：时间顺序（最新在下）+ 过滤成功类型
    m_feedback = new FeedbackPanel(m_stack);
    m_stack->addWidget(m_feedback);

    // 页 3：日志（默认关闭显示提示行；启用后为级别过滤 + 自动滚动 + 清空 + 复制全部 + 流水）
    m_logPanel = new LogPanel(m_stack);
    m_stack->addWidget(m_logPanel);

    // 页 4：命令终端（保持原功能：输出区 + 输入行 + 工具按钮）
    m_terminal = new CommandTerminal(this, m_stack);
    m_stack->addWidget(m_terminal);

    // v3.10.1：给过程面板加一条 30px 标题行（"过程 · 进度" + 收起按钮），
    //   右列 = 结果（上）+ 过程（下），与原型一致。
    {
        auto* stackHost = new QWidget(bottomPanel);
        auto* stackLay = new QVBoxLayout(stackHost);
        stackLay->setContentsMargins(0, 0, 0, 0);
        stackLay->setSpacing(0);
        auto* dockHead = new QWidget(stackHost);
        dockHead->setFixedHeight(30);
        auto* dockHeadLay = new QHBoxLayout(dockHead);
        dockHeadLay->setContentsMargins(6, 0, 2, 0);
        dockHeadLay->setSpacing(2);
        m_dockTitle = new QLabel(slime::ui::t("ui.bc2bf86ea76e"), dockHead);
        m_dockTitle->setMaximumWidth(76);
        m_dockTitle->setObjectName(QStringLiteral("dockTitle"));
        QFont dockFont = m_dockTitle->font();
        dockFont.setBold(true);
        m_dockTitle->setFont(dockFont);
        dockHeadLay->addWidget(m_dockTitle);
        dockHeadLay->addSpacing(8);
        {
            static const char* const kTabNames[4] = { "ui.tab.progress", "ui.tab.feedback", "ui.tab.log", "ui.tab.command" };
            for (int i = 0; i < 4; ++i) {
                auto* tab = new QToolButton(dockHead);
                tab->setText(slime::ui::t(kTabNames[i]));
                tab->setCheckable(true);
                tab->setAutoExclusive(true);
                tab->setProperty("dockTab", true);
                tab->setCursor(Qt::PointingHandCursor);
                tab->setToolButtonStyle(Qt::ToolButtonTextOnly);
                tab->setFixedWidth(40);   // v3.10.1：窄右列也放得下 4 个页签（不再变成 …）
                // 全局 QSS 给 QToolButton 留了 0 10px 的左右内边距，2 个汉字在 40px 内会被省略成 …；
                // 这里只覆盖内边距与字号，颜色/选中态仍由主题 QSS 决定。
                tab->setStyleSheet(QStringLiteral("QToolButton{padding:0px 2px;font-size:10px;}"));
                tab->setToolTip(slime::ui::t(kTabNames[i]));
                {
                    QFont tabFont = tab->font();
                    tabFont.setPointSizeF(8.0);
                    tab->setFont(tabFont);
                }
                connect(tab, &QToolButton::clicked, this, [this, i]() { showDock(i); });
                dockHeadLay->addWidget(tab);
                m_dockHeadTabs.push_back(tab);
            }
        }
        dockHeadLay->addStretch();
        m_dockToggleBtn = new QToolButton(dockHead);
        m_dockToggleBtn->setText(slime::ui::t("ui.5d5815647c76"));
        m_dockToggleBtn->setAutoRaise(true);
        m_dockToggleBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_dockToggleBtn->setCursor(Qt::PointingHandCursor);
        m_dockToggleBtn->setToolTip(slime::ui::t("ui.c4708ce077c6"));
        m_dockToggleBtn->setFixedWidth(52);
        connect(m_dockToggleBtn, &QToolButton::clicked, this, [this]() {
            if (m_dockCollapsed) showDock(m_nav ? m_nav->currentRow() : 0);
            else hideDock();
        });
        dockHeadLay->addWidget(m_dockToggleBtn);
        stackLay->addWidget(dockHead);
        stackLay->addWidget(m_stack, 1);
        bottomLay->addWidget(stackHost, 1);
    }
    m_rightSplit->addWidget(bottomPanel);
    m_rightSplit->setStretchFactor(0, 1);
    m_rightSplit->setStretchFactor(1, 0);
    m_rightSplit->setSizes(QList<int>() << 560 << 240);

    // v3.4.2（参考 PyCharm 截图）：隐藏分类竖排列表 —— 只保留左导航底部的图标条切换，
    // 面板打开时不再顶起左下角按钮；分类切换仍走 selectBottomCategory（m_nav 仅作数据源）。
    // v3.4.2：分类切换只由左导航图标条驱动（原竖排分类列表已不参与布局）
    // v3.4.2（参考 PyCharm）：竖排分类列表不参与布局，仅作数据源（图标条切换已同步它）
    m_nav->hide();
    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::selectBottomCategory);
    selectBottomCategory(0);
    updateNavBadges();

    // ================= v3.4.0：把既有面板装配进「搜索页」，并构建其余四页 =================
    //   page0 搜索：40% 参数栏（既有 6 张卡片） + 60% 结果栏（既有 QTextEdit 结果视图）
    //   page1 构建范围 / page2 设置（见 buildV340Pages）
    {
        auto* page0 = new QWidget(m_pageStack);
        auto* page0Lay = new QVBoxLayout(page0);
        page0Lay->setContentsMargins(0, 0, 0, 0);
        page0Lay->setSpacing(0);
        page0Lay->addWidget(m_hSplit, 1);
        m_pageStack->addWidget(page0);
        buildV340Pages();
        showPage(0);
    }

    // 初始尺寸（任务书 1.5）：左栏 400、底部 180；v3.4.0：中部行高优先
    QList<int> hSizes; hSizes << 380 << 480 << 360;   // v3.10.0：三纵列（参数 | 地图 | 结果）
    m_hSplit->setSizes(hSizes);
    // v3.10.1：m_vSplit 只剩页面栈（过程面板已移入右列），无需设置双段尺寸

    // ---- 状态栏（QMainWindow 原生）----
    // v3.3.6（任务书 调整 1）：进度行的补充信息（已用时/预计剩余/阶段说明）放在状态栏，
    // 进度行本身保持「进度条 + 百分比 + 阶段文字」的紧凑形态。
    statusBar()->showMessage(slime::ui::t("ui.b796f2d4ca85"));
    // v3.3.7.2（任务书 1.3）：临时消息（showMessage）与右侧永久控件之间必须**有分隔符** ——
    // 否则「扫描完成，正在匹配图案」+「扫描 11265898000/… 区块（25.6%）」会被读成一句话。
    // 该分隔符只在存在临时消息时显示（messageChanged），消息清空后自动隐藏。
    // v3.5.28（用户要求）：状态栏右侧信息全部移除，改为**工具栏搜索状态面板**：
    //   阶段（搜索中/匹配中/排序中）｜结果数 ｜已用时 ｜预计剩余。
    m_statusSep = nullptr;
    m_w->searchStatus = new QWidget(m_toolBar);
    m_w->searchStatus->setObjectName(QStringLiteral("searchStatusBox"));
    auto* ssLay = new QHBoxLayout(m_w->searchStatus);
    ssLay->setContentsMargins(8, 0, 8, 0);
    ssLay->setSpacing(10);
    m_w->searchStage = new QLabel(slime::ui::t("ui.2437d377fef4"), m_w->searchStatus);
    m_w->searchStage->setObjectName(QStringLiteral("searchStage"));
    m_w->progressInfo = new QLabel(slime::ui::t("ui.ce0d3fa9e579"), m_w->searchStatus);
    m_w->progressInfo->setWordWrap(false);
    m_w->progressElapsed = new QLabel(slime::ui::t("ui.6701fe136b68"), m_w->searchStatus);
    m_w->progressEta = new QLabel(slime::ui::t("ui.d5c3799233cf"), m_w->searchStatus);
    ssLay->addWidget(m_w->searchStage);
    ssLay->addWidget(m_w->progressInfo);
    ssLay->addWidget(m_w->progressElapsed);
    ssLay->addWidget(m_w->progressEta);
    if (m_toolBar && m_toolBarSpacerAction) m_toolBar->insertWidget(m_toolBarSpacerAction, m_w->searchStatus);
    // v3.6.0（用户要求）：联系我们按钮回到界面（放工具栏右侧）
    m_w->contactBtn = new QPushButton(slime::ui::t("ui.152d3db75889"), m_toolBar);
    m_w->contactBtn->setFlat(true);
    connect(m_w->contactBtn, &QPushButton::clicked, this, &MainWindow::showContact);
    if (m_toolBar && m_toolBarSpacerAction) m_toolBar->insertWidget(m_toolBarSpacerAction, m_w->contactBtn);
    // 左下角常驻提醒（搜索时会被实时状态覆盖）
    statusBar()->showMessage(slime::ui::t("ui.4865609e48db"));

    // ---- 连接 ----
    // v3.6.5：条件变化时刷新「开始搜索」可用状态
    if (m_w->seedEdit)
        connect(m_w->seedEdit, &QLineEdit::textChanged, this, [this](const QString&) { updateStartEnabled(); });
    if (m_w->strictRadio)
        connect(m_w->strictRadio, &QRadioButton::toggled, this, [this](bool) { updateStartEnabled(); });
    connect(m_w->bedrockRadio, &QRadioButton::toggled, this, [this](bool) { updateStartEnabled(); });
    if (m_w->densityRadio)
        connect(m_w->densityRadio, &QRadioButton::toggled, this, [this](bool) { updateStartEnabled(); });
    if (m_w->targetCount)
        connect(m_w->targetCount, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int) { updateStartEnabled(); });
    connect(m_w->javaRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) onVersionChanged();
    });
    connect(m_w->bedrockRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) onVersionChanged();
    });
    connect(m_w->strictRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) onModeChanged();
    });
    connect(m_w->densityRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) onModeChanged();
    });
    connect(m_w->drawPattern, &QPushButton::clicked, this, &MainWindow::editPattern);
    // v3.5.26：群系版本切换 —— 地图重采样，环形搜索的群系缓存也一并失效
    if (m_w->mapVersionCombo) {
        connect(m_w->mapVersionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            m_mapJavaVersionId = m_w->mapVersionCombo->currentData().toInt();
            m_ringBiome.reset();
            m_ringBiomeVersion = -1;
            updateMapFromForm();
            saveMapVersionToConfig();
        });
    }
    // v3.5.18：内嵌图案编辑器 —— 画布变化直接写回 m_pattern 并刷新提示
    if (m_w->inlinePattern) {
        connect(m_w->inlinePattern, &slime::PatternEditorPanel::patternChanged, this,
                [this](const slime::Pattern& p) {
                    m_pattern = p;
                    m_patternSet = !p.empty() && p.activeCount() > 0;
                    updateStartEnabled();   // v3.6.6（用户实测）：画完图案要立刻可用
                    if (m_w->patternInfo) {
                        if (!m_patternSet) {
                            m_w->patternInfo->setText(slime::ui::t("ui.a1b385fa0140"));
                        } else {
                            m_w->patternInfo->setText(slime::ui::t("ui.e10371e74d05")
                                                          .arg(m_pattern.rows())
                                                          .arg(m_pattern.cols())
                                                          .arg(m_pattern.activeCount()));
                        }
                    }
                });
    }
    connect(m_w->importCache, &QPushButton::clicked, this, &MainWindow::importCache);
    connect(m_w->clearCache, &QPushButton::clicked, this, &MainWindow::clearCache);
    connect(m_w->convertBtn, &QPushButton::clicked, this, &MainWindow::convertBlockCoords);
    connect(m_w->start, &QPushButton::clicked, this, &MainWindow::startSearch);
    connect(m_w->cancel, &QPushButton::clicked, this, &MainWindow::cancelSearch);
    connect(m_w->gpuCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onGpuCheckChanged);
    connect(m_w->copyBtn, &QPushButton::clicked, this, &MainWindow::copyResults);

    // 算法模式切换（仅密度搜索可用；参数即时保存到 config.json）
    for (QRadioButton* r : { m_w->algExactRadio, m_w->algLayeredRadio, m_w->algSampleRadio, m_w->algBruteRadio }) {
        connect(r, &QRadioButton::toggled, this, [this](bool on) {
            if (!on) return;
            updateAlgorithmPanels();
            saveAlgorithmToConfig();
        });
    }
    connect(m_w->layerBlock, &QComboBox::currentIndexChanged, this, [this](int) { saveAlgorithmToConfig(); });
    connect(m_w->layerRatio, &QDoubleSpinBox::valueChanged, this, [this](double) {
        updateAlgorithmPanels();
        saveAlgorithmToConfig();
    });
    connect(m_w->sampleStep, &QSpinBox::valueChanged, this, [this](int) {
        updateAlgorithmPanels();
        saveAlgorithmToConfig();
    });
    connect(m_w->sampleExact, &QCheckBox::toggled, this, [this](bool) {
        updateAlgorithmPanels();
        saveAlgorithmToConfig();
    });

    connect(m_w->presetNear, &QPushButton::clicked, this, [this]() {
        m_wholeMapConfirmed = false;
        applyPreset(20000);      // v3.10.12.8：原 ±5000 → ±20000
    });
    connect(m_w->presetLocal, &QPushButton::clicked, this, [this]() {
        m_wholeMapConfirmed = false;
        applyPreset(200000);     // v3.10.12.8：原 ±50000 → ±200000
    });
    connect(m_w->presetGlobal, &QPushButton::clicked, this, [this]() {
        m_wholeMapConfirmed = false;
        applyPreset(1000000);    // v3.10.12.8：原 ±300000 → ±1000000
    });
    connect(m_w->presetWorld, &QPushButton::clicked, this, [this]() {
        confirmWholeMapSearch(true, kJavaBoundChunk);
    });
    connect(m_w->presetBedrock, &QPushButton::clicked, this, [this]() {
        confirmWholeMapSearch(true, kBedrockBoundChunk);
    });

    // 边界截断：编辑结束后校验（v3.1.0）
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        connect(s, &QSpinBox::editingFinished, this, &MainWindow::clampRangeToBound);
    }

    // v3.5.16：半径是唯一入口；范围框（隐藏）变化时把半径显示同步过来
    if (m_w->rangeRadius) {
        connect(m_w->rangeRadius, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int r) { if (!m_rangeSyncGuard) setSearchRadius(r); });
    }
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { syncRadiusFromRange(); });
    }
    setSearchRadius(m_w->rangeRadius ? m_w->rangeRadius->value() : 1000);

    // 结果排序（只改显示顺序）
    // 日志面板：「启用日志」入口（面板内按钮）与工具菜单一致
    connect(m_logPanel, &LogPanel::enableRequested, this, &MainWindow::showEnableLogDialog);
    // 命令终端：历史持久化到 config.json
    connect(m_terminal, &CommandTerminal::historyChanged, this, [this](const QStringList& h) {
        if (!m_loadingConfig) saveTerminalHistoryToConfig(h);
    });

    // 尺寸（任务书 1.5）：默认 1280×860，最小 1000×650
    resize(1280, 860);
    setMinimumSize(1000, 650);

    onModeChanged();
    onVersionChanged();
    updateCacheStatusUi();
    // v3.5.6：区域形状切换 → 置灰/恢复
    if (m_w->shapeRingRadio) {
        connect(m_w->shapeRingRadio, &QRadioButton::toggled, this, [this](bool on) {
            // v3.10.12.10：环形覆盖率天然只有 ~20% 左右，最小密度 0.3 会导致永远 0 结果。
            //   切入环形时先把阈值降为 0（用户仍可手动调高），切回正方形时恢复原值。
            if (on) {
                m_minDensityBeforeRing = m_w->minDensity ? m_w->minDensity->value() : 0.3;
                if (m_w->minDensity && m_w->minDensity->value() > 0.0) {
                    m_w->minDensity->setValue(0.0);
                    m_minDensityCommitted = 0.0;
                    if (!m_uiQuiet)
                        addFeedback(2, slime::ui::tr(slime::ui::t("ui.c8ae47b91135")));
                }
            } else if (m_w->minDensity && m_w->minDensity->value() == 0.0) {
                m_w->minDensity->setValue((std::max)(0.05, m_minDensityBeforeRing));
                m_minDensityCommitted = m_w->minDensity->value();
            }
            updateSearchShapeEnabled();
        });
    }
    if (m_w->strictRadio) {
        connect(m_w->strictRadio, &QRadioButton::toggled, this, [this](bool) { updateSearchShapeEnabled(); });
    }
    if (m_w->densityRadio) {
        connect(m_w->densityRadio, &QRadioButton::toggled, this, [this](bool) { updateSearchShapeEnabled(); });
    }
    if (m_w->shapeSquareRadio) {
        connect(m_w->shapeSquareRadio, &QRadioButton::toggled, this, [this](bool) { updateSearchShapeEnabled(); });
    }
    updateSearchShapeEnabled();
    updateGpuStatusUi();
    // v3.4.8（用户要求）：主题实际生效变化时（浅↔深），自绘图案立即重画 —— 之前要重启/重载才正常
    connect(&slime::ThemeManager::instance(), &slime::ThemeManager::effectiveThemeChanged, this,
            [this](bool dark) {
                refreshThemedIcons();
                Logger::info(std::string("主题切换为") + (dark ? "深色" : "浅色") + " ⇒ 已重绘界面图案");
            });
}

// ============================================================================
// v3.4.0（ui_prototype/SlimeFinder-UI.html）：左导航 / 标签页 / 停靠面板 / 其余四页
// ============================================================================
// 图标：按原型的内联 SVG 线条风格用 QPainter 画（20×20，1.6px 描边，当前色）
// v3.4.8（用户：切换外观主题时图案要跟着变）：颜色**直接按主题取**（不用 QPalette ——
// QSS 换肤不会改调色板，这正是「切主题后图案看不清、重新加载才正常」的原因）。
QColor MainWindow::themedIconColor() const
{
    return slime::ThemeManager::instance().isDark() ? QColor(0xF0, 0xF0, 0xF0) : QColor(0x11, 0x11, 0x11);
}

QIcon MainWindow::v340NavIcon(int kind, double scale)
{
    QPixmap pm(40, 40); // 2× 供高 DPI；setIconSize(20,20) 会缩放
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2.0, 2.0);
    // v3.4.6：图案缩放（文字与布局不受影响）—— 以中心缩放绘制
    const double s = (scale > 0.1 && scale < 2.0) ? scale : 1.0;
    p.translate(10.0 * (1.0 - s), 10.0 * (1.0 - s));
    p.scale(s, s);
    QPen pen(themedIconColor());
    pen.setWidthF(1.6);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (kind) {
    case 0: // 搜索：圆 + 手柄
        p.drawEllipse(QPointF(9, 9), 5.2, 5.2);
        p.drawLine(QPointF(13, 13), QPointF(17, 17));
        break;
    case 1: // 缓存构建：方块堆叠 + 记录线（一眼看出是「构建/写入」而不是搜索）
        p.drawRect(QRectF(3.0, 3.0, 6.5, 6.5));
        p.drawRect(QRectF(10.5, 3.0, 6.5, 6.5));
        p.drawRect(QRectF(3.0, 10.5, 6.5, 6.5));
        p.drawRect(QRectF(10.5, 10.5, 6.5, 6.5));
        break;
    case 2: // 设置：圆 + 放射线
        p.drawEllipse(QPointF(10, 10), 2.6, 2.6);
        p.drawLine(QPointF(10, 2), QPointF(10, 4.4));
        p.drawLine(QPointF(10, 15.6), QPointF(10, 18));
        p.drawLine(QPointF(2, 10), QPointF(4.4, 10));
        p.drawLine(QPointF(15.6, 10), QPointF(18, 10));
        p.drawLine(QPointF(4.3, 4.3), QPointF(6, 6));
        p.drawLine(QPointF(14, 14), QPointF(15.7, 15.7));
        p.drawLine(QPointF(15.7, 4.3), QPointF(14, 6));
        p.drawLine(QPointF(6, 14), QPointF(4.3, 15.7));
        break;
    case 3: // 更新历史：钟表
        p.drawEllipse(QPointF(10, 10), 7, 7);
        p.drawLine(QPointF(10, 5.5), QPointF(10, 10));
        p.drawLine(QPointF(10, 10), QPointF(13, 12));
        break;
    case 4: // v3.5.3：地图 —— 折角纸 + 定位针
        p.drawRect(QRectF(2.8, 3.4, 14.4, 13.2));
        p.drawLine(QPointF(8.0, 3.4), QPointF(8.0, 16.6));
        p.drawLine(QPointF(12.0, 3.4), QPointF(12.0, 16.6));
        p.drawEllipse(QPointF(10.0, 9.2), 2.2, 2.2);
        p.drawLine(QPointF(10.0, 11.4), QPointF(10.0, 14.0));
        break;
    default: // 控件状态：四宫格
        p.drawRect(QRectF(3, 3, 5.5, 5.5));
        p.drawRect(QRectF(11.5, 3, 5.5, 5.5));
        p.drawRect(QRectF(3, 11.5, 5.5, 5.5));
        p.drawRect(QRectF(11.5, 11.5, 5.5, 5.5));
        break;
    }
    p.end();
    return QIcon(pm);
}

QIcon MainWindow::v340DockIcon(int kind)
{
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2.0, 2.0);
    QPen pen(themedIconColor()); // v3.4.8：与导航图标一致，按主题取色（切主题立即重绘）
    pen.setWidthF(1.6);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (kind) {
    case 0: // 进度流水：三行横线
        p.drawLine(QPointF(4, 6), QPointF(16, 6));
        p.drawLine(QPointF(4, 10), QPointF(12, 10));
        p.drawLine(QPointF(4, 14), QPointF(15, 14));
        break;
    case 1: // 反馈：气泡
        p.drawRect(QRectF(3.5, 4.5, 13, 9));
        p.drawLine(QPointF(7.5, 13.5), QPointF(7.5, 16.5));
        p.drawLine(QPointF(7.5, 16.5), QPointF(10.5, 13.5));
        break;
    case 2: // 日志：文档 + 行
        p.drawRect(QRectF(4.5, 2.5, 11, 15));
        p.drawLine(QPointF(7, 7), QPointF(13, 7));
        p.drawLine(QPointF(7, 10), QPointF(13, 10));
        p.drawLine(QPointF(7, 13), QPointF(10, 13));
        break;
    case 3: // 命令终端：提示符
        p.drawLine(QPointF(3.5, 6), QPointF(7.5, 10));
        p.drawLine(QPointF(7.5, 10), QPointF(3.5, 14));
        p.drawLine(QPointF(10, 14), QPointF(16, 14));
        break;
    default: // 收起：向下箭头
        p.drawLine(QPointF(5, 8), QPointF(10, 13));
        p.drawLine(QPointF(10, 13), QPointF(15, 8));
        break;
    }
    p.end();
    return QIcon(pm);
}

// v3.4.8（用户要求）：一键恢复所有设置 —— 走 SettingsDialog 的出厂默认值（与「恢复默认」同源），
// 然后立即生效 + 落盘 + 换肤重绘，并给出反馈。
void MainWindow::resetAllSettings()
{
    if (!m_settingsPage) return;
    const AppSettings def = SettingsDialog::factoryDefaults();
    m_settingsPage->setValues(def);
    m_settings = m_settingsPage->currentValues();
    applyAppSettings(m_settings);
    writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                    m_terminal ? m_terminal->history() : QStringList());
    const slime::ThemeMode mode = slime::ThemeManager::modeFromString(m_settings.theme);
    slime::ThemeManager::instance().apply(qApp, mode);
    refreshThemedIcons();
    Logger::info("已恢复所有设置（出厂默认值）并写入 config.json");
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.cee60fc4e81c")));
    setStatus(slime::ui::tr(slime::ui::t("ui.b04e535b0549")));
}

// v3.4.8（用户实测：切换外观主题后图案要立即跟着变，否则看不清）：
//   导航/停靠/收起等图案都是 QPainter 现画的，颜色取自当前主题；主题一切换就重建一遍。
void MainWindow::refreshThemedIcons()
{
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (m_navButtons[i]) {
            m_navButtons[i]->setIcon(v340NavIcon(i < kNavCount ? kNavIconKind[i] : 0, 1.0));
        }
    }
    for (int i = 0; i < m_dockIcons.size(); ++i) {
        if (m_dockIcons[i]) m_dockIcons[i]->setIcon(v340DockIcon(i));
    }
    if (m_dockCollapse) m_dockCollapse->setIcon(v340DockIcon(4));
    std::fprintf(stderr, "[主题] 已按当前主题重绘图案：导航 %d 个 / 停靠 %d 个 + 收起 1 个\n",
                 static_cast<int>(m_navButtons.size()), static_cast<int>(m_dockIcons.size()));
}

// 页面切换（标签页与左导航同源）
void MainWindow::showPage(int page)
{
    if (!m_pageStack || page < 0 || page >= m_pageStack->count()) return;
    // v3.4.8（用户要求：缓存构建作为导航条单独一项）：进「缓存构建」= 开启构建模式，
    // 回「搜索」= 关闭。这样构建页天然只含构建相关内容，搜索页也不会再被"顺带"当成构建入口。
    setBuildModeActive(page == kPageBuild); // v3.4.9：进构建页=开、其它页=关（取代原搜索页开关）
    m_pageStack->setCurrentIndex(page);
    for (int i = 0; i < m_tabButtons.size(); ++i) {
        if (m_tabButtons[i]) m_tabButtons[i]->setChecked(i == page);
    }
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (m_navButtons[i]) m_navButtons[i]->setChecked(i == page);
    }
    m_currentPage = page;
    // v3.4.8（用户要求：动画可延长，但绝不能影响布局）：只对**选中项**重建图案 pixmap 做缩放，
    // iconSize(24) 与文字位置一个字都不动 ⇒ 布局零影响；时长 150 → 280ms，OutCubic 缓动更顺滑。
    if (page >= 0 && page < m_navButtons.size() && m_navButtons[page]) {
        QToolButton* b = m_navButtons[page];
        const int kind = (page < kNavCount) ? kNavIconKind[page] : 0;
        auto* scaleAnim = new QVariantAnimation(b);
        scaleAnim->setDuration(280);
        scaleAnim->setStartValue(0.78);
        scaleAnim->setEndValue(1.0);
        scaleAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(scaleAnim, &QVariantAnimation::valueChanged, this, [this, b, kind](const QVariant& v) {
            b->setIcon(v340NavIcon(kind, v.toDouble()));
        });
        connect(scaleAnim, &QVariantAnimation::finished, this, [this, b, kind]() {
            b->setIcon(v340NavIcon(kind, 1.0));
        });
        scaleAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    // v3.4.8（用户要求）：主按钮随页面切换 —— 构建页 = 开始构建（不要求版本/种子），其它页 = 开始搜索
    if (m_w && m_w->start) {
        m_w->start->setText(page == kPageBuild ? slime::ui::t("ui.b3e1c68ff574")
                                               : slime::ui::t("ui.bd198edcb9cc"));
    }
    if (m_tabHint) {
        m_tabHint->setText(page == kPageBuild
                               ? slime::ui::t("ui.2b4394b6d8b9")
                               : slime::ui::t("ui.4c9e1c3d1b92"));
    }
}

// 停靠面板：0..3 切到对应分类并展开；hideDock 收起（高度归 0，行由 auto 承载）
void MainWindow::showDock(int index)
{
    // v3.10.2：过程面板固定在右列下半；折叠时只隐藏内容，标题行 + 页签常驻，随时可再展开
    if (!m_rightSplit || m_rightSplit->count() < 2) return;
    if (m_pageStack && m_pageStack->currentIndex() != 0) showPage(0);
    QWidget* dock = m_rightSplit->widget(1);
    if (!dock) return;
    const bool alreadyActive = !m_dockCollapsed && m_nav && index >= 0 &&
                               index < m_nav->count() && m_nav->currentRow() == index;
    if (alreadyActive) { hideDock(); return; }   // 点已展开的同一页签 = 收起（保留点击手感）
    dock->setVisible(true);
    dock->setMinimumHeight(120);
    dock->setMaximumHeight(QWIDGETSIZE_MAX);
    if (m_stack) m_stack->setVisible(true);
    m_dockCollapsed = false;
    if (m_dockToggleBtn) m_dockToggleBtn->setText(slime::ui::t("ui.5d5815647c76"));
    if (m_nav && index >= 0 && index < m_nav->count()) m_nav->setCurrentRow(index);
    const int h = (m_dockHeight > 0) ? m_dockHeight : 240;
    const QList<int> sizes = m_rightSplit->sizes();
    if (sizes.size() == 2) {
        const int total = sizes[0] + sizes[1];
        m_rightSplit->setSizes(QList<int>{ (std::max)(160, total - h), h });
    }
    for (int i = 0; i < m_dockIcons.size(); ++i) {
        if (m_dockIcons[i]) m_dockIcons[i]->setChecked(i == index);
    }
}

void MainWindow::hideDock()
{
    // v3.10.2：只折叠内容（标题行 + 页签保留），确保「折叠后还能再展开」
    if (!m_rightSplit || m_rightSplit->count() < 2) return;
    QWidget* dock = m_rightSplit->widget(1);
    if (!dock) return;
    const QList<int> sizes = m_rightSplit->sizes();
    if (sizes.size() == 2 && sizes[1] > 60) m_dockHeight = sizes[1];
    if (m_stack) m_stack->setVisible(false);
    dock->setMinimumHeight(30);
    dock->setMaximumHeight(30);
    m_dockCollapsed = true;
    if (m_dockToggleBtn) m_dockToggleBtn->setText(slime::ui::t("ui.b0e24833f752"));
    for (QPushButton* b : m_dockIcons) {
        if (b) b->setChecked(false);
    }
    setStatus(slime::ui::tr(slime::ui::t("ui.71b2100f6466")));
}

// 其余四页：构建范围 / 设置 / 更新历史 / 控件状态（搜索页是 page0，已由 buildUi 装配）
void MainWindow::buildV340Pages()
{
    // ---------------- 页 2：缓存构建（v3.4.8：独立导航项；唯一可写缓存的模式） ----------------
    {
        // v3.4.8（用户要求）：构建独立成页 —— 本页只放构建相关内容，因此**不需要**选游戏版本、
        // 填世界种子、选搜索模式/算法；这些在搜索页才需要。顶部用一行灰字说明固定下来的选项。
        auto* buildPage = new QWidget(m_pageStack);
        buildPage->setObjectName(QStringLiteral("buildPage"));
        auto* buildPageLay = new QHBoxLayout(buildPage);
        buildPageLay->setContentsMargins(8, 8, 8, 8);
        buildPageLay->setSpacing(10);
        auto* leftCol = new QWidget(buildPage);
        leftCol->setObjectName(QStringLiteral("cacheLeftCol"));
        leftCol->setMinimumWidth(360);
        leftCol->setMaximumWidth(480);
        auto* leftLay = new QVBoxLayout(leftCol);
        leftLay->setContentsMargins(0, 0, 0, 0);
        leftLay->setSpacing(8);
        auto* rightCol = new QWidget(buildPage);
        rightCol->setObjectName(QStringLiteral("cacheRightCol"));
        auto* rightLay = new QVBoxLayout(rightCol);
        rightLay->setContentsMargins(0, 0, 0, 0);
        rightLay->setSpacing(8);
        buildPageLay->addWidget(leftCol, 0);
        buildPageLay->addWidget(rightCol, 1);
        {
            // v3.12.3：缓存页优先展示下载缓存（本版为空状态，后续版本接入）
            auto* download = new QGroupBox(slime::ui::t("ui.cache.download.title"), buildPage);
            download->setProperty("card", true);
            auto* dlLay = new QVBoxLayout(download);
            dlLay->setContentsMargins(8, 4, 8, 6);
            auto* dlEmpty = new QLabel(slime::ui::t("ui.cache.download.empty"), download);
            dlEmpty->setWordWrap(true);
            dlLay->addWidget(dlEmpty);
            auto* dlRo = new QLabel(slime::ui::t("ui.cache.download.readonly"), download);
            dlRo->setObjectName(QStringLiteral("topSummary"));
            dlRo->setWordWrap(true);
            dlLay->addWidget(dlRo);
            m_cacheDownloadSummary = new QLabel(QStringLiteral("0"), download);
            m_cacheDownloadSummary->setObjectName(QStringLiteral("topSummary"));
            dlLay->addWidget(m_cacheDownloadSummary);
            m_cacheDownloadTable = new QTableWidget(0, 4, download);
            m_cacheDownloadTable->setHorizontalHeaderLabels(
                { slime::ui::t("ui.cache.pack.source"), slime::ui::t("ui.cache.pack.range"),
                  slime::ui::t("ui.cache.pack.size"), slime::ui::t("ui.cache.pack.action") });
            m_cacheDownloadTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            m_cacheDownloadTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            m_cacheDownloadTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            m_cacheDownloadTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            m_cacheDownloadTable->verticalHeader()->setVisible(false);
            m_cacheDownloadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            m_cacheDownloadTable->setSelectionMode(QAbstractItemView::NoSelection);
            m_cacheDownloadTable->setMaximumHeight(150);
            dlLay->addWidget(m_cacheDownloadTable);
            auto* manage = new QPushButton(slime::ui::t("ui.cache.download.title"), download);
            manage->setObjectName(QStringLiteral("cacheDownloadManage"));
            dlLay->addWidget(manage);
            QObject::connect(manage, &QPushButton::clicked, this, [this]() {
                if (!m_cacheStore) return;
                CacheDownloadDialog dlg(m_cacheStore.get(), this);
                dlg.onChanged = [this]() { refreshCacheUi(); };
                dlg.exec();
                refreshCacheUi();
            });
            leftLay->addWidget(download);
        }
        {
            auto* mapBox = new QGroupBox(slime::ui::t("ui.cache.map.title"), rightCol);
            mapBox->setProperty("card", true);
            auto* mapLay = new QVBoxLayout(mapBox);
            mapLay->setContentsMargins(8, 4, 8, 6);
            m_cacheCoverageMap = new CacheCoverageMap(mapBox);
            m_cacheCoverageMap->setMinimumHeight(260);
            mapLay->addWidget(m_cacheCoverageMap, 1);
            auto* mapHint = new QLabel(slime::ui::t("ui.cache.map.hint"), mapBox);
            mapHint->setObjectName(QStringLiteral("topSummary"));
            mapHint->setWordWrap(true);
            mapLay->addWidget(mapHint);
            rightLay->addWidget(mapBox, 1);
        }
        {
            auto* local = new QGroupBox(slime::ui::t("ui.cache.local.title"), buildPage);
            local->setProperty("card", true);
            auto* localLay = new QVBoxLayout(local);
            localLay->setContentsMargins(8, 4, 8, 6);
            m_cacheLocalSummary = new QLabel(QStringLiteral("0"), local);
            m_cacheLocalSummary->setObjectName(QStringLiteral("topSummary"));
            localLay->addWidget(m_cacheLocalSummary);
            m_cacheLocalTable = new QTableWidget(0, 4, local);
            m_cacheLocalTable->setHorizontalHeaderLabels(
                { slime::ui::t("ui.cache.pack.source"), slime::ui::t("ui.cache.pack.range"),
                  slime::ui::t("ui.cache.pack.size"), slime::ui::t("ui.cache.pack.action") });
            m_cacheLocalTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            m_cacheLocalTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            m_cacheLocalTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            m_cacheLocalTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            m_cacheLocalTable->verticalHeader()->setVisible(false);
            m_cacheLocalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            m_cacheLocalTable->setSelectionMode(QAbstractItemView::NoSelection);
            m_cacheLocalTable->setMaximumHeight(150);
            localLay->addWidget(m_cacheLocalTable);
            rightLay->addWidget(local);
        }
        {
            auto* info = new QGroupBox(slime::ui::t("ui.55fdaa2e6652"), buildPage);
            info->setProperty("card", true);
            auto* infoLay = new QVBoxLayout(info);
            infoLay->setContentsMargins(8, 4, 8, 6);
            auto* infoText = new QLabel(
                slime::ui::t("ui.cd668bd50c3d"), info);
            infoText->setWordWrap(true);
            infoLay->addWidget(infoText);
            // v3.4.9：构建提示行（范围/位图体积/续传/完成条件）随构建功能一起放在本页
            m_w->buildHint = new QLabel(QString(), info);
            m_w->buildHint->setWordWrap(true);
            m_w->buildHint->setVisible(false);
            infoLay->addWidget(m_w->buildHint);
            auto* fixedLine = new QLabel(
                slime::ui::tr(slime::ui::t("ui.8f50bddd8b88")),
                info);
            fixedLine->setObjectName(QStringLiteral("buildFixedFacts"));
            fixedLine->setWordWrap(true);
            infoLay->addWidget(fixedLine);
            leftLay->addWidget(info);
        }
        // 原有卡片挂进滚动区（内容与搜索页共享同一份范围控件）
        auto* buildScroll = new QScrollArea(buildPage);
        buildScroll->setWidgetResizable(true);
        buildScroll->setFrameShape(QFrame::NoFrame);
        auto* page = new QWidget(buildScroll);
        page->setObjectName(QStringLiteral("buildExtra"));
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(2, 6, 2, 6);
        lay->setSpacing(10);
        m_buildExtra = page;
        buildScroll->setWidget(page);
        leftLay->addWidget(buildScroll, 1);
        m_cacheLegacyNotice = new QLabel(slime::ui::t("ui.cache.old.notice"), buildPage);
        m_cacheLegacyNotice->setObjectName(QStringLiteral("topSummary"));
        m_cacheLegacyNotice->setWordWrap(true);
        m_cacheLegacyNotice->setVisible(false);
        leftLay->addWidget(m_cacheLegacyNotice);
        m_pageStack->addWidget(buildPage);
        auto* cap = new QLabel(slime::ui::t("ui.31dc91dcb1bc"), page);
        cap->setObjectName(QStringLiteral("colCaption"));
        lay->addWidget(cap);
        // 范围与预算（与搜索页同一份：编辑后同步回搜索页控件 → 引擎只读搜索页那份）
        auto* card = new QGroupBox(slime::ui::t("ui.918bc4a13f88"), page);
        card->setProperty("card", true);
        auto* grid = new QGridLayout(card);
        grid->setContentsMargins(8, 4, 8, 6);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(4);
        grid->addWidget(new QLabel(slime::ui::t("ui.785b52eb97c1"), card), 0, 0, Qt::AlignRight);
        auto* rangeRow = new QWidget(card);
        auto* rangeLay = new QHBoxLayout(rangeRow);
        rangeLay->setContentsMargins(0, 0, 0, 0);
        rangeLay->setSpacing(4);
        m_w->bMinX = new QSpinBox(rangeRow);
        m_w->bMaxX = new QSpinBox(rangeRow);
        m_w->bMinZ = new QSpinBox(rangeRow);
        m_w->bMaxZ = new QSpinBox(rangeRow);
        for (QSpinBox* s : { m_w->bMinX, m_w->bMaxX, m_w->bMinZ, m_w->bMaxZ }) {
            s->setRange(-30000000, 30000000);
            s->setFixedWidth(84);
            s->setVisible(false);   // v3.5.16：范围改由「半径」一个输入决定（四边框保留为内部状态）
        }
        // v3.5.16：构建页也只填一个半径（与搜索页双向同步）
        m_w->bRadius = new QSpinBox(rangeRow);
        m_w->bRadius->setRange(1, 15000000);
        m_w->bRadius->setFixedWidth(96);
        m_w->bRadius->setGroupSeparatorShown(true);
        m_w->bRadius->setKeyboardTracking(false);
        m_w->bRadius->setToolTip(slime::ui::t("ui.42ee662c2f29"));
        rangeLay->addWidget(new QLabel(slime::ui::t("ui.ef40894b0c64"), rangeRow));
        rangeLay->addWidget(m_w->bRadius);
        auto* sync = new QLabel(slime::ui::t("ui.672ae5326a09"), rangeRow);
        sync->setObjectName(QStringLiteral("topSummary"));
        rangeLay->addWidget(sync);
        rangeLay->addStretch(1);
        grid->addWidget(rangeRow, 0, 1);
        grid->addWidget(new QLabel(slime::ui::t("ui.0dcf0e012a69"), card), 1, 0, Qt::AlignRight);
        auto* budRow = new QWidget(card);
        auto* budLay = new QHBoxLayout(budRow);
        budLay->setContentsMargins(0, 0, 0, 0);
        budLay->setSpacing(4);
        m_w->bBudget = new QSpinBox(budRow);
        m_w->bBudget->setRange(64, 32768);
        // v3.4.12（用户要求）：单位从输入框里拿出来（框后单独一个小标签）
        m_w->bBudget->setFixedWidth(96);
        budLay->addWidget(m_w->bBudget);
        {
            auto* unit = new QLabel(QStringLiteral("MB"), budRow);
            unit->setObjectName(QStringLiteral("unitLabel"));
            budLay->addWidget(unit);
        }
        auto* budTip = new QLabel(slime::ui::t("ui.434313d50d37"), budRow);
        budTip->setObjectName(QStringLiteral("topSummary"));
        budLay->addWidget(budTip);
        budLay->addStretch(1);
        grid->addWidget(budRow, 1, 1);
        // v3.4.12（用户红框要求）：删掉「操作」整行（预估 / 开始构建 / 中断 + 提示）。
        // 开始与中断统一走工具栏主按钮（进入本页后主按钮自动变成「开始构建」，取消即中断）；
        // 预估改为下面的「构建预估（实时）」卡片，不再需要按钮与弹窗。
        lay->addWidget(card);

        // v3.4.12（用户黄框要求）：预估信息单独成卡片，**实时**跟着范围/预算更新（不再以弹窗形式显示）
        auto* estCard = new QGroupBox(slime::ui::t("ui.1ca7f1347d3a"), page);
        estCard->setProperty("card", true);
        auto* estLay = new QVBoxLayout(estCard);
        estLay->setContentsMargins(8, 4, 8, 6);
        m_w->bEstimate = nullptr; // v3.4.12：预估按钮已移除（信息实时显示）
        m_w->bSummaryLive = new QLabel(slime::ui::t("ui.786d17b3a524"), estCard);
        m_w->bSummaryLive->setObjectName(QStringLiteral("buildEstimateLive"));
        m_w->bSummaryLive->setWordWrap(true);
        estLay->addWidget(m_w->bSummaryLive);
        lay->addWidget(estCard);
        // 构建进度（与主界面共用同一进度组件）
        auto* progCard = new QGroupBox(slime::ui::t("ui.b60d9a287dc2"), page);
        progCard->setProperty("card", true);
        auto* progLay = new QVBoxLayout(progCard);
        progLay->setContentsMargins(8, 4, 8, 6);
        m_w->bBar = new QProgressBar(progCard);
        m_w->bBar->setRange(0, 1000);
        m_w->bBar->setValue(0);
        m_w->bBar->setTextVisible(false);
        progLay->addWidget(m_w->bBar);
        m_w->bText = new QLabel(slime::ui::t("ui.062e5e670f73"), progCard);
        m_w->bText->setWordWrap(true);
        progLay->addWidget(m_w->bText);
        lay->addWidget(progCard);
        // 完成汇总
        auto* sumCard = new QGroupBox(slime::ui::t("ui.86b0b92a37f4"), page);
        sumCard->setProperty("card", true);
        auto* sumLay = new QVBoxLayout(sumCard);
        sumLay->setContentsMargins(8, 4, 8, 6);
        m_w->bSummary = new QLabel(slime::ui::t("ui.6beea7b41bb8"),
                                   sumCard);
        m_w->bSummary->setWordWrap(true);
        sumLay->addWidget(m_w->bSummary);
        auto* pathRow = new QWidget(sumCard);
        auto* pathLay = new QHBoxLayout(pathRow);
        pathLay->setContentsMargins(0, 0, 0, 0);
        pathLay->setSpacing(6);
        m_w->bPath = new QLabel(slime::ui::t("ui.f22200ab7561"), pathRow);
        m_w->bPath->setObjectName(QStringLiteral("resultCaption"));
        m_w->bPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pathLay->addWidget(m_w->bPath, 1);
        auto* openDir = new QPushButton(slime::ui::t("ui.031c10557843"), pathRow);
        openDir->setProperty("mini", true);
        connect(openDir, &QPushButton::clicked, this, [this]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(appDirPath().wstring())));
        });
        pathLay->addWidget(openDir);
        sumLay->addWidget(pathRow);
        lay->addWidget(sumCard);
        // v3.4.8：本页已作为独立导航页加入 m_pageStack（见上），不再挂搜索页参数列、也不再隐藏

        // 连接：预算写回 config（范围与搜索页共用同一组控件）
        // v3.4.9（用户实测：构建范围与搜索范围在部分情况下不同步）：改为**双向同步**，
        // 用 m_rangeSyncGuard 防止来回触发；两侧都挂在 valueChanged 上，因此程序性改动
        //（快速预设 / 命令终端 / 整图确认 / 自检）也会自动跟随。
        const auto mirrorToSearch = [this]() {
            if (!m_w || !m_w->bMinX || m_rangeSyncGuard) return;
            m_rangeSyncGuard = true;
            m_w->minX->setValue(m_w->bMinX->value());
            m_w->maxX->setValue(m_w->bMaxX->value());
            m_w->minZ->setValue(m_w->bMinZ->value());
            m_w->maxZ->setValue(m_w->bMaxZ->value());
            m_rangeSyncGuard = false;
            updateBuildHintUi();
            updateBuildEstimateLive(); // v3.4.12：范围一改，预估立刻跟着变
        };
        for (QSpinBox* s : { m_w->bMinX, m_w->bMaxX, m_w->bMinZ, m_w->bMaxZ }) {
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [mirrorToSearch](int) { mirrorToSearch(); });
        }
        // 反向：搜索页范围（含预设/命令/整图确认等程序性改动）→ 构建页
        for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { pushSearchRangeToBuild(); });
        }
        pushSearchRangeToBuild(); // 初始对齐
        connect(m_w->bBudget, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int mb) {
            m_settings.cacheMaxMB = mb;
            applyAppSettings(m_settings);
            updateBuildEstimateLive(); // v3.4.12：预算一改，预估立刻跟着变
        });
        // v3.4.12：预估按钮已移除（改为实时卡片）
        connect(m_w->bStart, &QPushButton::clicked, this, [this]() {
            setBuildModeActive(true); // v3.4.9：搜索页开关已移除，直接置构建模式
            startSearch();
        });
        connect(m_w->bAbort, &QPushButton::clicked, this, [this]() { cancelSearch(); });
    }

    // ---------------- 页 2：设置（竖向五标签 + 三等分按钮；复用 SettingsDialog 内容） ----------------
    {
        auto* page = new QWidget(m_pageStack);
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        m_settingsPage = new SettingsDialog(m_settings, page); // 作为普通子控件嵌入（而不是弹窗）
        connect(m_settingsPage, &SettingsDialog::contactRequested, this, [this]() { showContact(); });
        // v3.4.2：**设置立即生效** —— 设置页内任意输入控件变化后 60ms 自动应用（无需点「应用/确定」）
        m_settingsApplyTimer = new QTimer(this);
        m_settingsApplyTimer->setSingleShot(true);
        // v3.4.8（用户：设置生效速度调快一点）：250ms → 60ms；仍是「合并连击」写法，
        // 拖滑块/连点复选框只在停手后 60ms 应用一次，不会重复写盘。
        m_settingsApplyTimer->setInterval(60);
        connect(m_settingsApplyTimer, &QTimer::timeout, this, [this]() {
            if (!m_settingsPage || m_settingsSyncGuard) return;
            // v3.4.8（用户报「设置里打开了也不生效」的根因）：必须读**活控件值**。
            // settings() 返回的是「点过『应用』后的快照」，而设置页把「应用」按钮隐藏了 ⇒
            // 之前页面上的改动永远不会进入 m_settings（只有主题因单独读下拉而侥幸生效）。
            m_settings = m_settingsPage->currentValues();
            applyAppSettings(m_settings);      // 立即生效（含主题重放）
            // v3.4.6：主题档位直接读设置页主题下拉的 currentData()（light/dark/system），
            // 拿不到再退回 m_settings.theme；然后强制重放整份 QSS。
            {
                QString themeVal = m_settings.theme;
                if (m_settingsPage) {
                    for (QComboBox* cb : m_settingsPage->findChildren<QComboBox*>()) {
                        const QString data = cb->currentData().toString();
                        if (data == QLatin1String("light") || data == QLatin1String("dark") ||
                            data == QLatin1String("system")) {
                            themeVal = data;
                            break;
                        }
                    }
                }
                m_settings.theme = themeVal;
                const slime::ThemeMode m = slime::ThemeManager::modeFromString(themeVal);
                // v3.4.8（用户：设置生效速度调快一点）：只在档位**真的变化**时重放整份 QSS。
                // 旧实现每次设置改动都重放 10KB 样式表（Qt 要重新 polish 全部控件）⇒ 任何设置项
                // 都要等半秒才生效；档位来源就是本下拉的 currentData()，比较可靠、不会漏换肤。
                if (slime::ThemeManager::instance().mode() != m) {
                    slime::ThemeManager::instance().apply(qApp, m);
                    Logger::info("主题重放：" + slime::ThemeManager::modeDisplayName(m).toStdString() +
                                 "（qss 长度 " + std::to_string(qApp->styleSheet().size()) + "）");
                }
            }
            // v3.4.3：同时**持久化**到 config.json —— 否则重启后设置回到旧值（用户实测）
            // v3.4.11（用户拍板）：改成「合并写」——拖动滑块时每 60ms 都写整份 config.json 会卡；
            //   这里只登记待写，500ms 内没有新改动才真正落盘（拖动结束由 sliderReleased 立即 flush）。
            scheduleConfigWrite();
            syncSettingsPageFromState(); // 让设置页与真实状态保持一致（命令/程序化改动也能反映）
            slime::IncidentLog::note("设置已立即生效并写入 config.json（设置页自动应用）");
        });
        m_settingsPage->setWindowFlags(Qt::Widget);
        lay->addWidget(m_settingsPage, 1);
        // v3.4.2：把设置页里所有可交互控件的变更信号汇总到「立即生效」定时器
        // v3.4.3：SpinBox 用 valueChanged（editingFinished 需要失焦/回车 ⇒ 用户会觉得「没生效」）
        for (QSpinBox* sb : m_settingsPage->findChildren<QSpinBox*>()) {
            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this](int) { m_settingsApplyTimer->start(); });
        }
        for (QDoubleSpinBox* sb : m_settingsPage->findChildren<QDoubleSpinBox*>()) {
            connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [this](double) { m_settingsApplyTimer->start(); });
        }
        for (QComboBox* cb : m_settingsPage->findChildren<QComboBox*>()) {
            connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this](int) { m_settingsApplyTimer->start(); });
            connect(cb, &QComboBox::currentTextChanged, this, [this](const QString&) { m_settingsApplyTimer->start(); });
        }
        for (QCheckBox* cb : m_settingsPage->findChildren<QCheckBox*>()) {
            connect(cb, &QCheckBox::toggled, this, [this](bool) { m_settingsApplyTimer->start(); });
        }
        for (QRadioButton* rb : m_settingsPage->findChildren<QRadioButton*>()) {
            connect(rb, &QRadioButton::toggled, this, [this](bool) { m_settingsApplyTimer->start(); });
        }
        // v3.4.6：删除上一版按「单选按钮文字」匹配的错误接线（设置页主题是 QComboBox）
        // 说明保留：下面用 currentData() 直接读档位。
        if (false)
        // 同时把档位写回 m_settings 并落盘，保证重启后仍是深色。
        {
            auto* content = m_settingsPage->findChild<QWidget*>();
            Q_UNUSED(content);
            for (QRadioButton* rb : m_settingsPage->findChildren<QRadioButton*>()) {
                const QString tx = rb->text();
                slime::ThemeMode mode;
                if (tx.contains(slime::ui::t("ui.30b2c979ac87"))) {
                    mode = slime::ThemeMode::Dark;
                } else if (tx.contains(slime::ui::t("ui.80ec9e2b1bc4"))) {
                    mode = slime::ThemeMode::Light;
                } else if (tx.contains(slime::ui::t("ui.f4bbd91f79ba"))) {
                    mode = slime::ThemeMode::System;
                } else {
                    continue; // 其它单选（默认模式等）不处理
                }
                connect(rb, &QRadioButton::toggled, this, [this, mode](bool on) {
                    if (!on) return;
                    m_settings.theme = slime::ThemeManager::modeToString(mode);
                    slime::ThemeManager::instance().apply(qApp, mode); // 立即换肤
                    writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                                    m_terminal ? m_terminal->history() : QStringList());
                    addFeedback(2, slime::ui::tr(slime::ui::t("ui.b6634ac47177"))
                                       .arg(slime::ThemeManager::modeDisplayName(mode)));
                });
            }
        }
        for (QSlider* sl : m_settingsPage->findChildren<QSlider*>()) {
            connect(sl, &QSlider::valueChanged, this, [this](int) { m_settingsApplyTimer->start(); });
            // v3.4.11：拖动结束立刻落盘（拖动过程中只更新内存 + 延迟合并写，避免每帧写文件导致卡顿）
            connect(sl, &QSlider::sliderReleased, this, [this]() { flushPendingConfigWrite(); });
        }
        for (QLineEdit* le : m_settingsPage->findChildren<QLineEdit*>()) {
            connect(le, &QLineEdit::textChanged, this, [this](const QString&) { m_settingsApplyTimer->start(); });
        }
        // v3.4.2：设置立即生效 ⇒ 隐藏对话框式按钮（应用 / 取消 / 确定 / 恢复默认 / 重置所有设置）
        for (QPushButton* b : m_settingsPage->findChildren<QPushButton*>()) {
            const QString tx = b ? b->text() : QString();
            if (tx.contains(slime::ui::t("ui.f526c89937e1")) || tx.contains(slime::ui::t("ui.4d0b4688c787")) ||
                tx.contains(slime::ui::t("ui.4562024ddec7")) || tx.contains(slime::ui::t("ui.a191935bc678")) ||
                tx.contains(slime::ui::t("ui.ad5d566fb4a3"))) {
                b->hide();
            }
        }
        connect(m_settingsPage, &SettingsDialog::applied, this, [this]() {
            m_settings = m_settingsPage->currentValues();
            applyAppSettings(m_settings);
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.e02dedc30bc9")));
        });
        // v3.4.8（用户实测：找不到「恢复所有设置」）：设置页底部固定放一条操作栏，
        // 按钮是页面自己的子控件（不在 SettingsDialog 内），因此不会被"隐藏对话框按钮"的逻辑连带隐藏。
        {
            auto* bar = new QWidget(page);
            auto* barLay = new QHBoxLayout(bar);
            barLay->setContentsMargins(8, 2, 8, 8);
            barLay->setSpacing(8);
            auto* resetAll = new QPushButton(slime::ui::t("ui.cc342fcf8208"), bar);
            resetAll->setObjectName(QStringLiteral("resetAllSettingsBtn"));
            resetAll->setToolTip(slime::ui::t("ui.2c7b52e20f4f"));
            connect(resetAll, &QPushButton::clicked, this, [this]() { resetAllSettings(); });
            barLay->addWidget(resetAll);
            auto* tip = new QLabel(slime::ui::t("ui.527d252d2fe4"), bar);
            tip->setObjectName(QStringLiteral("topSummary"));
            barLay->addWidget(tip);
            barLay->addStretch(1);
            lay->addWidget(bar);
        }
        m_pageStack->addWidget(page);
    }

    // v3.4.1：原「控件状态」页已按需求移除（设计样张不在应用内展示）
}


void MainWindow::updateBuildEstimateLive()
{
    if (!m_w || !m_w->bSummaryLive) return;
    const int64_t total = buildTotalChunks();
    const double bitmapMB = static_cast<double>(total) / 8.0 / (1024.0 * 1024.0);
    const double legacyMB = static_cast<double>(total) * 0.1 * 8.0 / (1024.0 * 1024.0);
    const int capMB = (m_w->bBudget != nullptr) ? m_w->bBudget->value() : 2048; // 读控件，不读文件
    const bool over = bitmapMB > static_cast<double>(capMB);
    m_w->bSummaryLive->setText(
        slime::ui::tr(slime::ui::t("ui.80e7a66cb853")
            .arg(m_w->minX->value())
            .arg(m_w->maxX->value())
            .arg(m_w->minZ->value())
            .arg(m_w->maxZ->value())
            .arg(static_cast<qlonglong>(total))
            .arg(bitmapMB, 0, 'f', 1)
            .arg(legacyMB, 0, 'f', 1)
            .arg(capMB)
            .arg(over ? slime::ui::tr(slime::ui::t("ui.f5e7cf7143c3"))
                      : slime::ui::tr(slime::ui::t("ui.23c0b832220a")))));
}

// 构建预估（构建前确认弹窗用；与实时卡片共用同一算法）
void MainWindow::showBuildEstimate()
{
    const int64_t total = buildTotalChunks();
    const double bitmapMB = static_cast<double>(total) / 8.0 / (1024.0 * 1024.0);
    const double legacyMB = static_cast<double>(total) * 0.1 * 8.0 / (1024.0 * 1024.0);
    const int64_t capMB = cacheMaxBytes() / (1024 * 1024);
    const QString body = slime::ui::t("ui.284d887f6e44")
                             .arg(m_w->minX->value())
                             .arg(m_w->maxX->value())
                             .arg(m_w->minZ->value())
                             .arg(m_w->maxZ->value())
                             .arg(total)
                             .arg(bitmapMB, 0, 'f', 1)
                             .arg(legacyMB, 0, 'f', 1)
                             .arg(capMB)
                             .arg(bitmapMB > static_cast<double>(capMB)
                                      ? slime::ui::t("ui.f5e7cf7143c3")
                                      : slime::ui::t("ui.23c0b832220a"));
    if (m_w->bSummary) m_w->bSummary->setText(body);
    setStatus(slime::ui::tr(slime::ui::t("ui.8bd24afccf9b"))
                  .arg(total)
                  .arg(bitmapMB, 0, 'f', 1)
                  .arg(capMB));
    if (!m_uiQuiet) {
        QMessageBox box(this);
        box.setWindowTitle(slime::ui::t("ui.dfb05f73ba2c"));
        box.setText(body);
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
    }
}

// ============================================================================
// v3.3.4（任务书 1.2/1.3）：菜单栏与工具栏（全部使用 QMainWindow 原生接口）
// ============================================================================

