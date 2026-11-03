#include "filters.h"

#include <QCoreApplication>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <thread>
#include <vector>

namespace filters {

namespace {

// ================================================================ helpers

// Runs fn(begin, end) over [0, count) split into bands, one per CPU core.
template <typename F>
void parallel(int count, F fn, int minPerBand = 16)
{
    const int cores = std::max(1u, std::thread::hardware_concurrency());
    const int bands = std::clamp(count / std::max(1, minPerBand), 1, cores);
    if (bands <= 1) {
        fn(0, count);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(size_t(bands - 1));
    for (int b = 1; b < bands; ++b)
        threads.emplace_back([=] { fn(count * b / bands, count * (b + 1) / bands); });
    fn(0, count / bands);
    for (auto &t : threads)
        t.join();
}

inline int clamp255(double v) { return v <= 0 ? 0 : v >= 255 ? 255 : int(v + 0.5); }
inline int clamp255i(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

QImage straight(const QImage &src) { return src.convertToFormat(QImage::Format_ARGB32); }
QImage premultiplied(const QImage &src) { return src.convertToFormat(QImage::Format_ARGB32_Premultiplied); }

// Per-pixel color operation on unpremultiplied RGB (alpha kept).
template <typename F>
QImage perPixel(const QImage &src, F fn)
{
    QImage img = straight(src);
    if (img.isNull())
        return src;
    img.bits(); // detach before the threads write
    const int w = img.width();
    parallel(img.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb p = line[x];
                if (qAlpha(p) == 0)
                    continue;
                int r = qRed(p), g = qGreen(p), b = qBlue(p);
                fn(r, g, b);
                line[x] = qRgba(r, g, b, qAlpha(p));
            }
        }
    });
    return premultiplied(img);
}

using Lut = std::array<quint8, 256>;

QImage applyLuts(const QImage &src, const Lut &lr, const Lut &lg, const Lut &lb)
{
    return perPixel(src, [&](int &r, int &g, int &b) {
        r = lr[r];
        g = lg[g];
        b = lb[b];
    });
}

template <typename F>
Lut makeLut(F fn)
{
    Lut lut;
    for (int i = 0; i < 256; ++i)
        lut[size_t(i)] = quint8(clamp255(fn(i / 255.0) * 255.0));
    return lut;
}

inline double luma(int r, int g, int b) { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }

// ---- box blur on 4 interleaved channels (premultiplied ARGB32) ----

void boxBlurH(const QImage &src, QImage &dst, int r)
{
    const int w = src.width();
    const int n = 2 * r + 1;
    parallel(src.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const quint8 *s = src.constScanLine(y);
            quint8 *d = dst.scanLine(y);
            for (int c = 0; c < 4; ++c) {
                quint32 sum = 0;
                for (int i = -r; i <= r; ++i)
                    sum += s[std::clamp(i, 0, w - 1) * 4 + c];
                for (int x = 0; x < w; ++x) {
                    d[x * 4 + c] = quint8((sum + quint32(n / 2)) / quint32(n));
                    sum += s[std::min(x + r + 1, w - 1) * 4 + c];
                    sum -= s[std::max(x - r, 0) * 4 + c];
                }
            }
        }
    });
}

void boxBlurV(const QImage &src, QImage &dst, int r)
{
    const int w = src.width(), h = src.height();
    const int n = 2 * r + 1;
    parallel(w, [&](int x0, int x1) {
        const int cols = (x1 - x0) * 4;
        std::vector<quint32> sum(size_t(cols), 0);
        for (int i = -r; i <= r; ++i) {
            const quint8 *s = src.constScanLine(std::clamp(i, 0, h - 1)) + x0 * 4;
            for (int k = 0; k < cols; ++k)
                sum[size_t(k)] += s[k];
        }
        for (int y = 0; y < h; ++y) {
            quint8 *d = dst.scanLine(y) + x0 * 4;
            for (int k = 0; k < cols; ++k)
                d[k] = quint8((sum[size_t(k)] + quint32(n / 2)) / quint32(n));
            const quint8 *add = src.constScanLine(std::min(y + r + 1, h - 1)) + x0 * 4;
            const quint8 *sub = src.constScanLine(std::max(y - r, 0)) + x0 * 4;
            for (int k = 0; k < cols; ++k)
                sum[size_t(k)] += quint32(add[k]) - quint32(sub[k]);
        }
    }, 8);
}

// Box sizes approximating a Gaussian of `sigma` with three passes.
std::array<int, 3> gaussBoxes(double sigma)
{
    const double ideal = std::sqrt(12.0 * sigma * sigma / 3.0 + 1.0);
    int wl = int(std::floor(ideal));
    if (wl % 2 == 0)
        --wl;
    const int wu = wl + 2;
    const double mIdeal = (12.0 * sigma * sigma - 3.0 * wl * wl - 12.0 * wl - 9.0) / (-4.0 * wl - 4.0);
    const int m = int(std::lround(mIdeal));
    std::array<int, 3> radii{};
    for (int i = 0; i < 3; ++i)
        radii[size_t(i)] = std::max(0, ((i < m ? wl : wu) - 1) / 2);
    return radii;
}

