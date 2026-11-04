#include "exportdialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr QSize kPreviewSize(360, 220);

QWidget *rowWidget(QLayout *layout, QWidget *parent)
{
    auto *w = new QWidget(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    w->setLayout(layout);
    return w;
}
} // namespace

bool ExportDialog::hasOptions(const QByteArray &format, const QImage &composite)
{
    if (format == "bmp") // only the background for transparent areas
        return composite.hasAlphaChannel();
    return format == "png" || format == "jpeg" || format == "webp" || format == "ico" || format == "tiff"
           || imageio::hasQuality(format);
}

QSlider *ExportDialog::addSlider(const QString &label, int min, int max, int value, const QString &tip)
{
    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    slider->setValue(value);
    auto *number = new QLabel(QString::number(value), this);
    number->setMinimumWidth(32);
    auto *row = new QHBoxLayout;
    row->addWidget(slider);
    row->addWidget(number);
    QWidget *w = rowWidget(row, this);
    if (!tip.isEmpty()) {
        slider->setToolTip(tip);
        w->setToolTip(tip);
    }
    m_form->addRow(label, w);
    connect(slider, &QSlider::valueChanged, this, [this, number](int v) {
        number->setText(QString::number(v));
        scheduleEstimate();
    });
    return slider;
}

ExportDialog::ExportDialog(const QImage &composite, const imageio::SaveOptions &options, QWidget *parent)
    : QDialog(parent)
    , m_image(composite)
    , m_options(options)
    , m_hasAlpha(composite.hasAlphaChannel())
{
    const QString fmt = QString::fromLatin1(options.format == "jpeg" ? "JPEG" : options.format.toUpper());
    setWindowTitle(tr("%1 options").arg(fmt));
    auto *main = new QVBoxLayout(this);
    m_form = new QFormLayout;
    main->addLayout(m_form);
    QFormLayout *form = m_form;

    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(1);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(150);
    connect(m_timer, &QTimer::timeout, this, &ExportDialog::updateEstimate);

    auto check = [this](const QString &text, bool on, const QString &tip = {}) {
        auto *cb = new QCheckBox(text, this);
        cb->setChecked(on);
        cb->setToolTip(tip);
        connect(cb, &QCheckBox::toggled, this, &ExportDialog::scheduleEstimate);
        return cb;
    };

    if (options.format == "png") {
        m_compression = new QSlider(Qt::Horizontal, this);
        m_compression->setRange(0, 9);
        m_compression->setValue(options.pngCompression);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Faster"), this));
        row->addWidget(m_compression);
        row->addWidget(new QLabel(tr("Smaller"), this));
        form->addRow(tr("Compression:"), row);
        form->addRow(new QLabel(tr("Transparency is preserved."), this));
    }

    if (options.format == "tiff") {
        m_tiffLzw = check(tr("LZW compression (lossless, smaller file)"), options.tiffLzw);
        form->addRow(m_tiffLzw);
        form->addRow(new QLabel(tr("Transparency is preserved."), this));
    }

    if (imageio::hasQuality(options.format)) {
        m_quality = addSlider(tr("Quality:"), 1, 100, options.quality,
                              options.format == "heif" ? tr("Higher quality means fewer artifacts and a larger file")
                                                       : tr("Higher quality means fewer artifacts and a larger file; "
                                                            "100 is lossless"));
        form->addRow(new QLabel(tr("Transparency is preserved."), this));
    }

    if (options.format == "ico") {
        auto *grid = new QGridLayout;
        const QList<int> choices = imageio::icoSizeChoices();
        for (int i = 0; i < choices.size(); ++i) {
            const int s = choices[i];
            auto *cb = new QCheckBox(QStringLiteral("%1 × %1").arg(s), this);
            cb->setChecked(options.icoSizes.contains(s));
            cb->setProperty("icoSize", s);
            connect(cb, &QCheckBox::toggled, this, [this] {
                bool any = false;
                for (QCheckBox *c : m_icoSizes)
                    any = any || c->isChecked();
                if (m_saveButton)
                    m_saveButton->setEnabled(any);
            });
            m_icoSizes.append(cb);
            grid->addWidget(cb, i / 4, i % 4);
        }
        form->addRow(tr("Sizes:"), grid);
        if (m_image.width() != m_image.height())
            form->addRow(new QLabel(tr("The image is not square: it is centered on a transparent square."), this));
    }

    if (options.format == "jpeg") {
        const codecs::JpegOptions &j = options.jpeg;
        m_quality = addSlider(tr("Quality:"), 1, 100, j.quality,
                              tr("Higher quality means fewer artifacts and a larger file"));
        if (codecs::haveJpeg()) {
            m_subsampling = new QComboBox(this);
            m_subsampling->addItem(tr("4:4:4 — full color (sharp text, screenshots)"));
            m_subsampling->addItem(tr("4:2:2 — half horizontal color"));
            m_subsampling->addItem(tr("4:2:0 — quarter color (smallest, photos)"));
            m_subsampling->setCurrentIndex(std::clamp(j.subsampling, 0, 2));
            m_subsampling->setToolTip(tr("Chroma subsampling: how much color detail is stored. "
                                         "Reduced color blurs colored text and thin colored lines."));
            connect(m_subsampling, &QComboBox::currentIndexChanged, this, &ExportDialog::scheduleEstimate);
            form->addRow(tr("Color subsampling:"), m_subsampling);
        }
        m_progressive = check(tr("Progressive (loads gradually on the web)"), j.progressive,
                              tr("Stores the image in several passes; often slightly smaller"));
        form->addRow(QString(), m_progressive);
        m_optimize = check(tr("Optimize coding (smaller file, a bit slower)"), j.optimize,
                           tr("Computes optimal Huffman tables for this image"));
        form->addRow(QString(), m_optimize);
        if (codecs::haveJpeg()) {
            m_smoothing = new QSpinBox(this);
            m_smoothing->setRange(0, 100);
            m_smoothing->setValue(j.smoothing);
            m_smoothing->setSpecialValueText(tr("Off"));
            m_smoothing->setToolTip(tr("Smooths the image before compression: reduces noise and file "
                                       "size, but blurs details"));
            connect(m_smoothing, &QSpinBox::valueChanged, this, &ExportDialog::scheduleEstimate);
            form->addRow(tr("Smoothing:"), m_smoothing);
        }
    }

    if (options.format == "webp") {
        const codecs::WebpOptions &w = options.webp;
        m_webpMode = new QComboBox(this);
        m_webpMode->addItem(tr("Lossy (smaller, like JPEG)"));
        m_webpMode->addItem(tr("Lossless (exact pixels, like PNG)"));
        m_webpMode->setCurrentIndex(w.lossless ? 1 : 0);
        connect(m_webpMode, &QComboBox::currentIndexChanged, this, [this] {
            updateVisibility();
            scheduleEstimate();
        });
        form->addRow(tr("Compression:"), m_webpMode);

        const bool native = codecs::haveWebp();
        m_quality = addSlider(tr("Quality:"), native ? 0 : 1, native ? 100 : 99, w.quality,
                              tr("Higher quality means fewer artifacts and a larger file"));
        m_lossyRows << form->itemAt(form->rowCount() - 1, QFormLayout::FieldRole)->widget();
        if (native) {
            m_preset = new QComboBox(this);
            m_preset->addItems({tr("General"), tr("Picture (indoor, portraits)"), tr("Photo (outdoor, natural light)"),
                                tr("Drawing (high contrast, lines)"), tr("Icon (small, colorful)"),
                                tr("Text (screenshots, documents)")});
            m_preset->setCurrentIndex(std::clamp(w.preset, 0, 5));
            m_preset->setToolTip(tr("Tunes the encoder for the kind of image"));
            connect(m_preset, &QComboBox::currentIndexChanged, this, &ExportDialog::scheduleEstimate);
            form->addRow(tr("Content type:"), m_preset);
            m_lossyRows << m_preset;
            m_method = addSlider(tr("Effort:"), 0, 6, w.method,
                                 tr("0 = fastest, 6 = slowest with the smallest file at the same quality"));
            m_lossyRows << form->itemAt(form->rowCount() - 1, QFormLayout::FieldRole)->widget();
            if (m_hasAlpha) {
                m_alphaQuality = addSlider(tr("Transparency quality:"), 0, 100, w.alphaQuality,
                                           tr("Compression of the transparency channel; 100 = exact"));
                m_lossyRows << form->itemAt(form->rowCount() - 1, QFormLayout::FieldRole)->widget();
            }
            m_sharpYuv = check(tr("Sharp color conversion (better edges, slower)"), w.sharpYuv,
                               tr("More accurate RGB→YUV conversion: fewer color fringes on sharp edges"));
            form->addRow(QString(), m_sharpYuv);
            m_lossyRows << m_sharpYuv;

            m_losslessLevel = addSlider(tr("Compression level:"), 0, 9, w.losslessLevel,
                                        tr("0 = fastest, 9 = smallest file; pixels are identical either way"));
            m_losslessRows << form->itemAt(form->rowCount() - 1, QFormLayout::FieldRole)->widget();
            m_nearLossless = new QSpinBox(this);
            m_nearLossless->setRange(0, 100);
            m_nearLossless->setValue(w.nearLossless);
            m_nearLossless->setToolTip(tr("100 = exactly lossless. Lower values allow tiny pixel changes "
                                          "for a smaller file."));
            connect(m_nearLossless, &QSpinBox::valueChanged, this, &ExportDialog::scheduleEstimate);
            form->addRow(tr("Near-lossless:"), m_nearLossless);
            m_losslessRows << m_nearLossless;
            if (m_hasAlpha) {
                m_exact = check(tr("Keep colors of fully transparent pixels"), w.exact,
                                tr("Normally they are changed to compress better; they are invisible anyway"));
                form->addRow(QString(), m_exact);
            }
        }
    }

    if (!imageio::formatHasAlpha(options.format) && m_hasAlpha) {
        m_background = new QPushButton(this);
        form->addRow(tr("Background for transparent areas:"), m_background);
        connect(m_background, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(m_options.background, this, tr("Background color"));
            if (c.isValid()) {
                m_options.background = c;
                updateBackgroundButton();
                scheduleEstimate();
            }
        });
        updateBackgroundButton();
    }
    if (options.format == "jpeg" || options.format == "webp" || imageio::hasQuality(options.format)) {
        m_estimate = new QLabel(this);
        form->addRow(tr("Estimated size:"), m_estimate);
        m_preview = new QLabel(this);
        m_preview->setFixedSize(kPreviewSize);
        m_preview->setAlignment(Qt::AlignCenter);
        m_preview->setFrameShape(QFrame::StyledPanel);
        m_preview->setToolTip(tr("Detail at 100 % from the center of the image"));
        main->addWidget(new QLabel(tr("Preview (100 %, center):"), this));
        main->addWidget(m_preview);
    }
    updateVisibility();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_saveButton = buttons->button(QDialogButtonBox::Save);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main->addWidget(buttons);
    updateEstimate();
}

