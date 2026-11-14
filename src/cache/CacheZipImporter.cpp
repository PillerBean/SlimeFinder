#include "cache/CacheZipImporter.h"

#include "cache/CachePack.h"
#include "cache/CacheStore.h"

#include "miniz.h"
#include "miniz_zip.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cstring>
#include <map>
#include <tuple>
#include <memory>
#include <system_error>
#include <vector>

namespace slime::cache {
namespace {

constexpr mz_uint64 kMaxZipBytes = 110ull * 1024 * 1024;      // 网盘单文件上限 100 MB + 余量
constexpr mz_uint64 kMaxTotalUncompressed = 2ull * 1024 * 1024 * 1024;
constexpr mz_uint64 kMaxEntryUncompressed = 768ull * 1024 * 1024;
constexpr mz_uint kMaxEntries = 20000;

struct ZipEntry {
    QString name;           // normalized, '/'
    mz_uint index = 0;
    mz_uint64 uncompressed = 0;
    mz_uint64 compressed = 0;
    mz_uint64 localHeader = 0;
};

struct ManifestPack {
    QString file;           // normalized local/...
    QString name;
    quint64 bytes = 0;
    QString sha256;
    PackRect rect;
};

struct ZipContents {
    std::vector<ZipEntry> entries;
    QMap<QString, mz_uint> names; // normalized name -> index
    QByteArray manifestJson;
    QByteArray batchJson;
    QList<ManifestPack> packs;
    QString rangeLabel;
    mz_uint64 totalUncompressed = 0;
};

QString normalizedEntryName(QString name)
{
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QStringLiteral("./"))) name.remove(0, 2);
    if (name.isEmpty()) return {};
    if (name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char(':'))) return {};
    QStringList parts = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return {};
    for (const QString& part : parts) {
        if (part == QStringLiteral("..") || part == QStringLiteral(".")) return {};
    }
    return parts.join(QLatin1Char('/'));
}

bool isAllowedRootFile(const QString& n)
{
    return n == QStringLiteral("manifest.json") || n == QStringLiteral("batch.json") ||
           n == QStringLiteral("README.txt");
}

bool isAllowedPackFile(const QString& n)
{
    if (!n.startsWith(QStringLiteral("local/"))) return false;
    if (!n.endsWith(QStringLiteral(".slmz"))) return false;
    if (n.size() <= 6) return false;
    if (n.contains(QStringLiteral("//"))) return false;
    return true;
}

bool readBytesFromZip(mz_zip_archive& zip, mz_uint index, QByteArray& out, size_t limit,
                      QString* err)
{
    mz_zip_archive_file_stat st{};
    if (!mz_zip_reader_file_stat(&zip, index, &st)) {
        if (err) *err = QStringLiteral("无法读取 zip 条目");
        return false;
    }
    if (st.m_uncomp_size > limit) {
        if (err) *err = QStringLiteral("zip 条目过大");
        return false;
    }
    void* p = mz_zip_reader_extract_to_heap(&zip, index, nullptr, 0);
    if (!p) {
        if (err) *err = QStringLiteral("zip 条目解压失败");
        return false;
    }
    out = QByteArray(static_cast<const char*>(p), static_cast<int>(st.m_uncomp_size));
    mz_free(p);
    return true;
}

bool parseManifest(const QByteArray& json, ZipContents& out, QString* err)
{
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (err) *err = QStringLiteral("manifest.json 格式错误");
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("formatVersion")).toInt() != 3) {
        if (err) *err = QStringLiteral("manifest 格式版本不支持");
        return false;
    }
    const QJsonArray arr = root.value(QStringLiteral("packs")).toArray();
    if (arr.isEmpty()) {
        if (err) *err = QStringLiteral("manifest 没有 pack");
        return false;
    }
    QSet<QString> seen;
    for (const auto& value : arr) {
        const QJsonObject o = value.toObject();
        ManifestPack p;
        p.file = normalizedEntryName(o.value(QStringLiteral("file")).toString());
        p.name = o.value(QStringLiteral("name")).toString(QFileInfo(p.file).fileName());
        p.bytes = static_cast<quint64>(o.value(QStringLiteral("bytes")).toDouble());
        p.sha256 = o.value(QStringLiteral("sha256")).toString().toLower();
        p.rect = PackRect{ o.value(QStringLiteral("minX")).toInt(),
                           o.value(QStringLiteral("maxX")).toInt(-1),
                           o.value(QStringLiteral("minZ")).toInt(),
                           o.value(QStringLiteral("maxZ")).toInt(-1) };
        if (!isAllowedPackFile(p.file) || p.sha256.size() != 64 || !p.rect.valid()) {
            if (err) *err = QStringLiteral("manifest pack 字段非法");
            return false;
        }
        if (seen.contains(p.file)) {
            if (err) *err = QStringLiteral("manifest 重复 pack");
            return false;
        }
        seen.insert(p.file);
        out.packs.push_back(std::move(p));
    }
    if (root.value(QStringLiteral("totalPacks")).toInt(out.packs.size()) != out.packs.size()) {
        if (err) *err = QStringLiteral("manifest pack 数量不一致");
        return false;
    }
    return true;
}

