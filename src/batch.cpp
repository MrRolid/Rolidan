#include "batch.h"

#include "document.h"
#include "exportdialog.h"
#include "project.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>
#include <cmath>

namespace batch {

namespace {
QImage loadComposite(const QString &path, QString *error)
{
    if (project::isProjectFile(path)) {
        DocState s;
        if (!project::load(path, &s, error))
            return {};
        return docops::composite(s);
    }
    const imageio::LoadResult r = imageio::loadFile(path);
    if (!r.ok() && error)
        *error = r.error;
    return r.image;
}

QString uniquePath(const QString &dir, const QString &base, const QString &suffix)
{
    QString path = QDir(dir).filePath(base + QLatin1Char('.') + suffix);
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QDir(dir).filePath(QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix));
    return path;
}

const char *adjustFilter(Adjust a)
{
    switch (a) {
    case Adjust::EnhanceColors: return "enhanceColors";
    case Adjust::Grayscale: return "grayscale";
    case Adjust::CleanDocument: return "cleanDocument";
    default: return nullptr;
    }
}
} // namespace

bool processFile(const QString &input, const Options &o, QString *outputPath, QString *error)
{
    QString err;
    QImage img = loadComposite(input, &err);
    if (img.isNull()) {
        if (error)
            *error = err.isEmpty() ? QCoreApplication::translate("batch", "Cannot read the image") : err;
        return false;
    }
    DocState s{img, {}, {}};
    if (o.adjust == Adjust::StraightenDocument) {
        const double skew = filters::detectSkew(s.base);
        if (skew != 0.0)
            s = filters::rotate(s, -skew, 1, 1);
    } else if (const char *id = adjustFilter(o.adjust)) {
        s = filters::apply(*filters::find(QLatin1String(id)), s, {}, o.adjustValues);
    }
    if (o.shrink && (s.base.width() > o.maxSize.width() || s.base.height() > o.maxSize.height())) {
        const QSize target = s.base.size().scaled(o.maxSize, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        s = docops::resizeImage(s, target);
    }
    if (!QDir().mkpath(o.outputDir)) {
        if (error)
            *error = QCoreApplication::translate("batch", "Cannot create the folder %1").arg(o.outputDir);
        return false;
    }
    QByteArray format;
    const QString probe = imageio::resolveSavePath(QStringLiteral("x"), o.save.format, &format);
    const QString suffix = QFileInfo(probe).suffix();
    const QString path = uniquePath(o.outputDir, QFileInfo(input).completeBaseName() + o.suffix, suffix);
    imageio::SaveOptions so = o.save;
    so.format = format;
    if (!imageio::save(s.base, path, so, error))
        return false;
    if (outputPath)
        *outputPath = path;
    return true;
}

} // namespace batch

// ================================================================ dialog