// Separable max filter (dilation of light areas) on RGB32.
QImage maxFilter(const QImage &src, int r)
{
    QImage tmp(src.size(), QImage::Format_RGB32), out(src.size(), QImage::Format_RGB32);
    const int w = src.width(), h = src.height();
    for (int y = 0; y < h; ++y) {
        const auto *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        auto *d = reinterpret_cast<QRgb *>(tmp.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int mr = 0, mg = 0, mb = 0;
            for (int i = std::max(0, x - r); i <= std::min(w - 1, x + r); ++i) {
                mr = std::max(mr, qRed(s[i]));
                mg = std::max(mg, qGreen(s[i]));
                mb = std::max(mb, qBlue(s[i]));
            }
            d[x] = qRgb(mr, mg, mb);
        }
    }
    for (int y = 0; y < h; ++y) {
        auto *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int mr = 0, mg = 0, mb = 0;
            for (int i = std::max(0, y - r); i <= std::min(h - 1, y + r); ++i) {
                const QRgb p = reinterpret_cast<const QRgb *>(tmp.constScanLine(i))[x];
                mr = std::max(mr, qRed(p));
                mg = std::max(mg, qGreen(p));
                mb = std::max(mb, qBlue(p));
            }
            d[x] = qRgb(mr, mg, mb);
        }
    }
    return out;
}

// Deterministic per-pixel random numbers (stable previews, thread-safe).
inline quint32 hash(quint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline double gaussian(quint32 seed)
{
    // Irwin–Hall with 4 uniforms, scaled to unit variance.
    double sum = 0;
    for (int i = 0; i < 4; ++i) {
        seed = hash(seed + 0x9e3779b9U);
        sum += (seed & 0xffffff) / double(0x1000000);
    }
    return (sum - 2.0) * 1.7320508;
}

QColor fillColor(int fill)
{
    switch (fill) {
    case 1: return Qt::white;
    case 2: return Qt::black;
    default: return Qt::transparent;
    }
}

QSizeF largestInscribed(double w, double h, double radians)
{
    if (w <= 0 || h <= 0)
        return {0, 0};
    const bool wide = w >= h;
    const double longSide = wide ? w : h, shortSide = wide ? h : w;
    const double s = std::abs(std::sin(radians)), c = std::abs(std::cos(radians));
    if (shortSide <= 2.0 * s * c * longSide || std::abs(s - c) < 1e-10) {
        const double x = 0.5 * shortSide;
        return wide ? QSizeF(x / s, x / c) : QSizeF(x / c, x / s);
    }
    const double cos2 = c * c - s * s;
    return {(w * c - h * s) / cos2, (h * c - w * s) / cos2};
}

int blurMargin(const Values &v) { return int(std::ceil(v.value(QStringLiteral("radius")) * 1.6)) + 2; }

} // namespace

// ================================================================ operations

QImage brightnessContrast(const QImage &src, double brightness, double contrast)
{
    const double b = std::clamp(brightness, -100.0, 100.0) / 100.0;
    const double c = std::clamp(contrast, -100.0, 100.0) / 100.0;
    const double slant = std::tan((std::min(c, 0.99) + 1.0) * M_PI / 4.0);
    const Lut lut = makeLut([&](double x) {
        x = b < 0 ? x * (1.0 + b) : x + (1.0 - x) * b;
        return (x - 0.5) * slant + 0.5;
    });
    return applyLuts(src, lut, lut, lut);
}

