// UpdateDialog.cpp — v3.10.12.10
#include "i18n/UiText.h"
#include "gui/dialogs/UpdateDialog.h"

#include <algorithm>

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QMessageBox>
#include <QTimer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
const char* kGithubApi =
    "https://api.github.com/repos/PillerBean/SlimeFinder/releases/latest";
const char* kGithubReleases = "https://github.com/PillerBean/SlimeFinder/releases/latest";
const char* kPanUrl = "https://wwben.lanzoul.com/b01d77eebe";
// v3.10.12.10：镜像回退用的版本清单（raw.githubusercontent.com，可被 gh-proxy 等代理）
const char* kManifestRaw =
    "https://raw.githubusercontent.com/PillerBean/SlimeFinder/main/version.json";
const char* kManifestBetaRaw =
    "https://raw.githubusercontent.com/PillerBean/SlimeFinder/main/version-beta.json";
const char* kGithubReleasesList =
    "https://api.github.com/repos/PillerBean/SlimeFinder/releases?per_page=5";
const char* kConfigRel = "update_config.json";

QStringList defaultMirrorPresets()
{
    // v3.10.12.10：内置候选镜像（检查更新失败时自动测速选最优；用户无需手动切换）。
    // 只有支持 raw.githubusercontent.com 与 release 资产代理的前缀才有效；
    // 失效的也保留在列表里，测速阶段会自然跳过。
    return {
        QStringLiteral("https://gh-proxy.com/"),
        QStringLiteral("https://ghfast.top/"),
        QStringLiteral("https://gh-proxy.org/"),
        QStringLiteral("https://ghproxy.net/"),
        QStringLiteral("https://ghproxy.cc/"),
        QStringLiteral("https://gh.llkk.cc/"),
        QStringLiteral("https://github.moeyy.xyz/"),
        QStringLiteral("https://gh-proxy.net/"),
    };
}

QString envOr(const char* name, const char* def)
{
    const QByteArray v = qgetenv(name);
    return v.isEmpty() ? QString::fromLatin1(def) : QString::fromUtf8(v);
}

QString normalizeVer(QString v)
{
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V'))) v = v.mid(1);
    return v;
}

QStringList versionParts(const QString& v)
{
    return normalizeVer(v).split(QLatin1Char('.'), Qt::SkipEmptyParts);
}
} // namespace

UpdateDialog::UpdateDialog(QWidget* parent)
    : QDialog(parent)
{
    buildUi();
    loadConfig();
    applySourceUi();
}

UpdateDialog::~UpdateDialog() = default;

void UpdateDialog::buildUi()
{
    setWindowTitle(slime::ui::t("ui.a6df38586d84"));
    resize(780, 570);
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    // ---- 来源选择 ----
    auto* srcRow = new QHBoxLayout();
    srcRow->addWidget(new QLabel(slime::ui::t("ui.ff199d6c0642"), this));
    m_source = new QComboBox(this);
    m_source->addItem(slime::ui::t("ui.854ed4cf5d4c"));
    m_source->addItem(slime::ui::t("ui.8bdb14e091f3"));
    m_source->addItem(slime::ui::t("ui.2f947dc750e1"));
    srcRow->addWidget(m_source, 1);
    root->addLayout(srcRow);

    // v3.10.12.10：更新通道（稳定=只正式版；测试=含 Pre-release）
    auto* chRow = new QHBoxLayout();
    chRow->addWidget(new QLabel(slime::ui::t("ui.268c85c4f8d7"), this));
    m_channel = new QComboBox(this);
    m_channel->addItem(slime::ui::t("ui.725b686f40ef"), Stable);
    m_channel->addItem(slime::ui::t("ui.587e9efae2db"), Test);
    m_channel->setToolTip(slime::ui::t("ui.83d8f760910f"));
    chRow->addWidget(m_channel, 1);
    root->addLayout(chRow);

    // 镜像前缀不再让用户选择：官方不可达时自动测速全部内置镜像并选最快。
    m_mirror = new QComboBox(this);
    m_mirror->setVisible(false);
    m_mirror->addItems(defaultMirrorPresets());

    auto* btnRow = new QHBoxLayout();
    m_checkBtn = new QPushButton(slime::ui::t("ui.a6df38586d84"), this);
    m_githubBtn = new QPushButton(slime::ui::t("ui.eff607b3516f"), this);
    m_panBtn = new QPushButton(slime::ui::t("ui.cfdde904fad7"), this);
    btnRow->addWidget(m_checkBtn);
    btnRow->addWidget(m_githubBtn);
    btnRow->addWidget(m_panBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    m_status = new QLabel(slime::ui::t("ui.4b144de87f39"), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_notes = new QPlainTextEdit(this);
    m_notes->setReadOnly(true);
    m_notes->setPlaceholderText(slime::ui::t("ui.c48b51f36c23"));
    root->addWidget(m_notes, 1);

    auto* dlRow = new QHBoxLayout();
    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    m_progress->setTextVisible(true);
    m_downloadBtn = new QPushButton(slime::ui::t("ui.f9ff8c365c95"), this);
    m_downloadBtn->setEnabled(false);
    m_downloadBtn->setVisible(false); // 无代码签名：只通知新版本，更新必须手动从发布页下载
    m_folderBtn = new QPushButton(slime::ui::t("ui.9b519e408eb3"), this);
    m_folderBtn->setVisible(false);
    auto* closeBtn = new QPushButton(slime::ui::t("ui.6c14bd7f6f9e"), this);
    dlRow->addWidget(m_progress, 1);
    dlRow->addWidget(m_downloadBtn);
    dlRow->addWidget(m_folderBtn);
    dlRow->addWidget(closeBtn);
    root->addLayout(dlRow);

    connect(m_source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applySourceUi();
        saveConfig();
    });
    connect(m_mirror, &QComboBox::currentTextChanged, this, [this](const QString&) { saveConfig(); });
    connect(m_channel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        // 通道切换后旧缓存不再适用
        m_cacheManifest.clear();
        m_cacheTime = 0;
        m_cacheChannel = -1;
        saveConfig();
        if (!m_latestTag.isEmpty())
            m_status->setText(slime::ui::t("ui.49747a3ed295"));
    });
    connect(m_checkBtn, &QPushButton::clicked, this, &UpdateDialog::startCheck);
    connect(m_folderBtn, &QPushButton::clicked, this, &UpdateDialog::openDownloadFolder);
    connect(m_githubBtn, &QPushButton::clicked, this, &UpdateDialog::openGithubPage);
    connect(m_panBtn, &QPushButton::clicked, this, &UpdateDialog::openPanPage);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    if (curlExe().isEmpty()) {
        m_checkBtn->setEnabled(false);
        m_status->setText(slime::ui::t("ui.3da75b73fed1"));
    }
}

