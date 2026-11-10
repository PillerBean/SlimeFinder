#include "gui/TelemetryClient.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    slime::gui::TelemetryClient c;
    c.setSettings(true, QString::fromLocal8Bit(argc > 1 ? argv[1] : "http://127.0.0.1:8765/v1/event"));
    c.reportAppStart("v3.10.12.12", "test");

    QJsonObject settings;
    settings.insert("grid_size", 5);
    settings.insert("min_density", 0.3);
    settings.insert("target", 5);
    settings.insert("threads", 12);
    QJsonObject range;
    range.insert("min_x", -1000);
    range.insert("max_x", 1000);
    range.insert("min_z", -1000);
    range.insert("max_z", 1000);
    QJsonObject results;
    results.insert("count", 3);
    results.insert("total_found", 10);
    results.insert("best_value", 35.5);
    results.insert("best_x", 12);
    results.insert("best_z", -34);
    QJsonArray top;
    QJsonObject r1; r1.insert("x", 12); r1.insert("z", -34); r1.insert("value", 35.5); top.append(r1);
    results.insert("top", top);
    QJsonObject speed;
    speed.insert("total_ms", 123.4);
    speed.insert("scanned_chunks", 1000000.0);
    speed.insert("chunks_per_sec", 8100000.0);

    QJsonObject search;
    search.insert("seed", "123456789");
    search.insert("edition", "java");
    search.insert("mode", "density");
    search.insert("algorithm", "exact");
    search.insert("range", range);
    search.insert("range_log10", 6);
    search.insert("settings", settings);
    search.insert("results", results);
    search.insert("speed", speed);
    search.insert("cancelled", false);
    search.insert("stopped_early", false);
    search.insert("gpu_used", true);

    c.reportSearchDetailed("v3.10.12.12", "test", "gfx1031 test", search);
    QTimer::singleShot(2500, &app, &QCoreApplication::quit);
    return app.exec();
}
