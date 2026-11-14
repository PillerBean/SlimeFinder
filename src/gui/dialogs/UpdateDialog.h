// UpdateDialog.h — v3.10.12.10：检查更新（GitHub Releases + 自动镜像测速 + 网盘备用）
//
// 设计：
//   * 联网检查/下载使用系统自带的 curl.exe（--ssl-no-revoke 兼容离线证书吊销检查），
//     不额外依赖 Qt Network 模块，也不要求用户安装 OpenSSL；
//   * 来源：
//       - 自动：先试 GitHub 官方，走不通时对内置/自定义镜像**自动测速选最优**（用户无需切换）；
//       - GitHub 官方：只走官方；
//       - 网盘：保留蓝奏云页面作为国内手动备用入口，不做自动更新适配；
//   * 镜像加速通过 raw.githubusercontent.com 的 version.json 清单工作（api.github.com 通常无法被代理）；
//   * 下载 zip 后用 Release 里的 SHA256SUMS 校验（取不到时给出未校验提示）。
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVector>

#include <functional>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QProcess;

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(QWidget* parent = nullptr);
    ~UpdateDialog() override;

    bool browserOpened() const { return m_browserOpened; }

    // 自动化探针入口（仅 tools/update_probe.cpp 使用；正式 UI 不调用）
    void probeStart(int sourceIndex, const QString& mirrorPrefix = QString());
    void probeDownload();
signals:
    void probeFinished(bool updateAvailable, const QString& detail);
    void probeDownloadFinished(bool ok, const QString& detail);

private:
    enum Source { Auto = 0, GitHub = 1, Pan = 2 };
    enum Channel { Stable = 0, Test = 1 };

    struct Asset {
        QString name;
        QString url;
        qint64 size = 0;
    };

    struct MirrorProbe {
        QString prefix;
        QByteArray manifest;
        double manifestSec = 0.0;
        double sampleSec = 0.0;
        qint64 sampleBytes = 0;
    };

    // ---- UI ----
    void buildUi();
    void loadConfig();
    void saveConfig() const;
    void applySourceUi();
    void setBusy(bool busy);
    void setStatusText(const QString& text);

    // ---- 检查更新（官方）----
    QStringList buildOfficialUrls() const;
    void startCheck();
    void tryNextApi();
    void onApiDone(int httpCode, const QByteArray& body);
    void parseLatest(const QByteArray& body);

    // ---- 官方不可达：镜像测速 ----
    QStringList mirrorPrefixes() const;
    void startMirrorSpeedTest();
    void probeNextMirrorManifest();
    void startMirrorSampleTest();
    void probeNextMirrorSample();
    void finishMirrorSelection();

    // ---- 下载 ----
    QStringList buildDownloadUrls(const QString& rawUrl) const;
    QString transformGithubUrl(const QString& raw, const QString& prefix) const;
    void startDownload();
    void tryNextDownload();
    void onZipDone(int httpCode, bool ok);
    void fetchSumsThenFinish();
    void onSumsDone(int httpCode, const QByteArray& body);
    void failDownloadVerification(const QString& reason, const QString& message);
    void finishDownloadOk();
    void openDownloadFolder();
    void startInstallerAndQuit();
    bool isInstalledCopy() const;

    // ---- 网盘备用 ----
    void openPanPage();
    void openGithubPage();

    // ---- curl 异步执行 ----
    void runCurl(const QStringList& args, const QString& outFile, bool readBack,
                 std::function<void(int, QByteArray)> done);
    QString curlExe() const;

    static bool versionNewer(const QString& latest, const QString& current);
    static QString releaseNotesFor(const QString& body);
    static QString manifestZipUrl(const QByteArray& body);

    // ---- UI 成员 ----
    QComboBox* m_source = nullptr;
    QComboBox* m_channel = nullptr;   // 稳定 / 测试通道
    QComboBox* m_mirror = nullptr;   // 保持隐藏：镜像前缀仅供自动测速/高级配置
    QLabel* m_status = nullptr;
    QPlainTextEdit* m_notes = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_checkBtn = nullptr;
    QPushButton* m_downloadBtn = nullptr;
    QPushButton* m_folderBtn = nullptr;
    QPushButton* m_githubBtn = nullptr;
    QPushButton* m_panBtn = nullptr;

    // ---- 状态 ----
    QString m_latestTag;
    QString m_latestVersion;
    QString m_notesText;
    Asset m_zipAsset;
    Asset m_sumsAsset;
    QString m_downloadDir;
    QString m_downloadPath;
    bool m_updateAvailable = false;
    bool m_browserOpened = false;
    bool m_pendingDownload = false;

    // 官方检查
    QStringList m_officialUrls;
    int m_officialIndex = 0;

    // 镜像测速
    QStringList m_mirrorCandidates;
    int m_mirrorProbeIndex = 0;
    QVector<MirrorProbe> m_mirrorProbes;
    int m_sampleIndex = 0;
    QString m_probeZipUrl;
    QStringList m_downloadUrls;
    int m_downloadIndex = 0;
    QString m_activeMirror;      // 本轮选中的镜像前缀（用于下载优先）
    QString m_cachedMirror;      // 上次选中的镜像（下次测速候选之一）
    qint64 m_cachedMirrorAt = 0; // 上次测速时间（Unix 秒）
    bool m_waitingForceCheck = false; // 已展示缓存，下一次点击=强制刷新
    QString m_cacheManifest;     // 缓存的清单原文（6 小时内复用）
    qint64 m_cacheTime = 0;
    int m_cacheChannel = -1;
    QString m_lastCheckStatus;   // 缓存时用于显示

    QProcess* m_proc = nullptr;
    QString m_curlOutFile;
    QString m_curlErrFile;
};
