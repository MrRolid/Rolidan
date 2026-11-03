#include "annotation.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QIODevice>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <atomic>
#include <cmath>

namespace {
constexpr quint32 kMagic = 0x524F4C31; // "ROL1"
constexpr quint16 kVersion = 4; // 4: text spans
// Version of the stream being read (types append fields in newer versions).
quint16 g_readVersion = kVersion;

QColor withOpacity(QColor c, double opacity)
{
    c.setAlphaF(float(c.alphaF() * std::clamp(opacity, 0.0, 1.0)));
    return c;
}

double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    if (len2 <= 1e-12)
        return QLineF(p, a).length();
    const double t = std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0);
    return QLineF(p, a + ab * t).length();
}

QColor toColor(const QVariant &v, const QColor &fallback)
{
    const QColor c = v.value<QColor>();
    return c.isValid() ? c : fallback;
}
} // namespace

const char *toolName(ToolId id)
{
    switch (id) {
    case ToolId::Pointer: return "pointer";
    case ToolId::Select: return "select";
    case ToolId::Crop: return "crop";
    case ToolId::Text: return "text";
    case ToolId::Arrow: return "arrow";
    case ToolId::Line: return "line";
    case ToolId::Rect: return "rect";
    case ToolId::Ellipse: return "ellipse";
    case ToolId::Brush: return "brush";
    case ToolId::Highlighter: return "highlighter";
    case ToolId::Cover: return "cover";
    case ToolId::Eyedropper: return "eyedropper";
    case ToolId::Clone: return "clone";
    case ToolId::Eraser: return "eraser";
    case ToolId::BlurBrush: return "blurBrush";
    case ToolId::Marker: return "marker";
    case ToolId::Polyline: return "polyline";
    case ToolId::Polygon: return "polygon";
    case ToolId::Bubble: return "bubble";
    case ToolId::PixelBrush: return "pixelBrush";
    case ToolId::Bucket: return "bucket";
    case ToolId::Straighten: return "straighten";
    }
    return "unknown";
}

const char *propName(Prop p)
{
    switch (p) {
    case Prop::Color: return "color";
    case Prop::Width: return "width";
    case Prop::Opacity: return "opacity";
    case Prop::Fill: return "fill";
    case Prop::HeadStart: return "headStart";
    case Prop::HeadEnd: return "headEnd";
    case Prop::HeadSize: return "headSize";
    case Prop::Radius: return "radius";
    case Prop::FontFamily: return "fontFamily";
    case Prop::FontSize: return "fontSize";
    case Prop::Bold: return "bold";
    case Prop::Italic: return "italic";
    case Prop::Underline: return "underline";
    case Prop::Align: return "align";
    case Prop::Background: return "background";
    case Prop::Outline: return "outline";
    case Prop::HighlightMode: return "highlightMode";
    case Prop::Size: return "size";
    case Prop::Hardness: return "hardness";
    case Prop::Aligned: return "aligned";
    case Prop::Strength: return "strength";
    case Prop::Number: return "number";
    case Prop::Cell: return "cell";
    case Prop::Tolerance: return "tolerance";
    case Prop::Contiguous: return "contiguous";
    }
    return "unknown";
}

double transformScale(const QTransform &t)
{
    const double sx = std::hypot(t.m11(), t.m12());
    const double sy = std::hypot(t.m21(), t.m22());
    return std::min(sx, sy);
}

QPointF snapAngle(const QPointF &origin, const QPointF &p, double stepDegrees)
{
    const QPointF d = p - origin;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-9)
        return p;
    const double step = qDegreesToRadians(stepDegrees);
    const double angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
    return origin + QPointF(std::cos(angle), std::sin(angle)) * len;
}

// ================================================================== Annotation

quint64 Annotation::nextId()
{
    static std::atomic<quint64> counter{1};
    return counter++;
}

void Annotation::applyDocumentTransform(const QTransform &t, DocTransformKind)
{
    transform = transform * t;
}

bool Annotation::hitTest(const QPointF &p, double tolerance) const
{
    if (outline().containsPoint(p, Qt::OddEvenFill))
        return true;
    return tolerance > 0 && boundingRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p);
}

