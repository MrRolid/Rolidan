#pragma once

#include "toolid.h"

#include <QColor>
#include <QDataStream>
#include <QFont>
#include <QImage>
#include <QList>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QVariant>
#include <memory>

class QPainter;
class QTextDocument;

// How a document-wide operation affects objects (spec C4).
// FreeRotate: arbitrary angle. Boxes (shapes, texts) keep their orientation and
// follow with their center; point geometry and pasted images rotate exactly.
enum class DocTransformKind { Translate, Rotate, Mirror, Scale, FreeRotate };

// An editable object above the raster base (spec C1). Objects are treated as
// immutable once stored in a DocState: to change one, clone it, modify the
// clone and replace the pointer.
//
// Geometry: ImageObject and TextObject keep local geometry plus `transform`
// (they can be rotated with the document). Shapes, lines and strokes store
// their geometry directly in document coordinates (transform stays identity),
// so stroke widths and arrow heads never get distorted.
class Annotation
{
public:
    enum class Type : quint8 { Image = 1, Text, Line, Shape, Stroke, Marker, Polyline };
    enum class HandleMode { Corners, Endpoints, TextWidth };

    virtual ~Annotation() = default;

    virtual Type type() const = 0;
    virtual std::shared_ptr<Annotation> clone() const = 0;
    virtual QRectF localRect() const = 0;
    // Paint in local coordinates; the caller has already set `transform`.
    virtual void paint(QPainter &p) const = 0;
    virtual qint64 memoryBytes() const { return 256; }
    virtual qint64 sharedDataKey() const { return 0; }

    virtual void applyDocumentTransform(const QTransform &t, DocTransformKind kind);

    // Geometry rectangle in document coordinates (handles, resizing).
    QRectF boundingRect() const { return transform.mapRect(localRect()); }
    // Everything that gets painted, incl. stroke width and arrow heads.
    virtual QRectF visualRect() const { return boundingRect(); }
    QPolygonF outline() const { return transform.map(QPolygonF(localRect())); }
    // `tolerance` is in document pixels (callers convert from screen pixels).
    virtual bool hitTest(const QPointF &docPoint, double tolerance) const;
    bool contains(const QPointF &docPoint) const { return hitTest(docPoint, 0.0); }

    // ---- editing via handles (spec D1, D3) ----
    virtual HandleMode handleMode() const { return HandleMode::Corners; }
    virtual bool keepsAspect() const { return false; }
    // Handle positions in document coordinates. Corners: TL, TR, BR, BL.
    virtual QList<QPointF> handlePoints() const;
    // Corners mode: fit the object from `oldBounds` into `newBounds`.
    virtual void resizeTo(const QRectF &oldBounds, const QRectF &newBounds);
    // Endpoints / TextWidth mode. `constrain` = Shift (15° steps for lines).
    virtual void moveHandle(int index, const QPointF &docPoint, bool constrain);

    // ---- properties (spec D1) ----
    virtual QList<Prop> props() const { return {}; }
    virtual QVariant prop(Prop) const { return {}; }
    virtual void setProp(Prop, const QVariant &) {}

    quint64 id = 0;
    QTransform transform;
    ToolId origin = ToolId::Pointer; // tool that created it (for remembering defaults)

    static quint64 nextId();

protected:
    virtual void writeData(QDataStream &) const = 0;
    virtual bool readData(QDataStream &) = 0;
    friend QByteArray serializeObjects(const QList<std::shared_ptr<const Annotation>> &);
    friend QList<std::shared_ptr<const Annotation>> deserializeObjects(const QByteArray &);
};

using ObjectPtr = std::shared_ptr<const Annotation>;
using ObjectList = QList<ObjectPtr>;

// Uniform-ish scale factor of a transform (for widths, font sizes: spec C4).
double transformScale(const QTransform &t);

// ------------------------------------------------------------------ image

class ImageObject : public Annotation
{
public:
    explicit ImageObject(QImage img = {});

    Type type() const override { return Type::Image; }
    std::shared_ptr<Annotation> clone() const override;
    QRectF localRect() const override { return QRectF(QPointF(0, 0), QSizeF(image.size())); }
    void paint(QPainter &p) const override;
    qint64 memoryBytes() const override { return image.sizeInBytes(); }
    qint64 sharedDataKey() const override { return image.cacheKey(); }
    bool keepsAspect() const override { return true; }

    QImage image;

protected:
    void writeData(QDataStream &) const override;
    bool readData(QDataStream &) override;
};

// ------------------------------------------------------------------ shape

// Rectangle (optionally rounded), ellipse, highlight rectangle, opaque cover.
class ShapeObject : public Annotation
{
public:
    enum class Kind : quint8 { Rect = 0, Ellipse = 1 };
    enum class Style : quint8 { Outline = 0, Highlight = 1, Cover = 2 };

    ShapeObject();