QString UpdateDialog::curlExe() const
{
    return QStandardPaths::findExecutable(QStringLiteral("curl.exe"));
}

void UpdateDialog::loadConfig()
{
    const QString path = QCoreApplication::applicationDirPath() + QLatin1Char('/') + QLatin1String(kConfigRel);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    int src = o.value(QStringLiteral("source")).toInt(Auto);
    // 兼容旧配置：0=自动 1=GitHub 2=镜像 3=网盘 ⇒ 新枚举 0=自动 1=GitHub 2=网盘
    if (src == 3) src = Pan;
    else if (src > 1) src = Auto;
    m_source->setCurrentIndex(src);
    const QString mirror = o.value(QStringLiteral("bestMirror")).toString();
    if (!mirror.isEmpty()) {
        m_cachedMirror = mirror;
        m_mirror->setCurrentText(mirror);
    }
    m_cachedMirrorAt = static_cast<qint64>(o.value(QStringLiteral("bestMirrorAt")).toDouble(0));
    const int ch = o.value(QStringLiteral("channel")).toInt(Stable);
    m_channel->setCurrentIndex(ch == Test ? Test : Stable);
    m_cacheManifest = o.value(QStringLiteral("cacheManifest")).toString();
    m_cacheTime = static_cast<qint64>(o.value(QStringLiteral("cacheTime")).toDouble(0));
    m_cacheChannel = o.value(QStringLiteral("cacheChannel")).toInt(-1);
}

void UpdateDialog::saveConfig() const
{
    if (!m_source || !m_mirror) return;
    QJsonObject o;
    o.insert(QStringLiteral("source"), m_source->currentIndex());
    o.insert(QStringLiteral("channel"), m_channel ? m_channel->currentIndex() : Stable);
    o.insert(QStringLiteral("bestMirror"), m_cachedMirror);
    o.insert(QStringLiteral("bestMirrorAt"), static_cast<double>(m_cachedMirrorAt));
    o.insert(QStringLiteral("cacheManifest"), m_cacheManifest);
    o.insert(QStringLiteral("cacheTime"), static_cast<double>(m_cacheTime));
    o.insert(QStringLiteral("cacheChannel"), m_cacheChannel);
    const QString path = QCoreApplication::applicationDirPath() + QLatin1Char('/') + QLatin1String(kConfigRel);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(o).toJson());
}

void UpdateDialog::applySourceUi()
{
    const int src = m_source->currentIndex();
    const bool pan = (src == Pan);
    m_mirror->setEnabled(false);
    m_mirror->setVisible(false);
    m_checkBtn->setEnabled(!pan && !curlExe().isEmpty());
    if (pan) {
        m_status->setText(slime::ui::t("ui.7b71aa40fffb"));
        m_downloadBtn->setEnabled(false);
    } else if (!pan && curlExe().isEmpty()) {
        m_status->setText(slime::ui::t("ui.91c96f3dea75"));
    } else if (m_latestTag.isEmpty()) {
        m_status->setText(slime::ui::t("ui.4b144de87f39"));
    }
}

