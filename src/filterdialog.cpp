#include "filterdialog.h"

#include "canvas.h"
#include "document.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace {
// Parameter labels are translated in the "filters" context (filters.cpp).
QString ftr(const char *s) { return QCoreApplication::translate("filters", s); }

QString settingsKey(const filters::Spec &spec, const QString &key)
{
    return QStringLiteral("filters/%1/%2").arg(spec.id, key);
}

// Values that start fresh every time instead of being remembered.
bool remembered(const filters::Spec &spec, const QString &key)
{
    return !(spec.id == QLatin1String("rotate") && key == QLatin1String("angle"));
}
} // namespace

FilterDialog::FilterDialog(const filters::Spec &spec, Canvas *canvas, const filters::Values &overrides,
                           QWidget *parent)
    : QDialog(parent)
    , m_spec(spec)
    , m_canvas(canvas)
    , m_doc(canvas->document())
    , m_original(canvas->document()->state())
    , m_selection(spec.selection ? canvas->areaSelection().intersected(QRect(QPoint(), m_original.base.size()))
                                 : QRect())
{
    setWindowTitle(filters::displayName(spec));
    m_values = filters::defaults(spec);
    QSettings settings;
    for (const filters::Param &p : spec.params) {
        if (remembered(spec, p.key))
            m_values[p.key] = std::clamp(settings.value(settingsKey(spec, p.key), p.def).toDouble(), p.min, p.max);
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        m_values[it.key()] = it.value();

    auto *main = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    main->addLayout(form);

    for (const filters::Param &p : spec.params) {
        const QString key = p.key;
        const double v = m_values.value(key);
        QWidget *field = nullptr;
        switch (p.type) {
        case filters::Param::Type::Int:
        case filters::Param::Type::Double: {
            const double factor = std::pow(10.0, p.decimals);
            auto *slider = new QSlider(Qt::Horizontal, this);
            slider->setRange(int(std::lround(p.min * factor)), int(std::lround(p.max * factor)));
            slider->setValue(int(std::lround(v * factor)));
            slider->setMinimumWidth(220);
            auto *spin = new QDoubleSpinBox(this);
            spin->setRange(p.min, p.max);
            spin->setDecimals(p.decimals);
            spin->setSingleStep(p.decimals > 0 ? 1.0 / factor : 1.0);
            spin->setSuffix(QString::fromUtf8(p.suffix));
            spin->setValue(v);
            spin->setObjectName(key);
            spin->setKeyboardTracking(false);
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->addWidget(slider, 1);
            row->addWidget(spin);
            field = new QWidget(this);
            field->setLayout(row);
            connect(slider, &QSlider::valueChanged, this, [this, key, factor](int i) { setValue(key, i / factor); });
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, key](double d) { setValue(key, d); });
            connect(this, &FilterDialog::valueChanged, this, [=](const QString &k, double d) {
                if (k != key)
                    return;
                const QSignalBlocker b1(slider), b2(spin);
                slider->setValue(int(std::lround(d * factor)));
                spin->setValue(d);
            });
            break;
        }
        case filters::Param::Type::Bool: {
            auto *cb = new QCheckBox(ftr(p.label), this);
            cb->setChecked(v != 0.0);
            cb->setObjectName(key);
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) { setValue(key, on ? 1 : 0); });
            connect(this, &FilterDialog::valueChanged, this, [=](const QString &k, double d) {
                if (k == key) {
                    const QSignalBlocker b(cb);
                    cb->setChecked(d != 0.0);
                }
            });
            if (*p.tip)
                cb->setToolTip(ftr(p.tip));
            form->addRow(QString(), cb);
            continue;
        }
        case filters::Param::Type::Choice: {
            auto *combo = new QComboBox(this);
            for (const char *c : p.choices)
                combo->addItem(ftr(c));
            combo->setCurrentIndex(int(v));
            combo->setObjectName(key);
            connect(combo, &QComboBox::currentIndexChanged, this, [this, key](int i) { setValue(key, i); });
            connect(this, &FilterDialog::valueChanged, this, [=](const QString &k, double d) {
                if (k == key) {
                    const QSignalBlocker b(combo);
                    combo->setCurrentIndex(int(d));
                }
            });
            field = combo;
            break;
        }
        }
        if (*p.tip)
            field->setToolTip(ftr(p.tip));
        form->addRow(ftr(p.label), field);
    }

    if (spec.id == QLatin1String("rotate")) {
        auto *detect = new QPushButton(tr("Straighten Document Automatically"), this);
        detect->setToolTip(tr("Finds the angle of text lines in a scanned or photographed document"));
        auto *info = new QLabel(this);
        connect(detect, &QPushButton::clicked, this, [this, info] {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const double skew = filters::detectSkew(m_original.base);
            QApplication::restoreOverrideCursor();
            if (skew == 0.0) {
                info->setText(tr("No tilted text lines found."));
                return;
            }
            info->setText(tr("Text is tilted by %1°.").arg(QLocale().toString(skew, 'f', 2)));
            setValue(QStringLiteral("angle"), std::round(-skew * 10.0) / 10.0);
        });
        form->addRow(QString(), detect);
        form->addRow(QString(), info);
        m_hadGrid = m_canvas->gridVisible();
        m_canvas->setGridVisible(true);
    }

    if (spec.selection) {
        auto *scope = new QLabel(m_selection.isEmpty() ? tr("Applies to the whole image.")
                                                        : tr("Applies to the selected area only."),
                                 this);
        scope->setEnabled(false);
        form->addRow(scope);
    }

    auto *bottom = new QHBoxLayout;
    m_previewBox = new QCheckBox(tr("Preview"), this);
    m_previewBox->setChecked(true);
    connect(m_previewBox, &QCheckBox::toggled, this, &FilterDialog::showPreview);
    bottom->addWidget(m_previewBox);
    m_busy = new QLabel(this);
    bottom->addWidget(m_busy, 1);
    auto *reset = new QPushButton(tr("Reset"), this);
    connect(reset, &QPushButton::clicked, this, [this] {
        for (const filters::Param &p : m_spec.params)
            setValue(p.key, p.def);
    });
    bottom->addWidget(reset);
    main->addLayout(bottom);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &FilterDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &FilterDialog::reject);
    main->addWidget(buttons);

    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(1);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(90);
    connect(m_timer, &QTimer::timeout, this, &FilterDialog::compute);
    compute();
}

