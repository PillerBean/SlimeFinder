// PatternEditor.cpp - 图案编辑器实现
#include "gui/PatternEditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace slime {

// ---------------- PatternGridWidget ----------------

PatternGridWidget::PatternGridWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(false);
}

void PatternGridWidget::setPattern(const Pattern& p)
{
    m_pat = p;
    update();
    updateGeometry();
}

QRect PatternGridWidget::cellRect(int row, int col) const
{
    const int m = 1;
    return QRect(col * m_cell + m, row * m_cell + m, m_cell - 2 * m, m_cell - 2 * m);
}

QSize PatternGridWidget::sizeHint() const
{
    const int rows = std::max(1, m_pat.rows());
    const int cols = std::max(1, m_pat.cols());
    return QSize(cols * m_cell + 2, rows * m_cell + 2);
}

QSize PatternGridWidget::minimumSizeHint() const { return sizeHint(); }

void PatternGridWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const int rows = m_pat.rows();
    const int cols = m_pat.cols();
    if (rows <= 0 || cols <= 0) return;

    p.fillRect(rect(), QColor(0xF0, 0xF0, 0xF0));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const QRect rc = cellRect(r, c);
            const CellState s = m_pat.at(r, c);
            QColor fill;
            switch (s) {
            case CellState::Green: fill = QColor(0x4C, 0xC9, 0x4E); break; // 绿：史莱姆
            case CellState::White: fill = Qt::white; break;                 // 白：非史莱姆
            default: fill = QColor(0xB0, 0xB0, 0xB0); break;               // 灰：忽略
            }
            p.fillRect(rc, fill);
            p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
            p.drawRect(rc.adjusted(0, 0, -1, -1));
        }
    }
}

void PatternGridWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton && ev->button() != Qt::RightButton) {
        QWidget::mousePressEvent(ev);
        return;
    }
    const int rows = m_pat.rows();
    const int cols = m_pat.cols();
    const int c = static_cast<int>((ev->position().x()) / m_cell);
    const int r = static_cast<int>((ev->position().y()) / m_cell);
    if (r >= 0 && r < rows && c >= 0 && c < cols) {
        emit cellClicked(r, c, ev->button());
    }
}

// ---------------- PatternEditor ----------------

PatternEditor::PatternEditor(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("绘制图案 — 左键循环：灰(忽略)→白(非史莱姆)→绿(史莱姆)；右键=忽略"));
    setModal(true);

    auto* root = new QVBoxLayout(this);

    auto* hint = new QLabel(QStringLiteral("左键点击循环切换状态；右键点击直接设为忽略（灰色）。绿色=史莱姆区块，白色=非史莱姆区块。"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_grid = new PatternGridWidget(this);
    connect(m_grid, &PatternGridWidget::cellClicked, this, &PatternEditor::onCellClicked);
    root->addWidget(m_grid, 0, Qt::AlignHCenter);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->addWidget(new QLabel(QStringLiteral("行数:"), this));
    m_rowsSpin = new QSpinBox(this);
    m_rowsSpin->setRange(1, 64);
    m_rowsSpin->setValue(5);
    sizeRow->addWidget(m_rowsSpin);
    sizeRow->addSpacing(8);
    sizeRow->addWidget(new QLabel(QStringLiteral("列数:"), this));
    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setRange(1, 64);
    m_colsSpin->setValue(5);
    sizeRow->addWidget(m_colsSpin);
    sizeRow->addSpacing(8);
    auto* btnSize = new QPushButton(QStringLiteral("调整大小"), this);
    connect(btnSize, &QPushButton::clicked, this, &PatternEditor::applySize);
    sizeRow->addWidget(btnSize);
    sizeRow->addStretch();
    root->addLayout(sizeRow);

    auto* actionRow = new QHBoxLayout();
    auto* btnReset = new QPushButton(QStringLiteral("全部重置为忽略"), this);
    connect(btnReset, &QPushButton::clicked, this, &PatternEditor::resetAll);
    actionRow->addWidget(btnReset);
    actionRow->addStretch();
    root->addLayout(actionRow);

    m_status = new QLabel(QStringLiteral("0 个有效格子"), this);
    root->addWidget(m_status);

    auto* btnRow = new QHBoxLayout();
    auto* btnCancel = new QPushButton(QStringLiteral("取消"), this);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    m_ok = new QPushButton(QStringLiteral("确定"), this);
    m_ok->setDefault(true);
    connect(m_ok, &QPushButton::clicked, this, [this]() { accept(); });
    btnRow->addStretch();
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(m_ok);
    root->addLayout(btnRow);

    rebuildGrid();
}

void PatternEditor::setInitial(const Pattern& p)
{
    if (p.empty()) return;
    m_editPat = p;
    m_rowsSpin->setValue(p.rows());
    m_colsSpin->setValue(p.cols());
    rebuildGrid();
    m_changed = false;
}

void PatternEditor::rebuildGrid()
{
    const int rows = m_rowsSpin->value();
    const int cols = m_colsSpin->value();
    Pattern np(rows, cols);
    const int oldRows = m_editPat.rows();
    const int oldCols = m_editPat.cols();
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (r < oldRows && c < oldCols) np.set(r, c, m_editPat.at(r, c));
        }
    }
    m_editPat = np;
    m_grid->setPattern(np);
    m_grid->resize(m_grid->sizeHint());
    const int active = np.activeCount();
    m_status->setText(QStringLiteral("%1 个有效格子（共 %2×%3）").arg(active).arg(rows).arg(cols));
    adjustSize();
}

void PatternEditor::applySize()
{
    // 校验输入（QSpinBox 已保证范围 1..64）
    m_changed = true;
    rebuildGrid();
}

void PatternEditor::resetAll()
{
    m_changed = true;
    const int rows = m_editPat.rows();
    const int cols = m_editPat.cols();
    m_editPat = Pattern(rows, cols);
    m_grid->setPattern(m_editPat);
    m_status->setText(QStringLiteral("0 个有效格子（共 %1×%2）").arg(rows).arg(cols));
    update();
}

void PatternEditor::onCellClicked(int row, int col, Qt::MouseButton button)
{
    m_changed = true;
    Pattern p = m_editPat;
    if (button == Qt::RightButton) {
        p.set(row, col, CellState::Ignore);
    } else {
        CellState cur = p.at(row, col);
        CellState next = CellState::Ignore;
        if (cur == CellState::Ignore) next = CellState::White;
        else if (cur == CellState::White) next = CellState::Green;
        else next = CellState::Ignore;
        p.set(row, col, next);
    }
    m_editPat = p;
    m_grid->setPattern(p);
    m_status->setText(QStringLiteral("%1 个有效格子（共 %2×%3）").arg(p.activeCount()).arg(p.rows()).arg(p.cols()));
}

} // namespace slime