void UpdateDialog::setBusy(bool busy)
{
    m_checkBtn->setEnabled(!busy && m_source->currentIndex() != Pan);
    m_source->setEnabled(!busy);
    m_mirror->setEnabled(false);
    m_downloadBtn->setEnabled(!busy && m_updateAvailable && !m_zipAsset.url.isEmpty());
    m_progress->setVisible(busy);
    if (!busy) m_progress->setRange(0, 100);
}

void UpdateDialog::setStatusText(const QString& text)
{
    m_status->setText(text);
}

void UpdateDialog::runCurl(const QStringList& args, const QString& outFile, bool readBack,
                           std::function<void(int, QByteArray)> done)
{
    if (m_proc) { m_proc->kill(); m_proc = nullptr; }
    m_curlOutFile = outFile;
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, readBack, done](int exitCode, QProcess::ExitStatus) {
                int http = 0;
                if (m_proc) {
                    const QString stdoutText = QString::fromUtf8(m_proc->readAllStandardOutput()).trimmed();
                    const QStringList parts = stdoutText.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                    if (!parts.isEmpty()) http = parts.last().toInt();
                    m_proc->deleteLater();
                    m_proc = nullptr;
                }
                QByteArray body;
                if (readBack && !m_curlOutFile.isEmpty()) {
                    QFile f(m_curlOutFile);
                    if (f.open(QIODevice::ReadOnly)) body = f.readAll();
                    f.remove();
                }
                if (exitCode != 0 && http == 0) http = 0;
                done(http, body);
            });
    connect(m_proc, &QProcess::errorOccurred, this, [this, done](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart && m_proc) {
            m_proc->deleteLater();
            m_proc = nullptr;
            done(0, QByteArray());
        }
    });
    QStringList secureArgs{ QStringLiteral("--proto"), QStringLiteral("=https"),
                            QStringLiteral("--proto-redir"), QStringLiteral("=https") };
    secureArgs += args;
    m_proc->start(curlExe(), secureArgs);
}

// ---------------- 检查更新 ----------------
QStringList UpdateDialog::buildOfficialUrls() const
{
    // 稳定通道：version.json（只含正式版）+ GitHub latest；
    // 测试通道：version-beta.json（含 Pre-release）+ releases 列表。
    // raw 清单优先（不消耗 GitHub API 限额）；镜像清单也只走 raw 清单，不代理 API。
    QStringList out;
    const bool test = m_channel && m_channel->currentIndex() == Test;
    const QString manifest = envOr("SLIME_UPDATE_MANIFEST_URL",
                                   test ? kManifestBetaRaw : kManifestRaw);
    out << manifest;
    // 24h 内测速选出的镜像：先作为清单回退尝试，避免每次都跑完整测速
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!m_cachedMirror.isEmpty() && m_cachedMirrorAt > 0 && now - m_cachedMirrorAt < 86400) {
        const QString viaMirror = transformGithubUrl(manifest, m_cachedMirror);
        if (!viaMirror.isEmpty()) out << viaMirror;
    }
    const QString api = envOr("SLIME_UPDATE_API_URL",
                              test ? kGithubReleasesList : kGithubApi);
    out << api;
    out.removeDuplicates();
    return out;
}

QString UpdateDialog::transformGithubUrl(const QString& raw, const QString& prefix) const
{
    if (raw.isEmpty() || prefix.isEmpty()) return {};
    QString t = prefix.trimmed();
    if (t.startsWith(QStringLiteral("api:"))) return {};
    if (t.contains(QStringLiteral("{url}"))) return t.replace(QStringLiteral("{url}"), raw);
    if (!t.endsWith(QLatin1Char('/'))) t += QLatin1Char('/');
    return t + raw;
}

QStringList UpdateDialog::buildDownloadUrls(const QString& rawUrl) const
{
    QStringList out;
    if (rawUrl.isEmpty()) return out;
    if (!m_activeMirror.isEmpty()) {
        const QString t = transformGithubUrl(rawUrl, m_activeMirror);
        if (!t.isEmpty()) out << t;
    }
    out << rawUrl;
    const int src = m_source->currentIndex();
    if (src != GitHub) {
        QStringList prefixes = mirrorPrefixes();
        for (const QString& p : prefixes) {
            const QString t = transformGithubUrl(rawUrl, p);
            if (!t.isEmpty()) out << t;
        }
    }
    out.removeDuplicates();
    return out;
}