BatchDialog::BatchDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Batch Convert"));
    resize(640, 560);
    auto *main = new QVBoxLayout(this);

    main->addWidget(new QLabel(tr("Images to convert:"), this));
    auto *listRow = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listRow->addWidget(m_list, 1);
    auto *listButtons = new QVBoxLayout;
    auto *addFilesButton = new QPushButton(tr("Add Files…"), this);
    auto *addFolderButton = new QPushButton(tr("Add Folder…"), this);
    auto *removeButton = new QPushButton(tr("Remove"), this);
    auto *clearButton = new QPushButton(tr("Clear"), this);
    for (QPushButton *b : {addFilesButton, addFolderButton, removeButton, clearButton})
        listButtons->addWidget(b);
    listButtons->addStretch();
    listRow->addLayout(listButtons);
    main->addLayout(listRow, 1);

    auto imageGlobs = [] {
        QStringList globs;
        for (const QByteArray &f : QImageReader::supportedImageFormats())
            globs << QStringLiteral("*.") + QString::fromLatin1(f);
        globs << QStringLiteral("*.") + QLatin1String(project::kSuffix);
        return globs;
    };
    connect(addFilesButton, &QPushButton::clicked, this, [this, imageGlobs] {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, tr("Add Files"), settings::lastDirectory(),
            tr("Images and projects (%1)").arg(imageGlobs().join(QLatin1Char(' '))));
        if (!paths.isEmpty())
            settings::setLastDirectory(QFileInfo(paths.first()).absolutePath());
        addFiles(paths);
    });
    connect(addFolderButton, &QPushButton::clicked, this, [this, imageGlobs] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder"), settings::lastDirectory());
        if (dir.isEmpty())
            return;
        QStringList paths;
        QDirIterator it(dir, imageGlobs(), QDir::Files | QDir::Readable);
        while (it.hasNext())
            paths << it.next();
        paths.sort(Qt::CaseInsensitive);
        addFiles(paths);
    });
    connect(removeButton, &QPushButton::clicked, this, [this] {
        qDeleteAll(m_list->selectedItems());
        updateState();
    });
    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_list->clear();
        updateState();
    });

    auto *form = new QFormLayout;
    main->addLayout(form);
    auto *outRow = new QHBoxLayout;
    m_output = new QLineEdit(this);
    auto *browse = new QPushButton(tr("Browse…"), this);
    outRow->addWidget(m_output, 1);
    outRow->addWidget(browse);
    form->addRow(tr("Output folder:"), outRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Output Folder"),
                                                              m_output->text().isEmpty() ? settings::lastDirectory()
                                                                                         : m_output->text());
        if (!dir.isEmpty())
            m_output->setText(dir);
    });
    connect(m_output, &QLineEdit::textChanged, this, &BatchDialog::updateState);

    auto *formatRow = new QHBoxLayout;
    m_format = new QComboBox(this);
    for (const imageio::FormatInfo &f : imageio::exportFormats())
        m_format->addItem(f.name, f.format);
    QSettings settings;
    m_format->setCurrentIndex(std::max(0, m_format->findData(settings.value("batch/format", "jpeg").toByteArray())));
    m_formatOptions = new QPushButton(tr("Options…"), this);
    m_formatOptions->setToolTip(tr("Quality and codec options, shown on the first image"));
    formatRow->addWidget(m_format, 1);
    formatRow->addWidget(m_formatOptions);
    form->addRow(tr("Format:"), formatRow);
    m_saveOptions = settings::saveOptions(m_format->currentData().toByteArray());
    connect(m_format, &QComboBox::currentIndexChanged, this, [this] {
        m_saveOptions = settings::saveOptions(m_format->currentData().toByteArray());
    });
    connect(m_formatOptions, &QPushButton::clicked, this, &BatchDialog::chooseOptions);

    auto *sizeRow = new QHBoxLayout;
    m_shrink = new QCheckBox(tr("Shrink to fit"), this);
    m_maxW = new QSpinBox(this);
    m_maxH = new QSpinBox(this);
    for (QSpinBox *sb : {m_maxW, m_maxH}) {
        sb->setRange(16, 16384);
        sb->setSuffix(QStringLiteral(" px"));
    }
    m_shrink->setChecked(settings.value("batch/shrink", false).toBool());
    m_maxW->setValue(settings.value("batch/maxWidth", 1920).toInt());
    m_maxH->setValue(settings.value("batch/maxHeight", 1920).toInt());
    sizeRow->addWidget(m_shrink);
    sizeRow->addWidget(m_maxW);
    sizeRow->addWidget(new QLabel(QStringLiteral("×"), this));
    sizeRow->addWidget(m_maxH);
    sizeRow->addStretch();
    form->addRow(tr("Size:"), sizeRow);
    m_shrink->setToolTip(tr("Larger images are reduced to fit inside this box; smaller ones are kept"));

    m_adjust = new QComboBox(this);
    m_adjust->addItem(tr("None"));
    m_adjust->addItem(tr("Enhance colors"));
    m_adjust->addItem(tr("Grayscale"));
    m_adjust->addItem(tr("Clean scanned document"));
    m_adjust->addItem(tr("Straighten document automatically"));
    m_adjust->setCurrentIndex(settings.value("batch/adjust", 0).toInt());
    m_adjust->setToolTip(tr("Uses the values last chosen in the Colors and Filters menus"));
    form->addRow(tr("Adjustment:"), m_adjust);

    m_suffix = new QLineEdit(settings.value("batch/suffix").toString(), this);
    m_suffix->setPlaceholderText(tr("e.g. -web"));
    m_suffix->setToolTip(tr("Added to each file name. Existing files are never overwritten."));
    form->addRow(tr("Name suffix:"), m_suffix);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    main->addWidget(m_progress);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    main->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(this);
    m_startButton = buttons->addButton(tr("Convert"), QDialogButtonBox::AcceptRole);
    m_closeButton = buttons->addButton(QDialogButtonBox::Close);
    connect(m_startButton, &QPushButton::clicked, this, &BatchDialog::start);
    connect(m_closeButton, &QPushButton::clicked, this, [this] {
        if (m_running)
            m_cancel->store(true);
        else
            reject();
    });
    main->addWidget(buttons);
    updateState();
}

BatchDialog::~BatchDialog()
{
    if (m_cancel)
        m_cancel->store(true);
}