FilterDialog::~FilterDialog()
{
    m_pool->clear();
    m_pool->waitForDone(); // a running computation posts to this dialog
    if (m_spec.id == QLatin1String("rotate"))
        m_canvas->setGridVisible(m_hadGrid);
}

void FilterDialog::setValue(const QString &key, double value)
{
    if (m_values.value(key) == value && m_values.contains(key))
        return;
    m_values[key] = value;
    emit valueChanged(key, value);
    changed();
}

void FilterDialog::changed()
{
    ++m_generation;
    m_busy->setText(tr("calculating…"));
    m_timer->start();
}

void FilterDialog::compute()
{
    const int generation = m_generation;
    const filters::Spec *spec = &m_spec;
    const DocState original = m_original;
    const QRect selection = m_selection;
    const filters::Values values = m_values;
    m_busy->setText(tr("calculating…"));
    m_pool->clear();
    m_pool->start([=, this] {
        DocState r = filters::apply(*spec, original, selection, values);
        QMetaObject::invokeMethod(
            this,
            [this, generation, r] {
                if (generation != m_generation)
                    return;
                m_result = r;
                m_readyGeneration = generation;
                m_busy->clear();
                showPreview();
            },
            Qt::QueuedConnection);
    });
}

void FilterDialog::showPreview()
{
    if (m_previewBox->isChecked() && previewReady())
        m_doc->setTransient(m_result);
    else if (!m_previewBox->isChecked())
        m_doc->setTransient(m_original);
}

DocState FilterDialog::result()
{
    if (!previewReady()) {
        m_timer->stop();
        m_pool->clear();
        m_pool->waitForDone();
        QCoreApplication::sendPostedEvents(this); // a finished computation
        if (!previewReady()) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            m_result = filters::apply(m_spec, m_original, m_selection, m_values);
            m_readyGeneration = m_generation;
            QApplication::restoreOverrideCursor();
        }
    }
    return m_result;
}

void FilterDialog::saveValues() const
{
    QSettings settings;
    for (const filters::Param &p : m_spec.params)
        if (remembered(m_spec, p.key))
            settings.setValue(settingsKey(m_spec, p.key), m_values.value(p.key));
}

void FilterDialog::accept()
{
    const DocState r = result();
    saveValues();
    m_doc->setTransient(m_original);
    m_doc->commit(r, filters::displayName(m_spec));
    QDialog::accept();
}

void FilterDialog::reject()
{
    m_timer->stop();
    m_pool->clear();
    m_pool->waitForDone();
    m_doc->setTransient(m_original);
    QDialog::reject();
}

void FilterDialog::applyDirect(const filters::Spec &spec, Canvas *canvas)
{
    Document *doc = canvas->document();
    if (doc->isEmpty())
        return;
    const QRect sel = spec.selection ? canvas->areaSelection() : QRect();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const DocState r = filters::apply(spec, doc->state(), sel, {});
    QApplication::restoreOverrideCursor();
    doc->commit(r, filters::displayName(spec));
}
