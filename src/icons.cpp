#include "icons.h"

#include <QFont>
#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <functional>

namespace icons {
namespace {

// All drawings use a 24 × 24 grid.
struct Pen
{
    QPainter &p;
    QColor fg;
    QColor accent;

    void stroke(qreal width = 1.7, Qt::PenStyle style = Qt::SolidLine)
    {
        QPen pen(fg, width, style, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
    }
    void fill(const QColor &c) { p.setPen(Qt::NoPen); p.setBrush(c); }
    QColor soft() const { QColor c = fg; c.setAlphaF(0.22); return c; }
};

using Draw = std::function<void(Pen &)>;

void floppy(Pen &d)
{
    QPainterPath body;
    body.moveTo(4, 3.5);
    body.lineTo(16.5, 3.5);
    body.lineTo(20.5, 7.5);
    body.lineTo(20.5, 20.5);
    body.lineTo(3.5, 20.5);
    body.lineTo(3.5, 4);
    body.closeSubpath();
    d.stroke();
    d.p.drawPath(body);
    d.p.drawRect(QRectF(7.5, 3.5, 8, 5.5));   // metal shutter
    d.fill(d.fg);
    d.p.drawRect(QRectF(12.5, 4.6, 1.8, 3.2)); // shutter slot
    d.stroke();
    d.p.drawRoundedRect(QRectF(6.5, 12.5, 11, 8), 1, 1); // label
}

void folder(Pen &d)
{
    QPainterPath back;
    back.moveTo(3, 19);
    back.lineTo(3, 5);
    back.lineTo(9, 5);
    back.lineTo(11, 7.5);
    back.lineTo(20, 7.5);
    back.lineTo(20, 10);
    d.stroke();
    d.p.drawPath(back);
    QPainterPath front;
    front.moveTo(3, 19);
    front.lineTo(6.5, 10.5);
    front.lineTo(22, 10.5);
    front.lineTo(18.5, 19);
    front.closeSubpath();
    d.p.setBrush(d.soft());
    d.p.drawPath(front);
}

void curvedArrow(Pen &d)
{
    QPainterPath path;
    path.moveTo(19.5, 18.5);
    path.cubicTo(19.5, 11, 14, 8.5, 6, 9);
    d.stroke();
    d.p.drawPath(path);
    d.p.drawPolyline(QPolygonF({QPointF(10, 5), QPointF(5.5, 9), QPointF(10, 13)}));
}

void rotateArrow(Pen &d)
{
    d.stroke();
    d.p.drawArc(QRectF(4.5, 5, 15, 15), 100 * 16, 260 * 16);
    // head at the start of the arc (top, pointing left)
    d.p.drawPolyline(QPolygonF({QPointF(13.5, 1.8), QPointF(10.2, 5.2), QPointF(13.5, 8.4)}));
}

void flip(Pen &d)
{
    d.stroke(1.4, Qt::DashLine);
    d.p.drawLine(QPointF(12, 2.5), QPointF(12, 21.5));
    d.fill(d.fg);
    d.p.drawPolygon(QPolygonF({QPointF(9.5, 5), QPointF(9.5, 19), QPointF(2.5, 19)}));
    d.stroke();
    d.p.drawPolygon(QPolygonF({QPointF(14.5, 5), QPointF(14.5, 19), QPointF(21.5, 19)}));
}

QPainterPath bubblePath()
{
    QPainterPath path;
    path.addRoundedRect(QRectF(3, 3.5, 18, 12.5), 3.5, 3.5);
    QPainterPath tail;
    tail.moveTo(7.5, 15);
    tail.lineTo(5.5, 21);
    tail.lineTo(12.5, 15);
    tail.closeSubpath();
    return path.united(tail);
}

const QHash<QString, Draw> &drawings()
{
    static const QHash<QString, Draw> map = [] {
        QHash<QString, Draw> m;
        // ---------------------------------------------------------- files
        m[QStringLiteral("document-open")] = folder;
        m[QStringLiteral("document-save")] = floppy;
        m[QStringLiteral("document-save-as")] = [](Pen &d) {
            floppy(d);
            // pencil
            d.p.save();
            d.p.translate(17.5, 17.5);
            d.p.rotate(-45);
            QPen halo(QGuiApplication::palette().color(QPalette::Window), 4.2, Qt::SolidLine, Qt::RoundCap);
            d.p.setPen(halo);
            d.p.drawLine(QPointF(-4, 0), QPointF(5, 0));
            d.p.setPen(QPen(d.accent, 2.4, Qt::SolidLine, Qt::FlatCap));
            d.p.drawLine(QPointF(-2.5, 0), QPointF(5, 0));
            d.fill(d.accent);
            d.p.drawPolygon(QPolygonF({QPointF(-2.6, -1.2), QPointF(-2.6, 1.2), QPointF(-5, 0)}));
            d.p.restore();
        };
        m[QStringLiteral("document-save-project")] = [](Pen &d) {
            floppy(d);
            // small stacked layers: editable annotations
            const QColor window = QGuiApplication::palette().color(QPalette::Window);
            for (int i = 0; i < 2; ++i) {
                const QRectF r(14 + i * 2.6, 13.5 + i * 2.6, 6.5, 6.5);
                d.p.setPen(QPen(window, 2.4));
                d.p.setBrush(window);
                d.p.drawRect(r);
                d.p.setPen(QPen(d.accent, 1.5));
                d.p.setBrush(i ? d.accent : window);
                d.p.drawRect(r);
            }
        };
        m[QStringLiteral("edit-paste")] = [](Pen &d) {
            d.stroke();
            d.p.drawRoundedRect(QRectF(4.5, 4.5, 15, 17), 1.5, 1.5);
            d.fill(d.fg);
            d.p.drawRoundedRect(QRectF(8.5, 2.5, 7, 4), 1, 1);
            d.stroke(1.4);
            d.p.drawLine(QPointF(8, 11), QPointF(16, 11));
            d.p.drawLine(QPointF(8, 14.5), QPointF(16, 14.5));
            d.p.drawLine(QPointF(8, 18), QPointF(13, 18));
        };
        m[QStringLiteral("edit-copy")] = [](Pen &d) {
            d.stroke();
            d.p.drawRoundedRect(QRectF(3.5, 3.5, 11.5, 13), 1.5, 1.5);
            d.p.setBrush(QGuiApplication::palette().color(QPalette::Window));
            d.p.drawRoundedRect(QRectF(9, 7.5, 11.5, 13), 1.5, 1.5);
        };
        m[QStringLiteral("notes")] = [](Pen &d) {
            d.stroke();
            d.p.drawRoundedRect(QRectF(4.5, 3.5, 15, 17.5), 1.5, 1.5);
            d.stroke(1.4);
            for (int i = 0; i < 4; ++i)
                d.p.drawLine(QPointF(8, 8 + i * 3.5), QPointF(i == 3 ? 12.5 : 16, 8 + i * 3.5));
            d.fill(d.fg);
            for (int i = 0; i < 3; ++i)
                d.p.drawRoundedRect(QRectF(7 + i * 4, 1.8, 1.8, 3.6), 0.9, 0.9); // spiral binding
        };
        m[QStringLiteral("notes-preview")] = [](Pen &d) {
            QPainterPath eye;
            eye.moveTo(2.5, 12);
            eye.quadTo(12, 2.5, 21.5, 12);
            eye.quadTo(12, 21.5, 2.5, 12);
            d.stroke();
            d.p.drawPath(eye);
            d.fill(d.fg);
            d.p.drawEllipse(QPointF(12, 12), 3.3, 3.3);
        };
        // ---------------------------------------------------------- edit / view
        m[QStringLiteral("edit-undo")] = curvedArrow;
        m[QStringLiteral("edit-redo")] = [](Pen &d) {
            d.p.translate(24, 0);
            d.p.scale(-1, 1);
            curvedArrow(d);
        };
        m[QStringLiteral("object-rotate-left")] = rotateArrow;
        m[QStringLiteral("object-rotate-right")] = [](Pen &d) {
            d.p.translate(24, 0);
            d.p.scale(-1, 1);
            rotateArrow(d);
        };
        m[QStringLiteral("object-flip-horizontal")] = flip;
        m[QStringLiteral("object-flip-vertical")] = [](Pen &d) {
            d.p.translate(24, 0);
            d.p.rotate(90);
            flip(d);
        };
        m[QStringLiteral("zoom-fit-best")] = [](Pen &d) {
            d.stroke();
            const qreal a = 3.5, b = 20.5, l = 5.5;
            d.p.drawPolyline(QPolygonF({QPointF(a, a + l), QPointF(a, a), QPointF(a + l, a)}));
            d.p.drawPolyline(QPolygonF({QPointF(b - l, a), QPointF(b, a), QPointF(b, a + l)}));
            d.p.drawPolyline(QPolygonF({QPointF(b, b - l), QPointF(b, b), QPointF(b - l, b)}));
            d.p.drawPolyline(QPolygonF({QPointF(a + l, b), QPointF(a, b), QPointF(a, b - l)}));
            d.p.setBrush(d.soft());
            d.p.drawRect(QRectF(8, 8, 8, 8));
        };
        m[QStringLiteral("zoom-original")] = [](Pen &d) {
            d.stroke();
            d.p.drawRoundedRect(QRectF(2.5, 4.5, 19, 15), 2, 2);
            QFont f = QGuiApplication::font();
            f.setPixelSize(10);
            f.setBold(true);
            d.p.setFont(f);
            d.p.drawText(QRectF(2.5, 4.5, 19, 15), Qt::AlignCenter, QStringLiteral("1:1"));
        };
        // ---------------------------------------------------------- tools
        m[QStringLiteral("edit-select")] = [](Pen &d) {
            QPolygonF cursor({QPointF(6, 2.5), QPointF(6, 19), QPointF(10, 15.2), QPointF(12.8, 21),
                              QPointF(15.4, 19.8), QPointF(12.7, 14.2), QPointF(18, 14)});
            d.stroke(1.5);
            d.p.setBrush(d.soft());
            d.p.drawPolygon(cursor);
        };
        m[QStringLiteral("select-rectangular")] = [](Pen &d) {
            d.stroke(1.6, Qt::DashLine);
            d.p.drawRect(QRectF(3.5, 5.5, 17, 13));
        };
        m[QStringLiteral("transform-crop")] = [](Pen &d) {
            d.stroke(2);
            d.p.drawPolyline(QPolygonF({QPointF(7, 2.5), QPointF(7, 17), QPointF(21.5, 17)}));
            d.p.drawPolyline(QPolygonF({QPointF(2.5, 7), QPointF(17, 7), QPointF(17, 21.5)}));
        };
        m[QStringLiteral("draw-text")] = [](Pen &d) {
            d.stroke(2.2);
            d.p.drawPolyline(QPolygonF({QPointF(5, 7), QPointF(5, 4.5), QPointF(19, 4.5), QPointF(19, 7)}));
            d.p.drawLine(QPointF(12, 4.5), QPointF(12, 19.5));
            d.stroke(1.8);
            d.p.drawLine(QPointF(9, 19.5), QPointF(15, 19.5));
        };
        m[QStringLiteral("draw-arrow")] = [](Pen &d) {
            d.stroke(2);
            d.p.drawLine(QPointF(4.5, 19.5), QPointF(15.5, 8.5));
            d.fill(d.fg);
            d.p.drawPolygon(QPolygonF({QPointF(20.5, 3.5), QPointF(17.8, 13), QPointF(11, 6.2)}));
        };
        m[QStringLiteral("draw-line")] = [](Pen &d) {
            d.stroke(2);
            d.p.drawLine(QPointF(4.5, 19.5), QPointF(19.5, 4.5));
        };
        m[QStringLiteral("draw-rectangle")] = [](Pen &d) {
            d.stroke(1.9);
            d.p.drawRect(QRectF(3.5, 5.5, 17, 13));
        };
        m[QStringLiteral("draw-ellipse")] = [](Pen &d) {
            d.stroke(1.9);
            d.p.drawEllipse(QRectF(2.5, 5, 19, 14));
        };
        m[QStringLiteral("draw-brush")] = [](Pen &d) {
            QPainterPath path;
            path.moveTo(3.5, 17);
            path.cubicTo(7, 8, 10, 8, 11, 13);
            path.cubicTo(12, 18, 16, 18, 20.5, 6);
            d.stroke(2.6);
            d.p.drawPath(path);
        };
        m[QStringLiteral("draw-highlight")] = [](Pen &d) {
            QColor band(255, 214, 0);
            band.setAlphaF(0.85);
            d.fill(band);
            d.p.drawRect(QRectF(2.5, 18, 19, 4));
            QPolygonF body({QPointF(8.5, 14.5), QPointF(16, 3.5), QPointF(20.5, 6.5), QPointF(13, 17.5)});
            d.stroke(1.5);
            d.p.setBrush(d.soft());
            d.p.drawPolygon(body);
            d.fill(d.fg);
            d.p.drawPolygon(QPolygonF({QPointF(8.5, 14.5), QPointF(13, 17.5), QPointF(8, 18.5)}));
        };
        m[QStringLiteral("draw-cover")] = [](Pen &d) {
            d.fill(d.fg);
            d.p.drawRoundedRect(QRectF(2.5, 6.5, 19, 11), 1.5, 1.5);
            d.fill(QGuiApplication::palette().color(QPalette::Window));
            for (int i = 0; i < 3; ++i)
                d.p.drawEllipse(QPointF(7.5 + i * 4.5, 12), 1.4, 1.4);
        };
        m[QStringLiteral("color-picker")] = [](Pen &d) {
            d.stroke(2.4);
            d.p.drawLine(QPointF(4.5, 19.5), QPointF(13, 11));
            d.stroke(1.7);
            d.p.drawLine(QPointF(10.5, 8.5), QPointF(15.5, 13.5));
            d.fill(d.fg);
            d.p.drawEllipse(QPointF(16.5, 7.5), 3.6, 3.6);
            d.fill(d.accent);
            d.p.drawEllipse(QPointF(4.2, 19.8), 1.6, 1.6);
        };
        m[QStringLiteral("draw-marker")] = [](Pen &d) {
            d.fill(d.accent);
            d.p.drawEllipse(QPointF(12, 12), 9, 9);
            QFont f = QGuiApplication::font();
            f.setPixelSize(13);
            f.setBold(true);
            d.p.setFont(f);
            d.p.setPen(Qt::white);
            d.p.drawText(QRectF(3, 3, 18, 18), Qt::AlignCenter, QStringLiteral("1"));
        };
        m[QStringLiteral("draw-polyline")] = [](Pen &d) {
            const QPolygonF pts({QPointF(3, 18.5), QPointF(8.5, 7), QPointF(15, 15.5), QPointF(21, 5)});
            d.stroke(1.9);
            d.p.drawPolyline(pts);
            d.fill(d.fg);
            for (const QPointF &pt : pts)
                d.p.drawEllipse(pt, 1.9, 1.9);
        };
        m[QStringLiteral("draw-polygon")] = [](Pen &d) {
            const QPolygonF pts({QPointF(12, 3), QPointF(21, 9.5), QPointF(17.5, 20.5), QPointF(6.5, 20.5),
                                 QPointF(3, 9.5)});
            d.stroke(1.9);
            d.p.setBrush(d.soft());
            d.p.drawPolygon(pts);
        };
        m[QStringLiteral("draw-bubble")] = [](Pen &d) {
            d.stroke(1.8);
            d.p.setBrush(d.soft());
            d.p.drawPath(bubblePath());
            d.stroke(1.5);
            d.p.drawLine(QPointF(7.5, 8), QPointF(16.5, 8));
            d.p.drawLine(QPointF(7.5, 11.5), QPointF(13.5, 11.5));
        };
        m[QStringLiteral("tool-clone")] = [](Pen &d) {
            d.stroke(1.7);
            d.p.setBrush(d.soft());
            d.p.drawEllipse(QPointF(12, 5.5), 3.5, 3.5);           // knob
            d.p.drawRect(QRectF(10, 9, 4, 5));                     // neck
            d.p.drawRoundedRect(QRectF(4.5, 14, 15, 4.5), 1.5, 1.5); // base
            d.stroke(2);
            d.p.drawLine(QPointF(5, 21.5), QPointF(19, 21.5));     // stamped line
        };
        m[QStringLiteral("draw-eraser")] = [](Pen &d) {
            QPolygonF body({QPointF(9, 20), QPointF(3.5, 14.5), QPointF(13.5, 4.5), QPointF(21, 12),
                            QPointF(13, 20)});
            d.stroke(1.7);
            d.p.drawPolygon(body);
            QPolygonF tip({QPointF(9, 20), QPointF(3.5, 14.5), QPointF(8.2, 9.8), QPointF(15.7, 17.3),
                           QPointF(13, 20)});
            d.p.setBrush(d.soft());
            d.p.drawPolygon(tip);
            d.p.drawLine(QPointF(13, 20), QPointF(21, 20));
        };
        m[QStringLiteral("tool-blur")] = [](Pen &d) {
            QPainterPath drop;
            drop.moveTo(12, 2.5);
            drop.cubicTo(12, 2.5, 4.5, 10.5, 4.5, 14.5);
            drop.cubicTo(4.5, 18.8, 8, 21.5, 12, 21.5);
            drop.cubicTo(16, 21.5, 19.5, 18.8, 19.5, 14.5);
            drop.cubicTo(19.5, 10.5, 12, 2.5, 12, 2.5);
            d.stroke(1.7);
            d.p.setBrush(d.soft());
            d.p.drawPath(drop);
            d.stroke(1.5);
            d.p.drawArc(QRectF(7.5, 12, 7, 6.5), 180 * 16, 90 * 16);
        };
        m[QStringLiteral("tool-pixelate")] = [](Pen &d) {
            // mosaic of squares in two tones inside a round brush outline
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    QColor c = d.fg;
                    c.setAlphaF((x + y) % 2 ? 0.85 : 0.3);
                    d.fill(c);
                    d.p.drawRect(QRectF(4 + x * 4, 4 + y * 4, 4, 4));
                }
            d.stroke(1.4);
            d.p.drawRect(QRectF(4, 4, 16, 16));
        };
        m[QStringLiteral("tool-bucket")] = [](Pen &d) {
            QPolygonF bucket({QPointF(4, 11), QPointF(11, 4), QPointF(18.5, 11.5), QPointF(11.5, 18.5)});
            d.stroke(1.7);
            d.p.setBrush(d.soft());
            d.p.drawPolygon(bucket);
            d.p.drawLine(QPointF(7.5, 7.5), QPointF(4.5, 4.5)); // handle
            // paint drop
            QPainterPath drop;
            drop.moveTo(20, 13);
            drop.cubicTo(20, 13, 22.5, 16.5, 22.5, 18);
            drop.cubicTo(22.5, 19.5, 21.4, 20.5, 20, 20.5);
            drop.cubicTo(18.6, 20.5, 17.5, 19.5, 17.5, 18);
            drop.cubicTo(17.5, 16.5, 20, 13, 20, 13);
            d.fill(d.accent);
            d.p.drawPath(drop);
            d.fill(d.accent);
            d.p.drawPolygon(QPolygonF({QPointF(4, 11), QPointF(18.5, 11.5), QPointF(11.5, 18.5)}));
        };
        m[QStringLiteral("tool-straighten")] = [](Pen &d) {
            d.stroke(1.4, Qt::DashLine);
            d.p.drawLine(QPointF(2.5, 17), QPointF(21.5, 17)); // level
            d.stroke(2);
            d.p.drawLine(QPointF(3.5, 17), QPointF(20, 8));    // tilted line
            d.stroke(1.5);
            d.p.drawArc(QRectF(-2.5, 11, 18, 12), 0, 28 * 16); // angle
            d.fill(d.accent);
            d.p.drawEllipse(QPointF(3.5, 17), 2, 2);
            d.p.drawEllipse(QPointF(20, 8), 2, 2);
        };
        m[QStringLiteral("go-next")] = [](Pen &d) {
            d.stroke(2.2);
            d.p.drawPolyline(QPolygonF({QPointF(9, 5), QPointF(16, 12), QPointF(9, 19)}));
        };
        m[QStringLiteral("go-previous")] = [](Pen &d) {
            d.stroke(2.2);
            d.p.drawPolyline(QPolygonF({QPointF(15, 5), QPointF(8, 12), QPointF(15, 19)}));
        };
        return m;
    }();
    return map;
}

class Engine : public QIconEngine
{
public:
    explicit Engine(Draw draw) : m_draw(std::move(draw)) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette pal = QGuiApplication::palette();
        QColor fg = pal.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::WindowText);
        if (mode == QIcon::Selected)
            fg = pal.color(QPalette::HighlightedText);
        QColor accent = pal.color(QPalette::Active, QPalette::Highlight);
        if (mode == QIcon::Disabled)
            accent = fg;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const qreal side = std::min(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 24.0, side / 24.0);
        Pen pen{*painter, fg, accent};
        m_draw(pen);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pm(size * scale);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            paint(&p, QRect(QPoint(), pm.size()), mode, state);
        }
        pm.setDevicePixelRatio(scale);
        return pm;
    }

    QIconEngine *clone() const override { return new Engine(m_draw); }
    QString key() const override { return QStringLiteral("rolidan"); }

private:
    Draw m_draw;
};

} // namespace

bool has(const QString &name)
{
    return drawings().contains(name);
}

QIcon get(const QString &name)
{
    if (name.isEmpty())
        return {};
    const auto it = drawings().constFind(name);
    if (it != drawings().constEnd())
        return QIcon(new Engine(*it));
    return QIcon::fromTheme(name);
}

} // namespace icons