QImage hueSaturation(const QImage &src, double hue, double saturation, double lightness)
{
    const double a = hue * M_PI / 180.0, cs = std::cos(a), sn = std::sin(a);
    const double h[3][3] = {
        {0.213 + cs * 0.787 - sn * 0.213, 0.715 - cs * 0.715 - sn * 0.715, 0.072 - cs * 0.072 + sn * 0.928},
        {0.213 - cs * 0.213 + sn * 0.143, 0.715 + cs * 0.285 + sn * 0.140, 0.072 - cs * 0.072 - sn * 0.283},
        {0.213 - cs * 0.213 - sn * 0.787, 0.715 - cs * 0.715 + sn * 0.715, 0.072 + cs * 0.928 + sn * 0.072}};
    const double s = 1.0 + std::clamp(saturation, -100.0, 100.0) / 100.0;
    const double sat[3][3] = {{0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s},
                              {0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s},
                              {0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s}};
    double m[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[i][j] = sat[i][0] * h[0][j] + sat[i][1] * h[1][j] + sat[i][2] * h[2][j];
    const double l = std::clamp(lightness, -100.0, 100.0) / 100.0;
    const Lut light = makeLut([&](double x) { return l < 0 ? x * (1.0 + l) : x + (1.0 - x) * l; });
    return perPixel(src, [&](int &r, int &g, int &b) {
        const int nr = clamp255(m[0][0] * r + m[0][1] * g + m[0][2] * b);
        const int ng = clamp255(m[1][0] * r + m[1][1] * g + m[1][2] * b);
        const int nb = clamp255(m[2][0] * r + m[2][1] * g + m[2][2] * b);
        r = light[size_t(nr)];
        g = light[size_t(ng)];
        b = light[size_t(nb)];
    });
}

QImage enhanceColors(const QImage &src, double strength, double vibrance, double clipPercent)
{
    const QImage img = straight(src);
    // Histograms of visible pixels.
    std::array<std::array<qint64, 256>, 3> hist{};
    qint64 total = 0;
    for (int y = 0; y < img.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) < 128)
                continue;
            ++hist[0][size_t(qRed(line[x]))];
            ++hist[1][size_t(qGreen(line[x]))];
            ++hist[2][size_t(qBlue(line[x]))];
            ++total;
        }
    }
    const double k = std::clamp(strength, 0.0, 100.0) / 100.0;
    std::array<Lut, 3> luts;
    for (int c = 0; c < 3; ++c) {
        const qint64 clip = qint64(total * std::clamp(clipPercent, 0.0, 20.0) / 100.0);
        int lo = 0, hi = 255;
        qint64 acc = 0;
        while (lo < 255 && acc + hist[size_t(c)][size_t(lo)] <= clip)
            acc += hist[size_t(c)][size_t(lo++)];
        acc = 0;
        while (hi > 0 && acc + hist[size_t(c)][size_t(hi)] <= clip)
            acc += hist[size_t(c)][size_t(hi--)];
        for (int i = 0; i < 256; ++i) {
            const double stretched = hi - lo < 8 ? i : (i - lo) * 255.0 / (hi - lo);
            luts[size_t(c)][size_t(i)] = quint8(clamp255(i + (stretched - i) * k));
        }
    }
    const double vib = std::clamp(vibrance, -100.0, 100.0) / 100.0;
    return perPixel(img, [&](int &r, int &g, int &b) {
        r = luts[0][size_t(r)];
        g = luts[1][size_t(g)];
        b = luts[2][size_t(b)];
        if (vib != 0.0) {
            const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
            const double amount = vib * (1.0 - (mx - mn) / 255.0); // dull colors get more
            const double y = luma(r, g, b);
            r = clamp255(y + (r - y) * (1.0 + amount));
            g = clamp255(y + (g - y) * (1.0 + amount));
            b = clamp255(y + (b - y) * (1.0 + amount));
        }
    });
}

QImage temperature(const QImage &src, double temp, double tint)
{
    const double t = std::clamp(temp, -100.0, 100.0) / 100.0;
    const double k = std::clamp(tint, -100.0, 100.0) / 100.0;
    const double gr = (1.0 + 0.25 * t) * (1.0 + 0.1 * k);
    const double gg = 1.0 - 0.2 * k;
    const double gb = (1.0 - 0.25 * t) * (1.0 + 0.1 * k);
    return applyLuts(src, makeLut([&](double x) { return x * gr; }), makeLut([&](double x) { return x * gg; }),
                     makeLut([&](double x) { return x * gb; }));
}

QImage levels(const QImage &src, int black, int white, double gamma)
{
    black = std::clamp(black, 0, 254);
    white = std::clamp(white, black + 1, 255);
    const double g = std::clamp(gamma, 0.1, 10.0);
    const Lut lut = makeLut([&](double x) {
        const double v = std::clamp((x * 255.0 - black) / (white - black), 0.0, 1.0);
        return std::pow(v, 1.0 / g);
    });
    return applyLuts(src, lut, lut, lut);
}

QImage grayscale(const QImage &src)
{
    return perPixel(src, [](int &r, int &g, int &b) { r = g = b = clamp255(luma(r, g, b)); });
}

QImage sepia(const QImage &src, double strength)
{
    const double k = std::clamp(strength, 0.0, 100.0) / 100.0;
    return perPixel(src, [k](int &r, int &g, int &b) {
        const double sr = 0.393 * r + 0.769 * g + 0.189 * b;
        const double sg = 0.349 * r + 0.686 * g + 0.168 * b;
        const double sb = 0.272 * r + 0.534 * g + 0.131 * b;
        r = clamp255(r + (sr - r) * k);
        g = clamp255(g + (sg - g) * k);
        b = clamp255(b + (sb - b) * k);
    });
}

QImage invert(const QImage &src)
{
    return perPixel(src, [](int &r, int &g, int &b) {
        r = 255 - r;
        g = 255 - g;
        b = 255 - b;
    });
}

QImage threshold(const QImage &src, int level)
{
    return perPixel(src, [level](int &r, int &g, int &b) { r = g = b = luma(r, g, b) >= level ? 255 : 0; });
}

QImage posterize(const QImage &src, int n)
{
    n = std::clamp(n, 2, 64);
    const Lut lut = makeLut([n](double x) { return std::round(x * (n - 1)) / (n - 1); });
    return applyLuts(src, lut, lut, lut);
}

QImage gaussianBlur(const QImage &srcIn, double radius)
{
    const double sigma = radius / 2.0;
    if (sigma < 0.3 || srcIn.isNull())
        return srcIn;
    QImage a = premultiplied(srcIn);
    a.bits(); // detach before the threads write
    QImage b(a.size(), QImage::Format_ARGB32_Premultiplied);
    for (int r : gaussBoxes(sigma)) {
        if (r <= 0)
            continue;
        boxBlurH(a, b, r);
        boxBlurV(b, a, r);
    }
    return a;
}