void UpdateDialog::startCheck()
{
    m_latestTag.clear();
    m_latestVersion.clear();
    m_zipAsset = Asset{};
    m_sumsAsset = Asset{};
    m_notesText.clear();
    m_notes->clear();
    m_updateAvailable = false;
    m_activeMirror.clear();
    m_downloadBtn->setEnabled(false);
    if (m_source->currentIndex() == Pan) {
        applySourceUi();
        return;
    }
    // 6 小时内的缓存结果：先展示缓存，第二次点击“强制刷新”才联网
    const qint64 nowSecs = QDateTime::currentSecsSinceEpoch();
    if (!m_waitingForceCheck && m_cacheChannel == m_channel->currentIndex() &&
        !m_cacheManifest.isEmpty() && m_cacheTime > 0 && nowSecs - m_cacheTime < 6 * 3600) {
        m_waitingForceCheck = true;
        m_checkBtn->setText(slime::ui::t("ui.d6cc30dcc227"));
        setStatusText(slime::ui::t("ui.37a8dd4f0ae3"));
        setBusy(true);
        m_progress->setRange(0, 0);
        parseLatest(m_cacheManifest.toUtf8());
        setBusy(false);
        return;
    }
    m_waitingForceCheck = false;
    m_checkBtn->setText(slime::ui::t("ui.a6df38586d84"));

    m_officialUrls = buildOfficialUrls();
    if (m_officialUrls.isEmpty()) {
        setStatusText(slime::ui::t("ui.31dfe582d4bc"));
        return;
    }
    m_officialIndex = 0;
    setBusy(true);
    m_progress->setRange(0, 0);
    setStatusText(slime::ui::t("ui.e705b91e38b9"));
    tryNextApi();
}

void UpdateDialog::tryNextApi()
{
    if (m_officialIndex >= m_officialUrls.size()) {
        // 官方走不通：自动测速镜像（用户不需要手动切换）
        if (m_source->currentIndex() == Auto) {
            startMirrorSpeedTest();
            return;
        }
        setBusy(false);
        setStatusText(slime::ui::t("ui.9a266b8bc047"));
        emit probeFinished(false, QStringLiteral("official-unreachable"));
        return;
    }
    const QString url = m_officialUrls.at(m_officialIndex++);
    setStatusText(slime::ui::t("ui.1610efe6bbfe")
                      .arg(m_officialIndex)
                      .arg(m_officialUrls.size())
                      .arg(url));
    const QString tmp = QDir::tempPath() + QStringLiteral("/slime_update_%1_%2.json")
                                             .arg(QCoreApplication::applicationPid())
                                             .arg(m_officialIndex);
    const QStringList args = {
        QStringLiteral("-L"), QStringLiteral("--max-time"),
        QStringLiteral("12"), QStringLiteral("-sS"), QStringLiteral("-A"),
        QStringLiteral("SlimeFinder-Updater/1.0"), QStringLiteral("-o"), tmp,
        QStringLiteral("-w"), QStringLiteral("%{http_code}"), url,
    };
    runCurl(args, tmp, true, [this](int code, const QByteArray& body) {
        if (code == 200 && !body.isEmpty()) onApiDone(code, body);
        else tryNextApi();
    });
}

QStringList UpdateDialog::mirrorPrefixes() const
{
    QStringList out;
    const QString envMirrors = QString::fromUtf8(qgetenv("SLIME_UPDATE_MIRRORS")).trimmed();
    if (!envMirrors.isEmpty()) {
        for (const QString& p : envMirrors.split(QLatin1Char(','), Qt::SkipEmptyParts))
            out << p.trimmed();
        return out;
    }
    if (!m_cachedMirror.isEmpty()) out << m_cachedMirror;
    for (const QString& p : defaultMirrorPresets())
        if (!out.contains(p)) out << p;
    return out;
}

void UpdateDialog::startMirrorSpeedTest()
{
    m_mirrorCandidates = mirrorPrefixes();
    m_mirrorProbes.clear();
    m_mirrorProbeIndex = 0;
    if (m_mirrorCandidates.isEmpty()) {
        setBusy(false);
        setStatusText(slime::ui::t("ui.7dd0b78fd953"));
        emit probeFinished(false, QStringLiteral("no-mirrors"));
        return;
    }
    m_progress->setRange(0, 0);
    setStatusText(slime::ui::t("ui.16a85c64312c")
                      .arg(m_mirrorCandidates.size()));
    probeNextMirrorManifest();
}

void UpdateDialog::probeNextMirrorManifest()
{
    if (m_mirrorProbeIndex >= m_mirrorCandidates.size()) {
        startMirrorSampleTest();
        return;
    }
    const QString prefix = m_mirrorCandidates.at(m_mirrorProbeIndex++);
    const QString url = transformGithubUrl(envOr("SLIME_UPDATE_MANIFEST_URL", kManifestRaw), prefix);
    if (url.isEmpty()) {
        probeNextMirrorManifest();
        return;
    }
    setStatusText(slime::ui::t("ui.c9ce02427591")
                      .arg(m_mirrorProbeIndex)
                      .arg(m_mirrorCandidates.size())
                      .arg(prefix));
    const QString tmp = QDir::tempPath() + QStringLiteral("/slime_manifest_%1_%2.json")
                                             .arg(QCoreApplication::applicationPid())
                                             .arg(m_mirrorProbeIndex);
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    const QStringList args = {
        QStringLiteral("-L"), QStringLiteral("--max-time"),
        QStringLiteral("8"), QStringLiteral("-sS"), QStringLiteral("-A"),
        QStringLiteral("SlimeFinder-Updater/1.0"), QStringLiteral("-o"), tmp,
        QStringLiteral("-w"), QStringLiteral("%{http_code}"), url,
    };
    runCurl(args, tmp, true, [this, prefix, t0](int code, const QByteArray& body) {
        const double sec = (QDateTime::currentMSecsSinceEpoch() - t0) / 1000.0;
        bool valid = false;
        if (code == 200 && !body.isEmpty()) {
            const QJsonObject o = QJsonDocument::fromJson(body).object();
            valid = !o.value(QStringLiteral("version")).toString().isEmpty() && o.contains(QStringLiteral("assets"));
        }
        if (valid) {
            MirrorProbe p;
            p.prefix = prefix;
            p.manifest = body;
            p.manifestSec = sec;
            m_mirrorProbes.push_back(p);
        }
        probeNextMirrorManifest();
    });
}

