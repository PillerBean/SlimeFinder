// CacheDownloadDialog.h - v3.12.3：下载缓存管理页（本地 v3 pack 管理占位）
//
// 当前尚未接入官方缓存目录/下载器；本对话框先完成“下载缓存”的页面结构：
//   * 可下载页：等待联网目录接入；
//   * 已下载页：读取 cache/downloaded 的只读 pack，可批量删除；
//   * 快捷范围 + 搜索 + 覆盖地图 + 批量选择。
#pragma once

#include "cache/CacheStore.h"

#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class CacheCoverageMap;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class CacheDownloadDialog : public QDialog {
public:
    explicit CacheDownloadDialog(slime::cache::CacheStore* store, QWidget* parent = nullptr);

    // 删除/刷新后通知主窗口刷新缓存页
    std::function<void()> onChanged;

private:
    void refresh();
    void updateSelectionUi();
    bool matchesRange(const slime::cache::CachePackEntry& e) const;
    bool matchesSearch(const slime::cache::CachePackEntry& e) const;

    slime::cache::CacheStore* m_store = nullptr;
    CacheCoverageMap* m_map = nullptr;
    QTableWidget* m_table = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_deleteSelected = nullptr;
    int m_rangeLimit = 0;
    std::vector<slime::cache::CachePackEntry> m_visible;
};
