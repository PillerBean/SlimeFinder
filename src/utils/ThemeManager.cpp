// ThemeManager.cpp - 夜间模式三档实现（v3.3.6 任务书 4.7）
//
// v3.4.6：两套 QSS 由统一 token 生成器整段重写（模板见本文件头部注释末的规则说明）：
//   · 全局 QWidget 只给 color / font —— **不给背景**（否则每个控件都刷底色、层次与边框观感消失）
//   · 容器类（QMainWindow/QDialog/QScrollArea/QStackedWidget/#navColumn/#progressRow/QGroupBox…）给窗口底
//   · 输入 / 文本 / 列表 / 表格 / 树 逐类给背景，浅深两套都覆盖，不会漏
//   · 纯文字类（QLabel/勾选/单选/工具按钮）显式透明 —— 避免「白底盖字」
//   · 草绿 #4CAF50 只用于悬停 / 按下 / 选中；进度条用灰填充
//   · 禁用态统一灰；分组框预留 20px 标题带（margin-top）避免标题压内容
//
// 实现要点（与原文件一致）：
//   1. apply() 记档位 → 解析实际生效深浅 → 设置 QApplication 样式表；仅在真的变化时发信号；
//   2. 「跟随系统」在 Windows 读注册表 AppsUseLightTheme，并监听 Qt 6.5+ colorSchemeChanged；
//   3. 不调用 setColorScheme()（只读系统设置）；
//   4. modeFromString / modeToString / lightQss / darkQss / contrastOk 均为确定性纯函数，自检可直接断言。
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <cmath>

namespace slime {

// v3.4.11：QSS 实际应用次数（跨函数共享）
std::atomic<int> s_applyCount{ 0 };

namespace {

// sRGB 单通道 -> 线性值（WCAG 2.1 相对亮度定义；0.03928 为规范给出的分段阈值）
double srgbToLinear(double c)
{
    return (c <= 0.03928) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

// 单个十六进制字符 -> 0..15；非法字符返回 -1
int hexValue(QChar c)
{
    const char16_t u = c.unicode();
    if (u >= u'0' && u <= u'9') return int(u - u'0');
    if (u >= u'a' && u <= u'f') return int(u - u'a') + 10;
    if (u >= u'A' && u <= u'F') return int(u - u'A') + 10;
    return -1;
}

// "#RRGGBB" / "#RGB" -> 相对亮度（0..1）；解析失败返回 false
bool relativeLuminance(const QString& hex, double& out)
{
    const QString s = hex.trimmed();
    if (s.size() != 4 && s.size() != 7) return false; // 只接受 #RGB / #RRGGBB
    if (s.at(0) != QLatin1Char('#')) return false;
    int v[6] = { 0, 0, 0, 0, 0, 0 };
    if (s.size() == 4) {
        for (int i = 0; i < 3; ++i) { // #abc -> #aabbcc
            const int d = hexValue(s.at(i + 1));
            if (d < 0) return false;
            v[i * 2] = d;
            v[i * 2 + 1] = d;
        }
    } else {
        for (int i = 0; i < 6; ++i) {
            const int d = hexValue(s.at(i + 1));
            if (d < 0) return false;
            v[i] = d;
        }
    }
    const double r = srgbToLinear((v[0] * 16 + v[1]) / 255.0);
    const double g = srgbToLinear((v[2] * 16 + v[3]) / 255.0);
    const double b = srgbToLinear((v[4] * 16 + v[5]) / 255.0);
    out = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return true;
}

} // namespace

// ============================================================================
// 单例与档位
// ============================================================================
ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr; // C++11 起局部静态初始化线程安全
    return mgr;
}

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {}

// 字符串 -> 档位："light" / "dark" / "system"，大小写不敏感、允许首尾空白；
// "system" 与任何未知值（含空串）都按「跟随系统」处理 —— 与 config.json 缺字段时的默认档一致。
ThemeMode ThemeManager::modeFromString(const QString& s)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("light")) return ThemeMode::Light;
    if (v == QLatin1String("dark")) return ThemeMode::Dark;
    return ThemeMode::System;
}

QString ThemeManager::modeToString(ThemeMode m)
{
    switch (m) {
    case ThemeMode::Light: return QStringLiteral("light");
    case ThemeMode::Dark: return QStringLiteral("dark");
    case ThemeMode::System: break;
    }
    return QStringLiteral("system");
}

QString ThemeManager::modeDisplayName(ThemeMode m)
{
    switch (m) {
    case ThemeMode::Light: return QStringLiteral("浅色");
    case ThemeMode::Dark: return QStringLiteral("深色");
    case ThemeMode::System: break;
    }
    return QStringLiteral("跟随系统");
}

ThemeMode ThemeManager::mode() const
{
    return m_mode;
}

bool ThemeManager::isDark() const
{
    return m_dark;
}