void UpdateDialog::startMirrorSampleTest()
{
    if (m_mirrorProbes.isEmpty()) {
        setBusy(false);
        setStatusText(slime::ui::t("ui.0947947cec9d"));
        emit probeFinished(false, QStringLiteral("mirrors-unreachable"));
        return;
    }
    if (m_mirrorProbes.size() == 1) {
        finishMirrorSelection();
        return;
    }
    // 用清单里的 zip 做 256 KB 采样测速（只测清单响应最快的前 3 个，避免等待过久）
    std::sort(m_mirrorProbes.begin(), m_mirrorProbes.end(),
              [](const MirrorProbe& a, const MirrorProbe& b) { return a.manifestSec < b.manifestSec; });
    if (m_mirrorProbes.size() > 3) m_mirrorProbes.resize(3);
    m_probeZipUrl = manifestZipUrl(m_mirrorProbes.first().manifest);
    if (m_probeZipUrl.isEmpty()) {
        finishMirrorSelection();
        return;
    }
    m_sampleIndex = 0;
    probeNextMirrorSample();
}

void UpdateDialog::probeNextMirrorSample()
{
    if (m_sampleIndex >= m_mirrorProbes.size()) {
        finishMirrorSelection();
        return;
    }
    MirrorProbe& p = m_mirrorProbes[m_sampleIndex];
    const QString url = transformGithubUrl(m_probeZipUrl, p.prefix);
    if (url.isEmpty()) {
        ++m_sampleIndex;
        probeNextMirrorSample();
        return;
    }
    setStatusText(slime::ui::t("ui.057071fe4a58")
                      .arg(m_sampleIndex + 1)
                      .arg(m_mirrorProbes.size())
                      .arg(p.prefix));
    const QString tmp = QDir::tempPath() + QStringLiteral("/slime_sample_%1_%2.bin")
                                             .arg(QCoreApplication::applicationPid())
                                             .arg(m_sampleIndex);
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    const QStringList args = {
        QStringLiteral("-L"), QStringLiteral("--max-time"),
        QStringLiteral("8"), QStringLiteral("--range"), QStringLiteral("0-262143"),
        QStringLiteral("-sS"), QStringLiteral("-A"), QStringLiteral("SlimeFinder-Updater/1.0"),
        QStringLiteral("-o"), tmp, QStringLiteral("-w"), QStringLiteral("%{http_code}"), url,
    };
    runCurl(args, tmp, false, [this, t0](int code, const QByteArray&) {
        const double sec = (QDateTime::currentMSecsSinceEpoch() - t0) / 1000.0;
        const QString tmp = QDir::tempPath() + QStringLiteral("/slime_sample_%1_%2.bin")
                                                 .arg(QCoreApplication::applicationPid())
                                                 .arg(m_sampleIndex);
        const qint64 bytes = QFileInfo::exists(tmp) ? QFileInfo(tmp).size() : 0;
        QFile::remove(tmp);
        if (code == 200 || code == 206) {
            m_mirrorProbes[m_sampleIndex].sampleSec = sec;
            m_mirrorProbes[m_sampleIndex].sampleBytes = bytes;
        }
        ++m_sampleIndex;
        probeNextMirrorSample();
    });
}

void UpdateDialog::finishMirrorSelection()
{
    MirrorProbe* best = nullptr;
    double bestRate = -1.0;
    for (MirrorProbe& p : m_mirrorProbes) {
        if (p.sampleBytes > 0 && p.sampleSec > 0.0) {
            const double rate = static_cast<double>(p.sampleBytes) / p.sampleSec;
            if (rate > bestRate) { bestRate = rate; best = &p; }
        }
    }
    if (!best) {
        // 没有采样成功：退化为清单响应最快的镜像
        double bestSec = 1e9;
        for (MirrorProbe& p : m_mirrorProbes) {
            if (p.manifestSec < bestSec) { bestSec = p.manifestSec; best = &p; }
        }
    }
    if (!best) {
        setBusy(false);
        setStatusText(slime::ui::t("ui.780e61c2248b"));
        emit probeFinished(false, QStringLiteral("speedtest-failed"));
        return;
    }
    m_activeMirror = best->prefix;
    m_cachedMirror = best->prefix;
    m_cachedMirrorAt = QDateTime::currentSecsSinceEpoch();
    m_mirror->setCurrentText(best->prefix);
    saveConfig();
    setBusy(false);
    QString rateText;
    if (best->sampleBytes > 0 && best->sampleSec > 0.0) {
        rateText = slime::ui::t("ui.2d964eb5d055")
                       .arg(static_cast<qint64>(best->sampleBytes / best->sampleSec / 1024.0));
    }
    setStatusText(slime::ui::t("ui.08831e1e0a75")
                      .arg(best->prefix, rateText));
    parseLatest(best->manifest);
}

