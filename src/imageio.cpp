#include "imageio.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <algorithm>

namespace imageio {


void initialize()
{
    // Default Qt 6 limit is 256 MB which silently rejects images above
    // roughly 8000x8000. 2 GB covers 16384x16384 ARGB32 (spec G2).
    QImageReader::setAllocationLimit(2048);
}

static bool isHighBitDepth(QImage::Format f)
{
    switch (f) {
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
    case QImage::Format_Grayscale16:
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        return true;
    default:
        return false;
    }
}

QImage normalize(QImage img, QStringList *notes)
{
    if (img.isNull())
        return img;
    bool converted = false;
    if (isHighBitDepth(img.format()))
        converted = true;
    const QColorSpace cs = img.colorSpace();
    if (cs.isValid() && cs != QColorSpace(QColorSpace::SRgb)) {
        img.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
        converted = true;
    }
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    img.setColorSpace(QColorSpace());
    img.setDevicePixelRatio(1.0); // spec B2: always use pixel dimensions
    if (converted && notes)
        notes->append(QCoreApplication::translate("imageio", "Converted to 8 bits per channel / sRGB"));
    return img;
}

static LoadResult readFrom(QImageReader &reader)
{
    LoadResult r;
    reader.setAutoTransform(true); // EXIF orientation (spec B7)
    const int frames = reader.imageCount();
    QImage img = reader.read();
    if (img.isNull()) {
        r.error = reader.error() == QImageReader::UnsupportedFormatError
                      ? QCoreApplication::translate("imageio", "Unsupported or unknown image format")
                      : QCoreApplication::translate("imageio", "The image could not be read: %1").arg(reader.errorString());
        return r;
    }
    r.image = normalize(std::move(img), &r.notes);
    if (frames > 1)
        r.notes.append(QCoreApplication::translate("imageio", "Animated image: only the first frame was loaded"));
    return r;
}

LoadResult loadFile(const QString &path)
{
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    LoadResult r = readFrom(reader);
    if (!r.ok() && !QFileInfo::exists(path))
        r.error = QCoreApplication::translate("imageio", "File not found: %1").arg(path);
    return r;
}

LoadResult loadData(const QByteArray &data, const QByteArray &formatHint)
{
    QByteArray copy = data;
    QBuffer buf(&copy);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf, formatHint);
    reader.setDecideFormatFromContent(true);
    return readFrom(reader);
}

bool isLoadableImageFile(const QString &path)
{
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    return QFileInfo(path).isFile() && reader.canRead();
}

// ---- saving ----

QList<FormatInfo> exportFormats()
{
    const QList<FormatInfo> all = {
        {"png", QStringLiteral("PNG"), {QStringLiteral("png")}},
        {"jpeg", QStringLiteral("JPEG"), {QStringLiteral("jpg"), QStringLiteral("jpeg")}},
        {"webp", QStringLiteral("WebP"), {QStringLiteral("webp")}},
        {"bmp", QStringLiteral("BMP"), {QStringLiteral("bmp")}},
        {"ico", QStringLiteral("ICO"), {QStringLiteral("ico")}},
    };
    const QList<QByteArray> supported = QImageWriter::supportedImageFormats();
    QList<FormatInfo> out;
    for (const auto &f : all) {
        // ICO uses our own writer (PNG entries), so it only needs PNG.
        const QByteArray needed = f.format == "ico" ? QByteArray("png") : f.format;
        if (supported.contains(needed))
            out.append(f);
    }
    return out;
}

QByteArray formatForSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    for (const auto &f : exportFormats()) {
        if (f.suffixes.contains(s))
            return f.format;
    }
    return {};
}

static QString defaultSuffix(const QByteArray &format)
{
    for (const auto &f : exportFormats()) {
        if (f.format == format)
            return f.suffixes.first();
    }
    return QStringLiteral("png");
}

bool isLossy(const SaveOptions &o)
{
    return o.format == "jpeg" || (o.format == "webp" && !o.webp.lossless);
}

bool formatHasAlpha(const QByteArray &format)
{
    return format == "png" || format == "webp" || format == "ico";
}

QString resolveSavePath(const QString &typedPath, const QByteArray &filterFormat,
                        QByteArray *formatOut)
{
    const QFileInfo fi(typedPath);
    const QString suffix = fi.suffix();
    QString path = typedPath;
    QByteArray format = filterFormat;
    if (suffix.isEmpty()) {
        if (path.endsWith(QLatin1Char('.')))
            path.chop(1);
        path += QLatin1Char('.') + defaultSuffix(filterFormat);
    } else if (const QByteArray f = formatForSuffix(suffix); !f.isEmpty()) {
        format = f;
    } else {
        path += QLatin1Char('.') + defaultSuffix(filterFormat);
    }
    if (formatOut)
        *formatOut = format;
    return path;
}

QImage prepareForFormat(const QImage &composite, const SaveOptions &o)
{
    if (formatHasAlpha(o.format))
        return composite.convertToFormat(QImage::Format_ARGB32);
    // Flatten transparency onto the chosen background (spec F5).
    QImage out(composite.size(), QImage::Format_RGB32);
    out.fill(o.background.isValid() ? o.background : QColor(Qt::white));
    QPainter p(&out);
    p.drawImage(0, 0, composite);
    p.end();
    return out;
}

