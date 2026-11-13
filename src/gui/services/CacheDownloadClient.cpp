#include "gui/services/CacheDownloadClient.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <memory>

namespace {
QUrl resolvePackUrl(const QUrl& manifestUrl, const QString& file, const QString& explicitUrl)
{
    if (!explicitUrl.isEmpty()) return QUrl(explicitUrl);
    if (file.startsWith(QStringLiteral("http://")) || file.startsWith(QStringLiteral("https://")))
        return QUrl(file);
    QUrl base = manifestUrl;
    QString dir = QFileInfo(manifestUrl.path()).path();
    if (!dir.endsWith(QLatin1Char('/'))) dir += QLatin1Char('/');
    base.setPath(dir + file);
    base.setQuery(QString());
    base.setFragment(QString());
    return base;
}

QString configValue(const QString& group, const QString& key)
{
    QFile f(QCoreApplication::applicationDirPath() + QStringLiteral("/config.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return {};
    return doc.object().value(QStringLiteral("cacheDownload")).toObject().value(key).toString();
}

QString normalizedError(const QNetworkReply* reply)
{
    if (!reply) return QStringLiteral("网络请求失败");
    return reply->errorString().isEmpty() ? QStringLiteral("网络请求失败") : reply->errorString();
}
} // namespace

CacheDownloadClient::CacheDownloadClient(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

QString CacheDownloadClient::primaryManifestUrl()
{
    const QString env = qEnvironmentVariable("SLIME_CACHE_MANIFEST");
    if (!env.trimmed().isEmpty()) return env;
    const QString configured = configValue(QStringLiteral("cacheDownload"), QStringLiteral("primary"));
    if (!configured.trimmed().isEmpty()) return configured;
    // v3.12.3：默认官方分发入口（Cloudflare Pages，国内可达性优于 workers.dev）。
    return QStringLiteral("https://slimefinder-cache.pages.dev/v3.12.3/manifest.json");
}

QString CacheDownloadClient::fallbackManifestUrl()
{
    const QString env = qEnvironmentVariable("SLIME_CACHE_MANIFEST_FALLBACK");
    if (!env.trimmed().isEmpty()) return env;
    return configValue(QStringLiteral("cacheDownload"), QStringLiteral("fallback"));
}

QString CacheDownloadClient::netdiskRangeUrl(int index)
{
    static const char* keys[] = { "netdiskNearby", "netdiskLocal", "netdiskLarge", "netdiskAll" };
    static const char* defaults[] = {
        "https://wwben.lanzoul.com/b01d7e5jyf",
        "https://wwben.lanzoul.com/b01d7e5k0h",
        "https://wwben.lanzoul.com/b01d7e5kah",
        "https://wwben.lanzoul.com/b01d7e5l9c"
    };
    if (index >= 0 && index < 4) {
        const QString v = configValue(QStringLiteral("cacheDownload"), QString::fromLatin1(keys[index]));
        if (!v.trimmed().isEmpty()) return v;
        return QString::fromLatin1(defaults[index]);
    }
    return netdiskUrl();
}

QString CacheDownloadClient::netdiskUrl()
{
    const QString env = qEnvironmentVariable("SLIME_CACHE_NETDISK");
    if (!env.trimmed().isEmpty()) return env;
    const QString cfg = configValue(QStringLiteral("cacheDownload"), QStringLiteral("netdisk"));
    if (!cfg.trimmed().isEmpty()) return cfg;
    return QStringLiteral("https://wwben.lanzoul.com/b01d7e4lre");
}

void CacheDownloadClient::fetchManifest(const QUrl& manifestUrl, ManifestCallback done)
{
    if (!manifestUrl.isValid() || manifestUrl.isEmpty()) {
        done(false, QStringLiteral("未配置缓存下载源"), {});
        return;
    }
    QNetworkRequest req(manifestUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "SlimeFinder/3.12.3");
    req.setTransferTimeout(30000);
    auto* reply = m_net->get(req);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, manifestUrl, done]() {
        reply->deleteLater();
        const QByteArray quota = reply->rawHeader("x-slimefinder-quota");
        m_quotaWarning = quota == "warn" ? QStringLiteral("warn")
                         : quota == "exhausted" ? QStringLiteral("exhausted")
                                                 : QString();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, normalizedError(reply), {});
            return;
        }
        const QByteArray body = reply->readAll();
        QJsonParseError parseErr{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            done(false, QStringLiteral("manifest JSON 解析失败"), {});
            return;
        }
        const QJsonObject root = doc.object();
        const int format = root.value(QStringLiteral("formatVersion")).toInt(0);
        if (format != 3) {
            done(false, QStringLiteral("manifest 格式版本不支持"), {});
            return;
        }
        QVector<RemoteCachePack> packs;
        const QString fallbackBase = root.value(QStringLiteral("fallbackBaseUrl")).toString();
        const QUrl fallbackManifest = fallbackBase.isEmpty() ? QUrl() : QUrl(fallbackBase);
        const QJsonArray arr = root.value(QStringLiteral("packs")).toArray();
        for (const auto& v : arr) {
            const QJsonObject o = v.toObject();
            RemoteCachePack p;
            p.file = o.value(QStringLiteral("file")).toString();
            p.name = o.value(QStringLiteral("name")).toString(p.file);
            p.bytes = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
            p.sha256 = o.value(QStringLiteral("sha256")).toString();
            p.minX = o.value(QStringLiteral("minX")).toInt();
            p.maxX = o.value(QStringLiteral("maxX")).toInt(-1);
            p.minZ = o.value(QStringLiteral("minZ")).toInt();
            p.maxZ = o.value(QStringLiteral("maxZ")).toInt(-1);
            p.source = o.value(QStringLiteral("source")).toString(QStringLiteral("downloaded"));
            p.url = resolvePackUrl(manifestUrl, p.file, o.value(QStringLiteral("url")).toString());
            if (fallbackManifest.isValid() && !fallbackManifest.isEmpty()) {
                const QString explicitFallback = o.value(QStringLiteral("fallbackUrl")).toString();
                p.fallbackUrl = resolvePackUrl(fallbackManifest, p.file, explicitFallback);
            }
            if (!p.file.isEmpty() && p.url.isValid() && p.maxX >= p.minX && p.maxZ >= p.minZ) {
                packs.push_back(std::move(p));
            }
        }
        std::sort(packs.begin(), packs.end(), [](const RemoteCachePack& a, const RemoteCachePack& b) {
            if (a.minZ != b.minZ) return a.minZ < b.minZ;
            return a.minX < b.minX;
        });
        done(true, {}, packs);
    });
}

