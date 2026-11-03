#pragma once

#include <QByteArray>
#include <QImage>

// Direct libjpeg / libwebp encoders (spec F5): Qt's image plugins expose only
// the quality, not chroma subsampling, progressive scans, WebP presets etc.
// When a library is not available at build time, the Qt plugin is used and
// the extra options are ignored.
namespace codecs {

struct JpegOptions
{
    int quality = 90;          // 1..100
    int subsampling = 0;       // 0 = 4:4:4 (screenshots: sharp colored text), 1 = 4:2:2, 2 = 4:2:0
    bool progressive = false;
    bool optimize = true;      // optimized Huffman tables
    int smoothing = 0;         // 0..100
};

struct WebpOptions
{
    bool lossless = false;
    int quality = 90;          // lossy: 0..100
    int preset = 0;            // 0 default, 1 picture, 2 photo, 3 drawing, 4 icon, 5 text
    int method = 4;            // lossy effort 0 (fast) .. 6 (slow, smaller)
    int alphaQuality = 100;    // lossy: 0..100
    bool sharpYuv = false;     // lossy: sharper RGB→YUV conversion
    int losslessLevel = 6;     // lossless: 0 (fast) .. 9 (smallest)
    int nearLossless = 100;    // lossless: 100 = off, lower = smaller with tiny changes
    bool exact = false;        // keep RGB values under fully transparent pixels
};

bool haveJpeg();
bool haveWebp();

// `image` must be Format_RGB32 (JPEG) or Format_ARGB32 (WebP). Returns an
// empty array on failure or when the library is not available.
QByteArray encodeJpeg(const QImage &image, const JpegOptions &o);
QByteArray encodeWebp(const QImage &image, const WebpOptions &o);

} // namespace codecs
