#include "retouch.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace retouch {

namespace {
constexpr int kTile = 64;

QImage premultiplied(const QImage &img)
{
    return img.format() == QImage::Format_ARGB32_Premultiplied
               ? img
               : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// One box pass of radius r along rows (horizontal) or columns, clamp-to-edge.
void boxPass(QImage &img, int r, bool horizontal)
{
    const int w = img.width(), h = img.height();
    const int n = horizontal ? w : h;      // samples per line
    const int lines = horizontal ? h : w;
    const int win = 2 * r + 1;
    std::vector<quint32> line(n);
    auto at = [&](int lineIdx, int i) -> quint32 & {
        return horizontal ? reinterpret_cast<quint32 *>(img.scanLine(lineIdx))[i]
                          : reinterpret_cast<quint32 *>(img.scanLine(i))[lineIdx];
    };
    for (int l = 0; l < lines; ++l) {
        for (int i = 0; i < n; ++i)
            line[i] = at(l, i);
        int sum[4] = {0, 0, 0, 0};
        auto add = [&](quint32 p, int sign) {
            sum[0] += sign * int(p & 0xff);
            sum[1] += sign * int((p >> 8) & 0xff);
            sum[2] += sign * int((p >> 16) & 0xff);
            sum[3] += sign * int(p >> 24);
        };
        auto sample = [&](int i) { return line[std::clamp(i, 0, n - 1)]; };
        for (int i = -r; i <= r; ++i)
            add(sample(i), 1);
        for (int i = 0; i < n; ++i) {
            const quint32 out = quint32((sum[0] + win / 2) / win) | (quint32((sum[1] + win / 2) / win) << 8)
                                | (quint32((sum[2] + win / 2) / win) << 16)
                                | (quint32((sum[3] + win / 2) / win) << 24);
            at(l, i) = out;
            add(sample(i - r), -1);
            add(sample(i + r + 1), 1);
        }
    }
}

inline quint32 mixPixel(quint32 b, quint32 e, int a256)
{
    quint32 out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int bc = int((b >> shift) & 0xff);
        const int ec = int((e >> shift) & 0xff);
        const int oc = bc + (((ec - bc) * a256 + 128) >> 8);
        out |= quint32(std::clamp(oc, 0, 255)) << shift;
    }
    return out;
}
} // namespace