static void configure(QImageWriter &w, const SaveOptions &o)
{
    if (o.format == "png") {
        // Qt's PNG handler derives the zlib level from quality: level = (100 - q) * 9 / 91.
        const int c = qBound(0, o.pngCompression, 9);
        w.setQuality(100 - (c * 91 + 8) / 9);
    } else if (o.format == "jpeg") {
        w.setQuality(qBound(0, o.jpeg.quality, 100));
        w.setOptimizedWrite(o.jpeg.optimize);
        w.setProgressiveScanWrite(o.jpeg.progressive);
    } else if (o.format == "webp") {
        w.setQuality(o.webp.lossless ? 100 : qBound(0, o.webp.quality, 99));
    }
}

QList<int> icoSizeChoices()
{
    return {16, 24, 32, 48, 64, 128, 256};
}

static void put16(QByteArray &b, quint16 v)
{
    b.append(char(v & 0xff));
    b.append(char(v >> 8));
}

static void put32(QByteArray &b, quint32 v)
{
    put16(b, quint16(v & 0xffff));
    put16(b, quint16(v >> 16));
}

QByteArray encodeIco(const QImage &composite, const QList<int> &sizesIn)
{
    QList<int> sizes;
    for (int s : sizesIn) {
        if (s >= 1 && s <= 256 && !sizes.contains(s))
            sizes.append(s);
    }
    std::sort(sizes.begin(), sizes.end());
    if (sizes.isEmpty() || composite.isNull())
        return {};
    // Center on a transparent square first (spec F).
    const int side = std::max(composite.width(), composite.height());
    QImage square(side, side, QImage::Format_ARGB32_Premultiplied);
    square.fill(Qt::transparent);
    {
        QPainter p(&square);
        p.drawImage((side - composite.width()) / 2, (side - composite.height()) / 2, composite);
    }
    QList<QByteArray> pngs;
    for (int s : sizes) {
        const QImage img = square.scaled(s, s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_ARGB32);
        QByteArray data;
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        QImageWriter w(&buf, "png");
        if (!w.write(img))
            return {};
        pngs.append(data);
    }
    QByteArray out;
    put16(out, 0);                 // reserved
    put16(out, 1);                 // type: icon
    put16(out, quint16(sizes.size()));
    quint32 offset = 6 + 16 * quint32(sizes.size());
    for (int i = 0; i < sizes.size(); ++i) {
        const int s = sizes[i];
        out.append(char(s >= 256 ? 0 : s)); // width, 0 = 256
        out.append(char(s >= 256 ? 0 : s)); // height
        out.append(char(0));                // palette colors
        out.append(char(0));                // reserved
        put16(out, 1);                      // color planes
        put16(out, 32);                     // bits per pixel
        put32(out, quint32(pngs[i].size()));
        put32(out, offset);
        offset += quint32(pngs[i].size());
    }
    for (const QByteArray &png : pngs)
        out.append(png);
    return out;
}

// Formats written by our own encoders instead of a Qt image plugin.
static bool encodedNatively(const QByteArray &format)
{
    return format == "ico" || (format == "jpeg" && codecs::haveJpeg()) || (format == "webp" && codecs::haveWebp());
}

QByteArray encode(const QImage &composite, const SaveOptions &o)
{
    if (o.format == "ico")
        return encodeIco(composite, o.icoSizes);
    if (o.format == "jpeg" && codecs::haveJpeg())
        return codecs::encodeJpeg(prepareForFormat(composite, o), o.jpeg);
    if (o.format == "webp" && codecs::haveWebp())
        return codecs::encodeWebp(prepareForFormat(composite, o), o.webp);
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, o.format);
    configure(w, o);
    if (!w.write(prepareForFormat(composite, o)))
        return {};
    return data;
}

bool save(const QImage &composite, const QString &path, const SaveOptions &o, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QCoreApplication::translate("imageio", "Cannot write to %1: %2").arg(path, file.errorString());
        return false;
    }
    if (encodedNatively(o.format)) {
        const QByteArray data = encode(composite, o);
        if (data.isEmpty() || file.write(data) != data.size()) {
            file.cancelWriting();
            if (error)
                *error = QCoreApplication::translate("imageio", "Encoding as %1 failed: %2")
                             .arg(QString::fromLatin1(o.format).toUpper(), file.errorString());
            return false;
        }
        if (!file.commit()) {
            if (error)
                *error = QCoreApplication::translate("imageio", "Cannot write to %1: %2").arg(path, file.errorString());
            return false;
        }
        return true;
    }
    QImageWriter w(&file, o.format);
    configure(w, o);
    if (!w.write(prepareForFormat(composite, o))) {
        file.cancelWriting();
        if (error)
            *error = QCoreApplication::translate("imageio", "Encoding as %1 failed: %2").arg(QString::fromLatin1(o.format).toUpper(),
                                                          w.errorString());
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QCoreApplication::translate("imageio", "Cannot write to %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString suggestedFileName(const QDateTime &when, const QByteArray &format)
{
    // Language-neutral on purpose (spec L).
    return when.toString(QStringLiteral("'image-'yyyy-MM-dd-HHmmss")) + QLatin1Char('.')
           + defaultSuffix(format);
}

QString nameFilter(const FormatInfo &f)
{
    QStringList globs;
    for (const auto &s : f.suffixes)
        globs << QStringLiteral("*.") + s;
    return QStringLiteral("%1 (%2)").arg(f.name, globs.join(QLatin1Char(' ')));
}

} // namespace imageio