QList<QPointF> Annotation::handlePoints() const
{
    const QRectF r = boundingRect();
    return {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
}

void Annotation::resizeTo(const QRectF &oldBounds, const QRectF &newBounds)
{
    if (oldBounds.width() <= 0 || oldBounds.height() <= 0)
        return;
    transform = transform * QTransform::fromTranslate(-oldBounds.left(), -oldBounds.top())
                * QTransform::fromScale(newBounds.width() / oldBounds.width(),
                                        newBounds.height() / oldBounds.height())
                * QTransform::fromTranslate(newBounds.left(), newBounds.top());
}

void Annotation::moveHandle(int, const QPointF &, bool) {}

// ================================================================== ImageObject

ImageObject::ImageObject(QImage img)
    : image(std::move(img))
{
    id = nextId();
}

std::shared_ptr<Annotation> ImageObject::clone() const
{
    return std::make_shared<ImageObject>(*this);
}

void ImageObject::paint(QPainter &p) const
{
    p.drawImage(QPointF(0, 0), image);
}

void ImageObject::writeData(QDataStream &s) const
{
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    image.save(&buf, "PNG", 100); // fast, lossless
    s << png;
}

bool ImageObject::readData(QDataStream &s)
{
    QByteArray png;
    s >> png;
    if (!image.loadFromData(png, "PNG"))
        return false;
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return true;
}

// ================================================================== ShapeObject

ShapeObject::ShapeObject()
{
    id = nextId();
}

std::shared_ptr<Annotation> ShapeObject::clone() const
{
    return std::make_shared<ShapeObject>(*this);
}

QRectF ShapeObject::visualRect() const
{
    const double h = style == Style::Outline ? width / 2.0 : 0.0;
    return rect.normalized().adjusted(-h, -h, h, h);
}

void ShapeObject::paint(QPainter &p) const
{
    const QRectF r = rect.normalized();
    p.save();
    auto drawShape = [&](const QRectF &rr, double rad) {
        if (kind == Kind::Ellipse)
            p.drawEllipse(rr);
        else if (rad > 0)
            p.drawRoundedRect(rr, rad, rad);
        else
            p.drawRect(rr);
    };
    switch (style) {
    case Style::Cover:
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(color.red(), color.green(), color.blue())); // always opaque (spec H1)
        drawShape(r, radius);
        break;
    case Style::Highlight:
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        p.setPen(Qt::NoPen);
        p.setBrush(withOpacity(color, opacity));
        drawShape(r, radius);
        break;
    case Style::Outline: {
        // Fill inside the stroke, so fill and stroke never overlap: with
        // opacity < 1 there are no darker seams.
        const double h = width / 2.0;
        if (fill.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(withOpacity(fill, opacity));
            const QRectF inner = width > 0 ? r.adjusted(h, h, -h, -h) : r;
            if (inner.isValid())
                drawShape(inner, std::max(0.0, radius - h));
        }
        if (width > 0) {
            QPen pen(withOpacity(color, opacity), width);
            pen.setJoinStyle(radius > 0 || kind == Kind::Ellipse ? Qt::RoundJoin : Qt::MiterJoin);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            drawShape(r, radius);
        }
        break;
    }
    }
    p.restore();
}

bool ShapeObject::hitTest(const QPointF &pt, double tolerance) const
{
    const QRectF r = rect.normalized();
    const bool filled = style != Style::Outline || fill.alpha() > 0;
    const double half = (style == Style::Outline ? width / 2.0 : 0.0) + tolerance;
    if (kind == Kind::Rect) {
        if (!r.adjusted(-half, -half, half, half).contains(pt))
            return false;
        if (filled)
            return true;
        const QRectF inner = r.adjusted(half, half, -half, -half);
        return !(inner.isValid() && inner.contains(pt));
    }
    const double a = std::max(r.width() / 2.0, 0.5), b = std::max(r.height() / 2.0, 0.5);
    const QPointF c = r.center();
    const double d = std::hypot((pt.x() - c.x()) / a, (pt.y() - c.y()) / b);
    const double edgeDist = std::abs(d - 1.0) * std::min(a, b);
    if (filled && d <= 1.0)
        return true;
    return edgeDist <= half;
}

void ShapeObject::applyDocumentTransform(const QTransform &t, DocTransformKind kind_)
{
    if (kind_ == DocTransformKind::FreeRotate) {
        const QRectF r = rect.normalized();
        rect = r.translated(t.map(r.center()) - r.center());
        return;
    }
    rect = t.mapRect(rect.normalized());
    if (kind_ == DocTransformKind::Scale) {
        const double s = transformScale(t);
        width *= s;
        radius *= s;
    }
}

void ShapeObject::resizeTo(const QRectF &, const QRectF &newBounds)
{
    rect = newBounds.normalized();
}

QList<Prop> ShapeObject::props() const
{
    switch (style) {
    case Style::Cover: return {Prop::Color};
    case Style::Highlight: return {Prop::Color, Prop::Opacity};
    case Style::Outline: break;
    }
    if (kind == Kind::Rect)
        return {Prop::Color, Prop::Width, Prop::Fill, Prop::Opacity, Prop::Radius};
    return {Prop::Color, Prop::Width, Prop::Fill, Prop::Opacity};
}

QVariant ShapeObject::prop(Prop p) const
{
    switch (p) {
    case Prop::Color: return color;
    case Prop::Width: return width;
    case Prop::Fill: return fill;
    case Prop::Opacity: return opacity;
    case Prop::Radius: return radius;
    default: return {};
    }
}

void ShapeObject::setProp(Prop p, const QVariant &v)
{
    switch (p) {
    case Prop::Color: color = toColor(v, color); break;
    case Prop::Width: width = std::max(0.0, v.toDouble()); break;
    case Prop::Fill: fill = toColor(v, fill); break;
    case Prop::Opacity: opacity = std::clamp(v.toDouble(), 0.0, 1.0); break;
    case Prop::Radius: radius = std::max(0.0, v.toDouble()); break;
    default: break;
    }
}

void ShapeObject::writeData(QDataStream &s) const
{
    s << quint8(kind) << quint8(style) << rect << color << width << fill << opacity << radius;
}

bool ShapeObject::readData(QDataStream &s)
{
    quint8 k = 0, st = 0;
    s >> k >> st >> rect >> color >> width >> fill >> opacity >> radius;
    kind = Kind(k);
    style = Style(st);
    return k <= 1 && st <= 2;
}

// ================================================================== LineObject

LineObject::LineObject()
{
    id = nextId();
}

std::shared_ptr<Annotation> LineObject::clone() const
{
    return std::make_shared<LineObject>(*this);
}

