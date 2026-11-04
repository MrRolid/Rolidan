#include "sizedialogs.h"

#include "filters.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QSettings>
#include <QGroupBox>
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

ResizeDialog::ResizeDialog(const QSize &current, double dpi, const QSize &screen, QWidget *parent)
    : QDialog(parent)
    , m_current(current)
    , m_origDpi(dpi)
    , m_screen(screen)
    , m_w(current.width())
    , m_h(current.height())
{
    setWindowTitle(tr("Resize Image"));
    QSettings settings;
    auto *main = new QVBoxLayout(this);
    m_original = new QLabel(this);
    main->addWidget(m_original);

    auto *sizeBox = new QGroupBox(tr("New size"), this);
    auto *grid = new QGridLayout(sizeBox);
    m_preset = new QComboBox(sizeBox);
    m_preset->addItem(tr("Choose…"), 0);
    m_preset->addItem(tr("Half (50 %)"), int(Half));
    m_preset->addItem(tr("Double (200 %)"), int(Double));
    const QList<QPair<QSize, QString>> boxes{
        {{640, 480}, {}},           {{800, 600}, {}},           {{1024, 768}, {}},
        {{1280, 720}, QStringLiteral("HD")}, {{1280, 1024}, {}},  {{1600, 1200}, {}},
        {{1920, 1080}, QStringLiteral("Full HD")}, {{2560, 1440}, QStringLiteral("QHD")},
        {{3840, 2160}, QStringLiteral("4K")}};
    for (const auto &b : boxes) {
        QString text = QStringLiteral("%1 × %2").arg(b.first.width()).arg(b.first.height());
        if (!b.second.isEmpty())
            text += QStringLiteral(" (%1)").arg(b.second);
        m_preset->addItem(text, b.first);
    }
    if (!m_screen.isEmpty()) {
        m_preset->addItem(tr("Best fit to screen (%1 × %2)").arg(m_screen.width()).arg(m_screen.height()), int(FitScreen));
        m_preset->addItem(tr("Screen size, ignore aspect ratio"), int(ScreenSize));
    }
    m_preset->setToolTip(tr("With \"Keep aspect ratio\" the image fits inside the chosen size"));
    auto *presetLabel = new QLabel(tr("&Preset:"), sizeBox);
    presetLabel->setBuddy(m_preset);
    grid->addWidget(presetLabel, 0, 0);
    grid->addWidget(m_preset, 0, 1, 1, 3);

    auto spin = [sizeBox](double max, int decimals, const QString &suffix) {
        auto *sb = new QDoubleSpinBox(sizeBox);
        sb->setRange(decimals ? 0.01 : 1, max);
        sb->setDecimals(decimals);
        sb->setSuffix(suffix);
        sb->setKeyboardTracking(false);
        return sb;
    };
    m_width = spin(kMaxSide, 0, {});
    m_height = spin(kMaxSide, 0, {});
    m_unit = new QComboBox(sizeBox);
    m_unit->addItems({tr("pixels"), tr("cm"), tr("inches")});
    m_widthPct = spin(10000, 2, QStringLiteral(" %"));
    m_heightPct = spin(10000, 2, QStringLiteral(" %"));
    for (QDoubleSpinBox *sb : {m_widthPct, m_heightPct})
        sb->setRange(0.01, 10000);
    auto label = [sizeBox](const QString &text, QWidget *buddy) {
        auto *l = new QLabel(text, sizeBox);
        l->setBuddy(buddy);
        return l;
    };
    grid->addWidget(label(tr("&Width:"), m_width), 1, 0);
    grid->addWidget(m_width, 1, 1);
    grid->addWidget(label(tr("H&eight:"), m_height), 1, 2);
    grid->addWidget(m_height, 1, 3);
    grid->addWidget(label(tr("&Units:"), m_unit), 2, 0);
    grid->addWidget(m_unit, 2, 1);
    grid->addWidget(new QLabel(tr("As percentage of the original:"), sizeBox), 3, 0, 1, 4);
    grid->addWidget(label(tr("Wi&dth:"), m_widthPct), 4, 0);
    grid->addWidget(m_widthPct, 4, 1);
    grid->addWidget(label(tr("Hei&ght:"), m_heightPct), 4, 2);
    grid->addWidget(m_heightPct, 4, 3);
    m_keep = new QCheckBox(tr("&Keep aspect ratio"), sizeBox);
    m_keep->setChecked(settings.value(QStringLiteral("resize/keepAspect"), true).toBool());
    grid->addWidget(m_keep, 5, 0, 1, 4);
    m_dpi = new QSpinBox(sizeBox);
    m_dpi->setRange(1, 9600);
    m_dpi->setSuffix(tr(" dpi"));
    m_dpi->setValue(dpi > 0 ? int(std::lround(dpi)) : 96);
    m_dpi->setToolTip(tr("Print resolution stored in the file; also used for cm and inches"));
    grid->addWidget(label(tr("&Resolution:"), m_dpi), 6, 0);
    grid->addWidget(m_dpi, 6, 1);
    main->addWidget(sizeBox);

    auto *qualityBox = new QGroupBox(tr("Quality"), this);
    auto *form = new QFormLayout(qualityBox);
    m_filter = new QComboBox(qualityBox);
    // Same choices as IrfanView, sharpest first. Data: filters::Resample.
    using R = filters::Resample;
    m_filter->addItem(tr("Lanczos – sharpest (recommended)"), int(R::Lanczos));
    m_filter->addItem(tr("Mitchell – smooth (bicubic)"), int(R::Mitchell));
    m_filter->addItem(tr("Hermite – smooth, no halos"), int(R::Hermite));
    m_filter->addItem(tr("Triangle (bilinear) – fast"), int(R::Bilinear));
    m_filter->addItem(tr("Bell – soft"), int(R::Bell));
    m_filter->addItem(tr("B-spline – softest, no halos"), int(R::BSpline));
    m_filter->addItem(tr("Nearest neighbor – hard pixels (pixel art, screenshots at 200 %)"), int(R::Nearest));
    m_filter->setCurrentIndex(std::max(0, m_filter->findData(settings.value(QStringLiteral("resize/filter"), 0).toInt())));
    form->addRow(tr("&Method:"), m_filter);
    m_sharpen = new QCheckBox(tr("&Sharpen after resampling"), qualityBox);
    m_sharpen->setChecked(settings.value(QStringLiteral("resize/sharpen"), false).toBool());
    m_sharpen->setToolTip(tr("Restores crispness lost when shrinking photos"));
    form->addRow(m_sharpen);
    main->addWidget(qualityBox);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    main->addWidget(m_info);
    main->addWidget(okCancel(this));

    m_unit->setCurrentIndex(std::clamp(settings.value(QStringLiteral("resize/unit"), 0).toInt(), 0, 2));
    unitChanged();

    connect(m_width, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        const double w = toPixels(v);
        setPixels(w, m_keep->isChecked() ? w * m_current.height() / m_current.width() : m_h, m_width);
    });
    connect(m_height, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        const double h = toPixels(v);
        setPixels(m_keep->isChecked() ? h * m_current.width() / m_current.height() : m_w, h, m_height);
    });
    connect(m_widthPct, &QDoubleSpinBox::valueChanged, this, [this](double p) {
        setPixels(m_current.width() * p / 100.0, m_keep->isChecked() ? m_current.height() * p / 100.0 : m_h, m_widthPct);
    });
    connect(m_heightPct, &QDoubleSpinBox::valueChanged, this, [this](double p) {
        setPixels(m_keep->isChecked() ? m_current.width() * p / 100.0 : m_w, m_current.height() * p / 100.0, m_heightPct);
    });
    connect(m_keep, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            setPixels(m_w, m_w * m_current.height() / m_current.width(), nullptr);
    });
    connect(m_unit, &QComboBox::currentIndexChanged, this, &ResizeDialog::unitChanged);
    connect(m_dpi, &QSpinBox::valueChanged, this, [this] {
        if (m_unit->currentIndex() != Pixels)
            unitChanged(); // same pixels, other physical size
        updateInfo();
    });
    connect(m_filter, &QComboBox::currentIndexChanged, this, &ResizeDialog::updateInfo);
    connect(m_preset, &QComboBox::activated, this, &ResizeDialog::applyPreset);
}