void BatchDialog::addFiles(const QStringList &paths)
{
    const QStringList existing = files();
    for (const QString &p : paths) {
        const QString abs = QFileInfo(p).absoluteFilePath();
        if (!existing.contains(abs))
            m_list->addItem(abs);
    }
    if (m_output->text().isEmpty() && m_list->count() > 0)
        m_output->setText(QDir(QFileInfo(m_list->item(0)->text()).absolutePath()).filePath(tr("converted")));
    updateState();
}

QStringList BatchDialog::files() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i)
        out << m_list->item(i)->text();
    return out;
}

void BatchDialog::setOutputDir(const QString &dir)
{
    m_output->setText(dir);
}

batch::Options BatchDialog::options() const
{
    batch::Options o;
    o.outputDir = m_output->text().trimmed();
    o.save = m_saveOptions;
    o.save.format = m_format->currentData().toByteArray();
    o.shrink = m_shrink->isChecked();
    o.maxSize = QSize(m_maxW->value(), m_maxH->value());
    o.adjust = batch::Adjust(m_adjust->currentIndex());
    o.suffix = m_suffix->text().trimmed();
    // Remembered values of the matching filter dialog.
    static const char *ids[] = {nullptr, "enhanceColors", "grayscale", "cleanDocument", nullptr};
    if (const char *id = ids[m_adjust->currentIndex()]) {
        QSettings s;
        const filters::Spec *spec = filters::find(QLatin1String(id));
        for (const filters::Param &p : spec->params)
            o.adjustValues[p.key] = s.value(QStringLiteral("filters/%1/%2").arg(spec->id, p.key), p.def).toDouble();
    }
    return o;
}

void BatchDialog::updateState()
{
    m_startButton->setEnabled(!m_running && m_list->count() > 0 && !m_output->text().trimmed().isEmpty());
    if (!m_running)
        m_status->setText(m_list->count() ? tr("%n image(s)", nullptr, m_list->count()) : QString());
}

void BatchDialog::chooseOptions()
{
    QImage sample;
    if (m_list->count() > 0)
        sample = imageio::loadFile(m_list->item(0)->text()).image;
    if (sample.isNull()) {
        sample = QImage(400, 300, QImage::Format_ARGB32_Premultiplied);
        sample.fill(Qt::white);
    }
    imageio::SaveOptions o = m_saveOptions;
    o.format = m_format->currentData().toByteArray();
    if (!ExportDialog::hasOptions(o.format, sample))
        return;
    ExportDialog dlg(sample, o, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_saveOptions = dlg.options();
        settings::setSaveOptions(m_saveOptions);
    }
}

void BatchDialog::start()
{
    if (m_running || m_list->count() == 0)
        return;
    const batch::Options o = options();
    if (o.outputDir.isEmpty())
        return;
    QSettings settings;
    settings.setValue("batch/format", o.save.format);
    settings.setValue("batch/shrink", o.shrink);
    settings.setValue("batch/maxWidth", o.maxSize.width());
    settings.setValue("batch/maxHeight", o.maxSize.height());
    settings.setValue("batch/adjust", int(o.adjust));
    settings.setValue("batch/suffix", o.suffix);

    const QStringList inputs = files();
    m_running = true;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_progress->setRange(0, int(inputs.size()));
    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_closeButton->setText(tr("Stop"));
    updateState();

    // One worker thread, files one after another (each filter is itself parallel).
    auto cancel = m_cancel;
    QPointer<BatchDialog> self(this);
    QThread *thread = QThread::create([=] {
        int ok = 0, failed = 0;
        QStringList errors;
        for (int i = 0; i < inputs.size() && !cancel->load(); ++i) {
            QString out, error;
            if (batch::processFile(inputs[i], o, &out, &error))
                ++ok;
            else {
                ++failed;
                errors << QStringLiteral("%1: %2").arg(QFileInfo(inputs[i]).fileName(), error);
            }
            QMetaObject::invokeMethod(qApp, [self, i, name = QFileInfo(inputs[i]).fileName()] {
                if (!self)
                    return;
                self->m_progress->setValue(i + 1);
                self->m_status->setText(tr("Converted %1").arg(name));
            });
        }
        const bool stopped = cancel->load();
        QMetaObject::invokeMethod(qApp, [self, ok, failed, errors, stopped, dir = o.outputDir] {
            if (!self)
                return;
            self->m_running = false;
            self->m_closeButton->setText(tr("Close"));
            QString text = tr("%n image(s) converted into %1.", nullptr, ok).arg(dir);
            if (failed)
                text += QLatin1Char(' ') + tr("%n failed:", nullptr, failed) + QLatin1Char('\n')
                        + errors.mid(0, 10).join(QLatin1Char('\n'));
            if (stopped)
                text += QLatin1Char(' ') + tr("Stopped.");
            self->m_status->setText(text);
            self->updateState();
            emit self->finished(ok, failed);
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