ExportDialog::~ExportDialog()
{
    m_pool->clear();
    m_pool->waitForDone(); // a running estimate posts to this dialog
}

void ExportDialog::updateVisibility()
{
    if (!m_webpMode)
        return;
    const bool lossless = m_webpMode->currentIndex() == 1;
    for (QWidget *w : std::as_const(m_lossyRows))
        m_form->setRowVisible(w, !lossless);
    for (QWidget *w : std::as_const(m_losslessRows))
        m_form->setRowVisible(w, lossless);
    if (!codecs::haveWebp()) // Qt plugin: lossless ignores the quality
        m_quality->setEnabled(!lossless);
    adjustSize();
}

imageio::SaveOptions ExportDialog::options() const
{
    imageio::SaveOptions o = m_options;
    if (m_compression)
        o.pngCompression = m_compression->value();
    if (!m_icoSizes.isEmpty()) {
        o.icoSizes.clear();
        for (QCheckBox *c : m_icoSizes) {
            if (c->isChecked())
                o.icoSizes.append(c->property("icoSize").toInt());
        }
    }
    if (m_tiffLzw)
        o.tiffLzw = m_tiffLzw->isChecked();
    if (imageio::hasQuality(o.format) && m_quality)
        o.quality = m_quality->value();
    if (o.format == "jpeg") {
        o.jpeg.quality = m_quality->value();
        if (m_subsampling)
            o.jpeg.subsampling = m_subsampling->currentIndex();
        o.jpeg.progressive = m_progressive->isChecked();
        o.jpeg.optimize = m_optimize->isChecked();
        if (m_smoothing)
            o.jpeg.smoothing = m_smoothing->value();
    }
    if (o.format == "webp") {
        o.webp.lossless = m_webpMode->currentIndex() == 1;
        o.webp.quality = m_quality->value();
        if (m_preset)
            o.webp.preset = m_preset->currentIndex();
        if (m_method)
            o.webp.method = m_method->value();
        if (m_alphaQuality)
            o.webp.alphaQuality = m_alphaQuality->value();
        if (m_sharpYuv)
            o.webp.sharpYuv = m_sharpYuv->isChecked();
        if (m_losslessLevel)
            o.webp.losslessLevel = m_losslessLevel->value();
        if (m_nearLossless)
            o.webp.nearLossless = m_nearLossless->value();
        if (m_exact)
            o.webp.exact = m_exact->isChecked();
    }
    return o;
}

