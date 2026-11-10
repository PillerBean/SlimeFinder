#include "gui/services/TelemetryClient.h"

#include "i18n/UiText.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace slime::gui {

namespace {
constexpr const char* kConfigFile = "telemetry.json";
constexpr const char* kDefaultEndpoint = "https://slimefinder-telemetry.pages.dev/v1/event";
constexpr const char* kSchema = "schema";
constexpr int kSchemaVersion = 1;

QString trimTo(const QString& s, int max) { return s.left(max); }

QString ramGB()
{
#ifdef _WIN32
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return QString::number(static_cast<qulonglong>(ms.ullTotalPhys) / (1024ULL * 1024ULL * 1024ULL));
    }
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) {
        return QString::number((static_cast<qulonglong>(pages) * static_cast<qulonglong>(pageSize)) /
                               (1024ULL * 1024ULL * 1024ULL));
    }
#endif
    return QStringLiteral("0");
}

QString osArch()
{
    const QString a = QSysInfo::currentCpuArchitecture();
    if (a.contains(QStringLiteral("64"))) return QStringLiteral("x86_64");
    if (a.contains(QStringLiteral("arm"))) return QStringLiteral("arm64");
    return a;
}
} // namespace

TelemetryClient::TelemetryClient(QObject* parent) : QObject(parent)
{
    load();
}

QString TelemetryClient::configPath()
{
    return QCoreApplication::applicationDirPath() + QLatin1Char('/') + QLatin1String(kConfigFile);
}

void TelemetryClient::load()
{
    m_enabled = false;
    m_endpoint.clear();
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) {
        m_endpoint = QString::fromLatin1(kDefaultEndpoint);
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_enabled = o.value(QStringLiteral("enabled")).toBool(false);
    m_endpoint = o.value(QStringLiteral("endpoint")).toString().trimmed();
    // 没有本地配置时使用已部署的 Pages 端点；默认仍然关闭
    if (m_endpoint.isEmpty()) m_endpoint = QString::fromLatin1(kDefaultEndpoint);
}

void TelemetryClient::setSettings(bool enabled, const QString& endpoint)
{
    m_enabled = enabled;
    m_endpoint = endpoint.trimmed();
    save();
}

