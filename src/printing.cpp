#include "printing.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPageLayout>
#include <QPageSetupDialog>
#include <QPainter>
#include <QPrintDialog>
#include <QPrintPreviewWidget>
#include <QPrinter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

// ---------------------------------------------------------------- layout

PrintLayout PrintLayout::load()
{
    QSettings s;
    PrintLayout l;
    l.size = Size(std::clamp(s.value(QStringLiteral("print/size"), 0).toInt(), 0, 2));
    l.percent = std::clamp(s.value(QStringLiteral("print/percent"), 100).toInt(), 1, 1000);
    l.center = s.value(QStringLiteral("print/center"), true).toBool();
    l.annotations = s.value(QStringLiteral("print/annotations"), true).toBool();
    l.orientation = Orientation(std::clamp(s.value(QStringLiteral("print/orientation"), 0).toInt(), 0, 2));
    l.marginMm = std::clamp(s.value(QStringLiteral("print/margin"), 10.0).toDouble(), 0.0, 100.0);
    return l;
}

void PrintLayout::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("print/size"), int(size));
    s.setValue(QStringLiteral("print/percent"), percent);
    s.setValue(QStringLiteral("print/center"), center);
    s.setValue(QStringLiteral("print/annotations"), annotations);
    s.setValue(QStringLiteral("print/orientation"), int(orientation));
    s.setValue(QStringLiteral("print/margin"), marginMm);
}

// ---------------------------------------------------------------- printing

namespace printing {

QRectF targetRect(const QSizeF &img, const QRectF &page, double deviceDpi, const PrintLayout &l)
{
    if (img.isEmpty() || page.isEmpty())
        return {};
    double scale = 1.0;
    switch (l.size) {
    case PrintLayout::Size::Fit:
        scale = std::min(page.width() / img.width(), page.height() / img.height());
        break;
    case PrintLayout::Size::Actual:
        scale = deviceDpi / std::max(1.0, l.dpi);
        break;
    case PrintLayout::Size::Scale:
        scale = deviceDpi / std::max(1.0, l.dpi) * l.percent / 100.0;
        break;
    }
    QRectF r(QPointF(0, 0), img * scale);
    if (l.center)
        r.moveCenter(page.center());
    else
        r.moveTopLeft(page.topLeft());
    return r;
}

double imageDpi(const QImage &image)
{
    const double dpi = image.dotsPerMeterX() * 0.0254;
    if (dpi < 1.0 || std::abs(dpi - 96.0) < 0.5)
        return 0.0;
    return dpi;
}

void setupPage(QPrinter *printer, const QSize &img, const PrintLayout &l)
{
    QPageLayout::Orientation o = QPageLayout::Portrait;
    switch (l.orientation) {
    case PrintLayout::Orientation::Auto:
        o = img.width() > img.height() ? QPageLayout::Landscape : QPageLayout::Portrait;
        break;
    case PrintLayout::Orientation::Portrait: o = QPageLayout::Portrait; break;
    case PrintLayout::Orientation::Landscape: o = QPageLayout::Landscape; break;
    }
    printer->setPageOrientation(o);
    const double m = l.marginMm;
    printer->setPageMargins(QMarginsF(m, m, m, m), QPageLayout::Millimeter);
}

void paint(QPrinter *printer, const QImage &image, const PrintLayout &l)
{
    QPainter p;
    if (!p.begin(printer))
        return;
    const int res = printer->resolution();
    const QRectF page(QPointF(0, 0), printer->pageLayout().paintRectPixels(res).size());
    const QRectF target = targetRect(QSizeF(image.size()), page, res, l);
    p.setClipRect(page);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(target, image);
    p.end();
}

} // namespace printing

// ---------------------------------------------------------------- dialog