void ExportDialog::scheduleEstimate()
{
    if (m_estimate)
        m_timer->start();
}

void ExportDialog::updateEstimate()
{
    if (!m_estimate)
        return;
    const int generation = ++m_generation;
    m_estimate->setText(tr("calculating…"));
    const imageio::SaveOptions o = options();
    const QImage image = m_image;
    const QSize ps = kPreviewSize - QSize(4, 4);
    m_pool->clear(); // drop a queued, not yet started estimate
    m_pool->start([this, generation, o, image, ps] {
        const QByteArray data = imageio::encode(image, o);
        // Decode the encoded result to show real compression artifacts.
        QImage part;
        const QImage decoded = QImage::fromData(data, o.format);
        if (!decoded.isNull()) {
            const QRect crop(QPoint((decoded.width() - ps.width()) / 2, (decoded.height() - ps.height()) / 2), ps);
            part = decoded.copy(crop.intersected(decoded.rect()));
        }
        QMetaObject::invokeMethod(this, [this, generation, data, part] { estimateReady(generation, data, part); },
                                  Qt::QueuedConnection);
    });
}

void ExportDialog::estimateReady(int generation, const QByteArray &data, const QImage &preview)
{
    if (generation != m_generation)
        return; // superseded by a newer setting
    m_estimate->setText(data.isEmpty() ? tr("unknown") : QLocale().formattedDataSize(data.size()));
    if (preview.isNull())
        return;
    QImage part = preview;
    part.setDevicePixelRatio(devicePixelRatioF()); // 1 image px = 1 device px
    m_preview->setPixmap(QPixmap::fromImage(part));
}

void ExportDialog::updateBackgroundButton()
{
    QPixmap pm(32, 16);
    pm.fill(m_options.background);
    m_background->setIcon(QIcon(pm));
    m_background->setText(m_options.background.name());
}
