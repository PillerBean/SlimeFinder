// v3.12.0：环形搜索、地图联动与挂机点逻辑。

#include <unordered_set>

// v3.14.5（flow-ring-async）：环形规划的工作线程产物。
//   * 只在 worker 中写入，完成后由 GUI 线程的 finishRingSearch() 读取；
//   * 用 shared_ptr 传递，runRingSearch() 返回后不会留下悬空的栈引用；
//   * options 里含 weightAt/rowSlime 等 std::function（各自按值持有数据），可安全跨线程移动。
struct MainWindow::RingSearchResult {
    slime::AfkPlanOptions options;
    std::vector<slime::AfkCandidate> candidates;
    std::shared_ptr<slime::GpuScan> gpu;   // 预筛/覆盖计算用到的 GPU 会话（可为空）
    slime::Version version = slime::Version::Java;
    int64_t seed = 0;
    int biomeVersion = 0;
    Rect region;
    std::chrono::steady_clock::time_point started;
    double computeSecs = 0.0;
    int ringInner = 24;
    int ringOuter = 128;
    int spanX = 0;
    int spanZ = 0;
    int startX = 0;
    int startZ = 0;
    int gridSize = 5;
    int targetCount = 1;
    int requestedTopN = 1;
    bool allowHomogeneousResults = false;
    int simDistance = 0;
    double uiMinDensity = 0.0;
    bool autoPrescan = false;
    bool bedrock = false;
    bool useBiomeWeight = true;   // v3.15.x：Java 环形是否按群系权重（默认开 = 旧行为）
    bool gpuScanEnabled = false;
    bool prescanUsed = false;
    bool prescanFallback = false;
    // v3.15.x（flow-k3-cache）：预筛种子来自缓存位图（cachePrescanSeeds）而非 GPU 全图扫描。
    bool cachePrescanUsed = false;
    bool k3Used = false;
    int k3Candidates = 0;
    bool cancelled = false;
    bool outOfMemory = false;
    bool emptyAfterFilter = false;
    uint64_t runId = 0;
};

