#pragma once

#include "history.h"

#include <QImage>
#include <QPointF>
#include <QRect>
#include <vector>

// Pixel engine for retouching the base image (spec C3, D5, H1): clone stamp,
// pixel eraser, blur brush, area blur and mosaic. Works on
// ARGB32_Premultiplied images; everything here is GUI-free and unit-tested.
namespace retouch {

// Soft round brush (spec D5): 1 inside radius*hardness, smoothstep falloff to
// 0 at the radius. `d` is the distance from the brush center.
double brushAlpha(double d, double radius, double hardness);

// Blur (approximately Gaussian: three box passes) of `rect` of `src`, using
// pixels around the rectangle as context. Returns an image of rect.size().
QImage blurRegion(const QImage &src, const QRect &rect, int radius);
// Mosaic: average color per cell, cells aligned to rect's top-left.
QImage pixelateRegion(const QImage &src, const QRect &rect, int cell);

// Flood fill (paint bucket) from `seed`: pixels whose color differs from the
// seed color by at most `tolerance` (0..100 %), connected to the seed or
// anywhere (`contiguous` false), are mixed with `color` by `opacity`.
// Returns the changed rectangle in `changed` (may be null).
QImage floodFill(const QImage &src, const QPoint &seed, const QColor &color, int tolerance, bool contiguous,
                 double opacity, QRect *changed = nullptr);

// Destructive area operations (spec H1), one undo step each.
DocState blurArea(const DocState &s, const QRect &rect, int radius);
DocState pixelateArea(const DocState &s, const QRect &rect, int cell);

enum class Mode { Clone, Erase, Blur, Pixelate };

// One brush stroke over the base. The mask of the stroke accumulates with
// MAX (not sum), so overlapping dabs never exceed `opacity`; the result is
// always mix(base before the stroke, effect, mask * opacity) (spec D5).
class Stroke
{
public:
    struct Params
    {
        Mode mode = Mode::Clone;
        double diameter = 40;   // px, 1..1000
        double hardness = 0.5;  // 0..1
        double opacity = 1.0;   // 0..1, ceiling for the whole stroke
        QPoint cloneOffset;     // source = target + offset (Clone)
        int blurRadius = 8;     // px (Blur); mosaic cell size (Pixelate)
    };

    Stroke(const QImage &before, const Params &params);

    // Dabs along the segment from the previous point (spacing 15 % of the
    // diameter). Returns the changed rectangle (image coordinates).
    QRect lineTo(const QPointF &p);
    const QImage &result() const { return m_result; }
    QImage takeResult() { return std::move(m_result); }
    QRect changedRect() const { return m_changed; }
    bool hasStarted() const { return m_started; }

private:
    QRect dab(const QPointF &c);
    void compose(const QRect &r);
    void ensureEffect(const QRect &r);

    QImage m_before;
    QImage m_result;
    Params m_p;
    std::vector<quint8> m_mask; // one byte per pixel, whole image
    QImage m_effect;            // blurred before (Blur), computed per tile on demand
    std::vector<bool> m_effectTiles;
    int m_tilesX = 0;
    QPointF m_last;
    double m_carry = 0;         // distance since the last dab
    bool m_started = false;
    QRect m_changed;
};

} // namespace retouch