void UpdateDialog::probeStart(int sourceIndex, const QString& mirrorPrefix)
{
    if (sourceIndex < Auto) sourceIndex = Auto;
    if (sourceIndex > Pan) sourceIndex = Pan;
    m_source->setCurrentIndex(sourceIndex);
    if (!mirrorPrefix.isEmpty()) m_mirror->setCurrentText(mirrorPrefix);
    startCheck();
}

void UpdateDialog::probeDownload()
{
    startDownload();
}

void UpdateDialog::onApiDone(int httpCode, const QByteArray& body)
{
    if (httpCode != 200) { tryNextApi(); return; }
    parseLatest(body);
}

void UpdateDialog::parseLatest(const QByteArray& body)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        tryNextApi();
        return;
    }
    QJsonObject o;
    if (doc.isObject()) {
        o = doc.object();
    } else if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            if (v.toObject().value(QStringLiteral("tag_name")).toString().isEmpty()) continue;
            o = v.toObject();
            break;
        }
    }
    if (o.isEmpty()) {
        tryNextApi();
        return;
    }
    // 两种格式：GitHub API（tag_name/body/assets[].browser_download_url）
    // 与 version.json 清单（version/notes/assets[].url）
    QString tag = o.value(QStringLiteral("tag_name")).toString();
    QString ver;
    if (tag.isEmpty()) {
        ver = normalizeVer(o.value(QStringLiteral("version")).toString());
        if (ver.isEmpty()) {
            tryNextApi();
            return;
        }
        tag = QStringLiteral("v") + ver;
    } else {
        ver = normalizeVer(tag);
    }
    const QString current = normalizeVer(QCoreApplication::applicationVersion());
    m_latestTag = tag;
    m_latestVersion = ver;
    m_notesText = o.contains(QStringLiteral("notes"))
                      ? o.value(QStringLiteral("notes")).toString()
                      : o.value(QStringLiteral("body")).toString();

    const QJsonArray assets = o.value(QStringLiteral("assets")).toArray();
    Asset zip;
    Asset sums;
    for (const QJsonValue& v : assets) {
        const QJsonObject a = v.toObject();
        Asset as;
        const QString rawName = a.value(QStringLiteral("name")).toString();
        as.name = QFileInfo(rawName).fileName();
        if (as.name.isEmpty() || as.name != rawName) continue; // 禁止资产名路径穿越
        as.url = a.value(QStringLiteral("browser_download_url")).toString();
        if (as.url.isEmpty()) as.url = a.value(QStringLiteral("url")).toString();
        as.size = static_cast<qint64>(a.value(QStringLiteral("size")).toDouble());
        if (as.name.endsWith(QStringLiteral("-Setup.exe")) && zip.url.isEmpty()) {
            zip = as;
        } else if (as.name.endsWith(QStringLiteral("-win64.zip")) && zip.url.isEmpty()) {
            zip = as;   // 旧版本兼容（3.10.12.10 起只发 Setup）
        } else if (as.name.startsWith(QStringLiteral("SHA256SUMS-")) && sums.url.isEmpty()) {
            sums = as;
        }
    }
    m_zipAsset = zip;
    m_sumsAsset = sums;

    if (!m_waitingForceCheck) {
        // 网络结果缓存 6 小时（按通道区分）
        m_cacheManifest = QString::fromUtf8(body);
        m_cacheTime = QDateTime::currentSecsSinceEpoch();
        m_cacheChannel = m_channel ? m_channel->currentIndex() : Stable;
        saveConfig();
    }

    setBusy(false);
    if (versionNewer(ver, current)) {
        m_updateAvailable = true;
        setStatusText(slime::ui::t("ui.1cc383f53f07").arg(ver, current) + QStringLiteral("\n") +
                      slime::ui::t("ui.update.manualOnly"));
        QString notes = releaseNotesFor(m_notesText);
        if (notes.isEmpty()) notes = slime::ui::t("ui.7e818e98207a");
        m_notes->setPlainText(notes);
        // 无代码签名证书：本程序不自动下载/安装更新，只提示新版本。
        m_downloadBtn->setVisible(false);
        m_downloadBtn->setEnabled(false);
        m_progress->setVisible(false);
        m_folderBtn->setVisible(false);
    } else {
        m_updateAvailable = false;
        setStatusText(slime::ui::t("ui.f11ef12bb9bd").arg(current));
        m_notes->setPlainText(releaseNotesFor(m_notesText));
        m_downloadBtn->setVisible(false);
        m_downloadBtn->setEnabled(false);
    }
    emit probeFinished(m_updateAvailable,
                       QStringLiteral("%1|%2|%3").arg(ver, m_zipAsset.name, m_activeMirror));
}