double LineObject::headLength() const
{
    return headSize > 0 ? headSize : std::max(4.0 * width, 8.0);
}

QPainterPath LineObject::shape() const
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    const QPointF d = p2 - p1;
    const double len = std::hypot(d.x(), d.y());
    const double hw = width / 2.0;
    if (len < 1e-6) {
        path.addEllipse(p1, hw, hw);
        return path;
    }
    const QPointF u = d / len;
    const QPointF n(-u.y(), u.x());
    const int heads = int(headStart) + int(headEnd);
    const double hl = heads ? std::min(headLength(), len / heads) : 0.0;
    const double hhw = std::max(hl * 0.45, hw); // half width of the head base

    const QPointF s = headStart ? p1 + u * (hl * 0.8) : p1;
    const QPointF e = headEnd ? p2 - u * (hl * 0.8) : p2;
    QPainterPath shaft;
    shaft.moveTo(s + n * hw);
    shaft.lineTo(e + n * hw);
    shaft.lineTo(e - n * hw);
    shaft.lineTo(s - n * hw);
    shaft.closeSubpath();
    // Round caps must be united, not added: an added ellipse runs the other
    // way round and cancels out with the shaft under the winding rule.
    auto cap = [&](const QPointF &c) {
        QPainterPath e;
        e.addEllipse(c, hw, hw);
        return e;
    };
    if (!headStart)
        shaft = shaft.united(cap(p1));
    if (!headEnd)
        shaft = shaft.united(cap(p2));
    auto head = [&](const QPointF &tip, const QPointF &dir) {
        const QPointF base = tip - dir * hl;
        QPainterPath h;
        h.moveTo(tip);
        h.lineTo(base + n * hhw);
        h.lineTo(base - n * hhw);
        h.closeSubpath();
        return h;
    };
    path = shaft;
    if (headEnd)
        path = path.united(head(p2, u));
    if (headStart)
        path = path.united(head(p1, -u));
    return path.simplified();
}

QRectF LineObject::visualRect() const
{
    const double m = std::max(width / 2.0, (headStart || headEnd) ? headLength() * 0.5 : 0.0) + 1.0;
    return localRect().adjusted(-m, -m, m, m);
}

void LineObject::paint(QPainter &p) const
{
    p.fillPath(shape(), withOpacity(color, opacity));
}

bool LineObject::hitTest(const QPointF &pt, double tolerance) const
{
    if (distanceToSegment(pt, p1, p2) <= width / 2.0 + tolerance)
        return true;
    return (headStart || headEnd) && shape().contains(pt);
}

void LineObject::applyDocumentTransform(const QTransform &t, DocTransformKind kind)
{
    p1 = t.map(p1);
    p2 = t.map(p2);
    if (kind == DocTransformKind::Scale) {
        const double s = transformScale(t);
        width *= s;
        headSize *= s;
    }
}

void LineObject::moveHandle(int index, const QPointF &docPoint, bool constrain)
{
    const QPointF other = index == 0 ? p2 : p1;
    const QPointF q = constrain ? snapAngle(other, docPoint) : docPoint;
    (index == 0 ? p1 : p2) = q;
}

QList<Prop> LineObject::props() const
{
    return {Prop::Color, Prop::Width, Prop::Opacity, Prop::HeadStart, Prop::HeadEnd, Prop::HeadSize};
}

QVariant LineObject::prop(Prop p) const
{
    switch (p) {
    case Prop::Color: return color;
    case Prop::Width: return width;
    case Prop::Opacity: return opacity;
    case Prop::HeadStart: return headStart;
    case Prop::HeadEnd: return headEnd;
    case Prop::HeadSize: return headSize;
    default: return {};
    }
}

void LineObject::setProp(Prop p, const QVariant &v)
{
    switch (p) {
    case Prop::Color: color = toColor(v, color); break;
    case Prop::Width: width = std::max(0.5, v.toDouble()); break;
    case Prop::Opacity: opacity = std::clamp(v.toDouble(), 0.0, 1.0); break;
    case Prop::HeadStart: headStart = v.toBool(); break;
    case Prop::HeadEnd: headEnd = v.toBool(); break;
    case Prop::HeadSize: headSize = std::max(0.0, v.toDouble()); break;
    default: break;
    }
}

void LineObject::writeData(QDataStream &s) const
{
    s << p1 << p2 << color << width << opacity << headStart << headEnd << headSize;
}

bool LineObject::readData(QDataStream &s)
{
    s >> p1 >> p2 >> color >> width >> opacity >> headStart >> headEnd >> headSize;
    return true;
}

// ================================================================== StrokeObject

StrokeObject::StrokeObject()
{
    id = nextId();
}

std::shared_ptr<Annotation> StrokeObject::clone() const
{
    return std::make_shared<StrokeObject>(*this);
}

QPainterPath StrokeObject::centerPath() const
{
    QPainterPath path;
    if (points.isEmpty())
        return path;
    path.moveTo(points.first());
    if (points.size() == 2) {
        path.lineTo(points[1]);
        return path;
    }
    // Quadratic smoothing through segment midpoints.
    for (int i = 1; i < points.size(); ++i) {
        const QPointF mid = (points[i - 1] + points[i]) / 2.0;
        if (i == 1)
            path.lineTo(mid);
        else
            path.quadTo(points[i - 1], mid);
    }
    path.lineTo(points.last());
    return path;
}

