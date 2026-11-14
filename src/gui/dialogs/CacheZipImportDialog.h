// CacheZipImportDialog.h - v3.12.3：网盘 zip 批量导入窗口
#pragma once

#include "cache/CacheZipImporter.h"
#include "cache/CacheStore.h"

#include <QDialog>

#include <functional>
#include <thread>
#include <vector>

class QLabel;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QCloseEvent;

class CacheZipImportDialog : public QDialog {
public:
    explicit CacheZipImportDialog(slime::cache::CacheStore* store, QWidget* parent = nullptr);
    ~CacheZipImportDialog() override;

    std::function<void()> onImported;

protected:
    void dragEnterEvent(QDragEnterEvent* ev) override;
    void dropEvent(QDropEvent* ev) override;
    void reject() override;
    void closeEvent(QCloseEvent* ev) override;

private:
    struct Item {
        QString path;
        slime::cache::ZipInspection info;
        bool inspected = false;
    };

    void addPaths(const QStringList& paths);
    void refresh();
    void startImport();

    slime::cache::CacheStore* m_store = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_start = nullptr;
    QPushButton* m_pick = nullptr;
    QPushButton* m_cancel = nullptr;
    QProgressBar* m_progress = nullptr;
    std::vector<Item> m_items;
    bool m_importing = false;
    bool m_inspecting = false;
    bool m_hasResult = false;
    int m_inspectDone = 0;
    int m_inspectTotal = 0;
    std::thread m_worker;
    std::thread m_inspectWorker;
};