// ============================================================================
// 系统主题检测（Windows 注册表）+ 运行时跟随
// ============================================================================
// HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme
// REG_DWORD：0 = 应用使用深色主题，1 = 应用使用浅色主题（Win10 1809 起存在该键）。
// 读不到（旧系统 / 策略删除 / 非 Windows）一律返回 false（即浅色），保证行为确定。
bool ThemeManager::systemPrefersDark()
{
#ifdef Q_OS_WIN
    QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    const QVariant v = personalize.value(QStringLiteral("AppsUseLightTheme"));
    if (!v.isValid()) return false;
    bool ok = false;
    const int appsUseLight = v.toInt(&ok);
    if (!ok) return false;
    return appsUseLight == 0;
#else
    return false;
#endif
}

void ThemeManager::watchSystemTheme()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (m_watching) return; // 只连接一次
    QStyleHints* hints = QGuiApplication::styleHints();
    if (!hints) return; // 尚无 QGuiApplication（纯命令行自检）时跳过
    connect(hints, &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { onSystemColorSchemeChanged(); });
    m_watching = true;
#else
    // Qt < 6.5 没有 colorSchemeChanged 信号：System 档位只在 apply() 时解析一次
#endif
}

void ThemeManager::onSystemColorSchemeChanged()
{
    if (m_mode != ThemeMode::System) return; // 手动档位（浅色 / 深色）不跟随系统
    // QApplication 是进程内唯一实例，直接取全局单例；为空时只更新状态，不会崩溃
    applyEffective(qobject_cast<QApplication*>(QCoreApplication::instance()), systemPrefersDark());
}

// ============================================================================
// 应用主题
// ============================================================================
void ThemeManager::apply(QApplication* app, ThemeMode mode)
{
    m_mode = mode;
    watchSystemTheme(); // 与档位无关：建立监听后才能跟随系统变化
    const bool dark =
        (mode == ThemeMode::System) ? systemPrefersDark() : (mode == ThemeMode::Dark);
    applyEffective(app, dark);
}