QPainterPath StrokeObject::shape() const
{
    const size_t key = qHashBits(points.constData(), size_t(points.size()) * sizeof(QPointF), qHash(width));
    if (key == m_shapeKey && !m_shapeCache.isEmpty())
        return m_shapeCache;
    QPainterPath path;
    const QRectF b = points.boundingRect();
    if (points.size() <= 1 || (b.width() < 1e-6 && b.height() < 1e-6)) {
        if (!points.isEmpty())
            path.addEllipse(points.first(), width / 2.0, width / 2.0);
    } else {
        QPainterPathStroker stroker;
        stroker.setWidth(width);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        path = stroker.createStroke(centerPath());
        path.setFillRule(Qt::WindingFill); // self-overlaps are filled once (spec D4)
    }
    m_shapeCache = path;
    m_shapeKey = key;
    return path;
}

QRectF StrokeObject::visualRect() const
{
    const double h = width / 2.0 + 1.0;
    return points.boundingRect().adjusted(-h, -h, h, h);
}

void StrokeObject::paint(QPainter &p) const
{
    p.save();
    if (highlighter)
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
    p.fillPath(shape(), withOpacity(color, opacity));
    p.restore();
}

bool StrokeObject::hitTest(const QPointF &pt, double tolerance) const
{
    const double limit = width / 2.0 + tolerance;
    if (points.size() == 1)
        return QLineF(pt, points.first()).length() <= limit;
    for (int i = 1; i < points.size(); ++i) {
        if (distanceToSegment(pt, points[i - 1], points[i]) <= limit)
            return true;
    }
    return false;
}

void StrokeObject::applyDocumentTransform(const QTransform &t, DocTransformKind kind)
{
    points = t.map(points);
    if (kind == DocTransformKind::Scale)
        width *= transformScale(t);
}

void StrokeObject::resizeTo(const QRectF &oldBounds, const QRectF &newBounds)
{
    const double sx = oldBounds.width() > 1e-6 ? newBounds.width() / oldBounds.width() : 1.0;
    const double sy = oldBounds.height() > 1e-6 ? newBounds.height() / oldBounds.height() : 1.0;
    for (QPointF &pt : points)
        pt = newBounds.topLeft() + QPointF((pt.x() - oldBounds.left()) * sx, (pt.y() - oldBounds.top()) * sy);
}

QList<Prop> StrokeObject::props() const
{
    return {Prop::Color, Prop::Width, Prop::Opacity};
}

QVariant StrokeObject::prop(Prop p) const
{
    switch (p) {
    case Prop::Color: return color;
    case Prop::Width: return width;
    case Prop::Opacity: return opacity;
    default: return {};
    }
}

void StrokeObject::setProp(Prop p, const QVariant &v)
{
    switch (p) {
    case Prop::Color: color = toColor(v, color); break;
    case Prop::Width: width = std::max(0.5, v.toDouble()); break;
    case Prop::Opacity: opacity = std::clamp(v.toDouble(), 0.0, 1.0); break;
    default: break;
    }
}

void StrokeObject::writeData(QDataStream &s) const
{
    s << points << color << width << opacity << highlighter;
}

bool StrokeObject::readData(QDataStream &s)
{
    s >> points >> color >> width >> opacity >> highlighter;
    return true;
}

// ================================================================== TextObject

TextObject::TextObject()
{
    id = nextId();
}

std::shared_ptr<Annotation> TextObject::clone() const
{
    return std::make_shared<TextObject>(*this);
}

double TextObject::padding() const
{
    if (bubble)
        return std::max(6.0, std::round(pixelSize * 0.5));
    return std::max(2.0, std::round(pixelSize * 0.2));
}

QPainterPath TextObject::bubblePath() const
{
    const QRectF r = localRect();
    const double radius = std::min(r.height() / 2.0, std::max(6.0, pixelSize * 0.6));
    QPainterPath body;
    body.addRoundedRect(r, radius, radius);
    bool ok = false;
    const QPointF tip = transform.inverted(&ok).map(tail);
    if (!ok || body.contains(tip))
        return body;
    const QPointF c = r.center();
    const QPointF d = tip - c;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-6)
        return body;
    const QPointF n(-d.y() / len, d.x() / len);
    const double half = std::clamp(std::min(r.width(), r.height()) * 0.18, 4.0, 40.0);
    QPainterPath tri;
    tri.moveTo(c + n * half);
    tri.lineTo(tip);
    tri.lineTo(c - n * half);
    tri.closeSubpath();
    return body.united(tri).simplified();
}

QRectF TextObject::visualRect() const
{
    if (!bubble)
        return boundingRect();
    const double m = std::max(1.5, pixelSize / 12.0);
    return transform.mapRect(bubblePath().boundingRect()).adjusted(-m, -m, m, m);
}

