#include "cache/CacheStore.h"
#include "cache/CacheZipImporter.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::printf("usage: zip_import_test <zip> <cache-root>\n");
        return 2;
    }
    slime::cache::ZipInspection info;
    if (!slime::cache::CacheZipImporter::inspect(QString::fromUtf8(argv[1]), info)) {
        std::printf("inspect FAILED: %s\n", info.error.toUtf8().constData());
        return 1;
    }
    std::printf("inspect OK: range=%s packs=%d bytes=%lld\n", info.rangeLabel.toUtf8().constData(),
                info.packCount, static_cast<long long>(info.bytes));
    int imported = 0, skipped = 0;
    QString err;
    slime::cache::CacheStore store{ std::filesystem::path(std::string(argv[2])) };
    std::string storeErr;
    if (!store.load(&storeErr)) {
        std::printf("store load FAILED: %s\n", storeErr.c_str());
        return 1;
    }
    if (!slime::cache::CacheZipImporter::importZip(QString::fromUtf8(argv[1]), store.root(), &err,
                                                   &imported, &skipped)) {
        std::printf("import FAILED: %s\n", err.toUtf8().constData());
        return 1;
    }
    if (!store.load(&storeErr)) {
        std::printf("reload FAILED: %s\n", storeErr.c_str());
        return 1;
    }
    std::printf("import OK: imported=%d skipped=%d cachePacks=%zu\n", imported, skipped,
                store.packs().size());
    return store.packs().empty() ? 1 : 0;
}