double ResizeDialog::toPixels(double v) const
{
    switch (m_unit->currentIndex()) {
    case Centimeters: return v / 2.54 * m_dpi->value();
    case Inches: return v * m_dpi->value();
    default: return v;
    }
}

double ResizeDialog::fromPixels(double px) const
{
    switch (m_unit->currentIndex()) {
    case Centimeters: return px / m_dpi->value() * 2.54;
    case Inches: return px / m_dpi->value();
    default: return px;
    }
}

void ResizeDialog::unitChanged()
{
    m_updating = true;
    const bool px = m_unit->currentIndex() == Pixels;
    for (QDoubleSpinBox *sb : {m_width, m_height}) {
        sb->setDecimals(px ? 0 : 2);
        sb->setRange(px ? 1 : 0.01, fromPixels(kMaxSide));
        sb->setSuffix(px ? tr(" px") : m_unit->currentIndex() == Centimeters ? tr(" cm") : tr(" in"));
    }
    m_updating = false;
    setPixels(m_w, m_h, nullptr);
}

void ResizeDialog::setPixels(double w, double h, QObject *source)
{
    if (m_updating)
        return;
    m_updating = true;
    m_w = std::clamp(w, 1.0, double(kMaxSide));
    m_h = std::clamp(h, 1.0, double(kMaxSide));
    if (source != m_width)
        m_width->setValue(fromPixels(m_w));
    if (source != m_height)
        m_height->setValue(fromPixels(m_h));
    if (source != m_widthPct)
        m_widthPct->setValue(100.0 * m_w / m_current.width());
    if (source != m_heightPct)
        m_heightPct->setValue(100.0 * m_h / m_current.height());
    m_updating = false;
    updateInfo();
}

