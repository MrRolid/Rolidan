#include "sizedialogs.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
constexpr int kMaxSide = 16384; // spec G2: largest supported image

QSpinBox *pixelSpin(int value, QWidget *parent)
{
    auto *sb = new QSpinBox(parent);
    sb->setRange(1, kMaxSide);
    sb->setValue(value);
    sb->setSuffix(QStringLiteral(" px"));
    return sb;
}

QDialogButtonBox *okCancel(QDialog *d)
{
    auto *b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, d);
    QObject::connect(b, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(b, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return b;
}
} // namespace

// ================================================================ ResizeDialog

ResizeDialog::ResizeDialog(const QSize &current, QWidget *parent)
    : QDialog(parent)
    , m_current(current)
{
    setWindowTitle(tr("Resize Image"));
    auto *main = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    main->addLayout(form);
    form->addRow(tr("Current size:"), new QLabel(QStringLiteral("%1 × %2 px").arg(current.width()).arg(current.height()), this));
    m_width = pixelSpin(current.width(), this);
    m_height = pixelSpin(current.height(), this);
    m_percent = new QDoubleSpinBox(this);
    m_percent->setRange(0.1, 1000.0);
    m_percent->setDecimals(1);
    m_percent->setValue(100.0);
    m_percent->setSuffix(QStringLiteral(" %"));
    m_keep = new QCheckBox(tr("Keep aspect ratio"), this);
    m_keep->setChecked(true);
    form->addRow(tr("Width:"), m_width);
    form->addRow(tr("Height:"), m_height);
    form->addRow(tr("Scale:"), m_percent);
    form->addRow(QString(), m_keep);
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    form->addRow(m_info);
    connect(m_width, &QSpinBox::valueChanged, this, &ResizeDialog::widthChanged);
    connect(m_height, &QSpinBox::valueChanged, this, &ResizeDialog::heightChanged);
    connect(m_percent, &QDoubleSpinBox::valueChanged, this, &ResizeDialog::percentChanged);
    main->addWidget(okCancel(this));
    updateInfo();
}

QSize ResizeDialog::newSize() const
{
    return {m_width->value(), m_height->value()};
}

void ResizeDialog::widthChanged(int w)
{
    if (m_updating)
        return;
    m_updating = true;
    if (m_keep->isChecked())
        m_height->setValue(std::max(1, int(std::lround(double(w) * m_current.height() / m_current.width()))));
    m_percent->setValue(100.0 * w / m_current.width());
    m_updating = false;
    updateInfo();
}

void ResizeDialog::heightChanged(int h)
{
    if (m_updating)
        return;
    m_updating = true;
    if (m_keep->isChecked()) {
        m_width->setValue(std::max(1, int(std::lround(double(h) * m_current.width() / m_current.height()))));
        m_percent->setValue(100.0 * m_width->value() / m_current.width());
    }
    m_updating = false;
    updateInfo();
}

void ResizeDialog::percentChanged(double p)
{
    if (m_updating)
        return;
    m_updating = true;
    m_width->setValue(std::max(1, int(std::lround(m_current.width() * p / 100.0))));
    m_height->setValue(std::max(1, int(std::lround(m_current.height() * p / 100.0))));
    m_updating = false;
    updateInfo();
}

void ResizeDialog::updateInfo()
{
    const QSize s = newSize();
    const qint64 bytes = qint64(s.width()) * s.height() * 4;
    m_info->setText(tr("New size: %1 × %2 px (%3 in memory). Annotations are scaled too.")
                        .arg(s.width())
                        .arg(s.height())
                        .arg(QLocale().formattedDataSize(bytes)));
}

// ================================================================ CanvasSizeDialog

CanvasSizeDialog::CanvasSizeDialog(const QSize &current, QWidget *parent)
    : QDialog(parent)
    , m_current(current)
{
    setWindowTitle(tr("Canvas Size"));
    auto *main = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    main->addLayout(form);
    form->addRow(tr("Current size:"), new QLabel(QStringLiteral("%1 × %2 px").arg(current.width()).arg(current.height()), this));
    m_width = pixelSpin(current.width(), this);
    m_height = pixelSpin(current.height(), this);
    form->addRow(tr("Width:"), m_width);
    form->addRow(tr("Height:"), m_height);

    // 3×3 anchor: where the current image stays.
    auto *grid = new QGridLayout;
    grid->setSpacing(2);
    m_anchor = new QButtonGroup(this);
    m_anchor->setExclusive(true);
    const QStringList arrows = {QStringLiteral("↖"), QStringLiteral("↑"), QStringLiteral("↗"),
                                QStringLiteral("←"), QStringLiteral("●"), QStringLiteral("→"),
                                QStringLiteral("↙"), QStringLiteral("↓"), QStringLiteral("↘")};
    for (int i = 0; i < 9; ++i) {
        auto *b = new QToolButton(this);
        b->setCheckable(true);
        b->setText(arrows[i]);
        b->setFixedSize(30, 30);
        m_anchor->addButton(b, i);
        grid->addWidget(b, i / 3, i % 3);
    }
    m_anchor->button(4)->setChecked(true);
    form->addRow(tr("Anchor:"), grid);

    m_fill = new QComboBox(this);
    m_fill->addItem(tr("Transparent"), QColor(Qt::transparent));
    m_fill->addItem(tr("White"), QColor(Qt::white));
    m_fill->addItem(tr("Black"), QColor(Qt::black));
    m_fill->addItem(tr("Other color…"));
    connect(m_fill, &QComboBox::activated, this, [this](int i) {
        if (i != 3)
            return;
        const QColor c = QColorDialog::getColor(m_custom, this, tr("Fill color"));
        if (c.isValid())
            m_custom = c;
        m_fill->setItemText(3, tr("Other color…") + QStringLiteral(" ") + m_custom.name());
    });
    form->addRow(tr("Fill new area:"), m_fill);
    form->addRow(new QLabel(tr("A smaller canvas crops the image; annotations move with it."), this));
    main->addWidget(okCancel(this));
}

QSize CanvasSizeDialog::newSize() const
{
    return {m_width->value(), m_height->value()};
}

void CanvasSizeDialog::setAnchor(int index)
{
    if (QAbstractButton *b = m_anchor->button(index))
        b->setChecked(true);
}

QPoint CanvasSizeDialog::offset() const
{
    const int a = std::max(0, m_anchor->checkedId());
    const int col = a % 3, row = a / 3;
    const QSize n = newSize();
    // 0 = keep left/top edge, 1 = center, 2 = keep right/bottom edge.
    auto place = [](int idx, int newLen, int oldLen) {
        switch (idx) {
        case 0: return 0;
        case 1: return (newLen - oldLen) / 2;
        default: return newLen - oldLen;
        }
    };
    return {place(col, n.width(), m_current.width()), place(row, n.height(), m_current.height())};
}

QColor CanvasSizeDialog::fill() const
{
    if (m_fill->currentIndex() == 3)
        return m_custom;
    return m_fill->currentData().value<QColor>();
}