void TelemetryClient::save() const
{
    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QJsonObject o;
    o.insert(QStringLiteral("enabled"), m_enabled);
    o.insert(QStringLiteral("endpoint"), m_endpoint);
    o.insert(QStringLiteral("note"), slime::ui::t("ui.3bbaf75b8117"));
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

QJsonObject TelemetryClient::hardware() const
{
    QJsonObject h;
    QString cpu = qEnvironmentVariable("PROCESSOR_IDENTIFIER").trimmed();
    if (cpu.isEmpty()) cpu = QSysInfo::currentCpuArchitecture();
    h.insert(QStringLiteral("cpu"), trimTo(cpu, 96));
    h.insert(QStringLiteral("cpu_threads"), QThread::idealThreadCount());
    h.insert(QStringLiteral("ram_gb"), ramGB().toInt());
    return h;
}

QJsonObject TelemetryClient::base(const QString& version, const QString& channel) const
{
    QJsonObject app;
    app.insert(QStringLiteral("version"), trimTo(version, 24));
    app.insert(QStringLiteral("channel"), trimTo(channel, 16));
    app.insert(QStringLiteral("language"), QStringLiteral("zh"));

    QJsonObject os;
    os.insert(QStringLiteral("name"), trimTo(QSysInfo::prettyProductName(), 64));
    os.insert(QStringLiteral("arch"), osArch());

    QJsonObject root;
    root.insert(QStringLiteral("app"), app);
    root.insert(QStringLiteral("os"), os);
    root.insert(QStringLiteral("hw"), hardware());
    return root;
}

void TelemetryClient::reportAppStart(const QString& version, const QString& channel)
{
    if (!m_enabled || m_endpoint.isEmpty() || m_appStartReported) return;
    m_appStartReported = true;
    QJsonObject e = base(version, channel);
    e.insert(QStringLiteral("schema"), kSchemaVersion);
    e.insert(QStringLiteral("event"), QStringLiteral("app_start"));
    post(e);
}

void TelemetryClient::reportSearchDetailed(const QString& version, const QString& channel,
                                            const QString& gpuName, const QJsonObject& search)
{
    if (!m_enabled || m_endpoint.isEmpty()) return;
    QJsonObject e = base(version, channel);
    e.insert(QStringLiteral("schema"), kSchemaVersion);
    e.insert(QStringLiteral("event"), QStringLiteral("search"));
    QJsonObject hw = e.value(QStringLiteral("hw")).toObject();
    hw.insert(QStringLiteral("gpu"), trimTo(gpuName, 96));
    hw.insert(QStringLiteral("gpu_available"), !gpuName.isEmpty());
    e.insert(QStringLiteral("hw"), hw);
    e.insert(QStringLiteral("search"), search);
    post(e);
}

void TelemetryClient::reportCacheBuild(const QString& version, const QString& channel, int64_t areaChunks,
                                       double durationMs, int64_t builtEntries, bool cancelled, bool hitBudget)
{
    if (!m_enabled || m_endpoint.isEmpty()) return;
    QJsonObject e = base(version, channel);
    e.insert(QStringLiteral("schema"), kSchemaVersion);
    e.insert(QStringLiteral("event"), QStringLiteral("cache_build"));
    QJsonObject s;
    s.insert(QStringLiteral("range_log10"), bucketLog10(areaChunks));
    s.insert(QStringLiteral("duration_ms"), static_cast<int>(std::clamp(durationMs, 0.0, 86400000.0)));
    s.insert(QStringLiteral("entries_bucket"), bucketCount(builtEntries));
    s.insert(QStringLiteral("cancelled"), cancelled);
    s.insert(QStringLiteral("hit_budget"), hitBudget);
    e.insert(QStringLiteral("cache"), s);
    post(e);
}

int TelemetryClient::bucketLog10(int64_t v)
{
    if (v <= 0) return 0;
    return std::clamp(static_cast<int>(std::floor(std::log10(static_cast<double>(v)))), 0, 30);
}

int TelemetryClient::bucketCount(int64_t v)
{
    if (v <= 1) return 1;
    if (v <= 5) return 5;
    if (v <= 20) return 20;
    if (v <= 100) return 100;
    if (v <= 1000) return 1000;
    if (v <= 10000) return 10000;
    if (v <= 100000) return 100000;
    return 1000000;
}

void TelemetryClient::post(const QJsonObject& event)
{
    const QString curl = QStandardPaths::findExecutable(QStringLiteral("curl.exe"));
    if (curl.isEmpty()) return;
    const QByteArray body = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (body.size() > 4000) return;

    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/slime_telemetry_XXXXXX.json"));
    tmp.setAutoRemove(false);
    if (!tmp.open()) return;
    tmp.write(body);
    tmp.flush();
    const QString path = tmp.fileName();
    tmp.close();

    auto* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [proc, path](int, QProcess::ExitStatus) {
                QFile::remove(path);
                proc->deleteLater();
            });
    connect(proc, &QProcess::errorOccurred, this, [path](QProcess::ProcessError) {
        QFile::remove(path);
    });
    proc->start(curl, { QStringLiteral("--silent"), QStringLiteral("--show-error"),
                        QStringLiteral("--ssl-no-revoke"), QStringLiteral("--max-time"),
                        QStringLiteral("10"), QStringLiteral("-X"), QStringLiteral("POST"),
                        QStringLiteral("-H"), QStringLiteral("Content-Type: application/json"),
                        QStringLiteral("--data-binary"), QStringLiteral("@") + path, m_endpoint });
    QTimer::singleShot(15000, proc, [proc, path]() {
        if (proc->state() != QProcess::NotRunning) proc->kill();
        QFile::remove(path);
    });
}

} // namespace slime::gui
