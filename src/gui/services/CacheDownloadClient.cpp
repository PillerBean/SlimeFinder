#include "gui/services/CacheDownloadClient.h"

#include <QCryptographicHash>
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
    base.setPath(QFileInfo(manifestUrl.path()).path() + QLatin1Char('/') + file);
    base.setQuery(QString());
    base.setFragment(QString());
    return base;
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
    return qEnvironmentVariable("SLIME_CACHE_MANIFEST");
}

QString CacheDownloadClient::fallbackManifestUrl()
{
    return qEnvironmentVariable("SLIME_CACHE_MANIFEST_FALLBACK");
}

void CacheDownloadClient::fetchManifest(const QUrl& manifestUrl, ManifestCallback done)
{
    if (!manifestUrl.isValid() || manifestUrl.isEmpty()) {
        done(false, QStringLiteral("未配置缓存下载源"), {});
        return;
    }
    QNetworkRequest req(manifestUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = m_net->get(req);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, manifestUrl, done]() {
        reply->deleteLater();
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
                                       const QString& destPath, DownloadCallback done)
{
    if (!packUrl.isValid()) {
        done(false, QStringLiteral("缓存包 URL 无效"));
        return;
    }
    const QString partPath = destPath + QStringLiteral(".part");
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    QNetworkRequest req(packUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = m_net->get(req);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [reply, partPath, destPath, pack, done]() {
                         reply->deleteLater();
                         if (reply->error() != QNetworkReply::NoError) {
                             QFile::remove(partPath);
                             done(false, normalizedError(reply));
                             return;
                         }
                         const QByteArray data = reply->readAll();
                         if (pack.bytes > 0 && data.size() != pack.bytes) {
                             QFile::remove(partPath);
                             done(false, QStringLiteral("缓存包大小校验失败"));
                             return;
                         }
                         if (!pack.sha256.isEmpty()) {
                             const QString got = QString::fromLatin1(
                                 QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
                             if (got.compare(pack.sha256, Qt::CaseInsensitive) != 0) {
                                 done(false, QStringLiteral("缓存包 SHA256 校验失败"));
                                 return;
                             }
                         }
                         QFile part(partPath);
                         if (!part.open(QIODevice::WriteOnly)) {
                             done(false, QStringLiteral("无法写入缓存包临时文件"));
                             return;
                         }
                         if (part.write(data) != data.size()) {
                             part.close();
                             QFile::remove(partPath);
                             done(false, QStringLiteral("缓存包写入失败"));
                             return;
                         }
                         part.close();
                         QFile::remove(destPath);
                         if (!QFile::rename(partPath, destPath)) {
                             QFile::remove(partPath);
                             done(false, QStringLiteral("缓存包替换失败"));
                             return;
                         }
                         done(true, {});
                     });
}