PrintDialog::PrintDialog(const QImage &withAnnotations, const QImage &base, QPrinter *printer, QWidget *parent)
    : QDialog(parent)
    , m_full(withAnnotations)
    , m_base(base)
    , m_printer(printer)
{
    setWindowTitle(tr("Print"));
    auto *main = new QHBoxLayout(this);

    auto *side = new QVBoxLayout;
    auto *sizeBox = new QGroupBox(tr("Size"), this);
    auto *sizeLay = new QGridLayout(sizeBox);
    m_fit = new QRadioButton(tr("&Fit to page"), sizeBox);
    m_actual = new QRadioButton(tr("&Actual size at"), sizeBox);
    m_scale = new QRadioButton(tr("&Scale"), sizeBox);
    auto *group = new QButtonGroup(this);
    for (QRadioButton *b : {m_fit, m_actual, m_scale})
        group->addButton(b);
    m_dpi = new QDoubleSpinBox(sizeBox);
    m_dpi->setRange(10, 4800);
    m_dpi->setDecimals(0);
    m_dpi->setSuffix(tr(" dpi"));
    m_percent = new QSpinBox(sizeBox);
    m_percent->setRange(1, 1000);
    m_percent->setSuffix(QStringLiteral(" %"));
    sizeLay->addWidget(m_fit, 0, 0, 1, 2);
    sizeLay->addWidget(m_actual, 1, 0);
    sizeLay->addWidget(m_dpi, 1, 1);
    sizeLay->addWidget(m_scale, 2, 0);
    sizeLay->addWidget(m_percent, 2, 1);
    side->addWidget(sizeBox);

    auto *pageBox = new QGroupBox(tr("Page"), this);
    auto *form = new QFormLayout(pageBox);
    m_orientation = new QComboBox(pageBox);
    m_orientation->addItems({tr("Automatic"), tr("Portrait"), tr("Landscape")});
    form->addRow(tr("&Orientation:"), m_orientation);
    m_margin = new QDoubleSpinBox(pageBox);
    m_margin->setRange(0, 100);
    m_margin->setDecimals(1);
    m_margin->setSuffix(tr(" mm"));
    form->addRow(tr("&Margins:"), m_margin);
    m_center = new QCheckBox(tr("&Center on page"), pageBox);
    form->addRow(m_center);
    m_annotations = new QCheckBox(tr("Print a&nnotations"), pageBox);
    m_annotations->setVisible(!m_full.isNull());
    form->addRow(m_annotations);
    auto *setup = new QPushButton(tr("Pape&r…"), pageBox);
    setup->setToolTip(tr("Paper size and printer page settings"));
    form->addRow(setup);
    side->addWidget(pageBox);
    side->addStretch(1);

    auto *buttons = new QDialogButtonBox(this);
    auto *printButton = buttons->addButton(tr("&Print…"), QDialogButtonBox::AcceptRole);
    printButton->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    side->addWidget(buttons);
    main->addLayout(side);

    m_preview = new QPrintPreviewWidget(m_printer, this);
    m_preview->setMinimumSize(420, 420);
    main->addWidget(m_preview, 1);

    const double dpi = printing::imageDpi(m_base);
    PrintLayout l = PrintLayout::load();
    l.dpi = dpi > 0 ? dpi : 96.0;
    setPrintLayout(l);

    connect(m_preview, &QPrintPreviewWidget::paintRequested, this,
            [this](QPrinter *p) { printing::paint(p, printedImage(), printLayout()); });
    for (QRadioButton *b : {m_fit, m_actual, m_scale})
        connect(b, &QRadioButton::toggled, this, &PrintDialog::changed);
    connect(m_dpi, &QDoubleSpinBox::valueChanged, this, &PrintDialog::changed);
    connect(m_margin, &QDoubleSpinBox::valueChanged, this, &PrintDialog::changed);
    connect(m_percent, &QSpinBox::valueChanged, this, &PrintDialog::changed);
    connect(m_center, &QCheckBox::toggled, this, &PrintDialog::changed);
    connect(m_annotations, &QCheckBox::toggled, this, &PrintDialog::changed);
    connect(m_orientation, &QComboBox::currentIndexChanged, this, &PrintDialog::changed);
    connect(setup, &QPushButton::clicked, this, &PrintDialog::pageSetup);
    connect(printButton, &QPushButton::clicked, this, &PrintDialog::choosePrinter);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    changed();
    resize(900, 600);
}

PrintLayout PrintDialog::printLayout() const
{
    PrintLayout l;
    l.size = m_actual->isChecked() ? PrintLayout::Size::Actual
             : m_scale->isChecked() ? PrintLayout::Size::Scale
                                    : PrintLayout::Size::Fit;
    l.dpi = m_dpi->value();
    l.percent = m_percent->value();
    l.center = m_center->isChecked();
    l.annotations = m_annotations->isChecked();
    l.orientation = PrintLayout::Orientation(m_orientation->currentIndex());
    l.marginMm = m_margin->value();
    return l;
}

void PrintDialog::setPrintLayout(const PrintLayout &l)
{
    m_updating = true;
    m_fit->setChecked(l.size == PrintLayout::Size::Fit);
    m_actual->setChecked(l.size == PrintLayout::Size::Actual);
    m_scale->setChecked(l.size == PrintLayout::Size::Scale);
    m_dpi->setValue(l.dpi);
    m_percent->setValue(l.percent);
    m_center->setChecked(l.center);
    m_annotations->setChecked(l.annotations);
    m_orientation->setCurrentIndex(int(l.orientation));
    m_margin->setValue(l.marginMm);
    m_updating = false;
    changed();
}

QImage PrintDialog::printedImage() const
{
    return m_annotations->isChecked() && !m_full.isNull() ? m_full : m_base;
}

void PrintDialog::changed()
{
    if (m_updating)
        return;
    const PrintLayout l = printLayout();
    m_dpi->setEnabled(l.size != PrintLayout::Size::Fit);
    m_percent->setEnabled(l.size == PrintLayout::Size::Scale);
    printing::setupPage(m_printer, m_base.size(), l);
    m_preview->updatePreview();
    m_preview->fitInView();
}

void PrintDialog::pageSetup()
{
    QPageSetupDialog dlg(m_printer, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    // Take over what was chosen there.
    m_updating = true;
    m_orientation->setCurrentIndex(m_printer->pageLayout().orientation() == QPageLayout::Landscape ? 2 : 1);
    m_margin->setValue(m_printer->pageLayout().margins(QPageLayout::Millimeter).left());
    m_updating = false;
    changed();
}

void PrintDialog::printNow()
{
    printing::setupPage(m_printer, m_base.size(), printLayout());
    printing::paint(m_printer, printedImage(), printLayout());
    printLayout().save();
}

void PrintDialog::choosePrinter()
{
    QPrintDialog dlg(m_printer, this);
    dlg.setOption(QAbstractPrintDialog::PrintPageRange, false);
    if (dlg.exec() != QDialog::Accepted)
        return;
    printNow();
    accept();
}
