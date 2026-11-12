// CacheZipImportDialog.h - v3.12.3：网盘 zip 批量导入窗口
#pragma once

#include "cache/CacheZipImporter.h"
#include "cache/CacheStore.h"

#include <QDialog>

#include <functional>
#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;

class CacheZipImportDialog : public QDialog {
public:
    explicit CacheZipImportDialog(slime::cache::CacheStore* store, QWidget* parent = nullptr);

    std::function<void()> onImported;

protected:
    void dragEnterEvent(QDragEnterEvent* ev) override;
    void dropEvent(QDropEvent* ev) override;

private:
    struct Item {
        QString path;
        slime::cache::ZipInspection info;
    };

    void addPaths(const QStringList& paths);
    void refresh();
    void startImport();

    slime::cache::CacheStore* m_store = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_start = nullptr;
    std::vector<Item> m_items;
};