double brushAlpha(double d, double radius, double hardness)
{
    if (d >= radius)
        return 0.0;
    const double inner = radius * std::clamp(hardness, 0.0, 1.0);
    if (d <= inner)
        return 1.0;
    const double t = (d - inner) / (radius - inner);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

QImage blurRegion(const QImage &srcIn, const QRect &rect, int radius)
{
    const QImage src = premultiplied(srcIn);
    const QRect r = rect.intersected(src.rect());
    if (r.isEmpty())
        return {};
    radius = std::max(1, radius);
    const QRect ctx = r.adjusted(-3 * radius, -3 * radius, 3 * radius, 3 * radius).intersected(src.rect());
    QImage work = src.copy(ctx);
    for (int pass = 0; pass < 3; ++pass) {
        boxPass(work, radius, true);
        boxPass(work, radius, false);
    }
    return work.copy(r.translated(-ctx.topLeft()));
}

QImage pixelateRegion(const QImage &srcIn, const QRect &rect, int cell)
{
    const QImage src = premultiplied(srcIn);
    const QRect r = rect.intersected(src.rect());
    if (r.isEmpty())
        return {};
    cell = std::max(1, cell);
    QImage out(r.size(), QImage::Format_ARGB32_Premultiplied);
    for (int cy = 0; cy < r.height(); cy += cell) {
        for (int cx = 0; cx < r.width(); cx += cell) {
            const int cw = std::min(cell, r.width() - cx), ch = std::min(cell, r.height() - cy);
            quint64 sum[4] = {0, 0, 0, 0};
            for (int y = 0; y < ch; ++y) {
                const auto *line = reinterpret_cast<const quint32 *>(src.constScanLine(r.y() + cy + y)) + r.x() + cx;
                for (int x = 0; x < cw; ++x)
                    for (int c = 0; c < 4; ++c)
                        sum[c] += (line[x] >> (8 * c)) & 0xff;
            }
            const quint64 n = quint64(cw) * ch;
            quint32 avg = 0;
            for (int c = 0; c < 4; ++c)
                avg |= quint32((sum[c] + n / 2) / n) << (8 * c);
            for (int y = 0; y < ch; ++y) {
                auto *line = reinterpret_cast<quint32 *>(out.scanLine(cy + y)) + cx;
                std::fill(line, line + cw, avg);
            }
        }
    }
    return out;
}

static DocState replaceRegion(const DocState &s, const QRect &r, const QImage &region)
{
    if (region.isNull())
        return s;
    DocState out = s;
    out.base = s.base.copy(); // history keeps the old buffer
    QPainter p(&out.base);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(r.topLeft(), region);
    p.end();
    return out;
}

DocState blurArea(const DocState &s, const QRect &rect, int radius)
{
    const QRect r = rect.intersected(s.base.rect());
    return replaceRegion(s, r, blurRegion(s.base, r, radius));
}

DocState pixelateArea(const DocState &s, const QRect &rect, int cell)
{
    const QRect r = rect.intersected(s.base.rect());
    return replaceRegion(s, r, pixelateRegion(s.base, r, cell));
}

// ---------------------------------------------------------------- Stroke

Stroke::Stroke(const QImage &before, const Params &params)
    : m_before(premultiplied(before))
    , m_p(params)
{
    m_p.diameter = std::clamp(m_p.diameter, 1.0, 1000.0);
    m_p.opacity = std::clamp(m_p.opacity, 0.0, 1.0);
    m_result = m_before.copy();
    m_mask.assign(size_t(m_before.width()) * size_t(m_before.height()), 0);
    if (m_p.mode == Mode::Blur || m_p.mode == Mode::Pixelate) {
        m_effect = QImage(m_before.size(), QImage::Format_ARGB32_Premultiplied);
        m_tilesX = (m_before.width() + kTile - 1) / kTile;
        const int tilesY = (m_before.height() + kTile - 1) / kTile;
        m_effectTiles.assign(size_t(m_tilesX) * size_t(tilesY), false);
    }
}

QRect Stroke::lineTo(const QPointF &p)
{
    if (!m_started) {
        m_started = true;
        m_last = p;
        m_carry = 0;
        return dab(p);
    }
    const double spacing = std::max(0.5, m_p.diameter * 0.15); // spec D5
    const QPointF d = p - m_last;
    const double len = std::hypot(d.x(), d.y());
    QRect dirty;
    if (len > 0) {
        const QPointF u = d / len;
        double pos = spacing - m_carry;
        while (pos <= len) {
            dirty |= dab(m_last + u * pos);
            pos += spacing;
        }
        m_carry = len - (pos - spacing);
    }
    m_last = p;
    return dirty;
}

QRect Stroke::dab(const QPointF &c)
{
    const double r = m_p.diameter / 2.0;
    const QRect bounds = QRect(QPoint(int(std::floor(c.x() - r)), int(std::floor(c.y() - r))),
                               QPoint(int(std::ceil(c.x() + r)), int(std::ceil(c.y() + r))))
                             .intersected(m_before.rect());
    if (bounds.isEmpty())
        return {};
    const int w = m_before.width();
    const QRect src = m_before.rect();
    const QPoint cpx(int(std::floor(c.x())), int(std::floor(c.y())));
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        const double dy = y + 0.5 - c.y();
        quint8 *mask = m_mask.data() + size_t(y) * size_t(w);
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const double dx = x + 0.5 - c.x();
            double a = brushAlpha(std::sqrt(dx * dx + dy * dy), r, m_p.hardness);
            if (x == cpx.x() && y == cpx.y())
                a = 1.0; // the pixel under the cursor is always hit (tiny brushes)
            if (a <= 0)
                continue;
            // Clone: source pixels outside the image are not copied (spec D5).
            if (m_p.mode == Mode::Clone && !src.contains(x + m_p.cloneOffset.x(), y + m_p.cloneOffset.y()))
                continue;
            const quint8 v = quint8(std::lround(a * 255.0));
            if (v > mask[x])
                mask[x] = v; // MAX, not sum (spec D5)
        }
    }
    compose(bounds);
    m_changed |= bounds;
    return bounds;
}

void Stroke::ensureEffect(const QRect &r)
{
    const int tx0 = r.left() / kTile, tx1 = r.right() / kTile;
    const int ty0 = r.top() / kTile, ty1 = r.bottom() / kTile;
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const size_t idx = size_t(ty) * size_t(m_tilesX) + size_t(tx);
            if (m_effectTiles[idx])
                continue;
            const QRect tile = QRect(tx * kTile, ty * kTile, kTile, kTile).intersected(m_before.rect());
            QImage blurred;
            if (m_p.mode == Mode::Pixelate) {
                // Cells aligned to the image origin, so tiles join seamlessly.
                const int c = std::max(2, m_p.blurRadius);
                const QRect cells = QRect(QPoint(tile.left() / c * c, tile.top() / c * c),
                                          QPoint((tile.right() / c + 1) * c - 1, (tile.bottom() / c + 1) * c - 1))
                                        .intersected(m_before.rect());
                blurred = pixelateRegion(m_before, cells, c).copy(tile.translated(-cells.topLeft()));
            } else {
                blurred = blurRegion(m_before, tile, m_p.blurRadius);
            }
            for (int y = 0; y < tile.height(); ++y)
                std::copy_n(reinterpret_cast<const quint32 *>(blurred.constScanLine(y)), tile.width(),
                            reinterpret_cast<quint32 *>(m_effect.scanLine(tile.y() + y)) + tile.x());
            m_effectTiles[idx] = true;
        }
    }
}

