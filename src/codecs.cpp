#include "codecs.h"

#include <QtGlobal>
#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>

#ifdef ROLIDAN_HAVE_JPEG
#include <jerror.h>
#include <jpeglib.h>
#endif
#ifdef ROLIDAN_HAVE_WEBP
#include <webp/encode.h>
#endif

namespace codecs {

// ================================================================ JPEG

#ifdef ROLIDAN_HAVE_JPEG
namespace {
struct ErrorManager
{
    jpeg_error_mgr pub;
    std::jmp_buf jump;
};

[[noreturn]] void errorExit(j_common_ptr cinfo)
{
    auto *err = reinterpret_cast<ErrorManager *>(cinfo->err);
    std::longjmp(err->jump, 1);
}

void silentMessage(j_common_ptr) {}
} // namespace

bool haveJpeg() { return true; }

QByteArray encodeJpeg(const QImage &imageIn, const JpegOptions &o)
{
    const QImage image = imageIn.format() == QImage::Format_RGB32 ? imageIn
                                                                    : imageIn.convertToFormat(QImage::Format_RGB32);
    if (image.isNull())
        return {};

    jpeg_compress_struct cinfo{};
    ErrorManager err{};
    unsigned char *buffer = nullptr;
    unsigned long size = 0;
    cinfo.err = jpeg_std_error(&err.pub);
    err.pub.error_exit = errorExit;
    err.pub.output_message = silentMessage;
    // `buffer` and `size` live in memory (their address is passed on), so they
    // are valid after longjmp.
    if (setjmp(err.jump)) {
        jpeg_destroy_compress(&cinfo);
        std::free(buffer);
        return {};
    }
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &buffer, &size);
    cinfo.image_width = JDIMENSION(image.width());
    cinfo.image_height = JDIMENSION(image.height());
    // Format_RGB32 is 0xffRRGGBB per pixel: bytes B, G, R, X on little endian.
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    cinfo.in_color_space = JCS_EXT_BGRX;
#else
    cinfo.in_color_space = JCS_EXT_XRGB;
#endif
    cinfo.input_components = 4;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, std::clamp(o.quality, 1, 100), TRUE);
    static const int factors[3][2] = {{1, 1}, {2, 1}, {2, 2}};
    const int s = std::clamp(o.subsampling, 0, 2);
    cinfo.comp_info[0].h_samp_factor = factors[s][0];
    cinfo.comp_info[0].v_samp_factor = factors[s][1];
    for (int c = 1; c < 3; ++c) {
        cinfo.comp_info[c].h_samp_factor = 1;
        cinfo.comp_info[c].v_samp_factor = 1;
    }
    cinfo.optimize_coding = o.optimize ? TRUE : FALSE;
    cinfo.smoothing_factor = std::clamp(o.smoothing, 0, 100);
    cinfo.dct_method = JDCT_ISLOW; // the most accurate
    if (o.progressive)
        jpeg_simple_progression(&cinfo);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(image.constScanLine(int(cinfo.next_scanline)));
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    QByteArray out(reinterpret_cast<const char *>(buffer), qsizetype(size));
    std::free(buffer);
    return out;
}
#else
bool haveJpeg() { return false; }
QByteArray encodeJpeg(const QImage &, const JpegOptions &) { return {}; }
#endif

// ================================================================ WebP

#ifdef ROLIDAN_HAVE_WEBP
bool haveWebp() { return true; }

QByteArray encodeWebp(const QImage &imageIn, const WebpOptions &o)
{
    const QImage image = imageIn.format() == QImage::Format_ARGB32 ? imageIn
                                                                     : imageIn.convertToFormat(QImage::Format_ARGB32);
    if (image.isNull() || image.width() > WEBP_MAX_DIMENSION || image.height() > WEBP_MAX_DIMENSION)
        return {};

    WebPConfig config;
    static const WebPPreset presets[] = {WEBP_PRESET_DEFAULT, WEBP_PRESET_PICTURE, WEBP_PRESET_PHOTO,
                                         WEBP_PRESET_DRAWING, WEBP_PRESET_ICON, WEBP_PRESET_TEXT};
    if (!WebPConfigPreset(&config, presets[std::clamp(o.preset, 0, 5)], float(std::clamp(o.quality, 0, 100))))
        return {};
    if (o.lossless) {
        if (!WebPConfigLosslessPreset(&config, std::clamp(o.losslessLevel, 0, 9)))
            return {};
        config.near_lossless = std::clamp(o.nearLossless, 0, 100);
    } else {
        config.method = std::clamp(o.method, 0, 6);
        config.alpha_quality = std::clamp(o.alphaQuality, 0, 100);
        config.use_sharp_yuv = o.sharpYuv ? 1 : 0;
    }
    config.exact = o.exact ? 1 : 0;
    config.thread_level = 1;
    if (!WebPValidateConfig(&config))
        return {};

    WebPPicture pic;
    if (!WebPPictureInit(&pic))
        return {};
    pic.width = image.width();
    pic.height = image.height();
    pic.use_argb = 1; // converted to YUV by the encoder when lossy (honours use_sharp_yuv)
    // Format_ARGB32 is 0xAARRGGBB per pixel: bytes B, G, R, A on little endian.
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    const int imported = WebPPictureImportBGRA(&pic, image.constBits(), int(image.bytesPerLine()));
#else
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    const int imported = WebPPictureImportRGBA(&pic, rgba.constBits(), int(rgba.bytesPerLine()));
#endif
    if (!imported) {
        WebPPictureFree(&pic);
        return {};
    }
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &writer;
    const int ok = WebPEncode(&config, &pic);
    WebPPictureFree(&pic);
    QByteArray out;
    if (ok)
        out = QByteArray(reinterpret_cast<const char *>(writer.mem), qsizetype(writer.size));
    WebPMemoryWriterClear(&writer);
    return out;
}
#else
bool haveWebp() { return false; }
QByteArray encodeWebp(const QImage &, const WebpOptions &) { return {}; }
#endif

} // namespace codecs
