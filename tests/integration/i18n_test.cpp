#include "i18n/UiText.h"
#include "i18n/UiStrings.h"

#include <QCoreApplication>
#include <QString>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    slime::ui::initTextCatalog();
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
    };

    slime::ui::setLanguage(slime::ui::Language::Chinese);
    check(slime::ui::t("ui.cache.zip.import.title") == QStringLiteral("导入网盘缓存"),
          "zh-CN cache import title");
    check(slime::ui::t("ui.cache.download.clickFetch").isEmpty() == false,
          "zh-CN cache fetch hint");

    slime::ui::setLanguage(slime::ui::Language::English);
    check(slime::ui::t("ui.cache.zip.import.title") == QStringLiteral("Import netdisk cache"),
          "en-US cache import title");
    check(slime::ui::t("ui.cache.zip.column.file") == QStringLiteral("File"),
          "en-US cache import file column");
    check(slime::ui::t("ui.cache.download.clickFetch") == QStringLiteral("Click \"Fetch list\" to load the cache list"),
          "en-US cache fetch hint");
    check(slime::ui::t("ui.update.manualOnly").contains(QStringLiteral("GitHub")),
          "en-US manual update hint");

    slime::ui::setLanguage(slime::ui::Language::Traditional);
    check(slime::ui::t("ui.cache.zip.import.title") == QStringLiteral("匯入網盤快取"),
          "zh-TW cache import title");
    check(slime::ui::t("ui.cache.zip.column.file") == QStringLiteral("檔案"),
          "zh-TW cache import file column");

    if (failures == 0) std::printf("i18n tests OK\n");
    return failures == 0 ? 0 : 1;
}