void CacheDownloadClient::downloadPack(const QUrl& packUrl, const RemoteCachePack& pack,
                                       const QString& destPath, DownloadCallback done,
                                       DownloadProgress progress)
{
    if (!packUrl.isValid()) {
        done(false, QStringLiteral("缓存包 URL 无效"));
        return;
    }
    const QString partPath = destPath + QStringLiteral(".part");
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    const qint64 offset = QFileInfo(partPath).size();
    QNetworkRequest req(packUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "SlimeFinder/3.12.3");
    req.setTransferTimeout(45000);
    if (offset > 0) {
        req.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(offset) + "-");
    }
    auto* reply = m_net->get(req);
    if (progress) {
        QObject::connect(reply, &QNetworkReply::downloadProgress, this,
                         [progress](qint64 got, qint64 total) { progress(got, total); });
    }
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [reply, partPath, destPath, pack, offset, done]() {
                         reply->deleteLater();
                         if (reply->error() != QNetworkReply::NoError) {
                             // 保留 .part，下一次可继续断点下载。
                             done(false, normalizedError(reply));
                             return;
                         }
                         const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         const QByteArray data = reply->readAll();
                         QFile part(partPath);
                         if (offset > 0 && status == 206) {
                             if (!part.open(QIODevice::Append)) {
                                 done(false, QStringLiteral("无法追加缓存包临时文件"));
                                 return;
                             }
                         } else {
                             if (offset > 0 && status == 200) {
                                 // 服务端不支持 Range：重新从头写。
                                 part.close();
                                 QFile::remove(partPath);
                             }
                             if (!part.open(QIODevice::WriteOnly)) {
                                 done(false, QStringLiteral("无法写入缓存包临时文件"));
                                 return;
                             }
                         }
                         if (part.write(data) != data.size()) {
                             part.close();
                             done(false, QStringLiteral("缓存包写入失败"));
                             return;
                         }
                         part.close();
                         const qint64 total = QFileInfo(partPath).size();
                         if (pack.bytes > 0 && total != pack.bytes) {
                             done(false, QStringLiteral("缓存包大小校验失败"));
                             return;
                         }
                         if (!pack.sha256.isEmpty()) {
                             QFile verify(partPath);
                             if (!verify.open(QIODevice::ReadOnly)) {
                                 done(false, QStringLiteral("无法读取缓存包临时文件"));
                                 return;
                             }
                             const QByteArray all = verify.readAll();
                             const QString got = QString::fromLatin1(
                                 QCryptographicHash::hash(all, QCryptographicHash::Sha256).toHex());
                             if (got.compare(pack.sha256, Qt::CaseInsensitive) != 0) {
                                 QFile::remove(partPath);
                                 done(false, QStringLiteral("缓存包 SHA256 校验失败"));
                                 return;
                             }
                         }
                         QFile::remove(destPath);
                         if (!QFile::rename(partPath, destPath)) {
                             done(false, QStringLiteral("缓存包替换失败"));
                             return;
                         }
                         done(true, {});
                     });

}

void CacheDownloadClient::downloadPackWithFallback(const QUrl& packUrl, const QUrl& fallbackUrl,
                                                   const RemoteCachePack& pack, const QString& destPath,
                                                   DownloadCallback done, DownloadProgress progress)
{
    if (fallbackUrl.isEmpty() || fallbackUrl == packUrl) {
        downloadPack(packUrl, pack, destPath, done, progress);
        return;
    }
    downloadPack(packUrl, pack, destPath, [this, fallbackUrl, pack, destPath, done, progress](bool ok, const QString& err) {
        if (ok) {
            done(true, {});
            return;
        }
        downloadPack(fallbackUrl, pack, destPath, [done, err](bool ok2, const QString& err2) {
            if (ok2) done(true, {});
            else done(false, err + QStringLiteral(" / ") + err2);
        });
    });
}
