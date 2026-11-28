#include "gui/services/BedrockRiverFilter.h"

#include <QFileInfo>
#include <QProcess>
#include <QStringList>

namespace slime {

BedrockRiverFilterResult BedrockRiverFilter::filter(std::vector<MatchResult>& results,
                                                    int64_t seed,
                                                    const QString& version,
                                                    const QString& helperPath)
{
    BedrockRiverFilterResult out;
    if (results.empty()) return out;
    if (!QFileInfo::exists(helperPath)) {
        out.error = QStringLiteral("helper missing");
        return out;
    }
    QProcess proc;
    proc.start(helperPath, { QStringLiteral("--batch"), QString::number(seed), version });
    if (!proc.waitForStarted(5000)) {
        out.error = QStringLiteral("helper start failed");
        return out;
    }
    QByteArray input;
    for (const auto& r : results) {
        const int cx = (r.minX + r.maxX) / 2;
        const int cz = (r.minZ + r.maxZ) / 2;
        input += QByteArray::number(cx * 16 + 8) + ' ' + QByteArray::number(cz * 16 + 8) + " 63\n";
    }
    proc.write(input);
    proc.closeWriteChannel();
    if (!proc.waitForFinished(60000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        out.error = QStringLiteral("helper failed");
        return out;
    }
    const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() != static_cast<int>(results.size())) {
        out.error = QStringLiteral("helper output mismatch");
        return out;
    }
    std::vector<MatchResult> kept;
    kept.reserve(results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        const QStringList fields = lines[static_cast<int>(i)].split(QLatin1Char(' '));
        const QString name = fields.isEmpty() ? QString() : fields.back().trimmed();
        if (name == QLatin1String("river") || name == QLatin1String("frozen_river"))
            ++out.removed;
        else
            kept.push_back(results[i]);
    }
    results.swap(kept);
    out.executed = true;
    return out;
}

} // namespace slime