QImage sharpen(const QImage &srcIn, double amountPercent, double radius, int thresholdLevel)
{
    const QImage src = premultiplied(srcIn);
    const QImage blurred = gaussianBlur(src, radius);
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    const double amount = std::max(0.0, amountPercent) / 100.0;
    const int w = src.width();
    parallel(src.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const auto *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            const auto *bl = reinterpret_cast<const QRgb *>(blurred.constScanLine(y));
            auto *d = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int a = qAlpha(s[x]);
                auto ch = [&](int sv, int bv) {
                    const int diff = sv - bv;
                    if (std::abs(diff) < thresholdLevel)
                        return sv;
                    return std::clamp(int(std::lround(sv + amount * diff)), 0, a);
                };
                d[x] = qRgba(ch(qRed(s[x]), qRed(bl[x])), ch(qGreen(s[x]), qGreen(bl[x])),
                             ch(qBlue(s[x]), qBlue(bl[x])), a);
            }
        }
    });
    return out;
}

QImage pixelate(const QImage &srcIn, int cell)
{
    cell = std::max(1, cell);
    QImage img = premultiplied(srcIn);
    if (cell == 1)
        return img;
    img.bits(); // detach before the threads write
    const int w = img.width(), h = img.height();
    const int rows = (h + cell - 1) / cell;
    parallel(rows, [&](int r0, int r1) {
        for (int cr = r0; cr < r1; ++cr) {
            const int y0 = cr * cell, y1 = std::min(h, y0 + cell);
            for (int x0 = 0; x0 < w; x0 += cell) {
                const int x1 = std::min(w, x0 + cell);
                quint64 sum[4] = {0, 0, 0, 0};
                for (int y = y0; y < y1; ++y) {
                    const quint8 *p = img.constScanLine(y) + x0 * 4;
                    for (int x = x0; x < x1; ++x, p += 4)
                        for (int c = 0; c < 4; ++c)
                            sum[c] += p[c];
                }
                const quint64 n = quint64(x1 - x0) * quint64(y1 - y0);
                quint8 avg[4];
                for (int c = 0; c < 4; ++c)
                    avg[c] = quint8((sum[c] + n / 2) / n);
                for (int y = y0; y < y1; ++y) {
                    quint8 *p = img.scanLine(y) + x0 * 4;
                    for (int x = x0; x < x1; ++x, p += 4)
                        std::copy(avg, avg + 4, p);
                }
            }
        }
    }, 1);
    return img;
}

QImage addNoise(const QImage &src, double amount, bool color, quint32 seed)
{
    const double sigma = std::clamp(amount, 0.0, 100.0) / 100.0 * 80.0;
    QImage img = straight(src);
    img.bits(); // detach before the threads write
    const int w = img.width();
    parallel(img.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb p = line[x];
                if (qAlpha(p) == 0)
                    continue;
                const quint32 base = hash(quint32(x) * 0x9e3779b1U ^ hash(quint32(y) + seed * 0x85ebca6bU));
                const double n0 = gaussian(base) * sigma;
                const double n1 = color ? gaussian(base ^ 0x68e31da4U) * sigma : n0;
                const double n2 = color ? gaussian(base ^ 0xb5297a4dU) * sigma : n0;
                line[x] = qRgba(clamp255(qRed(p) + n0), clamp255(qGreen(p) + n1), clamp255(qBlue(p) + n2), qAlpha(p));
            }
        }
    });
    return premultiplied(img);
}

QImage median(const QImage &srcIn, int radius)
{
    radius = std::clamp(radius, 1, 10);
    const QImage src = premultiplied(srcIn);
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    const int w = src.width(), h = src.height();
    const int half = ((2 * radius + 1) * (2 * radius + 1)) / 2;
    struct Median
    {
        int hist[256];
        int m = 0, below = 0; // below = count of values < m
        void add(int v) { ++hist[v]; below += v < m; }
        void remove(int v) { --hist[v]; below -= v < m; }
        int get(int half)
        {
            while (below > half) {
                --m;
                below -= hist[m];
            }
            while (below + hist[m] <= half) {
                below += hist[m];
                ++m;
            }
            return m;
        }
    };
    parallel(h, [&](int y0, int y1) {
        std::vector<Median> med(4);
        for (int y = y0; y < y1; ++y) {
            for (auto &md : med) {
                std::fill(std::begin(md.hist), std::end(md.hist), 0);
                md.m = 0;
                md.below = 0;
            }
            auto column = [&](int x, bool add) {
                x = std::clamp(x, 0, w - 1);
                for (int dy = -radius; dy <= radius; ++dy) {
                    const quint8 *p = src.constScanLine(std::clamp(y + dy, 0, h - 1)) + x * 4;
                    for (int c = 0; c < 4; ++c)
                        add ? med[size_t(c)].add(p[c]) : med[size_t(c)].remove(p[c]);
                }
            };
            for (int dx = -radius; dx <= radius; ++dx)
                column(dx, true);
            quint8 *d = out.scanLine(y);
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 4; ++c)
                    d[x * 4 + c] = quint8(med[size_t(c)].get(half));
                // keep premultiplied invariant: color <= alpha
                const int a = d[x * 4 + 3];
                for (int c = 0; c < 3; ++c)
                    d[x * 4 + c] = quint8(std::min<int>(d[x * 4 + c], a));
                column(x - radius, false);
                column(x + radius + 1, true);
            }
        }
    });
    return out;
}