QFont TextObject::font(double scale) const
{
    QFont f = family.isEmpty() ? QGuiApplication::font() : QFont(family);
    f.setPixelSize(std::max(1, int(std::lround(pixelSize * scale))));
    f.setBold(bold);
    f.setItalic(italic);
    f.setUnderline(underline);
    // No hinting: layout scales proportionally between editor zoom and export.
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

void TextObject::fillDocument(QTextDocument *doc, double scale, bool colors) const
{
    doc->setDocumentMargin(0);
    doc->setDefaultFont(font(scale));
    QTextOption opt;
    opt.setAlignment(Qt::Alignment(align));
    opt.setWrapMode(fixedWidth > 0 ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    doc->setDefaultTextOption(opt);
    doc->setPlainText(text.isEmpty() ? QStringLiteral(" ") : text);
    applySpans(doc, spans, colors);
    if (fixedWidth > 0) {
        doc->setTextWidth(fixedWidth * scale);
    } else {
        doc->setTextWidth(-1);
        doc->setTextWidth(doc->idealWidth()); // alignment of multiple lines
    }
}

void TextObject::applySpans(QTextDocument *doc, const QList<TextSpan> &spans, bool colors)
{
    const int length = doc->characterCount() - 1; // without the final paragraph separator
    QTextCursor c(doc);
    for (const TextSpan &sp : spans) {
        const int a = std::clamp(sp.start, 0, length), b = std::clamp(sp.start + sp.length, 0, length);
        if (b <= a)
            continue;
        QTextCharFormat f;
        if (sp.set & TextSpan::Bold)
            f.setFontWeight(sp.on & TextSpan::Bold ? QFont::Bold : QFont::Normal);
        if (sp.set & TextSpan::Italic)
            f.setFontItalic(sp.on & TextSpan::Italic);
        if (sp.set & TextSpan::Underline)
            f.setFontUnderline(sp.on & TextSpan::Underline);
        if (colors && (sp.set & TextSpan::Color))
            f.setForeground(sp.color);
        c.setPosition(a);
        c.setPosition(b, QTextCursor::KeepAnchor);
        c.mergeCharFormat(f);
    }
}

QList<TextSpan> TextObject::spansFrom(const QTextDocument *doc) const
{
    QList<TextSpan> out;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            const QTextCharFormat f = frag.charFormat();
            TextSpan sp;
            if (f.hasProperty(QTextFormat::FontWeight) && (f.fontWeight() >= QFont::DemiBold) != bold) {
                sp.set |= TextSpan::Bold;
                sp.on |= bold ? 0 : TextSpan::Bold;
            }
            if (f.hasProperty(QTextFormat::FontItalic) && f.fontItalic() != italic) {
                sp.set |= TextSpan::Italic;
                sp.on |= italic ? 0 : TextSpan::Italic;
            }
            if ((f.hasProperty(QTextFormat::TextUnderlineStyle) || f.hasProperty(QTextFormat::FontUnderline))
                && f.fontUnderline() != underline) {
                sp.set |= TextSpan::Underline;
                sp.on |= underline ? 0 : TextSpan::Underline;
            }
            if (f.hasProperty(QTextFormat::ForegroundBrush) && f.foreground().color() != color) {
                sp.set |= TextSpan::Color;
                sp.color = f.foreground().color();
            }
            if (!sp.set)
                continue;
            sp.start = frag.position();
            sp.length = frag.length();
            if (!out.isEmpty()) {
                TextSpan &last = out.last();
                TextSpan probe = sp;
                probe.start = last.start;
                probe.length = last.length;
                if (last.start + last.length == sp.start && probe == last) {
                    last.length += sp.length;
                    continue;
                }
            }
            out.append(sp);
        }
    }
    return out;
}

bool TextObject::boldAt(int pos) const
{
    for (const TextSpan &sp : spans)
        if ((sp.set & TextSpan::Bold) && pos >= sp.start && pos < sp.start + sp.length)
            return sp.on & TextSpan::Bold;
    return bold;
}

QSizeF TextObject::textSize() const
{
    QString spanKey;
    for (const TextSpan &sp : spans)
        spanKey += QStringLiteral("%1,%2,%3,%4,%5;").arg(sp.start).arg(sp.length).arg(sp.set).arg(sp.on).arg(sp.color.rgba());
    const QString key = QStringLiteral("%1|%2|%3|%4%5%6|%7|%8|%9")
                            .arg(text, family)
                            .arg(pixelSize)
                            .arg(bold)
                            .arg(italic)
                            .arg(underline)
                            .arg(align)
                            .arg(fixedWidth)
                            .arg(spanKey);
    if (key == m_sizeKey)
        return m_sizeCache;
    QTextDocument doc;
    fillDocument(&doc);
    QSizeF s = doc.size();
    if (fixedWidth > 0)
        s.setWidth(fixedWidth);
    m_sizeKey = key;
    m_sizeCache = s;
    return s;
}

QRectF TextObject::localRect() const
{
    const double pad = padding();
    const QSizeF s = textSize();
    return QRectF(0, 0, s.width() + 2 * pad, s.height() + 2 * pad);
}

void TextObject::paint(QPainter &p) const
{
    const QRectF r = localRect();
    const double pad = padding();
    p.save();
    p.setOpacity(p.opacity() * std::clamp(opacity, 0.0, 1.0));
    if (bubble) {
        p.setPen(QPen(color, std::max(1.5, pixelSize / 12.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(background.alpha() > 0 ? background : QColor(Qt::white));
        p.drawPath(bubblePath());
    } else if (background.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(background);
        p.drawRoundedRect(r, pad * 0.6, pad * 0.6);
    }
    QTextDocument doc;
    fillDocument(&doc);
    p.translate(pad, pad);
    QAbstractTextDocumentLayout::PaintContext ctx;
    if (outlineColor.alpha() > 0) {
        // The halo ignores span colors: one outline color for all text.
        QTextDocument halo;
        fillDocument(&halo, 1.0, false);
        // Halo for readability on busy backgrounds (spec 4 of the analysis).
        const double o = std::max(1.0, pixelSize / 14.0);
        ctx.palette.setColor(QPalette::Text, outlineColor);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0)
                    continue;
                p.save();
                p.translate(dx * o, dy * o);
                halo.documentLayout()->draw(&p, ctx);
                p.restore();
            }
        }
    }
    ctx.palette.setColor(QPalette::Text, color);
    doc.documentLayout()->draw(&p, ctx);
    p.restore();
}

