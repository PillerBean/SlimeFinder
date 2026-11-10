// v3.5.18：把图案编辑器做成可内嵌的面板（不弹窗）
#pragma once

#include <QWidget>

#include "gui/widgets/PatternEditor.h"

class QSpinBox;
class QPushButton;
class QLabel;

namespace slime {

// 内嵌图案编辑器：行/列 + 画布 + 重置；供「搜索模式 → 图案」直接使用
class PatternEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit PatternEditorPanel(QWidget* parent = nullptr);
    void setPattern(const Pattern& p);
    Pattern pattern() const { return m_editPat; }

signals:
    void patternChanged(const Pattern& p);

private slots:
    void applySize();
    void resetAll();
    void onCellClicked(int row, int col, Qt::MouseButton button);

private:
    void rebuildGrid();
    PatternGridWidget* m_grid = nullptr;
    QSpinBox* m_rowsSpin = nullptr;
    QSpinBox* m_colsSpin = nullptr;
    Pattern m_editPat;
};

} // namespace slime