QImage cleanDocument(const QImage &src, int mode, double whiten, double ink, double bwThreshold, bool despeckle)
{
    if (src.isNull())
        return src;
    // Flatten onto white paper.
    QImage img(src.size(), QImage::Format_RGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.drawImage(0, 0, src);
    }
    const int w = img.width(), h = img.height();
    const double wk = std::clamp(whiten, 0.0, 100.0) / 100.0;
    if (wk > 0) {
        // Paper (background) estimate: shrink, remove dark text with a max
        // filter, smooth, enlarge. Dividing by it removes shadows and color cast.
        const int f = std::max(1, std::min(w, h) / 150);
        QImage small = img.scaled(std::max(1, w / f), std::max(1, h / f), Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
        small = maxFilter(small, 3);
        small = gaussianBlur(small, 6).convertToFormat(QImage::Format_RGB32);
        const QImage bg = small.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGB32);
        parallel(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                const auto *bl = reinterpret_cast<const QRgb *>(bg.constScanLine(y));
                for (int x = 0; x < w; ++x) {
                    auto norm = [&](int v, int b) {
                        const double n = std::min(255.0, v * 255.0 / std::max(b, 16));
                        return clamp255(v + (n - v) * wk);
                    };
                    line[x] = qRgb(norm(qRed(line[x]), qRed(bl[x])), norm(qGreen(line[x]), qGreen(bl[x])),
                                   norm(qBlue(line[x]), qBlue(bl[x])));
                }
            }
        });
    }
    // Darker ink, cleaner (whiter) paper.
    const double black = std::clamp(ink, 0.0, 100.0) * 1.2;
    const double white = 255.0 - 20.0 * wk;
    const Lut lut = makeLut([&](double x) { return (x * 255.0 - black) / std::max(1.0, white - black); });
    const int level = int(std::clamp(bwThreshold, 0.0, 100.0) * 2.55);
    parallel(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int r = lut[size_t(qRed(line[x]))], g = lut[size_t(qGreen(line[x]))], b = lut[size_t(qBlue(line[x]))];
                if (mode >= 1) {
                    const int l = clamp255(luma(r, g, b));
                    r = g = b = mode == 2 ? (l >= level ? 255 : 0) : l;
                }
                line[x] = qRgb(r, g, b);
            }
        }
    });
    QImage out = premultiplied(img);
    if (despeckle)
        out = median(out, 1);
    return out;
}

// ================================================================ rotation

DocState rotate(const DocState &s, double degrees, int canvas, int fill)
{
    if (s.base.isNull())
        return s;
    const double w = s.base.width(), h = s.base.height();
    const double rad = degrees * M_PI / 180.0;
    QSizeF target(w, h);
    if (canvas == 0) {
        target = QTransform().rotate(degrees).mapRect(QRectF(0, 0, w, h)).size();
    } else if (canvas == 2) {
        target = largestInscribed(w, h, rad);
    }
    const QSize size(std::max(1, int(std::lround(target.width() - 1e-6))),
                     std::max(1, int(std::lround(target.height() - 1e-6))));
    const QTransform t = QTransform::fromTranslate(-w / 2.0, -h / 2.0) * QTransform().rotate(degrees)
                         * QTransform::fromTranslate(size.width() / 2.0, size.height() / 2.0);

    QImage base(size, QImage::Format_ARGB32_Premultiplied);
    base.fill(fillColor(fill));
    {
        QPainter p(&base);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);
        p.setTransform(t);
        if (fill != 0) {
            // Smooth edges against the fill color, not against transparency.
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        p.drawImage(QPointF(0, 0), s.base);
    }
    DocState out;
    out.base = base;
    out.notes = s.notes;
    for (const auto &o : s.objects) {
        auto c = o->clone();
        c->applyDocumentTransform(t, DocTransformKind::FreeRotate);
        out.objects.append(c);
    }
    return out;
}

