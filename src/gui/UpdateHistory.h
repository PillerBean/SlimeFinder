// UpdateHistory.h - v3.4.10（任务书《更新历史重写》）：更新历史结构化数据源 + 文本渲染。
//
// 数据规则见 UpdateHistory.cpp 头部注释；文本源为 docs/update_history.md（同源生成）。
#pragma once

#include <QString>
#include <vector>

namespace slime {

// 条目分类（界面按此着色；顺序即展示顺序：新增 → 修复 → 优化 → 调整 → 移除）
enum class HistoryTag { Added = 0, Fixed = 1, Improved = 2, Adjusted = 3, Removed = 4 };

struct HistoryItem {
    HistoryTag tag = HistoryTag::Added;
    QString text;
};

struct HistoryEntry {
    QString version;   // "3.4.10"
    QString date;      // "2026-09-24"（无留档的含「（推测）」）
    bool official = false;
    std::vector<HistoryItem> items; // 已按分类顺序排好
};

// 全部条目：严格按版本号从新到旧
const std::vector<HistoryEntry>& updateHistoryEntries();

// 完整更新历史文本（UTF-8，多行；每行以 \n 结尾）—— 帮助对话框与自检使用
const char* updateHistoryTextUtf8();

// 版本条目数（1.0.0 → 3.4.10；供自检断言完整性）
int updateHistoryEntryCount();

// 分类名（"新增" / "修复" / …）
const char* updateHistoryTagName(HistoryTag tag);

} // namespace slime