bool TextObject::hitTest(const QPointF &pt, double tolerance) const
{
    if (bubble) {
        bool ok = false;
        const QPointF local = transform.inverted(&ok).map(pt);
        if (ok) {
            QPainterPathStroker st;
            st.setWidth(2 * tolerance + 1);
            const QPainterPath path = bubblePath();
            if (path.contains(local) || st.createStroke(path).contains(local))
                return true;
        }
    }
    return Annotation::hitTest(pt, tolerance);
}

void TextObject::applyDocumentTransform(const QTransform &t, DocTransformKind kind)
{
    tail = t.map(tail); // the tail keeps pointing at the same spot of the image
    switch (kind) {
    case DocTransformKind::FreeRotate:
    case DocTransformKind::Mirror: {
        // Decision J2: text keeps reading direction, only its position mirrors.
        const QPointF c = boundingRect().center();
        const QPointF nc = t.map(c);
        transform = transform * QTransform::fromTranslate(nc.x() - c.x(), nc.y() - c.y());
        break;
    }
    case DocTransformKind::Scale: {
        const double s = transformScale(t);
        const QPointF origin = t.map(transform.map(QPointF(0, 0)));
        pixelSize = std::max(1, int(std::lround(pixelSize * s)));
        fixedWidth *= s;
        transform = QTransform(transform.m11(), transform.m12(), transform.m21(), transform.m22(),
                               origin.x(), origin.y());
        break;
    }
    default:
        transform = transform * t;
        break;
    }
}

QList<QPointF> TextObject::handlePoints() const
{
    const QRectF r = localRect();
    QList<QPointF> h{transform.map(QPointF(r.right(), r.center().y()))};
    if (bubble)
        h.append(tail);
    return h;
}

void TextObject::moveHandle(int index, const QPointF &docPoint, bool)
{
    if (index == 1) {
        tail = docPoint;
        return;
    }
    bool ok = false;
    const QTransform inv = transform.inverted(&ok);
    if (!ok)
        return;
    const QPointF local = inv.map(docPoint);
    fixedWidth = std::max(double(pixelSize), std::round(local.x() - 2 * padding()));
}

QList<Prop> TextObject::props() const
{
    return {Prop::Color, Prop::FontFamily, Prop::FontSize, Prop::Bold, Prop::Italic, Prop::Underline,
            Prop::Align, Prop::Background, Prop::Outline, Prop::Opacity};
}

QVariant TextObject::prop(Prop p) const
{
    switch (p) {
    case Prop::Color: return color;
    case Prop::Opacity: return opacity;
    case Prop::FontFamily: return family;
    case Prop::FontSize: return pixelSize;
    case Prop::Bold: return bold;
    case Prop::Italic: return italic;
    case Prop::Underline: return underline;
    case Prop::Align: return align;
    case Prop::Background: return background;
    case Prop::Outline: return outlineColor;
    default: return {};
    }
}

void TextObject::setProp(Prop p, const QVariant &v)
{
    switch (p) {
    case Prop::Color: color = toColor(v, color); break;
    case Prop::Opacity: opacity = std::clamp(v.toDouble(), 0.0, 1.0); break;
    case Prop::FontFamily: family = v.toString(); break;
    case Prop::FontSize: pixelSize = std::clamp(v.toInt(), 1, 2000); break;
    case Prop::Bold: bold = v.toBool(); break;
    case Prop::Italic: italic = v.toBool(); break;
    case Prop::Underline: underline = v.toBool(); break;
    case Prop::Align: align = v.toInt(); break;
    case Prop::Background: background = toColor(v, background); break;
    case Prop::Outline: outlineColor = toColor(v, outlineColor); break;
    default: break;
    }
}

void TextObject::writeData(QDataStream &s) const
{
    s << text << family << qint32(pixelSize) << bold << italic << underline << qint32(align) << color
      << background << outlineColor << opacity << fixedWidth << bubble << tail;
    s << quint32(spans.size());
    for (const TextSpan &sp : spans)
        s << qint32(sp.start) << qint32(sp.length) << sp.set << sp.on << sp.color;
}

bool TextObject::readData(QDataStream &s)
{
    qint32 px = 0, al = 0;
    s >> text >> family >> px >> bold >> italic >> underline >> al >> color >> background >> outlineColor
        >> opacity >> fixedWidth;
    if (g_readVersion >= 3)
        s >> bubble >> tail;
    spans.clear();
    if (g_readVersion >= 4) {
        quint32 n = 0;
        s >> n;
        if (n > 100000)
            return false;
        for (quint32 i = 0; i < n; ++i) {
            TextSpan sp;
            qint32 a = 0, l = 0;
            s >> a >> l >> sp.set >> sp.on >> sp.color;
            sp.start = a;
            sp.length = l;
            spans.append(sp);
        }
    }
    pixelSize = px;
    align = al;
    return px > 0;
}

// ================================================================== MarkerObject