void MainWindow::runRingSearch()
{
    if (!m_w) return;
    // v3.14.5：环形规划在后台运行，同一时刻只允许一个。重复入口（命令终端/快捷键）
    //   等价于「取消当前」：先两阶段取消，完成后由 finishRingSearch 复位按钮。
    if (m_ringSearching || m_ringJob.active()) {
        m_ringCancel->store(true, std::memory_order_relaxed);
        m_ringJob.requestCancel();
        setStatus(slime::ui::tr(slime::ui::t("ui.dd9a9f769e57")));
        return;
    }
    // v3.10.12.12：图案模式下「区域形状」面板被隐藏，但环形单选按钮可能仍是 checked。
    //   这里必须优先按当前模式判断，否则会继续跑环形搜索并显示环形结果。
    if (m_w->strictRadio && m_w->strictRadio->isChecked()) {
        setStatus(slime::ui::tr(slime::ui::t("ui.53b2e9a1f672")));
        return;
    }
    const bool bedrock = m_w->bedrockRadio->isChecked();
    int64_t seed = 0;
    const bool seedOk = m_w->seedEdit &&
                        parseSeedForVersion(m_w->seedEdit->text(), bedrock ? slime::Version::Bedrock
                                                                           : slime::Version::Java,
                                            &seed);
    const int minX = m_w->minX->value(), maxX = m_w->maxX->value();
    const int minZ = m_w->minZ->value(), maxZ = m_w->maxZ->value();
    if (maxX < minX || maxZ < minZ) {
        setStatus(slime::ui::tr(slime::ui::t("ui.93ec3b99247b")));
        return;
    }
    if (!seedOk) {
        setStatus(slime::ui::tr(slime::ui::t("ui.d2d6c0b7fdb4")));
        return;
    }
    const int spanX = maxX - minX + 1, spanZ = maxZ - minZ + 1;
    // v3.5.16：环形范围跟随「半径」设定值（不再卡 4096x4096）。规划器改成按 z 带流式处理，
    //   内存与范围无关；这里只按「区块总数」兜底（4 亿区块 ≈ 1~3 秒），并给出耗时提示。
    {
        const long long cells = static_cast<long long>(spanX) * static_cast<long long>(spanZ);
        // v3.10.2（用户反馈：环形范围太小、无法全图）：上限放宽到 Java 整图规模
    //   （±1,875,000 区块 ⇒ 3,750,001² ≈ 1.4e13 格）。快速模式成本与面积无关（抽样），
    //   但全图级仍可能需要数分钟；状态栏给出提示，搜索期间可取消。
        if (cells > 16000000000000LL) {
            setStatus(slime::ui::tr(slime::ui::t("ui.dbcd349ec8e3")).arg(spanX).arg(spanZ));
            return;
        }
        if (cells > 1000000000000LL) {
            setStatus(slime::ui::tr(slime::ui::t("ui.84839e3c68cb"))
                          .arg(spanX)
                          .arg(spanZ));
        } else if (cells > 40000000LL) {
            std::fprintf(stderr, "[环形] 大范围：%dx%d 区块 = %.1f 万，预计 0.3~3 秒\n", spanX, spanZ,
                         static_cast<double>(cells) / 10000.0);
        }
    }
    // 环形模式此前没有“开始前预计耗时”弹窗；按扫描档位给出明确预估。
    if (!m_uiQuiet) {
        const int confirmMode = m_w->ringScanCombo ? m_w->ringScanCombo->currentData().toInt() : 0;
        QString model;
        if (confirmMode == 2) model = slime::ui::t("ui.ring_time_square");
        else if (confirmMode == 3) model = slime::ui::t("ui.ring_time_k3");
        else if (confirmMode == 1) model = slime::ui::t("ui.ring_time_exact");
        else model = slime::ui::t("ui.ring_time_fast");
        const auto answer = QMessageBox::question(
            this, slime::ui::t("ui.ring_time_confirm_title"),
            slime::ui::t("ui.ring_time_confirm_body")
                .arg(spanX).arg(spanZ).arg(model));
        if (answer != QMessageBox::Yes) return;
    }
    // v3.5.8：环形 = 怪物生成环带 [24, 128]（过近不刷、过远消失）
    // v3.5.18：环形内外半径可在「设置 → 高级」里精调（默认 24 / 128）
    const int kRingInner = std::clamp(m_settings.afkRingInner, 0, 64);
    int kRingOuter = std::clamp(m_settings.afkRingOuter, 16, 1024);
    // v3.6.18：模拟距离会收窄环带外沿（16 格/区块）；>=8 区块时不影响默认 128
    if (m_w->simDistance) {
        const int simBlocks = m_w->simDistance->value() * 16;
        if (simBlocks < kRingOuter) kRingOuter = (std::max)(kRingInner + 8, simBlocks);
    }
    slime::AfkPlanOptions o;
    o.area = slime::Rect{ minX, maxX, minZ, maxZ };
    o.radius = kRingOuter;
    m_lastRingOuter = kRingOuter;   // v3.6.18：自检/状态可见的有效外沿
    o.innerRadius = kRingInner;
    o.topN = (std::max)(1, static_cast<int>(m_w->targetCount ? m_w->targetCount->value() : 5));
    const int requestedTopN = o.topN;
    // 同质化关闭时不扩大候选池：质量种类不足时返回少于 N，并明确提示。
    o.minDensity = m_w->minDensity ? (std::max)(0.0, (std::min)(1.0, m_w->minDensity->value())) : 0.0;
    // v3.6.0：目标数量较大时放宽最小间距（24 → 8 区块），否则密集区域挑不出足够多结果
    o.minSeparation = (requestedTopN > 8) ? 8 : 24;
    o.coarseStep = std::clamp(m_settings.ringCoarseStep, 1, 64);
    // v3.10.3：超大范围自动加大粗扫步长 —— 保持「粗扫 ≤ ~512² 个采样中心」，
    //   否则全图级环形搜索会跑几个数量级太久（表现为界面像卡死）。
    {
        const int64_t spanBlocks = static_cast<int64_t>((std::max)(spanX, spanZ)) * 16;
        const int64_t autoStep = std::clamp<int64_t>(spanBlocks / 512, 1, 1 << 20);
        if (autoStep > o.coarseStep) {
            std::fprintf(stderr, "[环形] 范围极大：粗扫步长 %d → %lld（结果仍精确到格）\n", o.coarseStep,
                         static_cast<long long>(autoStep));
            o.coarseStep = static_cast<int>(autoStep);
        }
    }
    o.statBands = std::clamp(m_settings.ringStatBands, 1, 16);
    // v3.7.6：环形覆盖可交给 GPU（失败/太小/加权会自动走 CPU）
    o.gpuScan = (m_gpuScan && m_gpuAvailable) ? m_gpuScan.get() : nullptr;
    // v3.5.17：0 = 快速（粗扫+精修），1 = 精确（逐区块）
    // v3.10.12.11：2 = 自动正方形预筛 + 环形精修
    // v3.15.x：3 = 方形预筛 + k=3 环带上界复核 + 精修
    const int ringScanMode = m_w->ringScanCombo ? m_w->ringScanCombo->currentData().toInt() : 0;
    const bool k3Mode = (ringScanMode == 3);
    const bool autoPrescan = (ringScanMode == 2 || k3Mode);
    o.scanStride = (ringScanMode == 1) ? 1 : 0;
    // v3.5.29：精确模式（逐区块）有内存/耗时上限，超了直接提示而不是跑崩
    if (o.scanStride == 1 && static_cast<long long>(spanX) * spanZ > 4000000LL) {
        setStatus(slime::ui::tr(slime::ui::t("ui.c11e33ee027d"))
                      .arg(spanX).arg(spanZ).arg(static_cast<qlonglong>(static_cast<long long>(spanX) * spanZ / 10000LL)));
        return;
    }
    const slime::Version v = bedrock ? slime::Version::Bedrock : slime::Version::Java;
    // v3.15.x: Bedrock 且缓存完整覆盖本区域时，环形/k3 的 predicate 直接读缓存。
    std::shared_ptr<slime::CacheManager> ringCache;
    if (bedrock && m_w->useCache && m_w->useCache->isChecked() && m_cacheStore) {
        const slime::cache::CacheSnapshot snap = m_cacheStore->snapshot();
        if (m_cacheStore->coversRange(snap, o.area)) {
            auto cm = std::make_shared<slime::CacheManager>();
            std::string err;
            if (m_cacheStore->fillCacheManagerRange(snap, *cm, o.area, &err) && cm->hasData()) {
                ringCache = cm;
                o.cache = cm.get();
                Logger::info("环形缓存：区域完整覆盖，使用 CacheManager");
            }
        }
    }
    const auto pred = [v, seed, ringCache](int cx, int cz) {
        if (ringCache) {
            const int hit = ringCache->lookup(cx, cz);
            if (hit >= 0) return hit == 1;
        }
        return slime::SlimeEngine::isSlimeChunk(v, seed, cx, cz);
    };
    // v3.5.15：Java 版给规划器一个「整行批量判定」回调（行基座只算一次，占用图快数倍）
    if (!bedrock)
        o.rowSlime = [seed](int cz, int cx0, int n, uint8_t* out) {
            slime::SlimeEngine::fillSlimeRowJava(seed, cz, cx0, n, out);
        };
    // v3.15.x：群系权重可关（仅 Java 环形）。默认勾选 = 与旧行为完全一致；
    //   关闭后不建群系图、不设 weightAt，排序/统计退化为纯方块数口径（Bedrock 始终如此）。
    const bool useBiomeWeight = !bedrock && (!m_w->weightBiome || m_w->weightBiome->isChecked());
    // v3.5.7：Java 版把「群系刷怪权重」一并算进去（建农场时效率优先）
    // v3.5.7：复用同一个 BiomeMap（setupGenerator 一次性 ~0.8s，反复创建会明显拖慢连续搜索）
    if (useBiomeWeight) {
        if (!m_ringBiome || m_ringBiomeVersion != m_mapJavaVersionId || m_ringBiomeSeed != seed) {
            m_ringBiome = std::make_unique<slime::BiomeMap>();
            if (!m_ringBiome->init(m_mapJavaVersionId, seed)) m_ringBiome.reset();
            m_ringBiomeVersion = m_mapJavaVersionId;
            m_ringBiomeSeed = seed;
        }
    } else {
        m_ringBiome.reset();
    }
    slime::BiomeMap* biome = m_ringBiome.get();
    if (useBiomeWeight && biome && biome->valid()) {
        // 一次性预取权重网格：v3.5.14 起按范围自适应 1:64 / 1:128 / 1:256
        //（只用于候选排序；最终统计仍是 1:4 逐格精确。大范围用 1:256，采样量只有 1:64 的 1/16）
        int gs = slime::AfkPlanner::rankingGridScale((std::max)(spanX, spanZ));
        const int pad = (o.radius + 15) / 16 + 2;
        const int b0x = (minX - pad) * 16, b0z = (minZ - pad) * 16;
        const int b1x = (maxX + pad) * 16, b1z = (maxZ + pad) * 16;
        // v3.10.3（用户反馈：启动超大范围搜索界面卡住）：权重网格原先按 1:64~1:256 预取，
        //   全图级会尝试分配数百 GB ⇒ 假死。这里自动放大格子，直到网格 ≤ 200 万格（≤16MB）。
        {
            const int64_t spanBx = static_cast<int64_t>(b1x) - b0x + 1;
            const int64_t spanBz = static_cast<int64_t>(b1z) - b0z + 1;
            while (static_cast<int64_t>((spanBx / gs) + 1) * ((spanBz / gs) + 1) > 2000000LL &&
                   gs < (1 << 20)) {
                gs <<= 1;
            }
        }
        const int c64x0 = b0x >= 0 ? b0x / gs : -((-b0x + gs - 1) / gs);
        const int c64z0 = b0z >= 0 ? b0z / gs : -((-b0z + gs - 1) / gs);
        const int c64x1 = b1x >= 0 ? b1x / gs : -((-b1x + gs - 1) / gs);
        const int c64z1 = b1z >= 0 ? b1z / gs : -((-b1z + gs - 1) / gs);
        const int gw = c64x1 - c64x0 + 1, gh = c64z1 - c64z0 + 1;
        std::vector<int> ids;
        std::vector<float> wgrid;
        // v3.5.24：排序权重网格按「版本+种子+范围+网格档位」记忆一格 —— 连续同类搜索可省 2~6 ms
        struct WGridCache {
            bool valid = false;
            int versionId = -1, gs = 0, x0 = 0, z0 = 0, w = 0, h = 0;
            int64_t seed = 0;
            std::vector<float> data;
        };
        static WGridCache s_wgrid;
        const bool cacheHit = s_wgrid.valid && s_wgrid.versionId == m_mapJavaVersionId && s_wgrid.seed == seed &&
                              s_wgrid.gs == gs && s_wgrid.x0 == c64x0 && s_wgrid.z0 == c64z0 &&
                              s_wgrid.w == gw && s_wgrid.h == gh;
        if (cacheHit) {
            wgrid = s_wgrid.data;
        } else if (gw > 0 && gh > 0 && biome->genArea(gs, c64x0 * gs, c64z0 * gs, gw, gh, ids)) {
            wgrid.resize(ids.size());
            for (size_t i = 0; i < ids.size(); ++i)
                wgrid[i] = static_cast<float>(slime::spawnWeight(ids[i], slime::Platform::Java));
            s_wgrid.valid = true;
            s_wgrid.versionId = m_mapJavaVersionId;
            s_wgrid.seed = seed;
            s_wgrid.gs = gs;
            s_wgrid.x0 = c64x0;
            s_wgrid.z0 = c64z0;
            s_wgrid.w = gw;
            s_wgrid.h = gh;
            s_wgrid.data = wgrid;
        }
        if (!wgrid.empty()) {
            o.weightAt = [c64x0, c64z0, gw, gh, wgrid, gs](int bx, int bz) {
                const int cx = bx >= 0 ? bx / gs : -((-bx + gs - 1) / gs);
                const int cz = bz >= 0 ? bz / gs : -((-bz + gs - 1) / gs);
                const int ix = cx - c64x0, iz = cz - c64z0;
                if (ix < 0 || iz < 0 || ix >= gw || iz >= gh) return 1.0;
                return static_cast<double>(wgrid[static_cast<size_t>(iz) * gw + ix]);
            };
            std::fprintf(stderr, "[环形] 权重网格 %dx%d 格（1:%d 预取）\n", gw, gh, gs);
        }
    }
    // v3.6.19：环形搜索同样要先清掉上一次的结果（此前只有引擎路径清），
    //   否则「环形搜索无果」时列表/地图仍显示旧内容，切换方式后看起来像没生效。
    clearResultsForNewSearch();
    // v3.14.5（flow-ring-async）：环形规划完全异步 —— 结果放进 shared_ptr，
    //   runRingSearch() 只准备参数、切运行态并启动 SearchJob，随后立即返回。
    auto state = std::make_shared<RingSearchResult>();
    state->options = std::move(o);
    state->version = v;
    state->seed = seed;
    state->biomeVersion = m_mapJavaVersionId;
    state->region = slime::Rect{ minX, maxX, minZ, maxZ };
    state->started = std::chrono::steady_clock::now();
    state->ringInner = kRingInner;
    state->ringOuter = kRingOuter;
    state->spanX = spanX;
    state->spanZ = spanZ;
    state->startX = m_w->startX ? m_w->startX->value() : 0;
    state->startZ = m_w->startZ ? m_w->startZ->value() : 0;
    state->gridSize = m_w->gridSize ? m_w->gridSize->value() : 5;
    state->uiMinDensity = m_w->minDensity ? m_w->minDensity->value() : 0.3;
    state->targetCount = (std::max)(1, static_cast<int>(m_w->targetCount ? m_w->targetCount->value() : 1));
    state->requestedTopN = requestedTopN;
    state->allowHomogeneousResults = (m_presetHomogeneous >= 0) ? (m_presetHomogeneous != 0)
                                                                 : m_settings.allowHomogeneousResults;
    state->simDistance = m_w->simDistance ? m_w->simDistance->value() : 0;
    state->autoPrescan = autoPrescan;
    state->k3Used = k3Mode;
    state->bedrock = bedrock;
    state->useBiomeWeight = useBiomeWeight;
    state->gpuScanEnabled = (state->options.gpuScan != nullptr);
    // 预筛/覆盖计算用到的 GPU 会话用 shared_ptr 持有，避免后台线程读到已被释放的成员。
    if (m_gpuScan && m_gpuAvailable) state->gpu = m_gpuScan;

    m_ringCancel->store(false, std::memory_order_relaxed);
    m_ringSearching = true;
    m_ringFinishDiscard = false;
    {
        const QString scanText = k3Mode ? slime::ui::t("ui.ring_scan_k3")
                                        : (autoPrescan ? slime::ui::t("ui.7ae0cd5f9a0b")
                                                       : ((state->options.scanStride == 1)
                                                              ? slime::ui::t("ui.277ba15949a6")
                                                              : slime::ui::t("ui.6c29e3499ee0")));
        setStatus(slime::ui::tr(slime::ui::t("ui.09d9fb4b92b6"))
                      .arg(spanX).arg(spanZ).arg(scanText));
    }
    // v3.6.0（用户反馈：点开始后按钮卡在"按下"、取消不可点）——
    //   一开始就切到运行态，并把按钮的按下/焦点状态清掉
    if (m_w->start) {
        m_w->start->setDown(false);
        m_w->start->clearFocus();
        m_w->start->setEnabled(false);
    }
    if (m_w->cancel) m_w->cancel->setEnabled(true);
    if (m_w->progress) {
        m_w->progress->setStyleSheet(QString());
        m_w->progress->setRange(0, 0);   // v3.5.26：环形没有细粒度进度 → 忙碌滚动
        if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("…"));
    }
    // v3.14.5（flow-ring-async）：工作线程只做纯计算，结果/标志直接写入 shared state；
    //   捕获的都是值拷贝（含 state），runRingSearch() 返回后不会悬空。
    // v3.15.x（flow-ring-cancel2）：取消标志也按值捕获 shared_ptr —— worker 不再通过 this
    //   访问 MainWindow 成员，窗口析构时即使线程被安全丢弃也不会 UAF。
    const auto ringCancel = m_ringCancel;
    // v3.15.x（flow-k3-cache）：ringCache 按值捕获 shared_ptr —— 后台线程可安全读取位图，
    //   且生命周期不再依赖 runRingSearch 的栈（pred 也持有同一份引用）。
    const auto ringRunner = [state, ringCancel, ringCache, autoPrescan, k3Mode, minX, maxX, minZ, maxZ, v,
                             seed, pred, bedrock](const SearchOptions&,
                                      const std::atomic<bool>& jobCancel,
                                      const ProgressFn&) -> SearchOutcome {
        // v3.5.26：后台降到「低于普通」优先级，避免把 UI 线程饿死（精确模式尤其明显）
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        // 引用 state 里的成员：写法与同步版一致，但生命周期由 shared_ptr 保证。
        auto& o = state->options;
        auto& cands = state->candidates;
        const int biomeVersionForStats = state->biomeVersion;
        auto& ringEmptyAfterFilter = state->emptyAfterFilter;
        auto& ringOutOfMemory = state->outOfMemory;
        auto& prescanUsed = state->prescanUsed;
        auto& prescanFallback = state->prescanFallback;
        auto& ringCancelled = state->cancelled;
        const auto cancelRequested = [&jobCancel, ringCancel]() {
            return jobCancel.load(std::memory_order_relaxed) ||
                   ringCancel->load(std::memory_order_relaxed);
        };
        if (cancelRequested()) ringCancel->store(true, std::memory_order_relaxed);
        try {
            if (autoPrescan) {
                // v3.15.x（flow-k3-cache）：预设参数对两种种子来源完全一致。
                slime::DensityPrescanParams pp;
                pp.version = v;
                pp.worldSeed = seed;
                pp.minX = minX;
                pp.maxX = maxX;
                pp.minZ = minZ;
                pp.maxZ = maxZ;
                pp.gridSize = 15;
                pp.minDensity = 0.18;
                pp.bandRows = 4096;
                pp.topPerBand = 512;
                pp.maxSeeds = 4000;
                pp.minSeparation = 16;
                std::vector<slime::DensitySeed> seeds;
                std::string perr;
                // v3.15.x（flow-k3-cache，集成实测后定稿）预筛来源优先级：
                //   1) GPU 全图预筛 —— 实测生产参数（10% 命中、阈值 0.18）下比 CPU 位图预筛快
                //      1.5~3.5×（8192²：69ms vs 111ms；16384²：190ms vs 662ms），默认仍走它；
                //   2) GPU 不可用/失败 且 Bedrock 缓存完整覆盖本区域 ⇒ cachePrescanSeeds
                //      （确定性种子、免 GPU 依赖；无 GPU 用户首次获得完整质量的预筛精修/k3 档）；
                //   3) 两路都不可用 ⇒ 回退快速抽样（原行为，语义不变）。
                // 数据与取舍见 docs/planning/k3_cache_prescan.md §6/§7。
                const bool gpuPrescanOk =
                    state->gpu && state->gpu->prescanDensitySeeds(pp, *ringCancel, seeds, &perr);
                bool cachePrescanOk = false;
                if (!gpuPrescanOk && ringCache) {
                    std::string cerr;
                    cachePrescanOk =
                        slime::cachePrescanSeeds(*ringCache, pp, ringCancel.get(), seeds, &cerr);
                    if (!cachePrescanOk && perr.empty()) perr = cerr;
                    if (!cachePrescanOk) {
                        std::fprintf(stderr, "[环形] 缓存预筛不可用，回退快速抽样：%s\n",
                                     cerr.empty() ? "原因未知" : cerr.c_str());
                    }
                }
                if ((gpuPrescanOk || cachePrescanOk) && !seeds.empty() && !cancelRequested()) {
                    o.seedCandidates.reserve(seeds.size());
                    for (const auto& sd : seeds) o.seedCandidates.emplace_back(sd.cx, sd.cz);
                    prescanUsed = true;
                    if (cachePrescanOk) {
                        state->cachePrescanUsed = true;
                        std::fprintf(stderr, "[环形] 缓存预筛：%zu 个分散种子（阈值 %.2f）\n",
                                     seeds.size(), pp.minDensity);
                    } else {
                        std::fprintf(stderr, "[环形] 正方形预筛：%zu 个分散种子（阈值 %.2f）\n",
                                     seeds.size(), pp.minDensity);
                    }
                    if (k3Mode) {
                        AfkPlanOptions base = o;
                        const auto baseCands = slime::AfkPlanner::plan(base, pred, ringCancel.get());
                        const int lower = baseCands.empty()
                                              ? 0
                                              : static_cast<int>(baseCands.front().coveredBlocks);
                        const auto filtered = slime::AfkPlanner::k3UpperBoundSeeds(
                            o, o.seedCandidates, pred, lower);
                        state->k3Candidates = static_cast<int>(filtered.size());
                        if (!filtered.empty()) o.seedCandidates = filtered;
                    }
                } else {
                    prescanFallback = true;
                    o.seedCandidates.clear();
                    o.scanStride = 0;
                    if (!state->gpu) {
                        std::fprintf(stderr, "[环形] GPU 不可用%s，正方形预筛回退快速抽样%s%s\n",
                                     ringCache ? "且缓存预筛不可用" : "", perr.empty() ? "" : "：",
                                     perr.c_str());
                    } else {
                        std::fprintf(stderr, "[环形] 正方形预筛不可用，回退快速抽样：%s\n", perr.c_str());
                    }
                }
            }
            if (cancelRequested()) {
                ringCancelled = true;
            } else {
                cands = slime::AfkPlanner::plan(o, pred, ringCancel.get());
                // 规划器已在主循环内检查取消；返回后仍复核一次并丢弃不完整结果。
                if (cancelRequested()) {
                    ringCancelled = true;
                    cands.clear();
                }
            }
        } catch (const std::bad_alloc&) {
            ringOutOfMemory = true;
        } catch (...) {
            ringOutOfMemory = true;
        }
    // v3.5.9：plan() 的粗扫/卷积是近似值 —— 对每个候选做「严格 1 格」重算：
    //   用游戏地形的最小单位 1:4 群系网格逐格累加，得到游戏口径的有效格与总权重。
    if (!cands.empty()) {
        // v3.5.12：8 个候选的 1:4 群系网格互相独立 → OpenMP 并行（每个线程用自己的 BiomeMap：
        //           cubiomes 生成器有状态，不可共用；init 仅 ~0.1ms，开销可忽略）。
        //           这一步原本是环形搜索的最大头（8 个候选 × 18ms ≈ 150ms）。
        // v3.5.14：把「候选 × 行带」摊成任务并行走（8 个候选 × 6 条行带 = 48 个任务），
        //           每个线程自带一个 BiomeMap，不必每个候选都重建生成器。实现见 core（基准也复用它）。
        const slime::AfkPlanner::StrictStatsInfo info =
            slime::AfkPlanner::measureStrictForCandidates(cands, o, pred, biomeVersionForStats, seed,
                                                          !bedrock && state->useBiomeWeight,
                                                          ringCancel.get());
        if (ringCancel->load(std::memory_order_relaxed)) {
            ringCancelled = true;
            cands.clear();
        }
        if (info.cells > 0)
            std::fprintf(stderr, "[环形] 候选权重网格：按需采样 %zu / %zu 格（%.1f%%）\n", info.samples,
                         info.cells, info.samples * 100.0 / static_cast<double>(info.cells));
        const bool weighted = static_cast<bool>(o.weightAt);
        std::stable_sort(cands.begin(), cands.end(),
                         [weighted](const slime::AfkCandidate& a, const slime::AfkCandidate& b) {
                             return weighted ? (a.coveredWeight > b.coveredWeight)
                                             : (a.coveredBlocks > b.coveredBlocks);
                         });

        // v3.5.16（用户反馈）：plan() 里用的是近似覆盖率，严格重算后的密度可能低于阈值。
        //   这里按严格值再筛一遍，并按「目标数量」截断 —— 保证结果密度一定 >= 设定的最小密度。
        if (o.minDensity > 0.0) {
            const size_t before = cands.size();
            cands.erase(std::remove_if(cands.begin(), cands.end(),
                                       [&](const slime::AfkCandidate& c) {
                                           // v3.5.26：按**界面显示值**（3 位小数）比较，
                                           //   避免 0.2996 在表格里显示成 30.0% 却又不满足 0.300 的矛盾
                                           const double shown = std::round(c.coveragePercent / 100.0 * 1000.0) / 1000.0;
                                           const double want = std::round(o.minDensity * 1000.0) / 1000.0;
                                           return shown < want;
                                       }),
                        cands.end());
            if (cands.size() != before) {
                std::fprintf(stderr, "[\u73af\u5f62] \u6700\u5c0f\u5bc6\u5ea6\u590d\u6838\uff1a%zu \u2192 %zu \u4e2a\n", before,
                             cands.size());
                if (cands.empty()) ringEmptyAfterFilter = true;
            }
        }
        if (!state->allowHomogeneousResults) {
            std::unordered_set<int64_t> seenQuality;
            std::vector<slime::AfkCandidate> kept;
            kept.reserve(cands.size());
            for (const auto& c : cands) {
                if (!seenQuality.insert(c.coveredBlocks).second) continue;
                kept.push_back(c);
            }
            cands.swap(kept);
        }
        if (static_cast<int>(cands.size()) > state->requestedTopN)
            cands.resize(static_cast<size_t>(state->requestedTopN));
        // v3.6.1：结果少于目标数量时在日志里说明被什么限制（避免以为程序只能出这么几个）
        if (static_cast<int>(cands.size()) < state->requestedTopN)
            std::fprintf(stderr, "[环形] 结果 %zu < 目标 %d：受最小密度(%.3f)/间距(%d)/质量种类限制\n",
                         cands.size(), state->requestedTopN, o.minDensity, o.minSeparation);
        if (!cands.empty()) {
            std::fprintf(stderr, "[环形] 严格 1 格重算完成：%zu 个候选（最优 %lld 格 / 权重 %lld）\n",
                         cands.size(), static_cast<long long>(cands.front().coveredBlocks),
                         static_cast<long long>(cands.front().coveredWeight));
        }
    }

        // 取消发生在严格统计期间：最重的一步跑完后不再把结果交给界面。
        if (cancelRequested() && !cands.empty()) {
            ringCancelled = true;
            cands.clear();
        }
        state->computeSecs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - state->started).count();
        return SearchOutcome{};
    };
    state->runId = m_ringJob.runId() + 1;
    {
        // 允许本窗口接收完成事件。析构会把 window 置空，之后 worker 不会再投递。
        std::lock_guard<std::mutex> lk(m_ringUiGate->mu);
        m_ringUiGate->window = this;
    }
    const auto ringUiGate = m_ringUiGate;
    const auto ringDone = [ringUiGate, state](const slime::SearchReport&) {
        // DoneFn 在工作线程执行：只在窗口仍存活时经 Qt 队列投递到 GUI 线程。
        // v3.15.x：用 shared gate + 互斥锁与「窗口析构」握手 —— 持锁期间才允许调用
        //   invokeMethod，析构方先把 window 置空，因此不会向已销毁的 this 投递。
        std::lock_guard<std::mutex> lk(ringUiGate->mu);
        MainWindow* w = ringUiGate->window;
        if (!w) return;
        QMetaObject::invokeMethod(w, [w, state]() { w->finishRingSearch(state); }, Qt::QueuedConnection);
    };
    if (!m_ringJob.start(SearchOptions{}, SearchProgressSink{}, ringDone, ringRunner)) {
        // 启动失败（线程未创建/状态机拒绝）也必须恢复界面，不能在运行态卡死。
        m_ringSearching = false;
        updateStartEnabled();
        if (m_w->start) m_w->start->setEnabled(true);
        if (m_w->cancel) {
            m_w->cancel->setEnabled(false);
            m_w->cancel->setDown(false);
        }
        if (m_w->progress) {
            m_w->progress->setRange(0, 1000);
            m_w->progress->setValue(1000);
            if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("100%"));
        }
        setStatus(slime::ui::tr(slime::ui::t("ui.ring_async_start_failed")));
        Logger::warning("环形搜索：后台 SearchJob 启动失败");
        return;
    }
    std::fprintf(stderr, "[环形] 后台规划已启动（runId=%llu）\n",
                 static_cast<unsigned long long>(state->runId));
}

