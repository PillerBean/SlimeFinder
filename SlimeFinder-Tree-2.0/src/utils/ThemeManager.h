// ThemeManager.h - v3.3.6（任务书 4.7）：夜间模式三档（浅色 / 深色 / 跟随系统）
//
// 职责：
//   · 保存当前档位（config.json 中写作 "light" / "dark" / "system"）；
//   · 集中提供浅色 / 深色两套 Qt 样式表（QSS）文本；
//   · 「跟随系统」档位下读取 Windows 注册表的应用主题，并监听 Qt 6.5+ 的
//     QStyleHints::colorSchemeChanged —— 系统切换主题后立即生效，无需重启程序。
//
// 关键约定：
//   1. 单例只在 GUI 线程使用（Qt 样式表只能在主线程设置）；
//   2. 不强制配色：绝不调用 QStyleHints::setColorScheme()，只读取 / 监听系统设置；
//   3. 全部静态查询函数（modeFromString / modeToString / lightQss / darkQss /
//      contrastOk）都是纯函数且完全确定性（不含时间、随机、环境相关内容），
//      自检可直接断言；
//   4. 只依赖 QtCore / QtGui / QtWidgets，不引入任何第三方库与位图资源。
//
// 深色配色（三原色 + 对比度全部经 contrastOk() 校验，见 ThemeManager.cpp 的色表）：
//   窗口底 #1e1e1e / 正文 #e6e6e6（13.36:1）、面板 #2d2d30、输入与列表底 #252526、
//   强调色 #0e639c 配白字 #ffffff（6.40:1）。
#pragma once

#include <QObject>
#include <QString>

class QApplication;

namespace slime {

// v3.3.6（任务书 4.7）：夜间模式三档
enum class ThemeMode { Light = 0, Dark = 1, System = 2 };

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();                       // 单例（GUI 线程使用）

    // 字符串 <-> 枚举（config.json 用 "light"/"dark"/"system"；大小写不敏感；未知值 → System）
    static ThemeMode modeFromString(const QString& s);
    static QString modeToString(ThemeMode m);
    static QString modeDisplayName(ThemeMode m);           // "浅色" / "深色" / "跟随系统"

    // 读取 Windows 注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
    // 的 AppsUseLightTheme（REG_DWORD）：0 → 深色(true)；1 → 浅色(false)；读不到 → false
    static bool systemPrefersDark();

    // 立即生效：设置 QApplication 样式表（并在 System 模式下监听系统主题变化）
    void apply(QApplication* app, ThemeMode mode);

    ThemeMode mode() const;        // 当前模式（默认 System）
    bool isDark() const;           // 当前实际生效是否为深色

    // 供自检使用：两套 QSS 文本（完全确定性，不含时间/随机内容）
    static QString lightQss();
    static QString darkQss();

    // 供自检使用：解析 "#RRGGBB" 并用相对亮度算对比度，ratio >= minRatio 返回 true
    // （解析失败返回 false）
    // v3.4.11：QSS 应用次数（自检据此断言「非主题变更不重放 QSS」）
    static int applyCount();

    static bool contrastOk(const QString& backgroundHex, const QString& foregroundHex,
                           double minRatio = 4.5);

signals:
    void effectiveThemeChanged(bool dark);   // 实际生效主题变化（含 System 跟随切换）

private:
    explicit ThemeManager(QObject* parent = nullptr);

    // 私有实现细节（连接 QStyleHints::colorSchemeChanged 等）
    void watchSystemTheme();                           // 建立系统主题监听（只连接一次）
    void applyEffective(QApplication* app, bool dark); // 落地 QSS；仅在实际变化时发信号
    void onSystemColorSchemeChanged();                 // 系统主题变化回调（仅 System 档位生效）

    ThemeMode m_mode = ThemeMode::System;  // 当前档位（默认跟随系统）
    bool m_dark = false;                   // 当前实际生效是否为深色
    bool m_watching = false;               // 是否已连接系统主题监听（保证只连一次）
};

} // namespace slime
