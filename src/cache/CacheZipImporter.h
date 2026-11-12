// CacheZipImporter.h - v3.12.3：官方缓存 zip 的严格校验与导入
#pragma once

#include <QString>
#include <QStringList>

#include <filesystem>

namespace slime::cache {

struct ZipInspection {
    bool ok = false;
    QString zipName;
    QString rangeLabel;
    int packCount = 0;
    qint64 bytes = 0;
    QString error;
    QStringList packFiles;
};

class CacheZipImporter {
public:
    // 只读取 zip 内的 manifest 并校验结构，不解压 pack、不写入缓存。
    static bool inspect(const QString& zipPath, ZipInspection& out);

    // 用户确认后才调用：完整校验 + 解压 + 合并 + 去重。
    // 遇到任何不合规内容应整包拒绝，不写入半成品。
    static bool importZip(const QString& zipPath, const std::filesystem::path& cacheRoot,
                          QString* error, int* imported, int* skipped);
};

} // namespace slime::cache