QString UpdateDialog::manifestZipUrl(const QByteArray& body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QJsonArray assets = o.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& v : assets) {
        const QJsonObject a = v.toObject();
        const QString name = a.value(QStringLiteral("name")).toString();
        if (!name.endsWith(QStringLiteral("-Setup.exe")) &&
            !name.endsWith(QStringLiteral("-win64.zip"))) continue;
        QString url = a.value(QStringLiteral("browser_download_url")).toString();
        if (url.isEmpty()) url = a.value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) return url;
    }
    return {};
}

QString UpdateDialog::releaseNotesFor(const QString& body)
{
    return body.trimmed();
}

bool UpdateDialog::versionNewer(const QString& latest, const QString& current)
{
    const QStringList a = versionParts(latest);
    const QStringList b = versionParts(current);
    const int n = std::max(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const int av = i < a.size() ? a.at(i).toInt() : 0;
        const int bv = i < b.size() ? b.at(i).toInt() : 0;
        if (av != bv) return av > bv;
    }
    return false;
}

// ---------------- 下载 ----------------
void UpdateDialog::startDownload()
{
    if (!m_updateAvailable || m_zipAsset.url.isEmpty()) return;
    m_downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (m_downloadDir.isEmpty()) m_downloadDir = QDir::homePath();
    m_downloadDir += QStringLiteral("/SlimeFinder-update-v") + m_latestVersion;
    QDir().mkpath(m_downloadDir);
    if (m_zipAsset.name.isEmpty() || m_zipAsset.name != QFileInfo(m_zipAsset.name).fileName()) return;
    m_downloadPath = m_downloadDir + QLatin1Char('/') + m_zipAsset.name;

    m_downloadUrls = buildDownloadUrls(m_zipAsset.url);
    if (m_downloadUrls.isEmpty()) {
        setStatusText(slime::ui::t("ui.4eff45d10cb0"));
        return;
    }
    m_downloadIndex = 0;
    m_pendingDownload = true;
    setBusy(true);
    m_progress->setRange(0, 0);
    setStatusText(slime::ui::t("ui.b5ab16bc1a7b")
                      .arg(m_downloadUrls.size()));
    tryNextDownload();
}

void UpdateDialog::tryNextDownload()
{
    if (m_downloadIndex >= m_downloadUrls.size()) {
        m_pendingDownload = false;
        setBusy(false);
        setStatusText(slime::ui::t("ui.ce4f259b9f06"));
        emit probeDownloadFinished(false, QStringLiteral("all-download-urls-failed"));
        return;
    }
    const QString url = m_downloadUrls.at(m_downloadIndex++);
    setStatusText(slime::ui::t("ui.018f6944c95e")
                      .arg(m_downloadIndex)
                      .arg(m_downloadUrls.size())
                      .arg(url));
    const QStringList args = {
        QStringLiteral("-L"), QStringLiteral("--max-time"),
        QStringLiteral("900"), QStringLiteral("-sS"), QStringLiteral("-A"),
        QStringLiteral("SlimeFinder-Updater/1.0"), QStringLiteral("-o"), m_downloadPath,
        QStringLiteral("-w"), QStringLiteral("%{http_code}"), url,
    };
    runCurl(args, m_downloadPath, false, [this](int code, const QByteArray&) {
        const bool fileOk = QFileInfo::exists(m_downloadPath) && QFileInfo(m_downloadPath).size() > 0;
        onZipDone(code, fileOk);
    });
}

void UpdateDialog::onZipDone(int httpCode, bool ok)
{
    if ((httpCode == 200 || httpCode == 206) && ok) {
        m_downloadBtn->setEnabled(false);
        m_progress->setRange(0, 0);
        setStatusText(slime::ui::t("ui.6089423ca2fc"));
        fetchSumsThenFinish();
        return;
    }
    tryNextDownload();
}

void UpdateDialog::failDownloadVerification(const QString& reason, const QString& message)
{
    m_pendingDownload = false;
    m_downloadBtn->setEnabled(false);
    setBusy(false);
    setStatusText(message);
    QFile::remove(m_downloadPath);
    emit probeDownloadFinished(false, reason);
}