// v3.14.5（flow-ring-async）：环形规划完成后的 GUI 线程收尾。
//   只由 ringDone 投递的队列回调进入；所有控件/地图/遥测操作都在 GUI 线程执行。
void MainWindow::finishRingSearch(const RingSearchResultPtr& result)
{
    if (!result) return;
    // 陈旧完成事件（已被新任务取代）不能改动当前界面。
    if (result->runId != 0 && result->runId != m_ringJob.runId()) return;
    // closeEvent 已判定可以关闭窗口：丢弃排队回调，避免窗口销毁过程中触碰控件。
    if (m_ringFinishDiscard) return;

    m_ringSearching = false;
    updateStartEnabled();

    const std::vector<slime::AfkCandidate>& cands = result->candidates;
    const slime::AfkPlanOptions& o = result->options;
    if (result->cancelled) {
        setStatus(slime::ui::tr(slime::ui::t("ui.397b1272aec8")));
    } else if (result->k3Used && result->prescanUsed) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.ring_k3_used"))
                           .arg(result->k3Candidates));
    } else if (result->autoPrescan && result->prescanUsed) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.29d84f827fc6")));
    } else if (result->autoPrescan && result->prescanFallback) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.c736d29daa5b")));
    }
    // v3.15.x（flow-k3-cache）：预筛来源（缓存位图 / GPU）只进日志，不改 UI 文案。
    if (result->autoPrescan && result->prescanUsed) {
        std::fprintf(stderr, "[环形] 预筛来源：%s（cachePrescanUsed=%s）\n",
                     result->cachePrescanUsed ? "缓存位图" : "GPU",
                     result->cachePrescanUsed ? "true" : "false");
    }
    if (m_resultsNotice) {
        const int got = static_cast<int>(cands.size());
        const int want = (std::max)(1, result->targetCount);
        if (got == 0)
            m_resultsNotice->setText(slime::ui::t("ui.e682244d5bfe"));
        else if (got < want)
            m_resultsNotice->setText(slime::ui::t("ui.af6b2c0e44d3")
                                         .arg(got).arg(want));
        else
            m_resultsNotice->setText(QString());
        m_resultsNotice->setVisible(!m_resultsNotice->text().isEmpty());
        if (!result->allowHomogeneousResults && got < result->requestedTopN) {
            addFeedback(2, slime::ui::t("ui.homogeneous_fewer").arg(got).arg(result->requestedTopN));
        }
    }
    // v3.6.0：结束（含异常/空结果）一律恢复按钮态
    if (m_w->start) m_w->start->setEnabled(true);
    if (m_w->cancel) m_w->cancel->setEnabled(false);
    if (m_w->cancel) m_w->cancel->setDown(false);
    if (m_w->progress) {
        m_w->progress->setRange(0, 1000);
        m_w->progress->setValue(1000);
        if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("100%"));
    }
    if (result->cancelled) {
        m_lastAfk.clear();
        m_lastOutcome = SearchOutcome{};
        m_hasLastOutcome = false;
        return;
    }
    if (result->version == Version::Bedrock && !m_bedrockRiverNoticeShown) {
        m_bedrockRiverNoticeShown = true;
        addFeedback(1, slime::ui::t("ui.bedrock_river_bug"));
        Logger::warning("基岩版 MCPE-161245：河流群系中的环形覆盖可能高估；本工具未自动排除河流群系");
    }
    if (result->outOfMemory)
        setStatus(slime::ui::tr(slime::ui::t("ui.a7216f4a8a80")));
    else if (result->emptyAfterFilter)
        setStatus(slime::ui::tr(slime::ui::t("ui.b3df7b1e43a6"))
                      .arg(o.minDensity, 0, 'f', 3));
    const int stride = slime::AfkPlanner::coarseChunkStride((std::max)(result->spanX, result->spanZ));
    const double secs = result->computeSecs > 0.0
                            ? result->computeSecs
                            : std::chrono::duration<double>(std::chrono::steady_clock::now() - result->started)
                                  .count();
    m_lastAfk = cands;

    SearchOutcome out;
    out.ok = true;
    out.results.reserve(cands.size());
    for (const auto& cd : cands) {
        slime::MatchResult r;
        r.minX = r.maxX = (cd.x >= 0 ? cd.x / 16 : -((-cd.x + 15) / 16));
        r.minZ = r.maxZ = (cd.z >= 0 ? cd.z / 16 : -((-cd.z + 15) / 16));
        r.density = cd.coveragePercent / 100.0;
        out.results.push_back(r);
    }
    out.totalFound = static_cast<int>(cands.size());
    // v3.10.12.11：环形「快速」在超大范围会等距抽样（约 40 万样本）。
    //   此前把整个范围面积当作已扫描区块，配合毫秒级耗时会出现「1595 万亿/秒」的假速度；
    //   这里按实际抽样点数报告。
    int64_t scannedStat = static_cast<int64_t>(result->spanX) * result->spanZ;
    // 自动正方形预筛成功时，实际扫的是整张正方形密度图；不能再按快速抽样的 40 万点报速度。
    if (!(result->autoPrescan && result->prescanUsed) && o.scanStride != 1 &&
        scannedStat > 20LL * 1000 * 1000) {
        int ss = 1;
        while ((static_cast<int64_t>(result->spanX) / ss) * (static_cast<int64_t>(result->spanZ) / ss) > 400000)
            ++ss;
        scannedStat = (static_cast<int64_t>(result->spanX) / ss) * (static_cast<int64_t>(result->spanZ) / ss);
    }
    out.stats.scannedChunks = scannedStat;
    out.stats.totalMs = secs * 1000.0;
    out.stats.scanMs = secs * 1000.0;
    RunInfo info;
    info.version = result->version;
    info.mode = Mode::Density;
    info.algorithm = Algorithm::Exact;
    info.startX = result->startX;
    info.startZ = result->startZ;
    info.region = result->region;
    info.gridSizeUsed = result->gridSize;
    info.minDensityUsed = result->uiMinDensity;
    info.targetCount = result->targetCount;
    m_lastOutcome = out;
    m_lastInfo = info;
    m_hasLastOutcome = true;
    if (m_telemetry) {
        const QString ringMode = result->autoPrescan ? QStringLiteral("auto")
                                                     : ((o.scanStride == 1) ? QStringLiteral("exact")
                                                                            : QStringLiteral("fast"));
        QJsonObject st;
        st.insert(QStringLiteral("radius"), o.radius);
        st.insert(QStringLiteral("inner_radius"), o.innerRadius);
        st.insert(QStringLiteral("target"), static_cast<double>(result->requestedTopN));
        st.insert(QStringLiteral("min_density"), o.minDensity);
        st.insert(QStringLiteral("min_separation"), o.minSeparation);
        st.insert(QStringLiteral("sim_distance"), result->simDistance);
        const QString seedStr = result->bedrock ? QString()
                                                : QString::number(result->seed);
        const QJsonObject search = buildTelemetrySearch(
            seedStr, result->bedrock, QStringLiteral("ring"), QStringLiteral("exact"), ringMode,
            info.region, result->startX, result->startZ, st, out, result->gpuScanEnabled);
        m_telemetry->reportSearchDetailed(
            QString::fromLatin1(kAppVersion), QStringLiteral("test"),
            result->gpu ? QString::fromStdString(result->gpu->deviceName()) : QString(), search);
    }
    populateResultText(out, info);
    if (m_map) {
        auto layer = m_map->layer();
        layer.afkRadius = result->ringOuter;
        layer.afkInnerRadius = result->ringInner;   // v3.5.8：地图上画双圆环带
        m_map->setLayer(layer);
        m_map->setResults(out.results);
    // v3.5.22（用户要求）：有结果时自动跳到地图对应位置并标注
    if (!out.results.empty() && m_map) {
        const auto& r0 = out.results.front();
        const int cx = (r0.minX + r0.maxX) / 2;
        const int cz = (r0.minZ + r0.maxZ) / 2;
        m_map->centerOnChunk(cx, cz);
        if (m_map && !m_map->isVisible()) toggleMapPanel();   // v3.6.2：按内容可见性判断
        setStatus(slime::ui::tr(slime::ui::t("ui.061e37177602")).arg(cx).arg(cz));
    }

        m_map->setAfkCandidates(cands);
        m_map->setFocusedAfk(-1);   // v3.5.10：搜索后默认不画任何环（避免多个环叠在一起）
        m_map->setAfkFocusHandler([this](int idx) {
            if (idx >= 0 && idx < static_cast<int>(m_lastAfk.size())) {
                const auto& cd = m_lastAfk[static_cast<size_t>(idx)];
                setStatus(slime::ui::tr(slime::ui::t("ui.46355f6279ae"))
                              .arg(idx + 1)
                              .arg(cd.x)
                              .arg(cd.z)
                              .arg(static_cast<qlonglong>(cd.coveredBlocks))
                              .arg(cd.coveredWeight, 0, 'f', 0));
            }
        });
        m_map->fitToContent();
        if (m_map && !m_map->isVisible()) toggleMapPanel();   // v3.6.2：按内容可见性判断
    }
    // v3.5.8：统计口径 —— 按「世界倒数第二层」的 y 统计可生成格数（洞穴更新前 y≥1，之后 y≥-63）
    const int spawnY = (result->version == slime::Version::Java && m_mapJavaVersionId >= 5) ? -63 : 1;
    // v3.5.15：把「界面刷新」也计时 —— 用户感知的等待 = 计算 + 结果列表 + 地图图层重建
    const double uiMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - result->started).count() -
        secs * 1000.0;
    std::fprintf(stderr, "[环形] 界面刷新（列表+地图）：%.1f ms（地图图层最近一次 %.0f ms）\n", uiMs,
                 m_map ? m_map->lastLayerBuildMs() : 0.0);
    setStatus(slime::ui::tr(slime::ui::t("ui.1d9c4069d770"))
                  .arg(result->ringInner)
                  .arg(result->ringOuter)
                  .arg(spawnY)
                  .arg(cands.size())
                  .arg(cands.empty() ? 0 : static_cast<qlonglong>(cands.front().coveredBlocks))
                  .arg(cands.empty() ? 0 : cands.front().coveredChunks)
                  .arg(cands.empty() ? 0.0 : cands.front().coveragePercent, 0, 'f', 1)
                  .arg(secs, 0, 'f', 3)
                  .arg(stride > 1 ? slime::ui::tr(slime::ui::t("ui.c29ba5f06d05")).arg(stride) : QString()));
    std::fprintf(stderr, "[环形] 搜索完成：候选=%zu 最优=(%d,%d) 覆盖=%lld 格 用时=%.3fs\n", cands.size(),
                 cands.empty() ? 0 : cands.front().x, cands.empty() ? 0 : cands.front().z,
                 cands.empty() ? 0LL : static_cast<long long>(cands.front().coveredBlocks), secs);
}

