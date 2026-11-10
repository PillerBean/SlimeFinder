// tools/update_probe.cpp — 检查更新的自动化探针（不随发布包分发）
#include <QApplication>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#include "gui/dialogs/UpdateDialog.h"

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    std::fprintf(stderr, "probe: before QApplication\n");
    std::fflush(stderr);
    QApplication app(argc, argv);
    std::fprintf(stderr, "probe: after QApplication\n");
    std::fflush(stderr);
    UpdateDialog dlg;
    std::fprintf(stderr, "probe: after dialog\n");
    std::fflush(stderr);
    const int src = argc > 1 ? std::atoi(argv[1]) : 0;
    const bool wantDownload = argc > 2 && QByteArray(argv[2]) == "download";
    QObject::connect(&dlg, &UpdateDialog::probeFinished, &app,
                     [&app, &dlg, wantDownload](bool available, const QString& detail) {
                         std::printf("PROBE_OK available=%d %s\n", available ? 1 : 0,
                                     detail.toUtf8().constData());
                         std::fflush(stdout);
                         if (wantDownload && available) {
                             dlg.probeDownload();
                             return;
                         }
                         app.exit(0);
                     });
    QObject::connect(&dlg, &UpdateDialog::probeDownloadFinished, &app,
                     [&app](bool ok, const QString& detail) {
                         std::printf("PROBE_DOWNLOAD ok=%d %s\n", ok ? 1 : 0,
                                     detail.toUtf8().constData());
                         std::fflush(stdout);
                         app.exit(ok ? 0 : 3);
                     });
    dlg.probeStart(src);
    std::fprintf(stderr, "probe: probeStart returned\n");
    std::fflush(stderr);
    QTimer::singleShot(180000, &app, [&app]() {
        std::printf("PROBE_TIMEOUT\n");
        std::fflush(stdout);
        app.exit(2);
    });
    return app.exec();
}
