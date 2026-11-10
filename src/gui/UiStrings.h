#pragma once

// v3.11.0：UI 文案全部来自 assets/i18n/*.json；这里只保留语言枚举和兼容 API。
// 新代码请直接使用 slime::ui::t("ui....")。

#include <QHash>
#include <QString>
#include <QStringList>

namespace slime {
namespace ui {

enum class Language { Chinese = 0, English = 1 };

enum class Key {
    ModePattern = 0,
    ModeDensity,
    ModePatternTip,
    ModeDensityTip,
    ShapeSquare,
    ShapeRing,
    ShapeSquareTip,
    ShapeRingTip,
    RingScanFast,
    RingScanExact,
    RingScanTip,
    GridSize,
    GridSizeTip,
    MinDensity,
    MinDensityTip,
    Target,
    TargetTip,
    StopAtTarget,
    StopAtTargetTip,
    SmartRange,
    SmartRangeTip,
    AdvancedParamsTitle,
    RingInner,
    RingInnerTip,
    RingOuter,
    RingOuterTip,
    RingCoarseStep,
    RingCoarseStepTip,
    StatBands,
    StatBandsTip
};

QString text(Key k);
QString tr(const QString& zh);
const QHash<QString, QString>& zhToEn();

Language& currentLanguageRef();
void setLanguage(Language lang);
Language language();
bool isEnglish();

} // namespace ui
} // namespace slime
