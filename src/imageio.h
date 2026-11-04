#pragma once

#include "codecs.h"

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace imageio {

struct LoadResult
{
    QImage image;          // ARGB32_Premultiplied, sRGB, devicePixelRatio 1
    QStringList notes;     // user-visible notes about conversions (spec B7)
    QString error;         // non-empty on failure
    bool ok() const { return !image.isNull(); }
};

// Must be called once at startup (spec B7: raise the 256 MB default).
void initialize();

LoadResult loadFile(const QString &path);
LoadResult loadData(const QByteArray &data, const QByteArray &formatHint = {});
// Bring any QImage into the internal format; appends notes about information loss.
QImage normalize(QImage img, QStringList *notes = nullptr);
bool isLoadableImageFile(const QString &path);

// ---- saving (spec F) ----

struct SaveOptions
{
    QByteArray format = "png";      // png | jpeg | webp | bmp | ico | tiff | avif | heif | jxl | qoi
    int pngCompression = 6;         // 0..9
    codecs::JpegOptions jpeg;
    codecs::WebpOptions webp;
    QColor background = Qt::white;  // used where the format has no alpha
    QList<int> icoSizes = {16, 32, 48, 256}; // spec F: ICO sizes
    bool tiffLzw = true;            // TIFF: LZW compression (lossless)
    int quality = 80;               // AVIF, HEIF, JPEG XL: 0..100, 100 = lossless (AVIF, JPEG XL)
};

// Formats with a quality setting through a plugin (KImageFormats).
bool hasQuality(const QByteArray &format);

// All sizes offered for ICO (spec F).
QList<int> icoSizeChoices();
// ICO with PNG-compressed entries; a non-square image is centered on a
// transparent square. Own writer: QImageWriter writes only one image.
QByteArray encodeIco(const QImage &composite, const QList<int> &sizes);

struct FormatInfo
{
    QByteArray format;     // Qt format name
    QString name;          // display name
    QStringList suffixes;  // first is the default
};

// Formats offered for export (PNG, JPG, WebP, BMP, ICO), filtered by the
// plugins actually available at runtime (spec F4).
QList<FormatInfo> exportFormats();
// Format for a filename suffix, empty if not exportable.
QByteArray formatForSuffix(const QString &suffix);
bool isLossy(const SaveOptions &o);
bool formatHasAlpha(const QByteArray &format);

// Spec F4: no suffix -> append filter's; supported suffix -> it wins;
// unsupported suffix -> append filter's. Returns the final path and format.
QString resolveSavePath(const QString &typedPath, const QByteArray &filterFormat,
                        QByteArray *formatOut);

// Spec F3: atomic write via QSaveFile. Returns false and sets *error on failure;
// the target file is left untouched in that case.
bool save(const QImage &composite, const QString &path, const SaveOptions &options,
          QString *error);
// Encode into memory (used for the JPG/WebP size estimate and preview).
QByteArray encode(const QImage &composite, const SaveOptions &options);
QImage prepareForFormat(const QImage &composite, const SaveOptions &options);

QString suggestedFileName(const QDateTime &when, const QByteArray &format);
QString nameFilter(const FormatInfo &f);

} // namespace imageio