double detectSkew(const QImage &src)
{
    if (src.isNull())
        return 0;
    const double scale = std::min(1.0, 1000.0 / std::max(src.width(), src.height()));
    QImage img(QSize(std::max(1, int(src.width() * scale)), std::max(1, int(src.height() * scale))),
               QImage::Format_RGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(img.rect(), src);
    }
    // Dark pixels relative to the average brightness (ink on paper).
    double mean = 0;
    for (int y = 0; y < img.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            mean += luma(qRed(line[x]), qGreen(line[x]), qBlue(line[x]));
    }
    mean /= double(img.width()) * img.height();
    const double limit = std::min(mean - 40.0, 160.0);
    std::vector<QPointF> pts;
    for (int y = 0; y < img.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (luma(qRed(line[x]), qGreen(line[x]), qBlue(line[x])) < limit)
                pts.emplace_back(x, y);
    }
    if (pts.size() < 50)
        return 0;
    if (pts.size() > 150000) { // thin out evenly
        std::vector<QPointF> thin;
        const size_t step = pts.size() / 150000 + 1;
        for (size_t i = 0; i < pts.size(); i += step)
            thin.push_back(pts[i]);
        pts.swap(thin);
    }
    const int width = img.width(), height = img.height();
    auto score = [&](double deg) {
        // Rows of a text line all fall into the same bin at the right angle.
        const double tn = std::tan(deg * M_PI / 180.0);
        const int offset = int(std::ceil(std::abs(tn) * width)) + 1;
        std::vector<int> bins(size_t(height + 2 * offset + 2), 0);
        for (const QPointF &p : pts) {
            const int b = int(std::lround(p.y() - p.x() * tn)) + offset;
            if (b >= 0 && b < int(bins.size()))
                ++bins[size_t(b)];
        }
        double sum = 0;
        for (int v : bins)
            sum += double(v) * v;
        return sum;
    };
    double best = 0, bestScore = -1;
    for (double a = -15.0; a <= 15.0001; a += 0.5) {
        const double sc = score(a);
        if (sc > bestScore) {
            bestScore = sc;
            best = a;
        }
    }
    const double coarse = best;
    for (double a = coarse - 0.5; a <= coarse + 0.5001; a += 0.05) {
        const double sc = score(a);
        if (sc > bestScore) {
            bestScore = sc;
            best = a;
        }
    }
    // A flat profile means no text lines: do not guess.
    return std::abs(score(best) - score(0)) < score(0) * 0.01 ? 0.0 : std::round(best * 100.0) / 100.0;
}

// ================================================================ registry

// The table below leaves trailing members at their defaults on purpose.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

namespace {
using T = Param::Type;


double val(const Values &v, const char *key) { return v.value(QLatin1String(key)); }
} // namespace

