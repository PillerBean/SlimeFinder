// PatternEditor.h - 图案编辑器对话框
//
// 操作说明：
//   左键点击：灰色(忽略) → 白色(非史莱姆) → 绿色(史莱姆) → 灰色…循环
//   右键点击：直接设为灰色（忽略）
//   支持调整行列（1~64），全部重置为忽略；确定后通过 pattern() 取回图案。
#pragma once

#include <QDialog>
#include <QWidget>

#include "core/SlimeEngine.h"

class QLabel;
class QSpinBox;
class QPushButton;

namespace slime {

// 可点击的图案网格画布
class PatternGridWidget : public QWidget {
    Q_OBJECT
public:
    explicit PatternGridWidget(QWidget* parent = nullptr);

    void setPattern(const Pattern& p);
    const Pattern& pattern() const noexcept { return m_pat; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void cellClicked(int row, int col, Qt::MouseButton button);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;

private:
    QRect cellRect(int row, int col) const;
    Pattern m_pat;
    int m_cell = 36;
};

class PatternEditor : public QDialog {
    Q_OBJECT
public:
    explicit PatternEditor(QWidget* parent = nullptr);
    ~PatternEditor() override = default;

    // 初始图案（如已有）
    void setInitial(const Pattern& p);
    // 确定后返回编辑结果（未确定则返回初始/空图案）
    Pattern pattern() const { return m_editPat; }

private slots:
    void applySize();
    void resetAll();
    void onCellClicked(int row, int col, Qt::MouseButton button);

private:
    void rebuildGrid();
    PatternGridWidget* m_grid = nullptr;
    QSpinBox* m_rowsSpin = nullptr;
    QSpinBox* m_colsSpin = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_ok = nullptr;
    Pattern m_editPat;
    bool m_changed = false;
};

} // namespace slime
