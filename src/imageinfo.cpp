#include "imageinfo.h"

#include "exif.h"
#include "project.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

namespace {
QString fileSize(qint64 bytes)
{
    const QLocale l;
    if (bytes < 1024)
        return QObject::tr("%1 B").arg(bytes);
    return QStringLiteral("%1 (%2 B)").arg(l.formattedDataSize(bytes, 1), l.toString(bytes));
}

QString degrees(double v, bool latitude)
{
    const QString hemi = latitude ? (v < 0 ? QStringLiteral("S") : QStringLiteral("N"))
                                  : (v < 0 ? QStringLiteral("W") : QStringLiteral("E"));
    return QStringLiteral("%1° %2").arg(QString::number(std::abs(v), 'f', 6), hemi);
}
} // namespace

ImageInfoDialog::ImageInfoDialog(const QString &path, const QImage &current, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Image Information"));
    auto *main = new QVBoxLayout(this);
    auto *content = new QWidget;
    auto *form = new QFormLayout(content);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const QLocale l;

    auto section = [&](const QString &title) {
        auto *h = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), content);
        if (form->rowCount() > 0)
            h->setContentsMargins(0, 10, 0, 0);
        form->addRow(h);
        m_text += (m_text.isEmpty() ? QString() : QStringLiteral("\n")) + QStringLiteral("[%1]\n").arg(title);
    };
    auto row = [&](const QString &label, const QString &value) {
        if (value.isEmpty())
            return;
        auto *v = new QLabel(value.toHtmlEscaped(), content);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->setWordWrap(true);
        form->addRow(label, v);
        m_text += QStringLiteral("%1 %2\n").arg(label, value);
    };

    const QFileInfo fi(path);
    QByteArray data;
    if (!path.isEmpty()) {
        section(tr("File"));
        row(tr("Name:"), fi.fileName());
        row(tr("Folder:"), QDir::toNativeSeparators(fi.absolutePath()));
        if (fi.exists()) {
            row(tr("File size:"), fileSize(fi.size()));
            row(tr("Modified:"), l.toString(fi.lastModified(), QLocale::LongFormat));
        } else {
            row(tr("File size:"), tr("the file no longer exists"));
        }
        if (project::isProjectFile(path)) {
            row(tr("Format:"), tr("Rolidan project"));
        } else {
            QImageReader reader(path);
            const QByteArray fmt = reader.format();
            if (!fmt.isEmpty())
                row(tr("Format:"), QString::fromLatin1(fmt).toUpper());
            const QSize s = reader.size();
            if (s.isValid() && s != current.size())
                row(tr("Size in file:"), tr("%1 × %2 px").arg(s.width()).arg(s.height()));
            QFile f(path);
            if (f.open(QIODevice::ReadOnly) && f.size() < 512LL * 1024 * 1024)
                data = f.read(std::min<qint64>(f.size(), 64LL * 1024 * 1024));
        }
    }

    section(tr("Image"));
    if (current.isNull()) {
        row(tr("Size:"), tr("no image"));
    } else {
        row(tr("Size:"), tr("%1 × %2 px").arg(current.width()).arg(current.height()));
        row(tr("Megapixels:"), l.toString(current.width() * double(current.height()) / 1e6, 'f', 2));
        const int dpi = int(std::lround(current.dotsPerMeterX() * 0.0254));
        if (dpi > 0 && dpi != 96) // Qt's default 96 dpi usually means "not set"
            row(tr("Resolution:"), tr("%1 dpi").arg(dpi));
        // Documents are always stored with alpha: look at the pixels themselves.
        bool transparent = false;
        if (current.hasAlphaChannel()) {
            const QImage argb = current.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < argb.height() && !transparent; ++y) {
                const auto *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
                for (int x = 0; x < argb.width(); ++x)
                    if (qAlpha(line[x]) != 255) {
                        transparent = true;
                        break;
                    }
            }
        }
        row(tr("Transparency:"), transparent ? tr("yes") : tr("no"));
        if (current.isGrayscale())
            row(tr("Colors:"), tr("grayscale"));
    }

    const exif::Info e = exif::read(data);
    if (e.valid) {
        const bool camera = !e.make.isEmpty() || !e.model.isEmpty() || e.exposure > 0 || e.fNumber > 0
                            || e.iso > 0 || !e.dateTaken.isEmpty();
        if (camera) {
            section(tr("Camera"));
            QString cam = e.model;
            if (!e.make.isEmpty() && !e.model.startsWith(e.make, Qt::CaseInsensitive))
                cam = (e.make + QLatin1Char(' ') + e.model).trimmed();
            row(tr("Camera:"), cam);
            row(tr("Lens:"), e.lens);
            if (!e.dateTaken.isEmpty()) {
                const QDateTime dt = QDateTime::fromString(e.dateTaken, QStringLiteral("yyyy:MM:dd HH:mm:ss"));
                row(tr("Taken:"), dt.isValid() ? l.toString(dt, QLocale::LongFormat) : e.dateTaken);
            }
            row(tr("Exposure:"), exif::formatExposure(e.exposure));
            if (e.fNumber > 0)
                row(tr("Aperture:"), QStringLiteral("f/%1").arg(l.toString(e.fNumber, 'g', 3)));
            if (e.iso > 0)
                row(tr("ISO:"), QString::number(e.iso));
            if (e.focalLength > 0) {
                QString focal = tr("%1 mm").arg(l.toString(e.focalLength, 'g', 4));
                if (e.focalLength35 > 0 && std::abs(e.focalLength35 - e.focalLength) > 0.5)
                    focal += QLatin1Char(' ') + tr("(%1 mm in 35 mm format)").arg(e.focalLength35);
                row(tr("Focal length:"), focal);
            }
            if (e.exposureBias != 0)
                row(tr("Exposure bias:"), tr("%1 EV").arg(l.toString(e.exposureBias, 'f', 1)));
            if (e.flash >= 0)
                row(tr("Flash:"), (e.flash & 1) ? tr("fired") : tr("did not fire"));
        }
        if (!e.software.isEmpty() || !e.artist.isEmpty() || !e.copyright.isEmpty() || !e.description.isEmpty()) {
            section(tr("Details"));
            row(tr("Description:"), e.description);
            row(tr("Author:"), e.artist);
            row(tr("Copyright:"), e.copyright);
            row(tr("Software:"), e.software);
        }
        if (e.hasGps) {
            section(tr("Location"));
            row(tr("Latitude:"), degrees(e.latitude, true));
            row(tr("Longitude:"), degrees(e.longitude, false));
            if (e.hasAltitude)
                row(tr("Altitude:"), tr("%1 m").arg(l.toString(e.altitude, 'f', 0)));
            m_mapUrl = QStringLiteral("https://www.openstreetmap.org/?mlat=%1&mlon=%2#map=16/%1/%2")
                           .arg(e.latitude, 0, 'f', 6)
                           .arg(e.longitude, 0, 'f', 6);
            auto *note = new QLabel(tr("The photo contains its location. Rolidan does not copy metadata "
                                       "into exported images, but the original file still has it."),
                                    content);
            note->setWordWrap(true);
            note->setEnabled(false);
            form->addRow(note);
        }
    } else if (!data.isEmpty()) {
        section(tr("Camera"));
        row(tr("EXIF:"), tr("none"));
    }

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    main->addWidget(scroll, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy = buttons->addButton(tr("Copy as Text"), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_text); });
    if (!m_mapUrl.isEmpty()) {
        auto *map = buttons->addButton(tr("Show on Map"), QDialogButtonBox::ActionRole);
        map->setObjectName(QStringLiteral("showOnMap"));
        map->setToolTip(tr("Opens OpenStreetMap in the web browser"));
        connect(map, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(m_mapUrl)); });
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main->addWidget(buttons);
    content->adjustSize();
    resize(480, std::min(720, content->heightForWidth(460) > 0 ? content->heightForWidth(460) + 110
                                                               : content->sizeHint().height() + 110));
}