MarkerObject::MarkerObject()
{
    id = nextId();
}

std::shared_ptr<Annotation> MarkerObject::clone() const
{
    return std::make_shared<MarkerObject>(*this);
}

QRectF MarkerObject::localRect() const
{
    const double r = diameter / 2.0;
    return QRectF(center - QPointF(r, r), QSizeF(diameter, diameter));
}

QColor MarkerObject::textColor() const
{
    // Relative luminance (sRGB approximation) decides black or white digits.
    const double l = 0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
    return l > 0.55 ? QColor(Qt::black) : QColor(Qt::white);
}

void MarkerObject::paint(QPainter &p) const
{
    p.save();
    p.setOpacity(p.opacity() * std::clamp(opacity, 0.0, 1.0));
    const QRectF r = localRect();
    p.setPen(QPen(QColor(255, 255, 255, 230), std::max(1.0, diameter / 16.0)));
    p.setBrush(color);
    p.drawEllipse(r);
    QFont f = QGuiApplication::font();
    f.setBold(true);
    const QString text = QString::number(number);
    f.setPixelSize(std::max(1, int(std::lround(diameter * (text.size() > 2 ? 0.42 : text.size() > 1 ? 0.5 : 0.6)))));
    f.setHintingPreference(QFont::PreferNoHinting);
    p.setFont(f);
    p.setPen(textColor());
    p.drawText(r, Qt::AlignCenter, text);
    p.restore();
}

bool MarkerObject::hitTest(const QPointF &pt, double tolerance) const
{
    return QLineF(pt, center).length() <= diameter / 2.0 + tolerance;
}

void MarkerObject::applyDocumentTransform(const QTransform &t, DocTransformKind kind)
{
    center = t.map(center);
    if (kind == DocTransformKind::Scale)
        diameter *= transformScale(t);
}

void MarkerObject::resizeTo(const QRectF &, const QRectF &n)
{
    diameter = std::max(8.0, std::min(n.width(), n.height()));
    center = n.center();
}

QList<Prop> MarkerObject::props() const
{
    return {Prop::Color, Prop::Size, Prop::Number, Prop::Opacity};
}

QVariant MarkerObject::prop(Prop p) const
{
    switch (p) {
    case Prop::Color: return color;
    case Prop::Size: return diameter;
    case Prop::Number: return number;
    case Prop::Opacity: return opacity;
    default: return {};
    }
}

void MarkerObject::setProp(Prop p, const QVariant &v)
{
    switch (p) {
    case Prop::Color: color = toColor(v, color); break;
    case Prop::Size: diameter = std::clamp(v.toDouble(), 8.0, 1000.0); break;
    case Prop::Number: number = std::clamp(v.toInt(), 0, 9999); break;
    case Prop::Opacity: opacity = std::clamp(v.toDouble(), 0.0, 1.0); break;
    default: break;
    }
}

void MarkerObject::writeData(QDataStream &s) const
{
    s << center << diameter << qint32(number) << color << opacity;
}

bool MarkerObject::readData(QDataStream &s)
{
    qint32 n = 0;
    s >> center >> diameter >> n >> color >> opacity;
    number = n;
    return diameter > 0;
}

// ================================================================== PolylineObject

PolylineObject::PolylineObject()
{
    id = nextId();
}

std::shared_ptr<Annotation> PolylineObject::clone() const
{
    return std::make_shared<PolylineObject>(*this);
}

double PolylineObject::headLength() const
{
    return headSize > 0 ? headSize : std::max(4.0 * width, 8.0);
}

QPainterPath PolylineObject::shape() const
{
    QPainterPath result;
    if (points.size() < 2)
        return result;
    QPolygonF pts = points;
    const bool hs = !closed && headStart, he = !closed && headEnd;
    const double hl = headLength();
    const double hw = width / 2.0;
    const double hhw = std::max(hl * 0.45, hw);
    // Shorten the ends under the heads so the shaft does not poke through.
    auto shorten = [&](int end, int neighbour) {
        const QPointF d = pts[end] - pts[neighbour];
        const double len = std::hypot(d.x(), d.y());
        if (len > 1e-6)
            pts[end] -= d / len * std::min(len, hl * 0.8);
    };
    if (he)
        shorten(int(pts.size()) - 1, int(pts.size()) - 2);
    if (hs)
        shorten(0, 1);
    QPainterPath center;
    center.moveTo(pts.first());
    for (int i = 1; i < pts.size(); ++i)
        center.lineTo(pts[i]);
    if (closed)
        center.closeSubpath();
    QPainterPathStroker st;
    st.setWidth(width);
    st.setJoinStyle(Qt::RoundJoin);
    st.setCapStyle(Qt::RoundCap);
    result = st.createStroke(center);
    result.setFillRule(Qt::WindingFill);
    auto head = [&](const QPointF &tip, const QPointF &from) {
        const QPointF d = tip - from;
        const double len = std::hypot(d.x(), d.y());
        QPainterPath h;
        if (len < 1e-6)
            return h;
        const QPointF u = d / len, n(-u.y(), u.x());
        const QPointF base = tip - u * std::min(hl, len);
        h.moveTo(tip);
        h.lineTo(base + n * hhw);
        h.lineTo(base - n * hhw);
        h.closeSubpath();
        return h;
    };
    result = result.simplified();
    if (he)
        result = result.united(head(points.last(), points[points.size() - 2]));
    if (hs)
        result = result.united(head(points.first(), points[1]));
    return result;
}