void Stroke::compose(const QRect &r)
{
    if (m_p.mode == Mode::Blur || m_p.mode == Mode::Pixelate)
        ensureEffect(r);
    const int w = m_before.width();
    const int scale = int(std::lround(m_p.opacity * 256.0)); // a256 = mask * opacity
    const QPoint off = m_p.cloneOffset;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const quint8 *mask = m_mask.data() + size_t(y) * size_t(w);
        const auto *before = reinterpret_cast<const quint32 *>(m_before.constScanLine(y));
        auto *out = reinterpret_cast<quint32 *>(m_result.scanLine(y));
        const quint32 *effectLine = nullptr;
        if (m_p.mode == Mode::Clone && y + off.y() >= 0 && y + off.y() < m_before.height())
            effectLine = reinterpret_cast<const quint32 *>(m_before.constScanLine(y + off.y())) + off.x();
        else if (m_p.mode == Mode::Blur || m_p.mode == Mode::Pixelate)
            effectLine = reinterpret_cast<const quint32 *>(m_effect.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const int m = mask[x];
            if (m == 0) {
                out[x] = before[x];
                continue;
            }
            const int a256 = (m * scale + 127) / 255;
            const quint32 e = m_p.mode == Mode::Erase ? 0u : effectLine[x];
            out[x] = mixPixel(before[x], e, a256);
        }
    }
}

// ================================================================ flood fill

QImage floodFill(const QImage &srcIn, const QPoint &seed, const QColor &color, int tolerance, bool contiguous,
                 double opacity, QRect *changed)
{
    if (changed)
        *changed = QRect();
    const QImage src = srcIn.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!src.rect().contains(seed))
        return src;
    const QImage straight = src.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    const QRgb ref = straight.pixel(seed);
    const int tol = int(std::lround(std::clamp(tolerance, 0, 100) * 2.55));
    auto similar = [&](QRgb p) {
        // Fully transparent pixels match each other whatever their (invisible) color.
        if (qAlpha(p) == 0 && qAlpha(ref) == 0)
            return true;
        return std::abs(qRed(p) - qRed(ref)) <= tol && std::abs(qGreen(p) - qGreen(ref)) <= tol
               && std::abs(qBlue(p) - qBlue(ref)) <= tol && std::abs(qAlpha(p) - qAlpha(ref)) <= tol;
    };
    std::vector<quint8> fill(size_t(w) * size_t(h), 0);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    auto mark = [&](int x, int y) {
        fill[size_t(y) * size_t(w) + size_t(x)] = 1;
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    };
    if (contiguous) {
        // Scanline fill with an explicit stack.
        std::vector<QPoint> stack{seed};
        while (!stack.empty()) {
            const QPoint p = stack.back();
            stack.pop_back();
            const auto *line = reinterpret_cast<const QRgb *>(straight.constScanLine(p.y()));
            if (fill[size_t(p.y()) * size_t(w) + size_t(p.x())] || !similar(line[p.x()]))
                continue;
            int l = p.x(), r = p.x();
            while (l > 0 && !fill[size_t(p.y()) * size_t(w) + size_t(l - 1)] && similar(line[l - 1]))
                --l;
            while (r < w - 1 && !fill[size_t(p.y()) * size_t(w) + size_t(r + 1)] && similar(line[r + 1]))
                ++r;
            for (int x = l; x <= r; ++x)
                mark(x, p.y());
            for (int ny : {p.y() - 1, p.y() + 1}) {
                if (ny < 0 || ny >= h)
                    continue;
                const auto *nl = reinterpret_cast<const QRgb *>(straight.constScanLine(ny));
                bool inRun = false;
                for (int x = l; x <= r; ++x) {
                    const bool ok = !fill[size_t(ny) * size_t(w) + size_t(x)] && similar(nl[x]);
                    if (ok && !inRun)
                        stack.emplace_back(x, ny);
                    inRun = ok;
                }
            }
        }
    } else {
        for (int y = 0; y < h; ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(straight.constScanLine(y));
            for (int x = 0; x < w; ++x)
                if (similar(line[x]))
                    mark(x, y);
        }
    }
    QImage out = src.copy();
    if (x1 < 0)
        return out;
    const quint32 target = qPremultiply(color.rgba());
    const int a256 = int(std::lround(std::clamp(opacity, 0.0, 1.0) * 256.0));
    for (int y = y0; y <= y1; ++y) {
        auto *line = reinterpret_cast<quint32 *>(out.scanLine(y));
        const quint8 *f = fill.data() + size_t(y) * size_t(w);
        for (int x = x0; x <= x1; ++x)
            if (f[x])
                line[x] = mixPixel(line[x], target, a256);
    }
    if (changed)
        *changed = QRect(QPoint(x0, y0), QPoint(x1, y1));
    return out;
}

} // namespace retouch