void ResizeDialog::applyPreset(int index)
{
    const QVariant v = m_preset->itemData(index);
    const double W = m_current.width(), H = m_current.height();
    if (v.typeId() == QMetaType::QSize) {
        const QSize box = v.toSize();
        if (m_keep->isChecked()) {
            const double s = std::min(box.width() / W, box.height() / H);
            setPixels(W * s, H * s, nullptr);
        } else {
            setPixels(box.width(), box.height(), nullptr);
        }
    } else {
        switch (v.toInt()) {
        case Half: setPixels(W / 2, H / 2, nullptr); break;
        case Double: setPixels(W * 2, H * 2, nullptr); break;
        case FitScreen: {
            const double s = std::min(m_screen.width() / W, m_screen.height() / H);
            setPixels(W * s, H * s, nullptr);
            break;
        }
        case ScreenSize:
            m_keep->setChecked(false);
            setPixels(m_screen.width(), m_screen.height(), nullptr);
            break;
        default: break;
        }
    }
    m_preset->setCurrentIndex(0); // a one-shot choice, like a button
}

QSize ResizeDialog::newSize() const
{
    return {std::max(1, int(std::lround(m_w))), std::max(1, int(std::lround(m_h)))};
}

int ResizeDialog::filter() const
{
    return m_filter->currentData().toInt();
}

bool ResizeDialog::sharpen() const
{
    return m_sharpen->isChecked();
}

double ResizeDialog::newDpi() const
{
    const int orig = m_origDpi > 0 ? int(std::lround(m_origDpi)) : 96;
    return m_dpi->value() == orig ? 0.0 : m_dpi->value();
}

void ResizeDialog::accept()
{
    QSettings settings;
    settings.setValue(QStringLiteral("resize/keepAspect"), m_keep->isChecked());
    settings.setValue(QStringLiteral("resize/filter"), filter());
    settings.setValue(QStringLiteral("resize/sharpen"), m_sharpen->isChecked());
    settings.setValue(QStringLiteral("resize/unit"), m_unit->currentIndex());
    QDialog::accept();
}

void ResizeDialog::updateInfo()
{
    const double dpi = m_dpi->value();
    auto physical = [](double w, double h, double dpi) {
        return tr("%1 × %2 cm at %3 dpi")
            .arg(QLocale().toString(w / dpi * 2.54, 'f', 1), QLocale().toString(h / dpi * 2.54, 'f', 1))
            .arg(dpi);
    };
    const double mp = double(m_current.width()) * m_current.height() / 1e6;
    m_original->setText(tr("Original size: %1 × %2 px (%3 MP), %4")
                            .arg(m_current.width())
                            .arg(m_current.height())
                            .arg(QLocale().toString(mp, 'f', mp < 1 ? 2 : 1))
                            .arg(physical(m_current.width(), m_current.height(), m_origDpi > 0 ? m_origDpi : 96)));
    const QSize s = newSize();
    const qint64 bytes = qint64(s.width()) * s.height() * 4;
    QString text = tr("New size: %1 × %2 px, %3 (%4 in memory). Annotations are scaled too.")
                       .arg(s.width())
                       .arg(s.height())
                       .arg(physical(s.width(), s.height(), dpi))
                       .arg(QLocale().formattedDataSize(bytes));
    if (s.width() > m_current.width() * 1.01 && filter() != int(filters::Resample::Nearest))
        text += QLatin1Char(' ') + tr("Enlarging cannot add detail; for pixel art choose Nearest neighbor.");
    m_info->setText(text);
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