QRectF PolylineObject::visualRect() const
{
    const double m = std::max(width / 2.0, (headStart || headEnd) && !closed ? headLength() * 0.5 : 0.0) + 1.0;
    return localRect().adjusted(-m, -m, m, m);
}

void PolylineObject::paint(QPainter &p) const
{
    if (closed && fill.alpha() > 0 && points.size() >= 3) {
        p.setPen(Qt::NoPen);
        p.setBrush(withOpacity(fill, opacity));
        p.drawPolygon(points);
    }
    p.fillPath(shape(), withOpacity(color, opacity));
}

bool PolylineObject::hitTest(const QPointF &pt, double tolerance) const
{
    const int n = int(points.size());
    for (int i = 1; i < n; ++i) {
        if (distanceToSegment(pt, points[i - 1], points[i]) <= width / 2.0 + tolerance)
            return true;
    }
    if (closed && n >= 3) {
        if (distanceToSegment(pt, points.last(), points.first()) <= width / 2.0 + tolerance)
            return true;
        if (fill.alpha() > 0 && points.containsPoint(pt, Qt::OddEvenFill))
            return true;
    }
    return (headStart || headEnd) && shape().contains(pt);
}

void PolylineObject::applyDocumentTransform(const QTransform &t, DocTransformKind kind)
{
    points = t.map(points);
    if (kind == DocTransformKind::Scale) {
        const double s = transformScale(t);
        width *= s;
        headSize *= s;
    }
}

void PolylineObject::moveHandle(int index, const QPointF &docPoint, bool constrain)
{
    if (index < 0 || index >= points.size())
        return;
    QPointF q = docPoint;
    if (constrain && points.size() > 1)
        q = snapAngle(points[index > 0 ? index - 1 : 1], docPoint);
    points[index] = q;
}

QList<Prop> PolylineObject::props() const
{
    if (closed)
        return {Prop::Color, Prop::Width, Prop::Fill, Prop::Opacity};
    return {Prop::Color, Prop::Width, Prop::Opacity, Prop::HeadStart, Prop::HeadEnd, Prop::HeadSize};
}

QVariant PolylineObject::prop(Prop p) const
{
    switch (p) {
    case Prop::Color: return color;
    case Prop::Width: return width;
    case Prop::Opacity: return opacity;
    case Prop::Fill: return fill;
    case Prop::HeadStart: return headStart;
    case Prop::HeadEnd: return headEnd;
    case Prop::HeadSize: return headSize;
    default: return {};
    }
}

void PolylineObject::setProp(Prop p, const QVariant &v)
{
    switch (p) {
    case Prop::Color: color = toColor(v, color); break;
    case Prop::Width: width = std::clamp(v.toDouble(), 0.5, 500.0); break;
    case Prop::Opacity: opacity = std::clamp(v.toDouble(), 0.0, 1.0); break;
    case Prop::Fill: fill = toColor(v, fill); break;
    case Prop::HeadStart: headStart = v.toBool(); break;
    case Prop::HeadEnd: headEnd = v.toBool(); break;
    case Prop::HeadSize: headSize = std::max(0.0, v.toDouble()); break;
    default: break;
    }
}

void PolylineObject::writeData(QDataStream &s) const
{
    s << points << closed << color << width << opacity << fill << headStart << headEnd << headSize;
}

bool PolylineObject::readData(QDataStream &s)
{
    s >> points >> closed >> color >> width >> opacity >> fill >> headStart >> headEnd >> headSize;
    return points.size() >= 2;
}

// ================================================================== serialization

QByteArray serializeObjects(const ObjectList &objects)
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_5);
    s << kMagic << kVersion << quint32(objects.size());
    for (const auto &o : objects) {
        s << quint8(o->type()) << o->transform << quint8(o->origin);
        o->writeData(s);
    }
    return out;
}

ObjectList deserializeObjects(const QByteArray &data)
{
    ObjectList result;
    QDataStream s(data);
    s.setVersion(QDataStream::Qt_6_5);
    quint32 magic = 0, count = 0;
    quint16 version = 0;
    s >> magic >> version >> count;
    if (magic != kMagic || version < 2 || version > kVersion || count > 100000)
        return {};
    g_readVersion = version;
    for (quint32 i = 0; i < count; ++i) {
        quint8 type = 0, origin = 0;
        QTransform t;
        s >> type >> t >> origin;
        std::shared_ptr<Annotation> obj;
        switch (Annotation::Type(type)) {
        case Annotation::Type::Image: obj = std::make_shared<ImageObject>(); break;
        case Annotation::Type::Text: obj = std::make_shared<TextObject>(); break;
        case Annotation::Type::Line: obj = std::make_shared<LineObject>(); break;
        case Annotation::Type::Shape: obj = std::make_shared<ShapeObject>(); break;
        case Annotation::Type::Stroke: obj = std::make_shared<StrokeObject>(); break;
        case Annotation::Type::Marker: obj = std::make_shared<MarkerObject>(); break;
        case Annotation::Type::Polyline: obj = std::make_shared<PolylineObject>(); break;
        }
        if (!obj || !obj->readData(s) || s.status() != QDataStream::Ok)
            return {};
        obj->transform = t;
        obj->origin = ToolId(origin);
        obj->id = Annotation::nextId();
        result.append(obj);
    }
    return result;
}
