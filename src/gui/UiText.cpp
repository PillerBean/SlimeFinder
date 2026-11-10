#include "gui/UiText.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>

#include "gui/UiStrings.h"

namespace slime {
namespace ui {
namespace {

struct Catalog {
    bool loaded = false;
    QHash<QString, QString> zh;
    QHash<QString, QString> en;
};

Catalog& catalog()
{
    static Catalog c;
    return c;
}

QString catalogDir()
{
    if (!QCoreApplication::instance()) return QString();
    return QCoreApplication::applicationDirPath() + QStringLiteral("/assets/i18n");
}

void loadOne(const QString& path, QHash<QString, QString>& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject o = doc.object();
    for (auto it = o.begin(); it != o.end(); ++it) out.insert(it.key(), it.value().toString());
}

void ensureLoaded()
{
    Catalog& c = catalog();
    if (c.loaded) return;
    c.loaded = true;   // 防止递归/重复加载
    const QString dir = catalogDir();
    if (dir.isEmpty()) return;
    loadOne(dir + QStringLiteral("/zh-CN.json"), c.zh);
    loadOne(dir + QStringLiteral("/en-US.json"), c.en);
    if (c.zh.isEmpty()) {
        std::fprintf(stderr, "[i18n] 未找到语言包：%s/zh-CN.json\n", dir.toUtf8().constData());
    }
}

} // namespace

void initTextCatalog()
{
    ensureLoaded();
}

QString t(const char* key)
{
    ensureLoaded();
    if (!key) return QString();
    const QString k = QString::fromUtf8(key);
    const Catalog& c = catalog();
    if (slime::ui::isEnglish()) {
        const auto it = c.en.constFind(k);
        if (it != c.en.constEnd() && !it.value().isEmpty()) return it.value();
    }
    const auto it = c.zh.constFind(k);
    if (it != c.zh.constEnd()) return it.value();
    // 语言包缺失时显示 key，避免显示空白；正式包必须包含全部 key。
    return k;
}

} // namespace ui
} // namespace slime