bool openZip(const QString& zipPath, QByteArray& zipData, mz_zip_archive& zip, ZipContents& out,
             QString* err)
{
    QFile f(zipPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("无法打开 zip");
        return false;
    }
    if (static_cast<mz_uint64>(f.size()) > kMaxZipBytes) {
        if (err) *err = QStringLiteral("zip 超过大小限制");
        return false;
    }
    zipData = f.readAll();
    if (zipData.isEmpty()) {
        if (err) *err = QStringLiteral("zip 为空");
        return false;
    }
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, zipData.constData(), static_cast<size_t>(zipData.size()), 0)) {
        if (err) *err = QStringLiteral("zip 结构损坏");
        return false;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    if (count == 0 || count > kMaxEntries) {
        if (err) *err = QStringLiteral("zip 条目数量非法");
        mz_zip_reader_end(&zip);
        return false;
    }
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) {
            if (err) *err = QStringLiteral("zip 条目损坏");
            mz_zip_reader_end(&zip);
            return false;
        }
        const QString raw = QString::fromUtf8(st.m_filename);
        const QString name = normalizedEntryName(raw);
        if (name.isEmpty()) {
            if (err) *err = QStringLiteral("zip 包含非法路径");
            mz_zip_reader_end(&zip);
            return false;
        }
        if (st.m_is_encrypted || !st.m_is_supported) {
            if (err) *err = QStringLiteral("zip 包含加密或不支持的条目");
            mz_zip_reader_end(&zip);
            return false;
        }
        const mz_uint32 unixMode = (st.m_external_attr >> 16) & 0xFFFFu;
        if ((unixMode & 0xF000u) == 0xA000u) {
            if (err) *err = QStringLiteral("zip 包含符号链接");
            mz_zip_reader_end(&zip);
            return false;
        }
        if (st.m_is_directory) {
            // 只允许 local/ 目录本身，不允许任意目录层级。
            if (name != QStringLiteral("local")) {
                if (err) *err = QStringLiteral("zip 包含不允许的目录");
                mz_zip_reader_end(&zip);
                return false;
            }
            continue;
        }
        if (!isAllowedRootFile(name) && !isAllowedPackFile(name)) {
            if (err) *err = QStringLiteral("zip 包含不允许的文件：%1").arg(name);
            mz_zip_reader_end(&zip);
            return false;
        }
        if (out.names.contains(name)) {
            if (err) *err = QStringLiteral("zip 包含重复条目");
            mz_zip_reader_end(&zip);
            return false;
        }
        if (st.m_uncomp_size > kMaxEntryUncompressed) {
            if (err) *err = QStringLiteral("zip 条目过大");
            mz_zip_reader_end(&zip);
            return false;
        }
        out.totalUncompressed += st.m_uncomp_size;
        ZipEntry e;
        e.name = name;
        e.index = i;
        e.uncompressed = st.m_uncomp_size;
        e.compressed = st.m_comp_size;
        e.localHeader = st.m_local_header_ofs;
        out.names.insert(name, i);
        out.entries.push_back(std::move(e));
    }
    if (out.totalUncompressed > kMaxTotalUncompressed ||
        out.totalUncompressed > static_cast<mz_uint64>(zipData.size()) * 60ull + 64ull * 1024 * 1024) {
        if (err) *err = QStringLiteral("zip 解压体积异常");
        mz_zip_reader_end(&zip);
        return false;
    }
    const auto manifestIt = out.names.constFind(QStringLiteral("manifest.json"));
    if (manifestIt == out.names.constEnd()) {
        if (err) *err = QStringLiteral("zip 缺少 manifest.json");
        mz_zip_reader_end(&zip);
        return false;
    }
    if (!readBytesFromZip(zip, manifestIt.value(), out.manifestJson, 8 * 1024 * 1024, err) ||
        !parseManifest(out.manifestJson, out, err)) {
        mz_zip_reader_end(&zip);
        return false;
    }

    // 每个 manifest pack 必须在 zip 中存在，且 zip 不能有未声明的 .slmz。
    QSet<QString> declared;
    for (const auto& p : out.packs) declared.insert(p.file);
    for (const auto& e : out.entries) {
        if (isAllowedPackFile(e.name) && !declared.contains(e.name)) {
            if (err) *err = QStringLiteral("zip 包含未声明的缓存包");
            mz_zip_reader_end(&zip);
            return false;
        }
    }
    for (const auto& p : out.packs) {
        if (!out.names.contains(p.file)) {
            if (err) *err = QStringLiteral("manifest 引用的 pack 不在 zip 中");
            mz_zip_reader_end(&zip);
            return false;
        }
    }

    const auto batchIt = out.names.constFind(QStringLiteral("batch.json"));
    if (batchIt != out.names.constEnd()) {
        if (readBytesFromZip(zip, batchIt.value(), out.batchJson, 256 * 1024, err)) {
            const QJsonObject b = QJsonDocument::fromJson(out.batchJson).object();
            out.rangeLabel = b.value(QStringLiteral("rangeLabel")).toString();
        }
    }
    if (out.rangeLabel.isEmpty()) {
        const auto& first = out.packs.front();
        out.rangeLabel = QStringLiteral("%1..%2, %3..%4")
                             .arg(first.rect.minX).arg(first.rect.maxX)
                             .arg(first.rect.minZ).arg(first.rect.maxZ);
    }
    return true;
}

