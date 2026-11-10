#include "gui/UiStrings.h"
#include "gui/UiText.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace slime {
namespace ui {
namespace {
const char* const kKeyTextKeys[] = {
    "ui.key.ModePattern = 0",
    "ui.key.ModeDensity",
    "ui.key.ModePatternTip",
    "ui.key.ModeDensityTip",
    "ui.key.ShapeSquare",
    "ui.key.ShapeRing",
    "ui.key.ShapeSquareTip",
    "ui.key.ShapeRingTip",
    "ui.key.RingScanFast",
    "ui.key.RingScanExact",
    "ui.key.RingScanTip",
    "ui.key.GridSize",
    "ui.key.GridSizeTip",
    "ui.key.MinDensity",
    "ui.key.MinDensityTip",
    "ui.key.Target",
    "ui.key.TargetTip",
    "ui.key.StopAtTarget",
    "ui.key.StopAtTargetTip",
    "ui.key.SmartRange",
    "ui.key.SmartRangeTip",
    "ui.key.AdvancedParamsTitle",
    "ui.key.RingInner",
    "ui.key.RingInnerTip",
    "ui.key.RingOuter",
    "ui.key.RingOuterTip",
    "ui.key.RingCoarseStep",
    "ui.key.RingCoarseStepTip",
    "ui.key.StatBands",
    "ui.key.StatBandsTip"
};
constexpr int kKeyCount = static_cast<int>(sizeof(kKeyTextKeys) / sizeof(kKeyTextKeys[0]));

QHash<QString, QString>& legacyMap()
{
    static QHash<QString, QString> map;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (QCoreApplication::instance()) {
            QFile f(QCoreApplication::applicationDirPath() +
                    QStringLiteral("/assets/i18n/legacy-zh-en.json"));
            if (f.open(QIODevice::ReadOnly)) {
                const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
                if (d.isObject()) {
                    const QJsonObject o = d.object();
                    for (auto it = o.begin(); it != o.end(); ++it) map.insert(it.key(), it.value().toString());
                }
            }
        }
    }
    return map;
}
} // namespace

Language& currentLanguageRef()
{
    static Language lang = Language::Chinese;
    return lang;
}

void setLanguage(Language lang) { currentLanguageRef() = lang; }
Language language() { return currentLanguageRef(); }
bool isEnglish() { return currentLanguageRef() == Language::English; }

QString text(Key k)
{
    const int idx = static_cast<int>(k);
    if (idx < 0 || idx >= kKeyCount) return QString();
    return t(kKeyTextKeys[idx]);
}

QString tr(const QString& zh)
{
    if (!isEnglish()) return zh;
    const auto it = legacyMap().constFind(zh);
    return it != legacyMap().constEnd() ? it.value() : zh;
}

const QHash<QString, QString>& zhToEn()
{
    return legacyMap();
}

} // namespace ui
} // namespace slime
