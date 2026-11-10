// TelemetryClient.h - 匿名统计客户端（默认关闭；用户主动开启后才发送）
//
// 隐私边界：
//   * 只发送版本 / 平台 / 硬件档位 / 搜索模式 / 范围档位 / 耗时 / 结果数档位；
//   * 绝不发送世界种子、坐标、存档路径、日志内容、账号或持久 ID；
//   * 服务端不保存 IP；详见 docs/privacy.md。
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace slime::gui {

class TelemetryClient : public QObject {
public:
    explicit TelemetryClient(QObject* parent = nullptr);

    void load();
    bool enabled() const noexcept { return m_enabled; }
    QString endpoint() const { return m_endpoint; }

    // 由“匿名统计”对话框调用；写入 telemetry.json
    void setSettings(bool enabled, const QString& endpoint);
    void save() const;

    // 启动事件（每次运行最多一次）
    void reportAppStart(const QString& version, const QString& channel);

    // 搜索完成/取消/失败事件；只传档位，不传种子/坐标
    // 详细搜索事件：search 已包含 seed / 搜索设置 / 结果 / 速度 / 状态
    void reportSearchDetailed(const QString& version, const QString& channel, const QString& gpuName,
                              const QJsonObject& search);

    // 缓存构建完成事件
    void reportCacheBuild(const QString& version, const QString& channel, int64_t areaChunks,
                          double durationMs, int64_t builtEntries, bool cancelled, bool hitBudget);

private:
    QJsonObject hardware() const;
    QJsonObject base(const QString& version, const QString& channel) const;
    void post(const QJsonObject& event);
    static QString configPath();
    static int bucketLog10(int64_t v);
    static int bucketCount(int64_t v);

    bool m_enabled = false;
    QString m_endpoint;
    bool m_appStartReported = false;
};

} // namespace slime::gui