const QList<Spec> &all()
{
    static const QList<Spec> specs = [] {
        QList<Spec> l;
        // ---------------------------------------------------------- Adjust
        l.append({QStringLiteral("brightnessContrast"), QT_TRANSLATE_NOOP("filters", "Brightness / Contrast"), Menu::Adjust,
                  {{QStringLiteral("brightness"), QT_TRANSLATE_NOOP("filters", "Brightness:"), T::Int, -100, 100, 0},
                   {QStringLiteral("contrast"), QT_TRANSLATE_NOOP("filters", "Contrast:"), T::Int, -100, 100, 0}},
                  true,
                  [](const QImage &i, const Values &v) {
                      return brightnessContrast(i, val(v, "brightness"), val(v, "contrast"));
                  }});
        l.append({QStringLiteral("hueSaturation"), QT_TRANSLATE_NOOP("filters", "Hue / Saturation"), Menu::Adjust,
                  {{QStringLiteral("hue"), QT_TRANSLATE_NOOP("filters", "Hue:"), T::Int, -180, 180, 0, 0, "°"},
                   {QStringLiteral("saturation"), QT_TRANSLATE_NOOP("filters", "Saturation:"), T::Int, -100, 100, 0},
                   {QStringLiteral("lightness"), QT_TRANSLATE_NOOP("filters", "Lightness:"), T::Int, -100, 100, 0}},
                  true,
                  [](const QImage &i, const Values &v) {
                      return hueSaturation(i, val(v, "hue"), val(v, "saturation"), val(v, "lightness"));
                  }});
        l.append({QStringLiteral("enhanceColors"), QT_TRANSLATE_NOOP("filters", "Enhance Colors"), Menu::Adjust,
                  {{QStringLiteral("strength"), QT_TRANSLATE_NOOP("filters", "Auto levels:"), T::Int, 0, 100, 100, 0, " %",
                    {}, QT_TRANSLATE_NOOP("filters", "Stretches each color channel to the full range: more contrast, "
                                      "removes color casts")},
                   {QStringLiteral("vibrance"), QT_TRANSLATE_NOOP("filters", "Vibrance:"), T::Int, -100, 100, 25, 0, "", {},
                    QT_TRANSLATE_NOOP("filters", "Boosts dull colors more than already saturated ones")},
                   {QStringLiteral("clip"), QT_TRANSLATE_NOOP("filters", "Clip:"), T::Double, 0, 5, 0.5, 1, " %", {},
                    QT_TRANSLATE_NOOP("filters", "Share of the darkest and lightest pixels ignored by auto levels")}},
                  true,
                  [](const QImage &i, const Values &v) {
                      return enhanceColors(i, val(v, "strength"), val(v, "vibrance"), val(v, "clip"));
                  }});
        l.append({QStringLiteral("temperature"), QT_TRANSLATE_NOOP("filters", "Color Temperature"), Menu::Adjust,
                  {{QStringLiteral("temperature"), QT_TRANSLATE_NOOP("filters", "Temperature:"), T::Int, -100, 100, 0, 0, "",
                    {}, QT_TRANSLATE_NOOP("filters", "Negative = cooler (blue), positive = warmer (yellow)")},
                   {QStringLiteral("tint"), QT_TRANSLATE_NOOP("filters", "Tint:"), T::Int, -100, 100, 0, 0, "", {},
                    QT_TRANSLATE_NOOP("filters", "Negative = green, positive = magenta")}},
                  true,
                  [](const QImage &i, const Values &v) {
                      return temperature(i, val(v, "temperature"), val(v, "tint"));
                  }});
        l.append({QStringLiteral("levels"), QT_TRANSLATE_NOOP("filters", "Levels"), Menu::Adjust,
                  {{QStringLiteral("black"), QT_TRANSLATE_NOOP("filters", "Black point:"), T::Int, 0, 254, 0},
                   {QStringLiteral("white"), QT_TRANSLATE_NOOP("filters", "White point:"), T::Int, 1, 255, 255},
                   {QStringLiteral("gamma"), QT_TRANSLATE_NOOP("filters", "Gamma:"), T::Double, 0.1, 5.0, 1.0, 2, "", {},
                    QT_TRANSLATE_NOOP("filters", "Above 1 brightens the midtones, below 1 darkens them")}},
                  true,
                  [](const QImage &i, const Values &v) {
                      return levels(i, int(val(v, "black")), int(val(v, "white")), val(v, "gamma"));
                  }});
        l.append({QStringLiteral("grayscale"), QT_TRANSLATE_NOOP("filters", "Grayscale"), Menu::Adjust, {}, true,
                  [](const QImage &i, const Values &) { return grayscale(i); }});
        l.append({QStringLiteral("sepia"), QT_TRANSLATE_NOOP("filters", "Sepia"), Menu::Adjust,
                  {{QStringLiteral("strength"), QT_TRANSLATE_NOOP("filters", "Strength:"), T::Int, 0, 100, 100, 0, " %"}},
                  true, [](const QImage &i, const Values &v) { return sepia(i, val(v, "strength")); }});
        l.append({QStringLiteral("invert"), QT_TRANSLATE_NOOP("filters", "Invert Colors"), Menu::Adjust, {}, true,
                  [](const QImage &i, const Values &) { return invert(i); }});
        l.append({QStringLiteral("threshold"), QT_TRANSLATE_NOOP("filters", "Black and White (Threshold)"), Menu::Adjust,
                  {{QStringLiteral("level"), QT_TRANSLATE_NOOP("filters", "Threshold:"), T::Int, 1, 255, 128, 0, "", {},
                    QT_TRANSLATE_NOOP("filters", "Pixels lighter than this become white, darker black")}},
                  true, [](const QImage &i, const Values &v) { return threshold(i, int(val(v, "level"))); }});
        l.append({QStringLiteral("posterize"), QT_TRANSLATE_NOOP("filters", "Posterize"), Menu::Adjust,
                  {{QStringLiteral("levels"), QT_TRANSLATE_NOOP("filters", "Levels per channel:"), T::Int, 2, 32, 4}},
                  true, [](const QImage &i, const Values &v) { return posterize(i, int(val(v, "levels"))); }});
        // ---------------------------------------------------------- Filters
        Spec blur{QStringLiteral("blur"), QT_TRANSLATE_NOOP("filters", "Blur"), Menu::Filters,
                  {{QStringLiteral("radius"), QT_TRANSLATE_NOOP("filters", "Blur radius:"), T::Double, 0.5, 100, 4, 1, " px"}},
                  true, [](const QImage &i, const Values &v) { return gaussianBlur(i, val(v, "radius")); }};
        blur.margin = blurMargin;
        l.append(blur);
        Spec sharp{QStringLiteral("sharpen"), QT_TRANSLATE_NOOP("filters", "Sharpen"), Menu::Filters,
                   {{QStringLiteral("amount"), QT_TRANSLATE_NOOP("filters", "Amount:"), T::Int, 1, 500, 80, 0, " %"},
                    {QStringLiteral("radius"), QT_TRANSLATE_NOOP("filters", "Detail radius:"), T::Double, 0.3, 20, 1.5, 1, " px", {},
                     QT_TRANSLATE_NOOP("filters", "Size of the details to sharpen")},
                    {QStringLiteral("threshold"), QT_TRANSLATE_NOOP("filters", "Threshold:"), T::Int, 0, 64, 2, 0, "", {},
                     QT_TRANSLATE_NOOP("filters", "Differences below this are left alone (keeps noise and skin smooth)")}},
                   true, [](const QImage &i, const Values &v) {
                       return sharpen(i, val(v, "amount"), val(v, "radius"), int(val(v, "threshold")));
                   }};
        sharp.margin = blurMargin;
        l.append(sharp);
        l.append({QStringLiteral("pixelate"), QT_TRANSLATE_NOOP("filters", "Pixelate"), Menu::Filters,
                  {{QStringLiteral("cell"), QT_TRANSLATE_NOOP("filters", "Cell size:"), T::Int, 2, 256, 12, 0, " px"}},
                  true, [](const QImage &i, const Values &v) { return pixelate(i, int(val(v, "cell"))); }});
        l.append({QStringLiteral("noise"), QT_TRANSLATE_NOOP("filters", "Add Noise"), Menu::Filters,
                  {{QStringLiteral("amount"), QT_TRANSLATE_NOOP("filters", "Amount:"), T::Int, 1, 100, 15, 0, " %"},
                   {QStringLiteral("color"), QT_TRANSLATE_NOOP("filters", "Color noise"), T::Bool, 0, 1, 0, 0, "", {},
                    QT_TRANSLATE_NOOP("filters", "Random color per channel instead of brightness only")}},
                  true, [](const QImage &i, const Values &v) {
                      return addNoise(i, val(v, "amount"), val(v, "color") != 0.0);
                  }});
        Spec med{QStringLiteral("median"), QT_TRANSLATE_NOOP("filters", "Reduce Noise"), Menu::Filters,
                 {{QStringLiteral("radius"), QT_TRANSLATE_NOOP("filters", "Filter radius:"), T::Int, 1, 5, 1, 0, " px", {},
                   QT_TRANSLATE_NOOP("filters", "Median filter: removes specks and grain, keeps edges")}},
                 true, [](const QImage &i, const Values &v) { return median(i, int(val(v, "radius"))); }};
        med.margin = [](const Values &v) { return int(val(v, "radius")) + 1; };
        l.append(med);
        Spec doc{QStringLiteral("cleanDocument"), QT_TRANSLATE_NOOP("filters", "Clean Scanned Document"), Menu::Filters,
                 {{QStringLiteral("mode"), QT_TRANSLATE_NOOP("filters", "Result:"), T::Choice, 0, 2, 1, 0, "",
                   {QT_TRANSLATE_NOOP("filters", "Color"), QT_TRANSLATE_NOOP("filters", "Grayscale"), QT_TRANSLATE_NOOP("filters", "Black and white")}},
                  {QStringLiteral("whiten"), QT_TRANSLATE_NOOP("filters", "Whiten paper:"), T::Int, 0, 100, 100, 0, " %", {},
                   QT_TRANSLATE_NOOP("filters", "Removes shadows, uneven light and the paper's color")},
                  {QStringLiteral("ink"), QT_TRANSLATE_NOOP("filters", "Darken ink:"), T::Int, 0, 100, 30, 0, " %"},
                  {QStringLiteral("bw"), QT_TRANSLATE_NOOP("filters", "Black and white threshold:"), T::Int, 1, 99, 75, 0, " %",
                   {}, QT_TRANSLATE_NOOP("filters", "Only for the black and white result")},
                  {QStringLiteral("despeckle"), QT_TRANSLATE_NOOP("filters", "Remove specks"), T::Bool, 0, 1, 1}},
                 false, [](const QImage &i, const Values &v) {
                     return cleanDocument(i, int(val(v, "mode")), val(v, "whiten"), val(v, "ink"), val(v, "bw"),
                                          val(v, "despeckle") != 0.0);
                 }};
        l.append(doc);
        // ---------------------------------------------------------- Image
        Spec rot{QStringLiteral("rotate"), QT_TRANSLATE_NOOP("filters", "Rotate by Angle"), Menu::Image,
                 {{QStringLiteral("angle"), QT_TRANSLATE_NOOP("filters", "Angle:"), T::Double, -180, 180, 0, 1, "°", {},
                   QT_TRANSLATE_NOOP("filters", "Clockwise; negative values rotate counterclockwise")},
                  {QStringLiteral("canvas"), QT_TRANSLATE_NOOP("filters", "Canvas:"), T::Choice, 0, 2, 0, 0, "",
                   {QT_TRANSLATE_NOOP("filters", "Enlarge to fit the whole image"), QT_TRANSLATE_NOOP("filters", "Keep the size"),
                    QT_TRANSLATE_NOOP("filters", "Crop off the empty corners")}},
                  {QStringLiteral("fill"), QT_TRANSLATE_NOOP("filters", "Fill corners:"), T::Choice, 0, 2, 0, 0, "",
                   {QT_TRANSLATE_NOOP("filters", "Transparent"), QT_TRANSLATE_NOOP("filters", "White"), QT_TRANSLATE_NOOP("filters", "Black")}}},
                 false};
        rot.document = [](const DocState &s, const Values &v) {
            return rotate(s, val(v, "angle"), int(val(v, "canvas")), int(val(v, "fill")));
        };
        l.append(rot);
        return l;
    }();
    return specs;
}