    Type type() const override { return Type::Shape; }
    std::shared_ptr<Annotation> clone() const override;
    QRectF localRect() const override { return rect; }
    QRectF visualRect() const override;
    void paint(QPainter &p) const override;
    bool hitTest(const QPointF &docPoint, double tolerance) const override;
    void applyDocumentTransform(const QTransform &t, DocTransformKind kind) override;
    void resizeTo(const QRectF &oldBounds, const QRectF &newBounds) override;
    QList<Prop> props() const override;
    QVariant prop(Prop) const override;
    void setProp(Prop, const QVariant &) override;

    Kind kind = Kind::Rect;
    Style style = Style::Outline;
    QRectF rect;                    // document coordinates
    QColor color = Qt::red;         // outline; fill color for Highlight / Cover
    double width = 3.0;
    QColor fill = Qt::transparent;  // Outline style only
    double opacity = 1.0;
    double radius = 0.0;            // rounded corners (Rect)

protected:
    void writeData(QDataStream &) const override;
    bool readData(QDataStream &) override;
};

// ------------------------------------------------------------------ line / arrow

class LineObject : public Annotation
{
public:
    LineObject();

    Type type() const override { return Type::Line; }
    std::shared_ptr<Annotation> clone() const override;
    QRectF localRect() const override { return QRectF(p1, p2).normalized(); }
    QRectF visualRect() const override;
    void paint(QPainter &p) const override;
    bool hitTest(const QPointF &docPoint, double tolerance) const override;
    void applyDocumentTransform(const QTransform &t, DocTransformKind kind) override;
    HandleMode handleMode() const override { return HandleMode::Endpoints; }
    QList<QPointF> handlePoints() const override { return {p1, p2}; }
    void moveHandle(int index, const QPointF &docPoint, bool constrain) override;
    QList<Prop> props() const override;
    QVariant prop(Prop) const override;
    void setProp(Prop, const QVariant &) override;

    double headLength() const; // spec D3: 4x width unless set explicitly
    QPainterPath shape() const; // shaft + heads, filled once (uniform opacity)

    QPointF p1, p2;
    QColor color = Qt::red;
    double width = 4.0;
    double opacity = 1.0;
    bool headStart = false;
    bool headEnd = true;
    double headSize = 0.0;

protected:
    void writeData(QDataStream &) const override;
    bool readData(QDataStream &) override;
};

// Snap `p` to 15° steps around `origin` (Shift while drawing lines, spec D3).
QPointF snapAngle(const QPointF &origin, const QPointF &p, double stepDegrees = 15.0);

// ------------------------------------------------------------------ freehand stroke

// Brush stroke or freehand highlighter. The whole stroke is one filled outline,
// so overlapping parts of a single stroke never get darker (spec D4).
class StrokeObject : public Annotation
{
public:
    StrokeObject();

    Type type() const override { return Type::Stroke; }
    std::shared_ptr<Annotation> clone() const override;
    QRectF localRect() const override { return points.boundingRect(); }
    QRectF visualRect() const override;
    void paint(QPainter &p) const override;
    bool hitTest(const QPointF &docPoint, double tolerance) const override;
    void applyDocumentTransform(const QTransform &t, DocTransformKind kind) override;
    void resizeTo(const QRectF &oldBounds, const QRectF &newBounds) override;
    QList<Prop> props() const override;
    QVariant prop(Prop) const override;
    void setProp(Prop, const QVariant &) override;

    QPainterPath centerPath() const;
    QPainterPath shape() const;

    QPolygonF points;           // document coordinates
    QColor color = Qt::red;
    double width = 4.0;
    double opacity = 1.0;
    bool highlighter = false;   // Multiply blending (spec D4)

protected:
    void writeData(QDataStream &) const override;
    bool readData(QDataStream &) override;

private:
    mutable QPainterPath m_shapeCache;
    mutable size_t m_shapeKey = 0;
};

// ------------------------------------------------------------------ text

// Text annotation (spec D2). Sizes are in image pixels (spec C2). Style applies
// to the whole object. Position/rotation live in `transform`; local (0,0) is the
// top-left corner of the padded box.
// Formatting of a part of a text (Etapa 4: per-character formatting). Only
// the overridden attributes are stored; the rest comes from the object.
struct TextSpan
{
    enum Attr : quint8 { Bold = 1, Italic = 2, Underline = 4, Color = 8 };
    int start = 0;
    int length = 0;
    quint8 set = 0; // overridden attributes (Attr bits)
    quint8 on = 0;  // values of Bold / Italic / Underline
    QColor color;   // when Color is set
    bool operator==(const TextSpan &o) const
    {
        return start == o.start && length == o.length && set == o.set && on == o.on
               && (!(set & Color) || color == o.color);
    }
};

class TextObject : public Annotation
{
public:
    TextObject();

