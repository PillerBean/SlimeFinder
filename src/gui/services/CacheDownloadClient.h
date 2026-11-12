// CacheDownloadClient.h - v3.12.3：官方缓存 manifest / pack 下载（R2 主源，国内网盘备用源预留）
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

#include <functional>

class QNetworkAccessManager;

struct RemoteCachePack {
    QString file;
    QString name;
    QUrl url;
    qint64 bytes = 0;
    QString sha256;
    qint32 minX = 0;
    qint32 maxX = -1;
    qint32 minZ = 0;
    qint32 maxZ = -1;
    QString source = QStringLiteral("downloaded");
};

class CacheDownloadClient : public QObject {
public:
    using ManifestCallback =
        std::function<void(bool ok, const QString& error, const QVector<RemoteCachePack>& packs)>;
    using DownloadCallback = std::function<void(bool ok, const QString& error)>;

    explicit CacheDownloadClient(QObject* parent = nullptr);

    // 主源/备用源 manifest 地址。空字符串表示未配置。
    static QString primaryManifestUrl();
    static QString fallbackManifestUrl();

    // manifestUrl 为空时直接返回失败“未配置下载源”。
    void fetchManifest(const QUrl& manifestUrl, ManifestCallback done);

    // packUrl 已由 manifest 解析；destPath 为最终 .slmz 路径，内部写 .part 后再校验/替换。
    void downloadPack(const QUrl& packUrl, const RemoteCachePack& pack, const QString& destPath,
                      DownloadCallback done);

private:
    QNetworkAccessManager* m_net = nullptr;
};