QString sha256File(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return {};
    return QString::fromLatin1(h.result().toHex());
}

} // namespace

bool CacheZipImporter::inspect(const QString& zipPath, ZipInspection& out)
{
    out = ZipInspection{};
    out.zipName = QFileInfo(zipPath).fileName();
    QByteArray data;
    mz_zip_archive zip{};
    ZipContents contents;
    QString err;
    if (!openZip(zipPath, data, zip, contents, &err)) {
        out.error = err;
        return false;
    }
    mz_zip_reader_end(&zip);
    out.ok = true;
    out.rangeLabel = contents.rangeLabel;
    out.packCount = contents.packs.size();
    out.bytes = 0;
    for (const auto& p : contents.packs) out.bytes += static_cast<qint64>(p.bytes);
    for (const auto& p : contents.packs) out.packFiles << p.file;
    return true;
}

bool CacheZipImporter::importZip(const QString& zipPath, const std::filesystem::path& cacheRoot,
                                 QString* error, int* imported, int* skipped,
                                 const ImportProgress& progress)
{
    const auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        if (imported) *imported = 0;
        if (skipped) *skipped = 0;
        return false;
    };
    ZipInspection inspection;
    if (!inspect(zipPath, inspection)) return fail(inspection.error);

    QByteArray data;
    mz_zip_archive zip{};
    ZipContents contents;
    QString err;
    if (!openZip(zipPath, data, zip, contents, &err)) return fail(err);

    QTemporaryDir temp;
    if (!temp.isValid()) {
        mz_zip_reader_end(&zip);
        return fail(QStringLiteral("无法创建导入临时目录"));
    }
    const QDir tempDir(temp.path());

    CacheStore store(cacheRoot);
    std::string loadErr;
    if (!store.load(&loadErr, true)) {
        mz_zip_reader_end(&zip);
        return fail(QStringLiteral("缓存索引加载失败：%1").arg(QString::fromStdString(loadErr)));
    }

    std::map<std::tuple<int, int, int, int>, CachePackEntry> existing;
    for (const auto& e : store.packs()) {
        existing[{ e.rect.minX, e.rect.maxX, e.rect.minZ, e.rect.maxZ }] = e;
    }

    struct Staged {
        ManifestPack pack;
        QString tempPath;
        QString finalPath;
        bool skip = false;
    };
    std::vector<Staged> staged;
    staged.reserve(contents.packs.size());

    int progressIndex = 0;
    const int progressTotal = static_cast<int>(contents.packs.size());
    for (const auto& p : contents.packs) {
        if (progress) progress(progressIndex, progressTotal, p.file);
        ++progressIndex;
        const mz_uint index = contents.names.value(p.file);
        // 安全：manifest 的 bytes 必须与 zip 条目的真实解压大小一致。
        // 否则下面按 p.bytes 构造 QByteArray 时可能越过 mz_zip_reader_extract_to_heap 的缓冲区。
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, index, &st) ||
            static_cast<quint64>(st.m_uncomp_size) != p.bytes) {
            mz_zip_reader_end(&zip);
            return fail(QStringLiteral("缓存包解压大小与 manifest 不一致：%1").arg(p.file));
        }
        void* heap = mz_zip_reader_extract_to_heap(&zip, index, nullptr, 0);
        if (!heap) {
            mz_zip_reader_end(&zip);
            return fail(QStringLiteral("缓存包解压失败：%1").arg(p.file));
        }
        const QByteArray bytes(static_cast<const char*>(heap), static_cast<int>(p.bytes));
        mz_free(heap);
        if (p.bytes > 0 && static_cast<quint64>(bytes.size()) != p.bytes) {
            mz_zip_reader_end(&zip);
            return fail(QStringLiteral("缓存包大小校验失败：%1").arg(p.file));
        }
        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        if (sha.compare(p.sha256, Qt::CaseInsensitive) != 0) {
            mz_zip_reader_end(&zip);
            return fail(QStringLiteral("缓存包 SHA256 校验失败：%1").arg(p.file));
        }

        const QString base = QFileInfo(p.file).fileName();
        if (base.isEmpty() || base.contains(QStringLiteral(".."))) {
            mz_zip_reader_end(&zip);
            return fail(QStringLiteral("缓存包文件名非法"));
        }
        const QString tempPath = tempDir.filePath(QStringLiteral("pack-%1.slmz").arg(staged.size()));
        QFile tf(tempPath);
        if (!tf.open(QIODevice::WriteOnly) || tf.write(bytes) != bytes.size()) {
            mz_zip_reader_end(&zip);
            return fail(QStringLiteral("无法写入导入临时文件"));
        }
        tf.close();

        PackInfo info;
        const std::filesystem::path nativeTemp(tempPath.toStdWString());
        std::string packErr;
        if (!peekPack(nativeTemp, info, &packErr)) {
            mz_zip_reader_end(&zip);
            return fail(QStringLiteral("缓存包格式非法：%1").arg(QString::fromStdString(packErr)));
        }
        if (info.rect.minX != p.rect.minX || info.rect.maxX != p.rect.maxX ||
            info.rect.minZ != p.rect.minZ || info.rect.maxZ != p.rect.maxZ) {
            mz_zip_reader_end(&zip);
            return fail(QStringLiteral("缓存包范围与 manifest 不一致：%1").arg(p.file));
        }

        Staged s;
        s.pack = p;
        s.tempPath = tempPath;
        s.finalPath = QString::fromStdWString((cacheRoot / CacheStore::kDownloadedDir / base.toStdWString()).wstring());
        const auto key = std::make_tuple(p.rect.minX, p.rect.maxX, p.rect.minZ, p.rect.maxZ);
        const auto it = existing.find(key);
        if (it != existing.end()) {
            const QString existingPath =
                QString::fromStdWString((cacheRoot / it->second.file).wstring());
            const QString existingSha = sha256File(existingPath);
            if (!existingSha.isEmpty() && existingSha.compare(sha, Qt::CaseInsensitive) == 0) {
                s.skip = true;
            } else {
                mz_zip_reader_end(&zip);
                return fail(QStringLiteral("同范围缓存已存在且内容不同，拒绝覆盖：%1").arg(it->second.file));
            }
        }
        staged.push_back(std::move(s));
    }
    mz_zip_reader_end(&zip);

    int added = 0, skippedCount = 0;
    for (const auto& s : staged) {
        if (s.skip) {
            ++skippedCount;
            continue;
        }
        QFile::remove(s.finalPath);
        if (!QFile::copy(s.tempPath, s.finalPath)) {
            return fail(QStringLiteral("无法写入下载缓存目录"));
        }
        ++added;
    }
    // 用 CacheStore::load 扫描新文件并按范围去重（不依赖 zip 内 index）。
    std::string reloadErr;
    if (!store.load(&reloadErr, true)) return fail(QStringLiteral("缓存索引重建失败：%1").arg(QString::fromStdString(reloadErr)));
    if (imported) *imported = added;
    if (skipped) *skipped = skippedCount;
    if (error) error->clear();
    return true;
}

} // namespace slime::cache
