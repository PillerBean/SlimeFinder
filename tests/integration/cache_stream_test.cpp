#include "gui/services/CacheDownloadClient.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { std::printf("usage: cache_stream_test manifest.json\n"); return 2; }
    CacheDownloadClient client;
    int total = -1;
    int packs = 0;
    bool ok = false;
    QString error;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&client, &QObject::destroyed, &loop, &QEventLoop::quit);
    const QString source = QString::fromUtf8(argv[1]);
    const QUrl sourceUrl = (source.startsWith(QStringLiteral("http://")) || source.startsWith(QStringLiteral("https://")))
                               ? QUrl(source)
                               : QUrl::fromLocalFile(source);
    client.fetchManifestStream(sourceUrl,
        [&](int n) { total = n; },
        [&](const RemoteCachePack&) { ++packs; },
        [&](bool good, const QString& err) { ok = good; error = err; loop.quit(); });
    timeout.start(15000);
    loop.exec();
    std::printf("total=%d packets=%d ok=%d error=%s\n", total, packs, ok ? 1 : 0, error.toUtf8().constData());
    return (ok && total == packs && packs > 0) ? 0 : 1;
}