// v3.14.5：关闭窗口 / GUI 自检共用的环形任务停止路径（两阶段取消 + 有界等待）。
bool MainWindow::stopRingSearchForShutdown(int timeoutMs)
{
    if (!m_ringSearching && !m_ringJob.active()) return true;
    m_ringCancel->store(true, std::memory_order_relaxed);
    m_ringJob.requestCancel();
    if (!m_ringJob.waitFor(timeoutMs)) {
        // v3.15.x（flow-ring-cancel2）：规划器所有长循环都能及时取消，正常应在亚秒级退出；
        //   超时说明仍有未覆盖的盲区。这里不阻塞关闭（由 closeEvent 决定 ignore），只记录证据。
        Logger::warning("环形任务在 " + std::to_string(timeoutMs) + "ms 内未响应取消，放弃本次关闭");
        return false;
    }
    // 已到 Finished：完成回调可能仍在队列里。窗口即将关闭，置标志丢弃，
    // 保证它不会在窗口销毁后触碰 this 的控件。
    m_ringFinishDiscard = true;
    m_ringSearching = false;
    // 关闭路径同样要留下“空闲态”（窗口可能被其它逻辑拦下而不真正关闭）。
    updateStartEnabled();
    if (m_w) {
        if (m_w->start) m_w->start->setEnabled(true);
        if (m_w->cancel) {
            m_w->cancel->setEnabled(false);
            m_w->cancel->setDown(false);
        }
        if (m_w->progress) {
            m_w->progress->setRange(0, 1000);
            m_w->progress->setValue(1000);
            if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("100%"));
        }
    }
    return true;
}