const Spec *find(const QString &id)
{
    for (const Spec &s : all())
        if (s.id == id)
            return &s;
    return nullptr;
}

Values defaults(const Spec &spec)
{
    Values v;
    for (const Param &p : spec.params)
        v.insert(p.key, p.def);
    return v;
}

QString displayName(const Spec &spec)
{
    return QCoreApplication::translate("filters", spec.name);
}

DocState apply(const Spec &spec, const DocState &s, const QRect &selection, const Values &values)
{
    Values v = defaults(spec);
    for (auto it = values.begin(); it != values.end(); ++it)
        v.insert(it.key(), it.value());
    if (spec.document)
        return spec.document(s, v);
    if (!spec.image || s.base.isNull())
        return s;
    const QRect canvas = s.base.rect();
    const QRect area = spec.selection && !selection.isEmpty() ? selection.intersected(canvas) : canvas;
    if (area.isEmpty())
        return s;
    DocState out = s;
    if (area == canvas) {
        out.base = premultiplied(spec.image(s.base, v));
        return out;
    }
    const int m = spec.margin ? spec.margin(v) : 0;
    const QRect context = area.adjusted(-m, -m, m, m).intersected(canvas);
    const QImage result = premultiplied(spec.image(s.base.copy(context), v));
    QImage base = s.base.copy();
    QPainter p(&base);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(area.topLeft(), result, area.translated(-context.topLeft()));
    p.end();
    out.base = base;
    return out;
}

} // namespace filters
