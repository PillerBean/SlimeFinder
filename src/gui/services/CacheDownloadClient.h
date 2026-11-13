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
    QUrl fallbackUrl;
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
    using DownloadProgress = std::function<void(qint64 received, qint64 total)>;

    explicit CacheDownloadClient(QObject* parent = nullptr);

    // 主源/备用源 manifest 地址。空字符串表示未配置。
    static QString primaryManifestUrl();
    static QString fallbackManifestUrl();
    // 国内网盘人工备用地址（文件夹；不是 manifest JSON）。
    static QString netdiskUrl();
    // index: 0=附近 1=局部 2=较大 3=全部；未单独配置时回退 netdisk
    static QString netdiskRangeUrl(int index);

    // manifestUrl 为空时直接返回失败“未配置下载源”。
    void fetchManifest(const QUrl& manifestUrl, ManifestCallback done);

    // packUrl 已由 manifest 解析；destPath 为最终 .slmz 路径，内部写 .part 后再校验/替换。
    void downloadPack(const QUrl& packUrl, const RemoteCachePack& pack, const QString& destPath,
                      DownloadCallback done, DownloadProgress progress = {});

    // 先主源，失败后尝试 fallbackUrl；两者均失败才回调失败。
    void downloadPackWithFallback(const QUrl& packUrl, const QUrl& fallbackUrl,
                                  const RemoteCachePack& pack, const QString& destPath,
                                  DownloadCallback done, DownloadProgress progress = {});

    // 最近一次 manifest 请求返回的配额状态：ok / warn / exhausted。
    QString quotaWarning() const { return m_quotaWarning; }

private:
    QNetworkAccessManager* m_net = nullptr;
    QString m_quotaWarning;
};