// v3.14.5：GUI 自检专用 —— 泵事件直到环形任务完成（或超时），返回是否已空闲。
bool MainWindow::waitForRingSearchIdle(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (m_ringSearching && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (m_ringSearching) QThread::msleep(2);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return !m_ringSearching;
}

// v3.5.4：地图面板（搜索页右侧）—— 与信息栏联动
void MainWindow::updateMapFromForm()
{
    if (!m_map || !m_w) return;
    MapPreviewWidget::Layer l = m_map->layer();
    const bool bedrock = m_w->bedrockRadio && m_w->bedrockRadio->isChecked();
    l.version = bedrock ? 1 : 0;
    int64_t parsedSeed = 0;
    const bool ok = m_w->seedEdit &&
                    parseSeedForVersion(m_w->seedEdit->text(), bedrock ? slime::Version::Bedrock : slime::Version::Java,
                                        &parsedSeed);
    if (ok) l.seed = parsedSeed;
    // v3.5.28（用户反馈）：没填种子时不要画「种子 0」的地图 —— 先提示填种子
    {
        const QString seedText = m_w->seedEdit ? m_w->seedEdit->text().trimmed() : QString();
        const bool needSeed = seedText.isEmpty() || !ok;   // v3.5.28：两版都要种子
        if (needSeed) {
            m_map->setPlaceholderText(slime::ui::tr(slime::ui::t("ui.0c9972317e16")));
            m_map->setLayer(l);
            setStatus(slime::ui::tr(slime::ui::t("ui.27c2c9373f1b")));
            return;
        }
        m_map->setPlaceholderText(QString());
    }
    l.autoDetail = true;
    l.afkRadius = 128;
    m_map->setLayer(l);
    // v3.5.21（用户要求）：基岩版群系上色 —— 官方 Wiki 明确「同一 64 位种子，Java 与基岩版
    //   的地形与群系一致（结构/洞穴/生物生成不同）」，所以基岩版直接用 Java 生成器跑同一份种子。
    //   1.18+ 才成立：更早版本两版群系算法不同，这里按所选 Java 版本判断。
    // 基岩版只用 1.18+ 的口径（更早两版群系算法不同）；默认取当前 Java 版本，若用户选过旧版本则回落到最新
    // v3.5.26：基岩版只在 1.18+ 与 Java 同种子一致 —— 选到更早的版本就回落到最新
    int biomeVersion = m_mapJavaVersionId;
    if (bedrock) {
        const auto& opts = slime::javaVersionOptions();
        int newest18 = slime::defaultJavaVersionId();
        for (const auto& v : opts) {
            if (QString::fromUtf8(v.label) == QLatin1String("1.18.2")) { newest18 = v.id; break; }
        }
        if (m_mapJavaVersionId > newest18) biomeVersion = slime::defaultJavaVersionId();
    }
    m_map->setBiomeSource(biomeVersion, l.seed);
    m_map->setBiomeParityNote(bedrock);

    // 视图：v3.5.16 不再随搜索范围自动移动地图中心（用户反馈「改范围会让地图中心莫名同步、位置错位」）。
    //   只有第一次显示地图面板、或用户点「定位/适配」类操作时才移动视图。
    if (m_w->minX && m_w->maxX && m_w->minZ && m_w->maxZ) {
        const int cx = m_w->minX->value() + (m_w->maxX->value() - m_w->minX->value()) / 2;
        const int cz = m_w->minZ->value() + (m_w->maxZ->value() - m_w->minZ->value()) / 2;
        if (!m_mapViewInitialised) {
            m_mapViewInitialised = true;
            m_mapFollowChunkX = cx;
            m_mapFollowChunkZ = cz;
            const double spanChunks = (std::max)(1, (std::max)(m_w->maxX->value() - m_w->minX->value() + 1,
                                                              m_w->maxZ->value() - m_w->minZ->value() + 1));
            const double scale = (std::max)(0.5, (std::min)(8.0, 700.0 / (spanChunks * 16.0)));
            m_map->setView(cx * 16.0, cz * 16.0, scale);
            m_map->refreshNow();
        }
    }
    std::fprintf(stderr, "[地图面板] 刷新：%s 种子=%lld 范围中心=(%d,%d) 群系=%s\n",
                 bedrock ? "基岩版" : "Java", static_cast<long long>(l.seed), m_mapFollowChunkX,
                 m_mapFollowChunkZ, m_map->layer().biomeAvailable ? "可用" : "不可用");
}

// v3.6.5（用户要求）：搜索条件不足时把「开始搜索」灰掉，并说明缺什么
// v3.6.18：模拟距离提示（>=8 区块时环带不变；<8 时外沿 = 16×D）
void MainWindow::updateSimDistanceNote()
{
    if (!m_w || !m_w->simDistance || !m_w->simDistanceNote) return;
    const int outerBase = std::clamp(m_settings.afkRingOuter, 16, 1024);
    const int inner = std::clamp(m_settings.afkRingInner, 0, 64);
    const int simBlocks = m_w->simDistance->value() * 16;
    if (simBlocks >= outerBase) {
        m_w->simDistanceNote->setText(slime::ui::t("ui.511104f69bef").arg(outerBase).arg(inner).arg(outerBase));
    } else {
        m_w->simDistanceNote->setText(slime::ui::t("ui.c2a121be1423").arg(inner).arg(simBlocks));
    }
}

// v3.6.19（用户反馈）：把「开始新搜索时清空旧结果」抽成一处 ——
//   引擎路径与环形路径都要调用，否则环形搜索无果时会留下上一次的列表/地图标记，
//   切换搜索方式后看起来"还在用旧的搜索方式"。
void MainWindow::clearResultsForNewSearch()
{
    m_lastAfk.clear();
    m_lastOutcome = SearchOutcome{};
    m_hasLastOutcome = false;
    if (m_w->resultTable) m_w->resultTable->setRowCount(0);
    if (m_resultCountLabel) m_resultCountLabel->setText(slime::ui::t("ui.a535edb5149b"));
    if (m_resultsNotice) {
        m_resultsNotice->setText(QString());
        m_resultsNotice->setVisible(false);
    }
    if (m_map) {
        m_map->setResults({});
        m_map->setAfkCandidates({});
        m_map->setFocusedAfk(-1);
    }
    if (m_w->infoStats) m_w->infoStats->setText(slime::ui::t("ui.bff5b3f4bbe4"));
    if (m_w->resultView) m_w->resultView->clear();
    // 结果文本区在 v3.5.x 起默认折叠：清空时保持折叠状态即可
}

void MainWindow::updateStartEnabled()
{
    if (!m_w || !m_w->start) return;
    if (m_running) { m_w->start->setEnabled(false); return; }
    const bool bedrock = m_w->bedrockRadio && m_w->bedrockRadio->isChecked();
    const QString seedText = m_w->seedEdit ? m_w->seedEdit->text().trimmed() : QString();
    int64_t parsedSeed = 0;
    const bool seedOk = parseSeedForVersion(seedText, bedrock ? slime::Version::Bedrock : slime::Version::Java,
                                            &parsedSeed);
    const bool patternMode = m_w->strictRadio && m_w->strictRadio->isChecked();
    const bool hasPattern = m_patternSet && m_pattern.activeCount() > 0;
    const bool buildMode = m_buildActive;

    QString why;
    if (!bedrock && (seedText.isEmpty() || !seedOk)) why = slime::ui::t("ui.5344ca1e9e8b");
    else if (bedrock && !seedText.isEmpty() && !seedOk) why = slime::ui::t("ui.seed_range_bedrock").arg(seedText);
    // v3.12.3：缓存构建页不需要种子；此前这里仍要求 seed，导致选好范围/预算后
    // 「开始构建」一直灰掉。构建模式只依赖范围和预算。
    else if (patternMode && !hasPattern && !buildMode) why = slime::ui::t("ui.779966e118d2");
    const bool can = why.isEmpty();
    m_w->start->setEnabled(can);
    m_w->start->setToolTip(can ? slime::ui::t("ui.8fdc065eda03") : why);
    if (m_cacheBuildBtn) {
        m_cacheBuildBtn->setEnabled(can);
        m_cacheBuildBtn->setToolTip(can ? slime::ui::t("ui.8fdc065eda03") : why);
    }
    // 提示只放在 tooltip 里（左栏已经有线程/GPU 提示，避免再占一行）
}

void MainWindow::toggleMapPanel()
{
    if (!m_mapPanel) return;
    // v3.6.2（用户要求）：折叠只收内容，标题行始终保留 —— 收起后就是 "> 地图"，点它展开
    const bool show = !(m_map && m_map->isVisible());   // v3.6.2：按内容可见性取反
    m_mapPanel->setVisible(true);
    if (m_map) m_map->setVisible(show);
    if (m_mapPlaceholder) m_mapPlaceholder->setVisible(!show);
    if (m_mapSpacer) m_mapSpacer->setVisible(!show);   // v3.6.9.1：折叠时由它吸收剩余空间
    if (m_mapToggle) m_mapToggle->setArrowType(show ? Qt::DownArrow : Qt::RightArrow);
    m_rightSplitUserMoved = false;   // 明确的折叠操作，允许重新自适应
    // v3.6.9（用户验收标准）：点击折叠按钮后，**按钮本身的位置必须和点击时完全一致**，
    //   再点一次恢复原样。QSplitter 在子控件隐藏时会立刻重分配像素（结果面板标题会跟着上下跳），
    //   所以这里在切换前后都把分隔条的像素尺寸「钉住」：折叠只是内容看不见，标题行原地不动。
    // v3.10.0：地图已是独立的中间列 ⇒ 折叠/展开钉住的是三纵列（hSplit）的宽度，
    //   标题行与折叠按钮因此原地不动（沿用 v3.6.9 的验收标准）。
    const QList<int> keepSizes = m_hSplit ? m_hSplit->sizes() : QList<int>();
    auto pinSizes = [this, keepSizes]() {
        if (keepSizes.size() >= 3) m_hSplit->setSizes(keepSizes);
    };
    pinSizes();
    m_mapPaneWidth = (keepSizes.size() >= 3) ? keepSizes[1] : m_mapPaneWidth;
    if (show) {
        updateMapFromForm();
        setStatus(slime::ui::tr(slime::ui::t("ui.1ff3e451eaf3")));
    } else {
        setStatus(slime::ui::tr(slime::ui::t("ui.9e8c9184ca74")));
    }
    pinSizes();
    QTimer::singleShot(0, this, pinSizes);
}

void MainWindow::focusMapOnChunk(int chunkX, int chunkZ)
{
    if (!m_map) return;
    if (m_map && !m_map->isVisible()) toggleMapPanel();   // v3.6.2：按内容可见性判断
    m_map->setView(chunkX * 16.0 + 8.0, chunkZ * 16.0 + 8.0, (std::max)(2.0, m_map->pxPerBlock()));
    m_map->refreshNow();
    setStatus(slime::ui::tr(slime::ui::t("ui.8700ac6906d4")).arg(chunkX).arg(chunkZ));
    std::fprintf(stderr, "[地图面板] 定位：区块(%d,%d) → 方块(%d,%d)\n", chunkX, chunkZ, chunkX * 16 + 8,
                 chunkZ * 16 + 8);
}

// v3.5.10：选中某个环形候选（点地图环心 / 点结果行）—— 地图只画这一条的环，中心落在环心
void MainWindow::focusAfkCandidate(int index)
{
    if (!m_map || index < 0 || index >= static_cast<int>(m_lastAfk.size())) return;
    const auto& cd = m_lastAfk[static_cast<size_t>(index)];
    if (m_map && !m_map->isVisible()) toggleMapPanel();   // v3.6.2：按内容可见性判断
    const QSize sz = (m_map->width() > 80 && m_map->height() > 80) ? m_map->size() : QSize(720, 480);
    // 让「环带 24~128」整体落在视口内（留 18% 余量）
    const double span = 2.0 * 128.0 * 1.18;
    double zoom = (std::min)(sz.width(), sz.height()) / span;
    zoom = (std::max)(0.25, (std::min)(8.0, zoom));
    m_map->setView(static_cast<double>(cd.x) + 0.5, static_cast<double>(cd.z) + 0.5, zoom);
    m_map->setFocusedAfk(index);
    m_map->refreshNow();
    setStatus(slime::ui::tr(slime::ui::t("ui.be49a883050c"))
                  .arg(index + 1)
                  .arg(cd.x)
                  .arg(cd.z)
                  .arg(static_cast<qlonglong>(cd.coveredBlocks))
                  .arg(cd.coveredWeight, 0, 'f', 0)
                  .arg(cd.coveredChunks));
    std::fprintf(stderr, "[地图面板] 选中环心 #%d：(%d,%d) 有效格 %lld\n", index, cd.x, cd.z,
                 static_cast<long long>(cd.coveredBlocks));
}

void MainWindow::computeAfkForMap()
{
    if (!m_map || !m_w) return;
    slime::AfkPlanOptions o;
    o.area = slime::Rect{ m_w->minX->value(), m_w->maxX->value(), m_w->minZ->value(), m_w->maxZ->value() };
    o.radius = 128;
    o.topN = 5;
    o.minSeparation = 24;
    o.coarseStep = 4;
    const bool bedrock = m_w->bedrockRadio && m_w->bedrockRadio->isChecked();
    const slime::Version v = bedrock ? slime::Version::Bedrock : slime::Version::Java;
    int64_t seed = 0;
    if (!m_w->seedEdit || !parseSeedForVersion(m_w->seedEdit->text(), v, &seed)) {
        setStatus(slime::ui::tr(slime::ui::t("ui.5344ca1e9e8b")));
        return;
    }
    const auto pred = [v, seed](int cx, int cz) { return slime::SlimeEngine::isSlimeChunk(v, seed, cx, cz); };
    const auto res = slime::AfkPlanner::plan(o, pred);
    m_map->setAfkCandidates(res);
    if (!res.empty()) m_map->fitToContent();
    setStatus(slime::ui::tr(slime::ui::t("ui.00028abeba59")).arg(res.size()));
}

// v3.5.2（任务 2）：地图预览 —— 把史莱姆区块、搜索结果、挂机点画在一张可缩放/平移的地图上
void MainWindow::showMapDialog()
{
    MapDialog::Context ctx;
    ctx.version = (m_w->bedrockRadio && m_w->bedrockRadio->isChecked()) ? 1 : 0;
    const slime::Version mapVersion =
        ctx.version == 1 ? slime::Version::Bedrock : slime::Version::Java;
    int64_t parsedSeed = 0;
    const bool seedOk = m_w->seedEdit &&
                        parseSeedForVersion(m_w->seedEdit->text(), mapVersion, &parsedSeed);
    if (seedOk) ctx.seed = parsedSeed;
    if (!seedOk) {
        QMessageBox::information(this, slime::ui::t("ui.faea94c8b095"),
                                 ctx.version == 1
                                     ? slime::ui::t("ui.seed_range_bedrock").arg(m_w->seedEdit->text())
                                     : slime::ui::t("ui.64cd88da6b9d"));
        setStatus(slime::ui::tr(slime::ui::t("ui.6616d54947cf")));
        return;
    }
    if (!m_lastOutcome.results.empty()) {
        int minX = INT_MAX, maxX = INT_MIN, minZ = INT_MAX, maxZ = INT_MIN;
        for (const MatchResult& r : m_lastOutcome.results) {
            minX = (std::min)(minX, r.minX);
            maxX = (std::max)(maxX, r.maxX);
            minZ = (std::min)(minZ, r.minZ);
            maxZ = (std::max)(maxZ, r.maxZ);
        }
        ctx.minX = minX - 1; ctx.maxX = maxX + 1;
        ctx.minZ = minZ - 1; ctx.maxZ = maxZ + 1;
        ctx.source = slime::ui::t("ui.491bd822f8b8").arg(m_lastOutcome.results.size());
    } else if (m_w->minX && m_w->maxX && m_w->minZ && m_w->maxZ) {
        ctx.minX = m_w->minX->value(); ctx.maxX = m_w->maxX->value();
        ctx.minZ = m_w->minZ->value(); ctx.maxZ = m_w->maxZ->value();
        ctx.source = slime::ui::t("ui.bba2e2e3b721");
    } else {
        return;
    }

    MapDialog dlg(ctx, this);
    dlg.setResults(m_lastOutcome.results);
    dlg.computeAfk();
    dlg.exec();
}

void MainWindow::showAfkDialog()
{
    AfkDialog::Context ctx;
    ctx.version = (m_w->bedrockRadio && m_w->bedrockRadio->isChecked()) ? 1 : 0;
    const slime::Version afkVersion =
        ctx.version == 1 ? slime::Version::Bedrock : slime::Version::Java;
    int64_t parsedSeed = 0;
    const bool seedOk = m_w->seedEdit &&
                        parseSeedForVersion(m_w->seedEdit->text(), afkVersion, &parsedSeed);
    if (seedOk) ctx.seed = parsedSeed;
    if (!seedOk) {
        QMessageBox::information(this, slime::ui::t("ui.faea94c8b095"),
                                 ctx.version == 1
                                     ? slime::ui::t("ui.seed_range_bedrock").arg(m_w->seedEdit->text())
                                     : slime::ui::t("ui.64cd88da6b9d"));
        setStatus(slime::ui::tr(slime::ui::t("ui.447c26e890a5")));
        return;
    }

    if (!m_lastOutcome.results.empty()) {
        int minX = INT_MAX, maxX = INT_MIN, minZ = INT_MAX, maxZ = INT_MIN;
        for (const MatchResult& r : m_lastOutcome.results) {
            minX = (std::min)(minX, r.minX);
            maxX = (std::max)(maxX, r.maxX);
            minZ = (std::min)(minZ, r.minZ);
            maxZ = (std::max)(maxZ, r.maxZ);
        }
        ctx.minX = minX - 2; ctx.maxX = maxX + 2;
        ctx.minZ = minZ - 2; ctx.maxZ = maxZ + 2;
        ctx.source = slime::ui::t("ui.767c07630078").arg(m_lastOutcome.results.size());
    } else if (m_w->minX && m_w->maxX && m_w->minZ && m_w->maxZ) {
        ctx.minX = m_w->minX->value(); ctx.maxX = m_w->maxX->value();
        ctx.minZ = m_w->minZ->value(); ctx.maxZ = m_w->maxZ->value();
        ctx.source = slime::ui::t("ui.bba2e2e3b721");
    } else {
        return;
    }
    std::fprintf(stderr, "[挂机点] 打开对话框：版本=%s 种子=%lld 区域=[%d..%d]x[%d..%d] 来源=%s\n",
                 ctx.version == 1 ? "基岩版" : "Java", static_cast<long long>(ctx.seed),
                 ctx.minX, ctx.maxX, ctx.minZ, ctx.maxZ, ctx.source.toUtf8().constData());

    AfkDialog dlg(ctx, this);
    dlg.exec();
}

void MainWindow::showGpuStatus()
{
    if (!m_gpuProbeDone) ensureGpuScanOnce(); // 与搜索路径相同的初始化
    const QString body = gpuStatusText();     // 对话框正文（自检直接断言同一函数）
    const bool Ok = m_gpuAvailable && m_gpuScan != nullptr;
    const QString text = Ok ? (QStringLiteral("✅ ") + body) : (QStringLiteral("❌ ") + body);
    updateGpuStatusUi(); // 同步左侧状态标签文字与主题色
    addFeedback(Ok ? 2 : 1, text);
    setStatus(Ok ? slime::ui::t("ui.0a94c990a118") : slime::ui::t("ui.3be003594bed"));
    slime::LogBus::info("GPU 状态查询：" + text.toStdString());
    if (!m_uiQuiet) {
        QMessageBox box(this);
        box.setWindowTitle(slime::ui::t("ui.e4d209f2e621"));
        box.setTextFormat(Qt::PlainText);
        box.setText(text);
        box.setIcon(Ok ? QMessageBox::Information : QMessageBox::Warning);
        box.exec();
    }
}

// ============================================================================
// v3.3.5（Bug 5）：日志开关 —— 菜单项在「启用日志…/关闭日志」之间动态切换
// ============================================================================
void MainWindow::updateLogMenuText()
{
    if (!m_actLogToggle) return;
    m_actLogToggle->setText(slime::LogBus::enabled() ? slime::ui::t("ui.0aa53272a236")
                                                     : slime::ui::t("ui.80005af43eec"));
}

void MainWindow::toggleLogging()
{
    if (slime::LogBus::enabled()) {
        // 关闭：停止记录、flush 并关闭文件；日志栏恢复默认提示
        slime::LogBus::setMode(slime::LogMode::Off);
        saveAlgorithmToConfig(); // 写回 config.json 的 log.mode = off
        if (m_logPanel) m_logPanel->refreshFromBus();
        updateLogMenuText();
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.0035e24be638")));
        setStatus(slime::ui::tr(slime::ui::t("ui.564c665e22ef")));
        return;
    }
    showEnableLogDialog(); // 未启用：走原有的两级选项对话框
    updateLogMenuText();
}

// ============================================================================
// v3.3.5（Bug 6）：GPU 实测吞吐率基准（方案 A；v3.3.6 起只报吞吐率，不给交叉点结论）
//   * 后台线程测量 CPU 与 GPU 在两种规模下的端到端耗时（含预热、多次取最优）
//   * 拟合 t(n) = 固定开销 + n / 吞吐，解出两者相等处的区块数 → 交叉点区间
//   * 结果写入 config.json（含设备名与核心数，硬件变化时自动失效重测）
//   * 同时给出实测吞吐率（方案 B 的信息也一并显示）
// ============================================================================