    Type type() const override { return Type::Text; }
    std::shared_ptr<Annotation> clone() const override;
    QRectF localRect() const override;
    void paint(QPainter &p) const override;
    bool hitTest(const QPointF &docPoint, double tolerance) const override;
    void applyDocumentTransform(const QTransform &t, DocTransformKind kind) override;
    HandleMode handleMode() const override { return HandleMode::TextWidth; }
    QList<QPointF> handlePoints() const override;
    void moveHandle(int index, const QPointF &docPoint, bool constrain) override;
    QList<Prop> props() const override;
    QVariant prop(Prop) const override;
    void setProp(Prop, const QVariant &) override;

    double padding() const;
    QFont font(double scale = 1.0) const;
    // Fill a document with this text and its formatting; `scale` is used by
    // the on-canvas editor. `colors` false leaves out span colors (outline).
    void fillDocument(QTextDocument *doc, double scale = 1.0, bool colors = true) const;
    // Per-character formatting <-> document (the on-canvas editor).
    static void applySpans(QTextDocument *doc, const QList<TextSpan> &spans, bool colors = true);
    // Differences of the document's character formats from this object's defaults.
    QList<TextSpan> spansFrom(const QTextDocument *doc) const;
    // Effective attributes of the character at `pos` (0-based).
    bool boldAt(int pos) const;

    QString text;
    QString family;
    int pixelSize = 24;
    bool bold = true;
    bool italic = false;
    bool underline = false;
    int align = Qt::AlignLeft;
    QColor color = Qt::red;
    QColor background = Qt::transparent;
    QColor outlineColor = Qt::transparent;
    double opacity = 1.0;
    double fixedWidth = 0.0;    // 0 = automatic width, otherwise wraps (spec D2)
    QList<TextSpan> spans;      // per-character formatting, sorted, non-overlapping
    bool bubble = false;        // speech bubble: background shape with a tail
    QPointF tail;               // bubble tail tip, document coordinates

    // Bubble outline (body + tail) in local coordinates.
    QPainterPath bubblePath() const;
    QRectF visualRect() const override;

protected:
    void writeData(QDataStream &) const override;
    bool readData(QDataStream &) override;

private:
    QSizeF textSize() const;
    mutable QSizeF m_sizeCache;
    mutable QString m_sizeKey;
};

// ------------------------------------------------------------------ marker

// Numbered marker for step-by-step screenshots: a filled circle with a number.
class MarkerObject : public Annotation
{
public:
    MarkerObject();

    Type type() const override { return Type::Marker; }
    std::shared_ptr<Annotation> clone() const override;
    QRectF localRect() const override;
    void paint(QPainter &p) const override;
    bool hitTest(const QPointF &docPoint, double tolerance) const override;
    void applyDocumentTransform(const QTransform &t, DocTransformKind kind) override;
    bool keepsAspect() const override { return true; }
    void resizeTo(const QRectF &oldBounds, const QRectF &newBounds) override;
    QList<Prop> props() const override;
    QVariant prop(Prop) const override;
    void setProp(Prop, const QVariant &) override;
    // Black or white, whichever reads better on `color`.
    QColor textColor() const;

    QPointF center;            // document coordinates
    double diameter = 32.0;
    int number = 1;
    QColor color = QColor(0xe5, 0x1c, 0x23);
    double opacity = 1.0;

protected:
    void writeData(QDataStream &) const override;
    bool readData(QDataStream &) override;
};

// ------------------------------------------------------------------ polyline

// Polyline (optionally with arrow heads) or closed polygon (optionally filled).
class PolylineObject : public Annotation
{
public:
    PolylineObject();

    Type type() const override { return Type::Polyline; }
    std::shared_ptr<Annotation> clone() const override;
    QRectF localRect() const override { return points.boundingRect(); }
    QRectF visualRect() const override;
    void paint(QPainter &p) const override;
    bool hitTest(const QPointF &docPoint, double tolerance) const override;
    void applyDocumentTransform(const QTransform &t, DocTransformKind kind) override;
    HandleMode handleMode() const override { return HandleMode::Endpoints; }
    QList<QPointF> handlePoints() const override { return QList<QPointF>(points.begin(), points.end()); }
    void moveHandle(int index, const QPointF &docPoint, bool constrain) override;
    QList<Prop> props() const override;
    QVariant prop(Prop) const override;
    void setProp(Prop, const QVariant &) override;

    double headLength() const;
    QPainterPath shape() const; // filled outline of the stroke incl. heads

    QPolygonF points;           // document coordinates
    bool closed = false;
    QColor color = QColor(0xe5, 0x1c, 0x23);
    double width = 4.0;
    double opacity = 1.0;
    QColor fill = Qt::transparent; // closed only
    bool headStart = false;
    bool headEnd = false;
    double headSize = 0.0;

protected:
    void writeData(QDataStream &) const override;
    bool readData(QDataStream &) override;
};

// Serialization for the internal clipboard format (and later the project format).
QByteArray serializeObjects(const ObjectList &objects);
ObjectList deserializeObjects(const QByteArray &data);
