// v3.5.18：内嵌图案编辑器实现（与弹窗版同一套画布：左键循环 灰→白→绿，右键=灰）
#include "gui/PatternEditorPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace slime {

PatternEditorPanel::PatternEditorPanel(QWidget* parent) : QWidget(parent)
{
    // v3.5.19：初始就给一个 5x5 空图案 —— 否则画布是 0x0，看起来「没有格子」
    m_editPat = Pattern(5, 5);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    m_grid = new PatternGridWidget(this);
    connect(m_grid, &PatternGridWidget::cellClicked, this, &PatternEditorPanel::onCellClicked);
    root->addWidget(m_grid, 0, Qt::AlignLeft);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(4);
    sizeRow->addWidget(new QLabel(QStringLiteral("行"), this));
    m_rowsSpin = new QSpinBox(this);
    m_rowsSpin->setRange(1, 32);
    m_rowsSpin->setValue(5);
    sizeRow->addWidget(m_rowsSpin);
    sizeRow->addWidget(new QLabel(QStringLiteral("列"), this));
    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setRange(1, 32);
    m_colsSpin->setValue(5);
    sizeRow->addWidget(m_colsSpin);
    auto* btnSize = new QPushButton(QStringLiteral("应用行列"), this);
    btnSize->setProperty("mini", true);
    connect(btnSize, &QPushButton::clicked, this, &PatternEditorPanel::applySize);
    sizeRow->addWidget(btnSize);
    auto* btnReset = new QPushButton(QStringLiteral("全部忽略"), this);
    btnReset->setProperty("mini", true);
    connect(btnReset, &QPushButton::clicked, this, &PatternEditorPanel::resetAll);
    sizeRow->addWidget(btnReset);
    sizeRow->addStretch();
    root->addLayout(sizeRow);

    rebuildGrid();
}

void PatternEditorPanel::setPattern(const Pattern& p)
{
    m_editPat = p;
    if (m_rowsSpin) m_rowsSpin->setValue(p.rows());
    if (m_colsSpin) m_colsSpin->setValue(p.cols());
    rebuildGrid();
}

void PatternEditorPanel::rebuildGrid()
{
    if (!m_grid) return;
    m_grid->setPattern(m_editPat);
    emit patternChanged(m_editPat);
}

void PatternEditorPanel::applySize()
{
    m_editPat = Pattern(m_rowsSpin->value(), m_colsSpin->value());
    rebuildGrid();
}

void PatternEditorPanel::resetAll()
{
    m_editPat = Pattern(m_editPat.rows(), m_editPat.cols());
    rebuildGrid();
}

void PatternEditorPanel::onCellClicked(int row, int col, Qt::MouseButton button)
{
    if (button == Qt::RightButton) {
        m_editPat.set(row, col, CellState::Ignore);
    } else {
        const CellState s = m_editPat.at(row, col);
        m_editPat.set(row, col, s == CellState::Ignore ? CellState::White
                                                     : (s == CellState::White ? CellState::Green : CellState::Ignore));
    }
    rebuildGrid();
}

} // namespace slime