void UpdateDialog::fetchSumsThenFinish()
{
    if (m_sumsAsset.url.isEmpty() || m_sumsAsset.name.isEmpty()) {
        failDownloadVerification(QStringLiteral("sums-missing"),
                                 slime::ui::t("ui.update.sums.required"));
        return;
    }
    const QStringList urls = buildDownloadUrls(m_sumsAsset.url);
    if (urls.isEmpty()) {
        failDownloadVerification(QStringLiteral("sums-unreachable"),
                                 slime::ui::t("ui.update.sums.required"));
        return;
    }
    const QString tmp = QDir::tempPath() + QStringLiteral("/slime_update_%1_sums.txt")
                                             .arg(QCoreApplication::applicationPid());
    const QStringList args = {
        QStringLiteral("-L"), QStringLiteral("--max-time"),
        QStringLiteral("30"), QStringLiteral("-sS"), QStringLiteral("-A"),
        QStringLiteral("SlimeFinder-Updater/1.0"), QStringLiteral("-o"), tmp,
        QStringLiteral("-w"), QStringLiteral("%{http_code}"), urls.first(),
    };
    runCurl(args, tmp, true, [this](int code, const QByteArray& body) {
        if (code == 200) onSumsDone(code, body);
        else finishDownloadOk();
    });
}

void UpdateDialog::onSumsDone(int httpCode, const QByteArray& body)
{
    Q_UNUSED(httpCode);
    QString want;
    const QString text = QString::fromUtf8(body);
    const QString zipName = QFileInfo(m_downloadPath).fileName();
    for (const QString& line : text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts)) {
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 2) continue;
        if (line.contains(zipName)) {
            for (const QString& p : parts) {
                if (p.size() == 64 && p.contains(QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")))) {
                    want = p.toLower();
                    break;
                }
            }
            break;
        }
    }
    if (want.isEmpty()) {
        failDownloadVerification(QStringLiteral("sha256-missing"),
                                 slime::ui::t("ui.update.sums.required"));
        return;
    }
    QFile f(m_downloadPath);
    QByteArray got;
    if (f.open(QIODevice::ReadOnly)) {
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (h.addData(&f)) got = h.result().toHex().toLower();
    }
    if (got != want) {
        QFile::remove(m_downloadPath);
        m_pendingDownload = false;
        setBusy(false);
        setStatusText(slime::ui::t("ui.e8b133e9b0cf"));
        emit probeDownloadFinished(false, QStringLiteral("sha256-mismatch"));
        return;
    }
    finishDownloadOk();
}

void UpdateDialog::finishDownloadOk()
{
    m_pendingDownload = false;
    setBusy(false);
    m_progress->setVisible(true);
    m_progress->setRange(0, 100);
    m_progress->setValue(100);
    m_folderBtn->setVisible(true);
    m_downloadBtn->setEnabled(false);
    if (m_downloadPath.endsWith(QStringLiteral("-Setup.exe"), Qt::CaseInsensitive) && isInstalledCopy()) {
        setStatusText(slime::ui::t("ui.f002c118e5c0"));
        const auto answer = QMessageBox::question(
            this, slime::ui::t("ui.b93a1ef02fdf"),
            slime::ui::t("ui.22d851990ad1"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            emit probeDownloadFinished(true, m_downloadPath);
            startInstallerAndQuit();
            return;
        }
        setStatusText(slime::ui::t("ui.3fa881bd3073"));
    } else if (m_downloadPath.endsWith(QStringLiteral("-Setup.exe"), Qt::CaseInsensitive)) {
        setStatusText(slime::ui::t("ui.58b397270ac5"));
    } else {
        setStatusText(slime::ui::t("ui.365f4ad9cc7b").arg(QDir::toNativeSeparators(m_downloadPath)));
    }
    m_notes->appendPlainText(slime::ui::t("ui.6d3e8e29a223") + m_downloadPath);
    emit probeDownloadFinished(true, m_downloadPath);
}

bool UpdateDialog::isInstalledCopy() const
{
#ifdef Q_OS_WIN
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG rc = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SlimeFinder", L"InstallDir",
                                 RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, &type, buf, &size);
    if (rc != ERROR_SUCCESS) return false;
    const QString installDir = QDir::cleanPath(QString::fromWCharArray(buf));
    const QString appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
    return installDir.compare(appDir, Qt::CaseInsensitive) == 0;
#else
    return false;
#endif
}

void UpdateDialog::startInstallerAndQuit()
{
    if (m_downloadPath.isEmpty()) return;
    // 以 /UPDATE=1 启动安装向导（保留 config.json/缓存）；用户自己点下一步。
    const bool started = QProcess::startDetached(m_downloadPath, { QStringLiteral("/UPDATE=1") },
                                                 QFileInfo(m_downloadPath).absolutePath());
    if (!started) {
        setStatusText(slime::ui::t("ui.1c9aa694df5d")
                          .arg(QDir::toNativeSeparators(m_downloadPath)));
        return;
    }
    QTimer::singleShot(200, qApp, &QCoreApplication::quit);
}

void UpdateDialog::openDownloadFolder()
{
    if (m_downloadDir.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_downloadDir));
}

// ---------------- 页面入口 ----------------
void UpdateDialog::openPanPage()
{
    m_browserOpened = true;
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(kPanUrl)));
}

void UpdateDialog::openGithubPage()
{
    m_browserOpened = true;
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(kGithubReleases)));
}