// 落地「实际生效的深浅」：设置样式表，并只在真的变化时发信号
void ThemeManager::applyEffective(QApplication* app, bool dark)
{
    if (app) {
        app->setStyleSheet(dark ? darkQss() : lightQss()); // 立即生效，无需重启
        // v3.4.11：统计「真正重放样式表」的次数（自检据此断言：非主题变更不重放 QSS）
        s_applyCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (dark == m_dark) return; // 未变化：不发信号（避免无谓刷新）
    m_dark = dark;
    emit effectiveThemeChanged(m_dark);
}

// ============================================================================
// v3.4.11：QSS 应用次数（只在真正落地样式表时 +1）
int ThemeManager::applyCount()
{
    return s_applyCount.load(std::memory_order_relaxed);
}

// 对比度自检
// ============================================================================
bool ThemeManager::contrastOk(const QString& backgroundHex, const QString& foregroundHex,
                              double minRatio)
{
    double lb = 0.0;
    double lf = 0.0;
    if (!relativeLuminance(backgroundHex, lb)) return false;
    if (!relativeLuminance(foregroundHex, lf)) return false;
    const double hi = std::max(lb, lf);
    const double lo = std::min(lb, lf);
    return (hi + 0.05) / (lo + 0.05) >= minRatio;
}


// ---------------------------------------------------------------- v3.4.6 浅色主题
QString ThemeManager::lightQss()
{
    static const QString qss = QString::fromUtf8(R"QSS339(
/* ===== v3.4.6 设计令牌（基准：ui_prototype/SlimeFinder-UI.html 样式规范） =====
   bg #ffffff / panel #f6f6f6 / panel2 #f2f2f2 / line #c9c9c9 / line2 #e4e4e4
   fg #111111 / fg2 #555555 / fg3 #8a8a8a / dis #b0b0b0 / fill #8f8f8f / sel #dcdcdc / zebra #f8f8f8
   草绿 #4CAF50 只用于悬停·按下·选中；按下 #3D8B40；浅绿底 #E8F5E9
   规则：全直角、无阴影/渐变、系统字体 12px；**全局不给背景**，背景只给容器与输入/文本/列表类控件 */
* { border-radius: 0px; }
QWidget {
    color: #111111;
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 12px;
}
/* 纯文字/勾选类：一律透明（避免「白底盖字」） */
QLabel, QCheckBox, QRadioButton, QToolButton, QGroupBox::title, QStatusBar QLabel {
    background: transparent;
}
/* 容器类：给窗口底色 */
QMainWindow, QDialog, QScrollArea, QAbstractScrollArea, QStackedWidget, QTabWidget::pane,
#navColumn, #navIcons, #progressRow, #tabBar, #dockHeader, #buildExtra {
    background-color: #ffffff;
}
QScrollArea > QWidget > QWidget { background-color: #ffffff; }
/* 卡片类 */
QGroupBox {
    background-color: #ffffff;
    border: 1px solid #c9c9c9;
    margin-top: 20px;
    padding: 4px 6px 6px 6px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 0px;
    height: 18px;
    padding: 0px 8px;
    background-color: #f2f2f2;
    border: 1px solid #c9c9c9;
    color: #111111;
    font-weight: bold;
}
/* 菜单栏 / 工具栏 / 状态栏 */
QMenuBar { background-color: #f6f6f6; border-bottom: 1px solid #c9c9c9; }
QMenuBar::item { background: transparent; padding: 4px 10px; }
QMenuBar::item:selected { background-color: #4CAF50; color: #ffffff; }
QMenu { background-color: #ffffff; border: 1px solid #c9c9c9; }
QMenu::item { padding: 4px 22px; background: transparent; }
QMenu::item:selected { background-color: #4CAF50; color: #ffffff; }
QMenu::separator { height: 1px; background-color: #e4e4e4; margin: 3px 6px; }
QToolBar { background-color: #f6f6f6; border: 0px; border-bottom: 1px solid #c9c9c9; spacing: 6px; padding: 3px 8px; }
QToolBar::separator { background-color: #c9c9c9; width: 1px; margin: 4px; }
QStatusBar { background-color: #f6f6f6; border-top: 1px solid #c9c9c9; color: #555555; font-size: 11px; }
QStatusBar::item { border: 0px; }
/* 按钮 */
QPushButton, QToolButton {
    background-color: #ffffff;
    color: #111111;
    border: 1px solid #c9c9c9;
    /* v3.6.7（用户要求）：按钮全部回到直角 */
    border-radius: 0px;
    min-height: 22px;
    max-height: 24px;
    padding: 0px 10px;
}
QPushButton:hover:!disabled, QToolButton:hover:!disabled { background-color: #4CAF50; border-color: #4CAF50; color: #ffffff; }
QPushButton:pressed:!disabled, QToolButton:pressed:!disabled { background-color: #3D8B40; border-color: #3D8B40; color: #ffffff; }
QPushButton:disabled, QToolButton:disabled { color: #b0b0b0; border-color: #e4e4e4; background-color: #f2f2f2; }
QPushButton:checked, QToolButton:checked { background-color: #4CAF50; border-color: #4CAF50; color: #ffffff; font-weight: bold; }
/* 左导航项：96 宽、上图案下文字、居中、选中整块草绿 */
QToolButton[navBtn="true"] {
    min-height: 52px; max-height: 56px;
    border: 0px; border-left: 2px solid transparent;
    background-color: #f6f6f6; color: #555555; font-size: 10px;
    padding: 0px; text-align: center;
}
QToolButton[navBtn="true"]:hover { background-color: #E8F5E9; color: #111111; border-left: 2px solid #4CAF50; }
QToolButton[navBtn="true"]:checked { background-color: #4CAF50; color: #ffffff; border-left: 2px solid #3D8B40; font-weight: bold; }
/* 兼容自检与旧皮肤：QPushButton 形态的导航项与 QToolButton 同规则 */
QPushButton[navBtn="true"] { min-height: 52px; max-height: 56px; border: 0px; border-left: 2px solid transparent; background-color: #f6f6f6; color: #555555; font-size: 10px; padding: 0px; text-align: center; }
QPushButton[navBtn="true"]:hover { background-color: #E8F5E9; color: #111111; border-left: 2px solid #4CAF50; }
QPushButton[navBtn="true"]:checked { background-color: #4CAF50; color: #ffffff; border-left: 2px solid #3D8B40; font-weight: bold; }
QPushButton[dockIcon="true"] { min-height: 28px; max-height: 30px; border: 0px; border-left: 2px solid transparent; background-color: #f6f6f6; padding: 0px; }
QPushButton[dockIcon="true"]:hover { background-color: #E8F5E9; color: #111111; border-left: 2px solid #4CAF50; }
QPushButton[dockIcon="true"]:checked { background-color: #4CAF50; border-left: 2px solid #3D8B40; }
QPushButton[mini="true"] { min-height: 16px; max-height: 18px; padding: 0px 5px; font-size: 11px; }
QPushButton[tabBtn="true"] { min-height: 24px; max-height: 26px; border: 0px; border-right: 1px solid #c9c9c9; border-bottom: 2px solid transparent; background-color: #f2f2f2; color: #555555; padding: 0px 12px; }
#primaryBtn { font-weight: bold; min-width: 96px; }
QPushButton#cacheBuildStart { background-color: #2E7D32; color: #ffffff; border: 1px solid #2E7D32; font-weight: bold; min-height: 30px; max-height: 34px; }
QPushButton#cacheBuildStart:hover:!disabled { background-color: #3D8B40; border-color: #3D8B40; }
QPushButton#cacheBuildStart:disabled { background-color: #161616; color: #5e5e5e; border-color: #2e2e2e; }
QPushButton#cacheBuildStart { background-color: #2E7D32; color: #ffffff; border: 1px solid #2E7D32; font-weight: bold; min-height: 30px; max-height: 34px; }
QPushButton#cacheBuildStart:hover:!disabled { background-color: #3D8B40; border-color: #3D8B40; }
QPushButton#cacheBuildStart:disabled { background-color: #e4e4e4; color: #b0b0b0; border-color: #e4e4e4; }
/* 输入类 */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QAbstractSpinBox {
    background-color: #ffffff; color: #111111; border: 1px solid #c9c9c9;
    min-height: 22px; max-height: 24px; padding: 0px 4px;
    selection-background-color: #4CAF50; selection-color: #ffffff;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #4CAF50; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled,
QAbstractSpinBox:disabled { color: #b0b0b0; border-color: #e4e4e4; background-color: #f2f2f2; }
/* 下拉框：去掉原生灰底方块，自绘 7x4 实心三角（样式规范：直角、无原生外观） */
QComboBox { padding-right: 18px; }
QComboBox::drop-down { border: none; width: 0px; }
QComboBox::down-arrow { image: none; width: 0px; height: 0px; }
QComboBox:disabled::down-arrow { image: none; }
/* 数字输入框：样式规范里没有 spinner ⇒ 彻底移除上下按钮与箭头（否则原生箭头会画成横杠） */
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button,
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 0px; height: 0px; border: 0px; background: transparent; }
QSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow,
QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow { width: 0px; height: 0px; image: none; }
QComboBox QAbstractItemView { background-color: #ffffff; border: 1px solid #c9c9c9; selection-background-color: #4CAF50; selection-color: #ffffff; }
/* 勾选 / 单选：直角方形；选中 = 1px 边框 + 内缩实心块（样式规范「复选 · 直角方形」） */
QRadioButton, QCheckBox { background: transparent; spacing: 6px; }
QRadioButton::indicator, QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #c9c9c9; background-color: #ffffff; }
QRadioButton::indicator:hover, QCheckBox::indicator:hover { border-color: #4CAF50; }
QRadioButton::indicator:checked, QCheckBox::indicator:checked {
    width: 16px; height: 16px; border: 0px; background: transparent;
    image: url(:/icons/check_on_light.png);
}
QRadioButton::indicator:disabled, QCheckBox::indicator:disabled { border-color: #dcdcdc; background-color: #f4f4f4; }
/* v3.4.8（用户：构建模式下无关项要明显标灰）：禁用文字更浅、卡片标题带也变浅 */
QGroupBox:disabled { border-color: #e4e4e4; }
QGroupBox:disabled::title { color: #b0b0b0; background-color: #f8f8f8; border-color: #e4e4e4; }
QRadioButton::indicator:checked:disabled, QCheckBox::indicator:checked:disabled {
    width: 16px; height: 16px; border: 0px; background: transparent;
    image: url(:/icons/check_on_light_disabled.png);
}
QRadioButton:disabled, QCheckBox:disabled, QLabel:disabled, QGroupBox:disabled, QWidget:disabled { color: #b0b0b0; }
/* 进度条：直角、草绿填充（v3.4.8 用户要求：进度条改绿色，与选中色统一） */
QProgressBar { background-color: #eceff1; border: 1px solid #c2c7cc; border-radius: 0px; min-height: 14px; max-height: 20px; color: #37474f; text-align: center; font-size: 11px; }
QProgressBar::chunk { border-radius: 0px; background-color: #37a24a; }
QProgressBar:disabled::chunk { background-color: #b0b0b0; }
/* 文本 / 列表 / 表格 / 树 */
QPlainTextEdit, QTextEdit, QListWidget, QTreeWidget, QTableWidget, QListView, QTreeView, QTableView {
    background-color: #ffffff; color: #111111; border: 1px solid #c9c9c9; outline: 0px;
}
QListWidget, QTreeWidget, QTableWidget { alternate-background-color: #f8f8f8; }
QListWidget::item, QTreeWidget::item, QTableWidget::item { min-height: 20px; border-bottom: 1px solid #e4e4e4; }
QListWidget::item:hover, QTreeWidget::item:hover, QTableWidget::item:hover { background-color: #E8F5E9; border-left: 2px solid #4CAF50; }
QListWidget::item:selected, QTreeWidget::item:selected, QTableWidget::item:selected { background-color: #dcdcdc; color: #111111; }
QHeaderView::section { background-color: #f2f2f2; color: #111111; border: 0px; border-right: 1px solid #c9c9c9; border-bottom: 1px solid #c9c9c9; padding: 2px 6px; font-weight: bold; }
QPlainTextEdit[mono="true"], QTextEdit[mono="true"] { font-family: Consolas, "Courier New", monospace; }
/* 标签页（底部停靠面板） */
QTabBar::tab { background-color: #f2f2f2; color: #555555; border: 0px; border-right: 1px solid #c9c9c9; border-bottom: 2px solid transparent; padding: 3px 10px; min-height: 18px; }
QTabBar::tab:hover { background-color: #E8F5E9; color: #111111; }
QTabBar::tab:selected { background-color: #ffffff; color: #111111; font-weight: bold; border-bottom: 2px solid #4CAF50; }
/* 分割条（同时是停靠面板抓手）：对比度足够 */
QSplitter::handle { background-color: #c9c9c9; border: 0px; }
QSplitter::handle:hover { background-color: #4CAF50; }
QSplitter::handle:horizontal { width: 5px; }
QSplitter::handle:vertical { height: 6px; }
/* 滚动条 */
QScrollBar:vertical { background-color: #f2f2f2; width: 12px; border-left: 1px solid #e4e4e4; }
QScrollBar::handle:vertical { background-color: #c9c9c9; min-height: 24px; }
QScrollBar::handle:vertical:hover { background-color: #4CAF50; }
QScrollBar:horizontal { background-color: #f2f2f2; height: 12px; border-top: 1px solid #e4e4e4; }
QScrollBar::handle:horizontal { background-color: #c9c9c9; min-width: 24px; }
QScrollBar::handle:horizontal:hover { background-color: #4CAF50; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
/* 提示 */
QToolTip { background-color: #f6f6f6; color: #111111; border: 1px solid #555555; padding: 3px 6px; }
#updateBadge { color: #8A5A00; background-color: #FFF3CD; border: 1px solid #E0C97F; padding: 2px 6px; }
/* ===== v3.4.10 更新历史（任务书《更新历史重写》）：折叠行 + 类型底色 + 五类标签 ===== */
#histList { background-color: #ffffff; border: 1px solid #c9c9c9; }
#histRow { background-color: #FFF3E0; border-bottom: 1px solid #e4e4e4; }
#histRow[histType="official"] { background-color: #F3F8F3; }
#histRow[histType="beta"] { background-color: #FFF3E0; border-left: 3px solid #F0B27A; }
#histHeader { background: transparent; }
#histHeader:hover { background-color: #E8F5E9; }
#histVersion { font-weight: bold; }
#histVersion[histType="official"] { color: #2E7D32; }
#histVersion[histType="beta"] { color: #B26A00; }
#histDate { color: #8a8a8a; font-size: 11px; }
#histBadge { font-size: 11px; }
#histBadge[histType="official"] { color: #2E7D32; }
#histBadge[histType="beta"] { color: #B26A00; }
#histBody, #histItem { background: transparent; }
#histItemText { color: #555555; }
#histTag { font-weight: bold; }
#histTag[histTag="n"] { color: #2E7D32; }
#histTag[histTag="f"] { color: #B26A00; }
#histTag[histTag="o"] { color: #1565C0; }
#histTag[histTag="a"] { color: #6A1B9A; }
#histTag[histTag="r"] { color: #B71C1C; }
/* v3.4.11：命令终端 Tab 候选框（不抢焦点的小列表；沿用既有令牌） */
#cmdPopup { background-color: #ffffff; border: 1px solid #c9c9c9; outline: 0px; }
#cmdPopup::item { padding: 2px 8px; min-height: 18px; border-bottom: 1px solid #e4e4e4; }
#cmdPopup::item:hover { background-color: #E8F5E9; }
#cmdPopup::item:selected { background-color: #dcdcdc; color: #111111; }
#progressRow { border-bottom: 1px solid #e4e4e4; }
#progressRow QLabel { background: transparent; color: #555555; }
#progressPercent { color: #111111; font-weight: bold; }
#progressStage, #progressStageTail, #colCaption, #resultCaption, #modeHeader, #infoStats, #topSummary { color: #8a8a8a; background: transparent; }
#cacheStatus, #gpuStatus, #threadsHint, #buildHint { background: transparent; }
#resultSum, #stripRow { background-color: #f2f2f2; border: 1px solid #c9c9c9; color: #555555; padding: 2px 6px; }
#tabBar, #dockHeader { border-bottom: 1px solid #c9c9c9; }
#sectionLabel { color: #555555; background: transparent; font-weight: bold; }
)QSS339");
    return qss;
}

// ---------------------------------------------------------------- v3.4.6 深色主题
QString ThemeManager::darkQss()
{
    static const QString qss = QString::fromUtf8(R"QSS339(
/* ===== v3.4.6 设计令牌（基准：ui_prototype/SlimeFinder-UI.html 样式规范） =====
   bg #111111 / panel #1b1b1b / panel2 #161616 / line #4a4a4a / line2 #2e2e2e
   fg #f0f0f0 / fg2 #b4b4b4 / fg3 #8a8a8a / dis #5e5e5e / fill #8a8a8a / sel #333333 / zebra #191919
   草绿 #4CAF50 只用于悬停·按下·选中；按下 #3D8B40；浅绿底 #1E2A1F
   规则：全直角、无阴影/渐变、系统字体 12px；**全局不给背景**，背景只给容器与输入/文本/列表类控件 */
* { border-radius: 0px; }
QWidget {
    color: #f0f0f0;
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 12px;
}
/* 纯文字/勾选类：一律透明（避免「白底盖字」） */
QLabel, QCheckBox, QRadioButton, QToolButton, QGroupBox::title, QStatusBar QLabel {
    background: transparent;
}
/* 容器类：给窗口底色 */
QMainWindow, QDialog, QScrollArea, QAbstractScrollArea, QStackedWidget, QTabWidget::pane,
#navColumn, #navIcons, #progressRow, #tabBar, #dockHeader, #buildExtra {
    background-color: #111111;
}
QScrollArea > QWidget > QWidget { background-color: #111111; }
/* 卡片类 */
QGroupBox {
    background-color: #111111;
    border: 1px solid #4a4a4a;
    margin-top: 20px;
    padding: 4px 6px 6px 6px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 0px;
    height: 18px;
    padding: 0px 8px;
    background-color: #161616;
    border: 1px solid #4a4a4a;
    color: #f0f0f0;
    font-weight: bold;
}
/* 菜单栏 / 工具栏 / 状态栏 */
QMenuBar { background-color: #1b1b1b; border-bottom: 1px solid #4a4a4a; }
QMenuBar::item { background: transparent; padding: 4px 10px; }
QMenuBar::item:selected { background-color: #4CAF50; color: #ffffff; }
QMenu { background-color: #111111; border: 1px solid #4a4a4a; }
QMenu::item { padding: 4px 22px; background: transparent; }
QMenu::item:selected { background-color: #4CAF50; color: #ffffff; }
QMenu::separator { height: 1px; background-color: #2e2e2e; margin: 3px 6px; }
QToolBar { background-color: #1b1b1b; border: 0px; border-bottom: 1px solid #4a4a4a; spacing: 6px; padding: 3px 8px; }
QToolBar::separator { background-color: #4a4a4a; width: 1px; margin: 4px; }
QStatusBar { background-color: #1b1b1b; border-top: 1px solid #4a4a4a; color: #b4b4b4; font-size: 11px; }
QStatusBar::item { border: 0px; }
/* 按钮 */
QPushButton, QToolButton {
    background-color: #111111;
    color: #f0f0f0;
    border: 1px solid #4a4a4a;
    min-height: 22px;
    max-height: 24px;
    padding: 0px 8px;
}
QPushButton:hover:!disabled, QToolButton:hover:!disabled { background-color: #4CAF50; border-color: #4CAF50; color: #ffffff; }
QPushButton:pressed:!disabled, QToolButton:pressed:!disabled { background-color: #3D8B40; border-color: #3D8B40; color: #ffffff; }
QPushButton:disabled, QToolButton:disabled { color: #5e5e5e; border-color: #2e2e2e; background-color: #161616; }
QPushButton:checked, QToolButton:checked { background-color: #4CAF50; border-color: #4CAF50; color: #ffffff; font-weight: bold; }
/* 左导航项：96 宽、上图案下文字、居中、选中整块草绿 */
QToolButton[navBtn="true"] {
    min-height: 52px; max-height: 56px;
    border: 0px; border-left: 2px solid transparent;
    background-color: #1b1b1b; color: #b4b4b4; font-size: 10px;
    padding: 0px; text-align: center;
}
QToolButton[navBtn="true"]:hover { background-color: #1E2A1F; color: #f0f0f0; border-left: 2px solid #4CAF50; }
QToolButton[navBtn="true"]:checked { background-color: #4CAF50; color: #ffffff; border-left: 2px solid #3D8B40; font-weight: bold; }
/* 兼容自检与旧皮肤：QPushButton 形态的导航项与 QToolButton 同规则 */
QPushButton[navBtn="true"] { min-height: 52px; max-height: 56px; border: 0px; border-left: 2px solid transparent; background-color: #1b1b1b; color: #b4b4b4; font-size: 10px; padding: 0px; text-align: center; }
QPushButton[navBtn="true"]:hover { background-color: #1E2A1F; color: #f0f0f0; border-left: 2px solid #4CAF50; }
QPushButton[navBtn="true"]:checked { background-color: #4CAF50; color: #ffffff; border-left: 2px solid #3D8B40; font-weight: bold; }
QPushButton[dockIcon="true"] { min-height: 28px; max-height: 30px; border: 0px; border-left: 2px solid transparent; background-color: #1b1b1b; padding: 0px; }
QPushButton[dockIcon="true"]:hover { background-color: #2E7D32; color: #ffffff; border-left: 2px solid #4CAF50; }
QPushButton[dockIcon="true"]:checked { background-color: #4CAF50; border-left: 2px solid #3D8B40; }
QPushButton[mini="true"] { min-height: 16px; max-height: 18px; padding: 0px 5px; font-size: 11px; }
QPushButton[tabBtn="true"] { min-height: 24px; max-height: 26px; border: 0px; border-right: 1px solid #4a4a4a; border-bottom: 2px solid transparent; background-color: #161616; color: #b4b4b4; padding: 0px 12px; }
#primaryBtn { font-weight: bold; min-width: 96px; }
/* 输入类 */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QAbstractSpinBox {
    background-color: #1a1a1a; color: #f0f0f0; border: 1px solid #4a4a4a;
    min-height: 22px; max-height: 24px; padding: 0px 4px;
    selection-background-color: #4CAF50; selection-color: #ffffff;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #4CAF50; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled,
QAbstractSpinBox:disabled { color: #5e5e5e; border-color: #2e2e2e; background-color: #161616; }
/* 下拉框：去掉原生灰底方块，自绘 7x4 实心三角（样式规范：直角、无原生外观） */
QComboBox { padding-right: 18px; }
QComboBox::drop-down { border: none; width: 0px; }
QComboBox::down-arrow { image: none; width: 0px; height: 0px; }
QComboBox:disabled::down-arrow { image: none; }
/* 数字输入框：样式规范里没有 spinner ⇒ 彻底移除上下按钮与箭头（否则原生箭头会画成横杠） */
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button,
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 0px; height: 0px; border: 0px; background: transparent; }
QSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow,
QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow { width: 0px; height: 0px; image: none; }
QComboBox QAbstractItemView { background-color: #111111; border: 1px solid #4a4a4a; selection-background-color: #4CAF50; selection-color: #ffffff; }
/* 勾选 / 单选：直角方形；选中 = 1px 边框 + 内缩实心块（样式规范「复选 · 直角方形」） */
QRadioButton, QCheckBox { background: transparent; spacing: 6px; }
QRadioButton::indicator, QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #4a4a4a; background-color: #1a1a1a; }
QRadioButton::indicator:hover, QCheckBox::indicator:hover { border-color: #4CAF50; }
QRadioButton::indicator:checked, QCheckBox::indicator:checked {
    width: 16px; height: 16px; border: 0px; background: transparent;
    image: url(:/icons/check_on_dark.png);
}
QRadioButton::indicator:disabled, QCheckBox::indicator:disabled { border-color: #333333; background-color: #191919; }
/* v3.4.8（用户：构建模式下无关项要明显标灰）：禁用文字更暗、卡片标题带也变暗 */
QGroupBox:disabled { border-color: #2e2e2e; }
QGroupBox:disabled::title { color: #5e5e5e; background-color: #191919; border-color: #2e2e2e; }
QRadioButton::indicator:checked:disabled, QCheckBox::indicator:checked:disabled {
    width: 16px; height: 16px; border: 0px; background: transparent;
    image: url(:/icons/check_on_dark_disabled.png);
}
QRadioButton:disabled, QCheckBox:disabled, QLabel:disabled, QGroupBox:disabled, QWidget:disabled { color: #5e5e5e; }
/* 进度条：直角、草绿填充（v3.4.8 用户要求：进度条改绿色，与选中色统一） */
QProgressBar { background-color: #1b2024; border: 1px solid #454b50; border-radius: 0px; min-height: 14px; max-height: 20px; color: #cfd8dc; text-align: center; font-size: 11px; }
QProgressBar::chunk { border-radius: 0px; background-color: #37a24a; }
QProgressBar:disabled::chunk { background-color: #5e5e5e; }
/* 文本 / 列表 / 表格 / 树 */
QPlainTextEdit, QTextEdit, QListWidget, QTreeWidget, QTableWidget, QListView, QTreeView, QTableView {
    background-color: #1a1a1a; color: #f0f0f0; border: 1px solid #4a4a4a; outline: 0px;
}
QListWidget, QTreeWidget, QTableWidget { alternate-background-color: #191919; }
QListWidget::item, QTreeWidget::item, QTableWidget::item { min-height: 20px; border-bottom: 1px solid #2e2e2e; }
QListWidget::item:hover, QTreeWidget::item:hover, QTableWidget::item:hover { background-color: #1E2A1F; border-left: 2px solid #4CAF50; }
QListWidget::item:selected, QTreeWidget::item:selected, QTableWidget::item:selected { background-color: #333333; color: #f0f0f0; }
QHeaderView::section { background-color: #161616; color: #f0f0f0; border: 0px; border-right: 1px solid #4a4a4a; border-bottom: 1px solid #4a4a4a; padding: 2px 6px; font-weight: bold; }
QPlainTextEdit[mono="true"], QTextEdit[mono="true"] { font-family: Consolas, "Courier New", monospace; }
/* 标签页（底部停靠面板） */
QTabBar::tab { background-color: #161616; color: #b4b4b4; border: 0px; border-right: 1px solid #4a4a4a; border-bottom: 2px solid transparent; padding: 3px 10px; min-height: 18px; }
QTabBar::tab:hover { background-color: #1E2A1F; color: #f0f0f0; }
QTabBar::tab:selected { background-color: #111111; color: #f0f0f0; font-weight: bold; border-bottom: 2px solid #4CAF50; }
/* 分割条（同时是停靠面板抓手）：对比度足够 */
QSplitter::handle { background-color: #4a4a4a; border: 0px; }
QSplitter::handle:hover { background-color: #4CAF50; }
QSplitter::handle:horizontal { width: 5px; }
QSplitter::handle:vertical { height: 6px; }
/* 滚动条 */
QScrollBar:vertical { background-color: #161616; width: 12px; border-left: 1px solid #2e2e2e; }
QScrollBar::handle:vertical { background-color: #4a4a4a; min-height: 24px; }
QScrollBar::handle:vertical:hover { background-color: #4CAF50; }
QScrollBar:horizontal { background-color: #161616; height: 12px; border-top: 1px solid #2e2e2e; }
QScrollBar::handle:horizontal { background-color: #4a4a4a; min-width: 24px; }
QScrollBar::handle:horizontal:hover { background-color: #4CAF50; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
/* 提示 */
QToolTip { background-color: #1b1b1b; color: #f0f0f0; border: 1px solid #b4b4b4; padding: 3px 6px; }
#updateBadge { color: #E0C97F; background-color: #3A2F12; border: 1px solid #6A5A2B; padding: 2px 6px; }

/* ===== v3.4.10 更新历史（任务书《更新历史重写》）：正式版暗绿底 / 测试版暗橙底 + 暗橙左线 ===== */
#histList { background-color: #111111; border: 1px solid #4a4a4a; }
#histRow { background-color: #2A1F12; border-bottom: 1px solid #2e2e2e; }
#histRow[histType="official"] { background-color: #182018; }
#histRow[histType="beta"] { background-color: #2A1F12; border-left: 3px solid #8A5A2B; }
#histHeader { background: transparent; }
#histHeader:hover { background-color: #1E2A1F; }
#histVersion { font-weight: bold; }
#histVersion[histType="official"] { color: #4CAF50; }
#histVersion[histType="beta"] { color: #E0A45C; }
#histDate { color: #8a8a8a; font-size: 11px; }
#histBadge { font-size: 11px; }
#histBadge[histType="official"] { color: #4CAF50; }
#histBadge[histType="beta"] { color: #E0A45C; }
#histBody, #histItem { background: transparent; }
#histItemText { color: #b4b4b4; }
#histTag { font-weight: bold; }
#histTag[histTag="n"] { color: #6FCF7A; }
#histTag[histTag="f"] { color: #E0A45C; }
#histTag[histTag="o"] { color: #6BA6F0; }
#histTag[histTag="a"] { color: #C79BE0; }
#histTag[histTag="r"] { color: #EF8A8A; }
/* v3.4.11：命令终端 Tab 候选框（深色） */
#cmdPopup { background-color: #1b1b1b; border: 1px solid #4a4a4a; outline: 0px; }
#cmdPopup::item { padding: 2px 8px; min-height: 18px; border-bottom: 1px solid #2e2e2e; }
#cmdPopup::item:hover { background-color: #1E2A1F; }
#cmdPopup::item:selected { background-color: #333333; color: #f0f0f0; }
#progressRow { border-bottom: 1px solid #2e2e2e; }
#progressRow QLabel { background: transparent; color: #b4b4b4; }
#progressPercent { color: #f0f0f0; font-weight: bold; }
#progressStage, #progressStageTail, #colCaption, #resultCaption, #modeHeader, #infoStats, #topSummary { color: #8a8a8a; background: transparent; }
#cacheStatus, #gpuStatus, #threadsHint, #buildHint { background: transparent; }
#resultSum, #stripRow { background-color: #161616; border: 1px solid #4a4a4a; color: #b4b4b4; padding: 2px 6px; }
#tabBar, #dockHeader { border-bottom: 1px solid #4a4a4a; }
#sectionLabel { color: #b4b4b4; background: transparent; font-weight: bold; }
)QSS339");
    return qss;
}

} // namespace slime
