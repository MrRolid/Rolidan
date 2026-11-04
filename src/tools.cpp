#include "tools.h"

#include "canvas.h"
#include "document.h"
#include "retouch.h"
#include "toast.h"
#include "settings.h"
#include "tooldefaults.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QCoreApplication>
#include <QLineF>
#include <QApplication>
#include <QPixmap>
#include <QtMath>
#include <cmath>

namespace {
constexpr double kHandleTolerance = 7.0;
constexpr int kNudgeMergeKey = 1001;

int iround(double v)
{
    return int(std::lround(v));
}

void drawHandle(QPainter &p, const QPointF &c)
{
    const QRectF r(c.x() - 3.5, c.y() - 3.5, 7, 7);
    p.setPen(QPen(QColor(30, 30, 30), 1));
    p.setBrush(Qt::white);
    p.drawRect(r);
}

void drawLabel(QPainter &p, const QRectF &anchor, const QString &text)
{
    const QFontMetrics fm(p.font());
    QRectF box(0, 0, fm.horizontalAdvance(text) + 10, fm.height() + 4);
    box.moveBottomLeft(anchor.topLeft() + QPointF(0, -4));
    if (box.top() < 0)
        box.moveTopLeft(anchor.topLeft() + QPointF(4, 4));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, text);
}

void drawSizeLabel(QPainter &p, const QRectF &anchor, const QSize &size)
{
    drawLabel(p, anchor, QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()));
}

// Distance of the rotation handle above the object's box, in screen pixels.
constexpr double kRotateHandleDistance = 26.0;

// A circular arrow: there is no standard "rotate" cursor.
QCursor rotateCursor()
{
    static const QCursor cursor = [] {
        const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
        QPixmap pm(QSize(24, 24) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath arc;
        const QRectF r(5, 5, 14, 14);
        arc.arcMoveTo(r, 120);
        arc.arcTo(r, 120, 270);
        const QPointF end = arc.currentPosition();
        QPainterPath head;
        head.moveTo(end + QPointF(-4.5, -1.5));
        head.lineTo(end + QPointF(1.5, -4.5));
        head.lineTo(end + QPointF(1.5, 2.5));
        head.closeSubpath();
        for (int pass = 0; pass < 2; ++pass) {
            const QColor c = pass == 0 ? Qt::white : Qt::black;
            p.setPen(QPen(c, pass == 0 ? 4.0 : 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(arc);
            p.setBrush(c);
            p.drawPath(head);
        }
        p.end();
        return QCursor(pm, 12, 12);
    }();
    return cursor;
}

void drawDashedRect(QPainter &p, const QRectF &r)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(r);
    p.setPen(QPen(Qt::black, 1, Qt::DashLine));
    p.drawRect(r);
}
} // namespace

Tool::Tool(Canvas *canvas)
    : QObject(canvas)
    , m_canvas(canvas)
{
}

QCursor Tool::cursorAt(const QPointF &, const QPointF &)
{
    return Qt::ArrowCursor;
}

// ================================================================ RectEditor

static QList<QPointF> handlePoints(const QRectF &r)
{
    const QPointF c = r.center();
    return {r.topLeft(), {c.x(), r.top()}, r.topRight(), {r.right(), c.y()},
            r.bottomRight(), {c.x(), r.bottom()}, r.bottomLeft(), {r.left(), c.y()}};
}

RectEditor::Hit RectEditor::hitTest(const Canvas *c, const QPointF &w) const
{
    if (rect.isEmpty())
        return None;
    const QRectF rw = c->toWidget(QRectF(rect));
    const auto pts = handlePoints(rw);
    const bool small = rw.width() < 3 * kHandleTolerance || rw.height() < 3 * kHandleTolerance;
    for (int i = 0; i < 8; ++i) {
        if (small && i % 2 == 1)
            continue; // only corners on tiny rectangles
        if (std::abs(pts[i].x() - w.x()) <= kHandleTolerance && std::abs(pts[i].y() - w.y()) <= kHandleTolerance)
            return Hit(i);
    }
    return rw.contains(w) ? Inside : None;
}

void RectEditor::begin(Hit hit, const QPointF &img)
{
    m_mode = hit;
    m_dragging = true;
    m_pressPt = QPoint(iround(img.x()), iround(img.y()));
    m_pressRect = hit == None ? QRect(m_pressPt, QSize(0, 0)) : rect;
    if (hit == None)
        rect = m_pressRect;
}

void RectEditor::update(const QPointF &img, bool shift, const QRect &bounds)
{
    if (!m_dragging)
        return;
    const int bl = bounds.left(), bt = bounds.top();
    const int br = bounds.left() + bounds.width(), bb = bounds.top() + bounds.height();
    const QPoint pt(std::clamp(iround(img.x()), bl, br), std::clamp(iround(img.y()), bt, bb));
    const QRect pr = m_pressRect;
    const int L = pr.left(), T = pr.top(), R = pr.left() + pr.width(), B = pr.top() + pr.height();

    if (m_mode == Inside) {
        const int dx = std::clamp(pt.x() - m_pressPt.x(), bl - L, br - R);
        const int dy = std::clamp(pt.y() - m_pressPt.y(), bt - T, bb - B);
        rect = pr.translated(dx, dy);
        return;
    }

    double r = ratio;
    if (r <= 0 && shift)
        r = (m_mode == None || pr.height() == 0) ? 1.0 : double(pr.width()) / pr.height();

    const bool corner = m_mode == None || m_mode == TopLeft || m_mode == TopRight
                        || m_mode == BottomRight || m_mode == BottomLeft;
    if (corner) {
        QPoint a; // anchor = opposite corner
        switch (m_mode) {
        case TopLeft: a = {R, B}; break;
        case TopRight: a = {L, B}; break;
        case BottomRight: a = {L, T}; break;
        case BottomLeft: a = {R, T}; break;
        default: a = m_pressPt; break;
        }
        const int sx = pt.x() >= a.x() ? 1 : -1;
        const int sy = pt.y() >= a.y() ? 1 : -1;
        int w = std::abs(pt.x() - a.x()), h = std::abs(pt.y() - a.y());
        const int maxW = sx > 0 ? br - a.x() : a.x() - bl;
        const int maxH = sy > 0 ? bb - a.y() : a.y() - bt;
        if (r > 0) {
            if (w > h * r)
                h = iround(w / r);
            else
                w = iround(h * r);
            if (w > maxW) {
                w = maxW;
                h = iround(w / r);
            }
            if (h > maxH) {
                h = maxH;
                w = iround(h * r);
            }
        } else {
            w = std::min(w, maxW);
            h = std::min(h, maxH);
        }
        rect = QRect(QPoint(sx > 0 ? a.x() : a.x() - w, sy > 0 ? a.y() : a.y() - h), QSize(w, h));
        return;
    }

    // Edge handles.
    const bool vertical = m_mode == Top || m_mode == Bottom;
    if (vertical) {
        const int fixed = m_mode == Top ? B : T;
        int top = std::min(fixed, pt.y()), h = std::abs(pt.y() - fixed);
        int left = L, w = pr.width();
        if (r > 0) {
            w = std::min(iround(h * r), bounds.width());
            h = iround(w / r);
            if (pt.y() < fixed)
                top = fixed - h;
            left = std::clamp(iround(pr.center().x() + 0.5 - w / 2.0), bl, br - w);
        }
        rect = QRect(left, top, w, h);
    } else {
        const int fixed = m_mode == Left ? R : L;
        int left = std::min(fixed, pt.x()), w = std::abs(pt.x() - fixed);
        int top = T, h = pr.height();
        if (r > 0) {
            h = std::min(iround(w / r), bounds.height());
            w = iround(h * r);
            if (pt.x() < fixed)
                left = fixed - w;
            top = std::clamp(iround(pr.center().y() + 0.5 - h / 2.0), bt, bb - h);
        }
        rect = QRect(left, top, w, h);
    }
}

void RectEditor::applyRatio(const QRect &bounds)
{
    if (ratio <= 0 || rect.isEmpty())
        return;
    int w = rect.width(), h = rect.height();
    if (w > h * ratio)
        w = iround(h * ratio);
    else
        h = iround(w / ratio);
    w = std::max(1, std::min(w, bounds.width()));
    h = std::max(1, std::min(h, bounds.height()));
    const QPointF c = QRectF(rect).center();
    QRect r(iround(c.x() - w / 2.0), iround(c.y() - h / 2.0), w, h);
    r.moveLeft(std::clamp(r.left(), bounds.left(), bounds.left() + bounds.width() - w));
    r.moveTop(std::clamp(r.top(), bounds.top(), bounds.top() + bounds.height() - h));
    rect = r;
}

void RectEditor::paintHandles(QPainter &p, const Canvas *c) const
{
    if (rect.isEmpty())
        return;
    for (const QPointF &pt : handlePoints(c->toWidget(QRectF(rect))))
        drawHandle(p, pt);
}

Qt::CursorShape RectEditor::cursorFor(Hit hit)
{
    switch (hit) {
    case TopLeft:
    case BottomRight: return Qt::SizeFDiagCursor;
    case TopRight:
    case BottomLeft: return Qt::SizeBDiagCursor;
    case Top:
    case Bottom: return Qt::SizeVerCursor;
    case Left:
    case Right: return Qt::SizeHorCursor;
    case Inside: return Qt::SizeAllCursor;
    default: return Qt::CrossCursor;
    }
}

// ================================================================ ObjectDrag

static ObjectPtr singleSelected(const Canvas *c)
{
    const ObjectList sel = c->selectedObjectList();
    return sel.size() == 1 ? sel.first() : ObjectPtr();
}

std::optional<QPointF> ObjectDrag::rotateHandlePos(const Canvas *c, const Annotation &o)
{
    if (!o.rotatable())
        return std::nullopt;
    const QRectF f = o.frameRect();
    const double a = o.rotation();
    const QTransform rot = rotationAbout(f.center(), a);
    // Above the top edge, in the object's own "up" direction.
    const QPointF up = QTransform().rotate(a).map(QPointF(0, -1));
    QPointF pos = c->toWidget(rot.map(QPointF(f.center().x(), f.top()))) + up * kRotateHandleDistance;
    if (!QRectF(c->rect()).adjusted(4, 4, -4, -4).contains(pos)) // no room: below the box
        pos = c->toWidget(rot.map(QPointF(f.center().x(), f.bottom()))) - up * kRotateHandleDistance;
    return pos;
}

int ObjectDrag::handleAt(const Canvas *c, const QPointF &w) const
{
    const ObjectPtr o = singleSelected(c);
    if (!o || o->id == c->hiddenObject())
        return -1;
    const QList<QPointF> pts = o->handlePoints();
    for (int i = 0; i < pts.size(); ++i) {
        const QPointF hw = c->toWidget(pts[i]);
        if (std::abs(hw.x() - w.x()) <= kHandleTolerance && std::abs(hw.y() - w.y()) <= kHandleTolerance)
            return i;
    }
    if (const auto r = rotateHandlePos(c, *o); r && QLineF(*r, w).length() <= kHandleTolerance + 1)
        return kRotateHandle;
    return -1;
}

QCursor ObjectDrag::cursorFor(const Canvas *c, int handle)
{
    const ObjectPtr o = singleSelected(c);
    if (!o || handle < 0)
        return Qt::ArrowCursor;
    if (handle == kRotateHandle)
        return rotateCursor();
    switch (o->handleMode()) {
    case Annotation::HandleMode::Corners: {
        // The diagonal through the handle, turned with the object.
        const double a = std::fmod(o->rotation() + (handle % 2 == 0 ? 45.0 : 135.0) + 360.0, 180.0);
        if (a < 22.5 || a >= 157.5)
            return Qt::SizeHorCursor;
        if (a < 67.5)
            return Qt::SizeFDiagCursor;
        if (a < 112.5)
            return Qt::SizeVerCursor;
        return Qt::SizeBDiagCursor;
    }
    case Annotation::HandleMode::TextWidth: return Qt::SizeHorCursor;
    case Annotation::HandleMode::Endpoints: return Qt::CrossCursor;
    }
    return Qt::ArrowCursor;
}

bool ObjectDrag::begin(Canvas *c, const QPointF &w)
{
    const int h = handleAt(c, w);
    if (h < 0)
        return false;
    const ObjectPtr o = singleSelected(c);
    m_active = true;
    m_changed = false;
    m_start = c->document()->state();
    m_id = o->id;
    m_handle = h;
    m_startRect = o->frameRect();
    m_startAngle = o->rotation();
    m_pivot = o->boundingRect().center();
    m_pressAngle = qRadiansToDegrees(std::atan2(c->toImage(w).y() - m_pivot.y(), c->toImage(w).x() - m_pivot.x()));
    m_angle = m_startAngle;
    return true;
}

void ObjectDrag::update(Canvas *c, const QPointF &img, Qt::KeyboardModifiers mods)
{
    if (!m_active)
        return;
    const ObjectPtr orig = docops::findObject(m_start, m_id);
    if (!orig)
        return;
    auto obj = orig->clone();
    const bool shift = mods & Qt::ShiftModifier;
    if (m_handle == kRotateHandle) {
        const double now = qRadiansToDegrees(std::atan2(img.y() - m_pivot.y(), img.x() - m_pivot.x()));
        // Raw angle of the box (0 for 90° rotated images), so Shift snaps to what you see.
        const double base = orig->transform.type() > QTransform::TxScale
                                ? qRadiansToDegrees(std::atan2(orig->transform.m12(), orig->transform.m11()))
                                : 0.0;
        double target = base + (now - m_pressAngle);
        if (shift)
            target = std::round(target / 15.0) * 15.0; // spec: Shift = 15° steps
        const double delta = target - base;
        obj->rotate(delta, m_pivot);
        m_angle = std::remainder(target, 360.0);
        if (std::abs(m_angle) < 1e-9)
            m_angle = 0.0;
        m_changed = true;
        c->document()->setTransient(docops::replaceObject(m_start, obj));
        return;
    }
    if (orig->handleMode() == Annotation::HandleMode::Corners) {
        // Rotated boxes are resized in their own frame: turn them upright,
        // resize, turn back.
        const bool rotated = m_startAngle != 0.0;
        QPointF pt = img;
        if (rotated) {
            obj->rotate(-m_startAngle, m_pivot);
            pt = rotationAbout(m_pivot, -m_startAngle).map(img);
        }
        const QRectF r0 = m_startRect;
        const QPointF anchor = m_handle == 0 ? r0.bottomRight()
                               : m_handle == 1 ? r0.bottomLeft()
                               : m_handle == 2 ? r0.topLeft()
                                               : r0.topRight();
        const double dx = std::abs(pt.x() - anchor.x()), dy = std::abs(pt.y() - anchor.y());
        double nw, nh;
        // Images keep their aspect ratio by default; Shift toggles (spec, stage 1).
        const bool lock = orig->keepsAspect() != shift;
        if (lock && r0.width() > 0 && r0.height() > 0) {
            const double s = std::max(dx / r0.width(), dy / r0.height());
            nw = std::max(1.0, std::round(r0.width() * s));
            nh = std::max(1.0, std::round(nw * r0.height() / r0.width()));
        } else {
            nw = std::max(1.0, std::round(dx));
            nh = std::max(1.0, std::round(dy));
        }
        const double left = (m_handle == 0 || m_handle == 3) ? anchor.x() - nw : anchor.x();
        const double top = (m_handle == 0 || m_handle == 1) ? anchor.y() - nh : anchor.y();
        obj->resizeTo(obj->boundingRect(), QRectF(left, top, nw, nh));
        if (rotated)
            obj->rotate(m_startAngle, m_pivot);
    } else {
        obj->moveHandle(m_handle, img, shift);
    }
    m_changed = true;
    c->document()->setTransient(docops::replaceObject(m_start, obj));
}

void ObjectDrag::finish(Canvas *c)
{
    if (m_active && m_changed)
        c->document()->commit(c->document()->state(), m_handle == kRotateHandle
                                                          ? QCoreApplication::translate("PointerTool", "Rotate")
                                                          : QCoreApplication::translate("PointerTool", "Resize"));
    m_active = false;
    m_changed = false;
}

void ObjectDrag::paintLabel(QPainter &p, const Canvas *c) const
{
    if (!m_active)
        return;
    const ObjectPtr o = singleSelected(c);
    if (!o)
        return;
    if (m_handle == kRotateHandle) {
        drawLabel(p, c->toWidget(o->boundingRect()), QStringLiteral("%1°").arg(m_angle, 0, 'f', 1));
    } else if (o->handleMode() == Annotation::HandleMode::Corners) {
        const QRectF f = o->frameRect();
        drawSizeLabel(p, c->toWidget(o->boundingRect()), f.toRect().size());
    }
}

void ObjectDrag::abort(Canvas *c)
{
    if (m_active && m_changed)
        c->document()->setTransient(m_start);
    m_active = false;
    m_changed = false;
}

void paintObjectSelection(QPainter &p, const Canvas *c)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const ObjectList sel = c->selectedObjectList();
    for (const auto &o : sel) {
        if (o->id == c->hiddenObject())
            continue;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 120, 215), 1, Qt::DashLine));
        if (o->handleMode() == Annotation::HandleMode::Endpoints)
            p.drawPolygon(c->viewTransform().map(QPolygonF(o->visualRect())));
        else
            p.drawPolygon(c->viewTransform().map(o->outline()));
    }
    if (sel.size() == 1 && sel.first()->id != c->hiddenObject()) {
        if (const auto r = ObjectDrag::rotateHandlePos(c, *sel.first())) {
            // Stem from the box to the round rotation handle.
            const ObjectPtr &o = sel.first();
            const QRectF f = o->frameRect();
            const QTransform rot = rotationAbout(f.center(), o->rotation());
            const QPointF top = c->toWidget(rot.map(QPointF(f.center().x(), f.top())));
            const QPointF bottom = c->toWidget(rot.map(QPointF(f.center().x(), f.bottom())));
            const QPointF from = QLineF(top, *r).length() < QLineF(bottom, *r).length() ? top : bottom;
            p.setPen(QPen(QColor(0, 120, 215), 1));
            p.drawLine(from, *r);
            p.setPen(QPen(QColor(30, 30, 30), 1));
            p.setBrush(QColor(0xe8, 0xf1, 0xfb));
            p.drawEllipse(*r, 5.0, 5.0);
        }
        for (const QPointF &pt : sel.first()->handlePoints()) {
            const QPointF w = c->toWidget(pt);
            if (sel.first()->handleMode() == Annotation::HandleMode::Endpoints) {
                p.setPen(QPen(QColor(30, 30, 30), 1));
                p.setBrush(Qt::white);
                p.drawEllipse(w, 4.5, 4.5);
            } else {
                p.setRenderHint(QPainter::Antialiasing, false);
                drawHandle(p, w);
                p.setRenderHint(QPainter::Antialiasing, true);
            }
        }
    }
    p.restore();
}

// ================================================================ PointerTool

void PointerTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_drag.begin(m_canvas, e->position())) {
        m_mode = Handle;
        return;
    }
    Document *doc = m_canvas->document();
    m_start = doc->state();
    m_pressImg = img;
    m_changed = false;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    QSet<quint64> sel = m_canvas->selectedObjects();
    if (const ObjectPtr o = m_canvas->objectAt(img)) {
        if (shift) {
            if (sel.contains(o->id))
                sel.remove(o->id);
            else
                sel.insert(o->id);
        } else if (!sel.contains(o->id)) {
            sel = {o->id};
        }
        m_canvas->setSelectedObjects(sel);
        m_mode = sel.contains(o->id) ? Moving : Idle;
        return;
    }
    if (!shift)
        m_canvas->setSelectedObjects({});
    m_mode = Rubber;
    m_rubber = QRectF(img, img);
}

void PointerTool::mouseDoubleClick(QMouseEvent *e, const QPointF &img)
{
    // Double-click on a text edits it (spec D2).
    if (const ObjectPtr o = m_canvas->objectAt(img); o && o->type() == Annotation::Type::Text) {
        m_mode = Idle;
        m_canvas->editTextObject(o->id);
        return;
    } else if (!o) {
        // Double-click on the image itself: full screen, like a viewer (Etapa 5).
        m_mode = Idle;
        emit m_canvas->imageDoubleClicked();
        return;
    }
    mousePress(e, img);
}

void PointerTool::mouseMove(QMouseEvent *e, const QPointF &img)
{
    Document *doc = m_canvas->document();
    switch (m_mode) {
    case Idle:
        return;
    case Handle:
        m_drag.update(m_canvas, img, e->modifiers());
        return;
    case Moving: {
        const QPointF d(std::round(img.x() - m_pressImg.x()), std::round(img.y() - m_pressImg.y()));
        if (!m_changed && d.isNull())
            return;
        DocState s = m_start;
        const QTransform t = QTransform::fromTranslate(d.x(), d.y());
        const QSet<quint64> sel = m_canvas->selectedObjects();
        for (auto &o : s.objects) {
            if (sel.contains(o->id)) {
                auto c = o->clone();
                c->applyDocumentTransform(t, DocTransformKind::Translate);
                o = c;
            }
        }
        m_changed = true;
        doc->setTransient(s);
        return;
    }
    case Rubber:
        m_rubber.setBottomRight(img);
        return;
    }
}

void PointerTool::mouseRelease(QMouseEvent *, const QPointF &)
{
    Document *doc = m_canvas->document();
    if (m_mode == Handle)
        m_drag.finish(m_canvas);
    if (m_mode == Moving && m_changed)
        doc->commit(doc->state(), tr("Move"));
    if (m_mode == Rubber) {
        const QRectF r = m_rubber.normalized();
        if (r.width() > 1 || r.height() > 1) {
            QSet<quint64> sel = m_canvas->selectedObjects();
            for (const auto &o : doc->state().objects) {
                if (o->visualRect().intersects(r))
                    sel.insert(o->id);
            }
            m_canvas->setSelectedObjects(sel);
        }
    }
    m_mode = Idle;
    m_changed = false;
}

bool PointerTool::keyPress(QKeyEvent *e)
{
    QPointF d;
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    switch (e->key()) {
    case Qt::Key_Left: d = {-step, 0}; break;
    case Qt::Key_Right: d = {step, 0}; break;
    case Qt::Key_Up: d = {0, -step}; break;
    case Qt::Key_Down: d = {0, step}; break;
    default: return false;
    }
    const QSet<quint64> sel = m_canvas->selectedObjects();
    if (sel.isEmpty() || m_mode != Idle)
        return false;
    Document *doc = m_canvas->document();
    DocState s = doc->state();
    for (auto &o : s.objects) {
        if (sel.contains(o->id)) {
            auto c = o->clone();
            c->applyDocumentTransform(QTransform::fromTranslate(d.x(), d.y()), DocTransformKind::Translate);
            o = c;
        }
    }
    // A series of nudges is one undo step (spec D1).
    doc->commit(s, tr("Move"), kNudgeMergeKey);
    return true;
}

void PointerTool::paintOverlay(QPainter &p)
{
    paintObjectSelection(p, m_canvas);
    const ObjectList sel = m_canvas->selectedObjectList();
    m_drag.paintLabel(p, m_canvas);
    if (m_mode == Rubber) {
        const QRectF r = m_canvas->toWidget(m_rubber.normalized());
        p.setPen(QPen(QColor(0, 120, 215), 1));
        p.setBrush(QColor(0, 120, 215, 40));
        p.drawRect(r);
    }
}

QCursor PointerTool::cursorAt(const QPointF &w, const QPointF &img)
{
    const int h = m_drag.handleAt(m_canvas, w);
    if (h >= 0)
        return ObjectDrag::cursorFor(m_canvas, h);
    if (m_mode == Moving)
        return Qt::SizeAllCursor;
    return m_canvas->objectAt(img) ? Qt::SizeAllCursor : Qt::ArrowCursor;
}

void PointerTool::abort()
{
    if (m_mode == Handle)
        m_drag.abort(m_canvas);
    if (m_mode == Moving && m_changed)
        m_canvas->document()->setTransient(m_start);
    m_mode = Idle;
    m_changed = false;
}

bool PointerTool::cancel()
{
    if (m_mode != Idle) {
        abort();
        return true;
    }
    if (!m_canvas->selectedObjects().isEmpty()) {
        m_canvas->setSelectedObjects({});
        return true;
    }
    return false;
}

void PointerTool::selectAll()
{
    QSet<quint64> all;
    for (const auto &o : m_canvas->document()->state().objects)
        all.insert(o->id);
    m_canvas->setSelectedObjects(all);
}

void PointerTool::deactivate()
{
    abort();
}

// ================================================================ SelectTool

static QRect canvasBounds(const Canvas *c)
{
    return QRect(QPoint(0, 0), c->document()->size());
}

void SelectTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_editor.rect = m_canvas->areaSelection();
    m_editor.ratio = 0;
    m_editor.begin(m_editor.hitTest(m_canvas, e->position()), img);
}

void SelectTool::mouseMove(QMouseEvent *e, const QPointF &img)
{
    if (!m_editor.dragging())
        return;
    m_editor.update(img, e->modifiers() & Qt::ShiftModifier, canvasBounds(m_canvas));
    m_canvas->setAreaSelection(m_editor.rect);
}

void SelectTool::mouseRelease(QMouseEvent *, const QPointF &)
{
    if (!m_editor.dragging())
        return;
    m_editor.stop();
    m_editor.end();
    m_canvas->setAreaSelection(m_editor.rect.isEmpty() ? QRect() : m_editor.rect);
}

void SelectTool::paintOverlay(QPainter &p)
{
    const QRect sel = m_canvas->areaSelection();
    if (sel.isEmpty())
        return;
    const QRectF r = m_canvas->toWidget(QRectF(sel));
    drawDashedRect(p, r);
    m_editor.rect = sel;
    m_editor.paintHandles(p, m_canvas);
    drawSizeLabel(p, r, sel.size());
}

QCursor SelectTool::cursorAt(const QPointF &w, const QPointF &)
{
    m_editor.rect = m_canvas->areaSelection();
    return RectEditor::cursorFor(m_editor.hitTest(m_canvas, w));
}

bool SelectTool::cancel()
{
    if (m_editor.dragging()) {
        m_editor.stop();
        m_canvas->setAreaSelection({});
        return true;
    }
    if (!m_canvas->areaSelection().isEmpty()) {
        m_canvas->setAreaSelection({});
        return true;
    }
    return false;
}

void SelectTool::selectAll()
{
    m_canvas->setAreaSelection(canvasBounds(m_canvas));
}

// ================================================================ CropTool

CropTool::CropTool(Canvas *canvas)
    : Tool(canvas)
{
    setRatioFromString(settings::cropRatio());
}

void CropTool::setRatioFromString(const QString &r)
{
    m_editor.ratio = 0;
    if (r == QLatin1String("original")) {
        const QSize s = m_canvas->document()->size();
        if (!s.isEmpty())
            m_editor.ratio = double(s.width()) / s.height();
        return;
    }
    const QStringList parts = r.split(QLatin1Char(':'));
    if (parts.size() == 2) {
        const double a = parts[0].toDouble(), b = parts[1].toDouble();
        if (a > 0 && b > 0)
            m_editor.ratio = a / b;
    }
}

void CropTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_editor.begin(m_editor.hitTest(m_canvas, e->position()), img);
}

void CropTool::mouseMove(QMouseEvent *e, const QPointF &img)
{
    if (!m_editor.dragging())
        return;
    m_editor.update(img, e->modifiers() & Qt::ShiftModifier, canvasBounds(m_canvas));
    syncSpins();
}

void CropTool::mouseRelease(QMouseEvent *, const QPointF &)
{
    if (!m_editor.dragging())
        return;
    m_editor.stop();
    m_editor.end();
    if (m_editor.rect.isEmpty())
        m_editor.rect = {};
    syncSpins();
}

bool CropTool::keyPress(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
        return apply();
    return false;
}

bool CropTool::apply()
{
    const QRect r = m_editor.rect.intersected(canvasBounds(m_canvas));
    if (r.isEmpty())
        return false;
    Document *doc = m_canvas->document();
    m_editor.rect = {};
    m_canvas->setAreaSelection({});
    if (r != canvasBounds(m_canvas))
        doc->commit(docops::crop(doc->state(), r), tr("Crop"));
    syncSpins();
    m_canvas->update();
    return true;
}

void CropTool::paintOverlay(QPainter &p)
{
    if (m_editor.rect.isEmpty())
        return;
    const QRectF imgW = m_canvas->toWidget(QRectF(canvasBounds(m_canvas)));
    const QRectF r = m_canvas->toWidget(QRectF(m_editor.rect));
    QPainterPath dim;
    dim.addRect(imgW);
    QPainterPath hole;
    hole.addRect(r);
    p.fillPath(dim.subtracted(hole), QColor(0, 0, 0, 140));
    drawDashedRect(p, r);
    m_editor.paintHandles(p, m_canvas);
    drawSizeLabel(p, r, m_editor.rect.size());
}

QCursor CropTool::cursorAt(const QPointF &w, const QPointF &)
{
    return RectEditor::cursorFor(m_editor.hitTest(m_canvas, w));
}

void CropTool::activate()
{
    setRatioFromString(settings::cropRatio());
    // Take over an existing area selection (spec A5).
    if (!m_canvas->areaSelection().isEmpty())
        setCropRect(m_canvas->areaSelection());
}

void CropTool::deactivate()
{
    m_editor.stop();
    m_editor.rect = {};
    syncSpins();
}

bool CropTool::cancel()
{
    if (m_editor.rect.isEmpty() && !m_editor.dragging())
        return false;
    m_editor.stop();
    m_editor.rect = {};
    syncSpins();
    return true;
}

void CropTool::selectAll()
{
    setCropRect(canvasBounds(m_canvas));
}

void CropTool::setCropRect(const QRect &r)
{
    m_editor.rect = r.intersected(canvasBounds(m_canvas));
    m_editor.applyRatio(canvasBounds(m_canvas));
    syncSpins();
    m_canvas->update();
}

void CropTool::syncSpins()
{
    if (!m_x)
        return;
    m_syncing = true;
    const QSize s = m_canvas->document()->size();
    const QRect r = m_editor.rect;
    for (QSpinBox *sb : {m_x, m_w})
        sb->setRange(0, std::max(0, s.width()));
    for (QSpinBox *sb : {m_y, m_h})
        sb->setRange(0, std::max(0, s.height()));
    m_x->setValue(r.x());
    m_y->setValue(r.y());
    m_w->setValue(r.width());
    m_h->setValue(r.height());
    m_syncing = false;
}

QWidget *CropTool::createOptions(QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(4, 0, 4, 0);
    auto addSpin = [&](const QString &label) {
        lay->addWidget(new QLabel(label, w));
        auto *sb = new QSpinBox(w);
        sb->setKeyboardTracking(false);
        lay->addWidget(sb);
        return sb;
    };
    m_x = addSpin(tr("X:"));
    m_y = addSpin(tr("Y:"));
    m_w = addSpin(tr("Width:"));
    m_h = addSpin(tr("Height:"));
    lay->addSpacing(12);
    lay->addWidget(new QLabel(tr("Aspect ratio:"), w));
    m_ratio = new QComboBox(w);
    m_ratio->addItem(tr("Free"), QStringLiteral("free"));
    m_ratio->addItem(tr("Original"), QStringLiteral("original"));
    for (const char *r : {"1:1", "4:3", "3:2", "16:9", "9:16"})
        m_ratio->addItem(QString::fromLatin1(r), QString::fromLatin1(r));
    m_ratio->addItem(tr("Custom…"), QStringLiteral("custom"));
    const QString current = settings::cropRatio();
    int idx = m_ratio->findData(current);
    if (idx < 0) {
        m_ratio->insertItem(m_ratio->count() - 1, current, current);
        idx = m_ratio->count() - 2;
    }
    m_ratio->setCurrentIndex(idx);
    lay->addWidget(m_ratio);
    lay->addSpacing(12);
    auto *apply = new QPushButton(tr("Crop"), w);
    auto *cancelBtn = new QPushButton(tr("Cancel"), w);
    lay->addWidget(apply);
    lay->addWidget(cancelBtn);
    lay->addStretch();

    auto onSpin = [this] {
        if (m_syncing)
            return;
        const QRect r(m_x->value(), m_y->value(), m_w->value(), m_h->value());
        m_editor.rect = r.intersected(canvasBounds(m_canvas));
        // Spin edits of width/height keep the ratio by adjusting the other side.
        if (m_editor.ratio > 0 && !m_editor.rect.isEmpty()) {
            QSpinBox *src = qobject_cast<QSpinBox *>(sender());
            QRect rr = m_editor.rect;
            if (src == m_w)
                rr.setHeight(std::max(1, iround(rr.width() / m_editor.ratio)));
            else if (src == m_h)
                rr.setWidth(std::max(1, iround(rr.height() * m_editor.ratio)));
            m_editor.rect = rr.intersected(canvasBounds(m_canvas));
        }
        syncSpins();
        m_canvas->update();
    };
    for (QSpinBox *sb : {m_x, m_y, m_w, m_h})
        connect(sb, &QSpinBox::valueChanged, this, onSpin);
    connect(m_ratio, &QComboBox::activated, this, [this](int i) {
        QString value = m_ratio->itemData(i).toString();
        if (value == QLatin1String("custom")) {
            bool ok = false;
            value = QInputDialog::getText(m_canvas, tr("Custom aspect ratio"),
                                          tr("Width:height (for example 5:4):"), QLineEdit::Normal,
                                          QStringLiteral("5:4"), &ok)
                        .trimmed();
            const QStringList parts = value.split(QLatin1Char(':'));
            if (!ok || parts.size() != 2 || parts[0].toDouble() <= 0 || parts[1].toDouble() <= 0) {
                m_ratio->setCurrentIndex(m_ratio->findData(settings::cropRatio()));
                return;
            }
            if (m_ratio->findData(value) < 0)
                m_ratio->insertItem(m_ratio->count() - 1, value, value);
            m_ratio->setCurrentIndex(m_ratio->findData(value));
        }
        settings::setCropRatio(value);
        setRatioFromString(value);
        m_editor.applyRatio(canvasBounds(m_canvas));
        syncSpins();
        m_canvas->update();
    });
    connect(apply, &QPushButton::clicked, this, [this] { this->apply(); });
    connect(cancelBtn, &QPushButton::clicked, this, [this] {
        cancel();
        m_canvas->update();
    });
    connect(w, &QObject::destroyed, this, [this] {
        m_x = m_y = m_w = m_h = nullptr;
        m_ratio = nullptr;
    });
    syncSpins();
    return w;
}

// ================================================================ DrawTool

DrawTool::DrawTool(Canvas *canvas, ToolId id)
    : Tool(canvas)
    , m_id(id)
{
}

std::shared_ptr<Annotation> DrawTool::create(const QPointF &img) const
{
    std::shared_ptr<Annotation> obj;
    switch (m_id) {
    case ToolId::Arrow:
    case ToolId::Line: {
        auto l = std::make_shared<LineObject>();
        l->p1 = l->p2 = img;
        obj = l;
        break;
    }
    case ToolId::Rect:
    case ToolId::Ellipse:
    case ToolId::Cover: {
        auto s = std::make_shared<ShapeObject>();
        s->kind = m_id == ToolId::Ellipse ? ShapeObject::Kind::Ellipse : ShapeObject::Kind::Rect;
        s->style = m_id == ToolId::Cover ? ShapeObject::Style::Cover : ShapeObject::Style::Outline;
        s->rect = QRectF(img, img);
        obj = s;
        break;
    }
    case ToolId::Marker: {
        // Next number after the highest marker in the document.
        int next = 1;
        for (const auto &o : m_canvas->document()->state().objects) {
            if (o->type() == Annotation::Type::Marker)
                next = std::max(next, static_cast<const MarkerObject *>(o.get())->number + 1);
        }
        auto m = std::make_shared<MarkerObject>();
        m->center = img;
        tooldefaults::applyTo(m_id, *m);
        m->number = next;
        return m;
    }
    case ToolId::Highlighter:
        if (tooldefaults::value(m_id, Prop::HighlightMode).toInt() == 1) {
            auto s = std::make_shared<ShapeObject>();
            s->style = ShapeObject::Style::Highlight;
            s->rect = QRectF(img, img);
            obj = s;
            break;
        }
        [[fallthrough]];
    case ToolId::Brush:
    default: {
        auto st = std::make_shared<StrokeObject>();
        st->highlighter = m_id == ToolId::Highlighter;
        st->points << img;
        obj = st;
        break;
    }
    }
    tooldefaults::applyTo(m_id, *obj);
    return obj;
}

void DrawTool::updateGeometry(const QPointF &img, bool shift)
{
    if (auto *m = dynamic_cast<MarkerObject *>(m_obj.get())) {
        m->center = img; // dragging positions the marker
        return;
    }
    if (auto *l = dynamic_cast<LineObject *>(m_obj.get())) {
        l->p2 = shift ? snapAngle(l->p1, img) : img;
    } else if (auto *s = dynamic_cast<ShapeObject *>(m_obj.get())) {
        QPointF a = m_pressImg, b = img;
        if (s->style != ShapeObject::Style::Outline) { // covers and highlights cover whole pixels
            a = QPointF(std::floor(a.x()), std::floor(a.y()));
            b = QPointF(std::round(b.x()), std::round(b.y()));
        }
        if (shift) { // square / circle
            const double side = std::max(std::abs(b.x() - a.x()), std::abs(b.y() - a.y()));
            b = QPointF(a.x() + (b.x() >= a.x() ? side : -side), a.y() + (b.y() >= a.y() ? side : -side));
        }
        s->rect = QRectF(a, b).normalized();
    } else if (auto *st = dynamic_cast<StrokeObject *>(m_obj.get())) {
        if (shift) { // straight stroke (spec D4)
            st->points = QPolygonF{st->points.first(), img};
        } else {
            const double minStep = std::max(0.5, 1.0 / m_canvas->scale());
            if (QLineF(st->points.last(), img).length() >= minStep)
                st->points << img;
        }
    }
}

bool DrawTool::isValid() const
{
    if (auto *l = dynamic_cast<LineObject *>(m_obj.get()))
        return QLineF(l->p1, l->p2).length() >= 2.0;
    if (auto *s = dynamic_cast<ShapeObject *>(m_obj.get()))
        return s->rect.width() >= 2.0 && s->rect.height() >= 2.0;
    return m_obj != nullptr; // a single click with a brush leaves a dot
}

void DrawTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_drag.begin(m_canvas, e->position()))
        return;
    m_canvas->setSelectedObjects({});
    m_start = m_canvas->document()->state();
    m_pressImg = img;
    m_obj = create(img);
    m_canvas->document()->setTransient(docops::addObjects(m_start, {m_obj}));
}

void DrawTool::mouseMove(QMouseEvent *e, const QPointF &img)
{
    if (m_drag.active()) {
        m_drag.update(m_canvas, img, e->modifiers());
        return;
    }
    if (!m_obj)
        return;
    auto next = m_obj->clone(); // keep stored objects immutable
    std::swap(m_obj, next);
    updateGeometry(img, e->modifiers() & Qt::ShiftModifier);
    m_canvas->document()->setTransient(docops::addObjects(m_start, {m_obj}));
}

void DrawTool::mouseRelease(QMouseEvent *e, const QPointF &img)
{
    if (m_drag.active()) {
        m_drag.finish(m_canvas);
        return;
    }
    if (!m_obj)
        return;
    // Final position and modifiers (Shift may be pressed after the last move).
    updateGeometry(img, e->modifiers() & Qt::ShiftModifier);
    Document *doc = m_canvas->document();
    if (isValid()) {
        doc->commit(docops::addObjects(m_start, {m_obj}), tr("Draw"));
        m_canvas->setSelectedObjects({m_obj->id}); // panel edits it right away (spec D1)
    } else {
        doc->setTransient(m_start);
    }
    m_obj.reset();
}

void DrawTool::paintOverlay(QPainter &p)
{
    if (!m_obj)
        paintObjectSelection(p, m_canvas);
}

QCursor DrawTool::cursorAt(const QPointF &w, const QPointF &)
{
    const int h = m_drag.handleAt(m_canvas, w);
    return h >= 0 ? QCursor(ObjectDrag::cursorFor(m_canvas, h)) : QCursor(Qt::CrossCursor);
}

void DrawTool::abort()
{
    m_drag.abort(m_canvas);
    if (m_obj) {
        m_canvas->document()->setTransient(m_start);
        m_obj.reset();
    }
}

bool DrawTool::cancel()
{
    if (isBusy()) {
        abort();
        return true;
    }
    if (!m_canvas->selectedObjects().isEmpty()) {
        m_canvas->setSelectedObjects({});
        return true;
    }
    return false;
}

// ================================================================ TextTool

namespace {
// Editor for text annotations: transparent, frameless, sized to its content.
class CanvasTextEdit : public QTextEdit
{
public:
    explicit CanvasTextEdit(QWidget *parent)
        : QTextEdit(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setAcceptRichText(false);
        setTabChangesFocus(true);
        setContentsMargins(0, 0, 0, 0);
        document()->setDocumentMargin(0);
        viewport()->setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet(QStringLiteral("QTextEdit { background: transparent; }"));
    }
};
} // namespace

TextTool::TextTool(Canvas *canvas, ToolId id)
    : Tool(canvas)
    , m_id(id)
{
    connect(canvas, &Canvas::viewChanged, this, [this] {
        if (m_editor)
            styleEditor();
    });
}

TextTool::~TextTool()
{
    delete m_editor;
}

void TextTool::beginEdit(const std::shared_ptr<TextObject> &obj, bool isNew)
{
    if (m_editor)
        finishEditing();
    m_obj = obj;
    m_isNew = isNew;
    m_dirty = false;
    m_originalText = obj->text;
    m_originalSpans = obj->spans;
    if (!isNew) {
        m_canvas->setHiddenObject(obj->id);
        m_canvas->setSelectedObjects({obj->id});
    } else {
        m_canvas->setSelectedObjects({});
    }
    m_editor = new CanvasTextEdit(m_canvas);
    m_editor->setPlainText(obj->text);
    TextObject::applySpans(m_editor->document(), obj->spans);
    m_editor->installEventFilter(this);
    connect(m_editor, &QTextEdit::textChanged, this, [this] { updateEditorGeometry(); });
    styleEditor();
    m_editor->show();
    m_editor->setFocus();
    m_editor->moveCursor(QTextCursor::End);
    m_canvas->update();
    emit optionsChanged();
}

void TextTool::styleEditor()
{
    if (!m_editor)
        return;
    const double s = m_canvas->scale();
    const QFont f = m_obj->font(s);
    m_editor->setFont(f);
    m_editor->document()->setDefaultFont(f);
    QPalette pal = m_editor->palette();
    pal.setColor(QPalette::Text, m_obj->color);
    pal.setColor(QPalette::Base, m_obj->background.alpha() > 0 ? m_obj->background : QColor(Qt::transparent));
    m_editor->setPalette(pal);
    QTextOption opt = m_editor->document()->defaultTextOption();
    opt.setAlignment(Qt::Alignment(m_obj->align));
    m_editor->document()->setDefaultTextOption(opt);
    if (m_obj->fixedWidth > 0) {
        m_editor->setLineWrapMode(QTextEdit::FixedPixelWidth);
        m_editor->setLineWrapColumnOrWidth(std::max(1, int(std::lround(m_obj->fixedWidth * s))));
        m_editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    } else {
        m_editor->setLineWrapMode(QTextEdit::NoWrap);
    }
    updateEditorGeometry();
}

void TextTool::updateEditorGeometry()
{
    if (!m_editor)
        return;
    const double s = m_canvas->scale();
    const double pad = m_obj->padding();
    const QPointF tl = m_canvas->toWidget(m_obj->transform.map(QPointF(pad, pad)));
    QTextDocument *doc = m_editor->document();
    const QFontMetricsF fm(m_editor->font());
    const double w = m_obj->fixedWidth > 0 ? m_obj->fixedWidth * s
                                           : doc->idealWidth() + fm.horizontalAdvance(QLatin1Char('M'));
    const double h = std::max(doc->size().height(), fm.height());
    QRectF g(tl, QSizeF(std::max(w, 8.0), h + 2));
    if (m_obj->transform.type() > QTransform::TxScale) {
        // A rotated text is edited upright, centered on where it is (Etapa 6).
        g.moveCenter(m_canvas->toWidget(m_obj->boundingRect().center()));
    }
    m_editor->setGeometry(g.toAlignedRect());
}

void TextTool::finishEditing()
{
    if (!m_editor)
        return;
    const QString text = m_editor->toPlainText();
    const QList<TextSpan> spans = m_obj->spansFrom(m_editor->document());
    QTextEdit *ed = m_editor;
    m_editor = nullptr;
    ed->removeEventFilter(this);
    ed->hide();
    ed->deleteLater();
    m_canvas->setHiddenObject(0);
    m_canvas->setFocus();

    auto obj = std::static_pointer_cast<TextObject>(m_obj->clone());
    obj->text = text;
    obj->spans = spans;
    Document *doc = m_canvas->document();
    const DocState s = doc->state();
    if (m_isNew) {
        if (!text.trimmed().isEmpty()) {
            doc->commit(docops::addObjects(s, {obj}), tr("Add text"));
            m_canvas->setSelectedObjects({obj->id});
        }
    } else if (text.trimmed().isEmpty()) {
        // An emptied text disappears (spec D2).
        doc->commit(docops::removeObjects(s, {obj->id}), tr("Delete text"));
    } else if (text != m_originalText || spans != m_originalSpans || m_dirty) {
        doc->commit(docops::replaceObject(s, obj), tr("Edit text"));
        m_canvas->setSelectedObjects({obj->id});
    }
    m_obj.reset();
    m_canvas->update();
    emit optionsChanged();
}

bool TextTool::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_editor && e->type() == QEvent::ShortcutOverride) {
        auto *k = static_cast<QKeyEvent *>(e);
        if ((k->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier)) == Qt::ControlModifier
            && (k->key() == Qt::Key_B || k->key() == Qt::Key_I || k->key() == Qt::Key_U)) {
            e->accept(); // Ctrl+B/I/U format the text, never trigger a menu shortcut
            return false;
        }
    }
    if (o == m_editor && e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        const bool ctrlEnter = (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)
                               && (k->modifiers() & Qt::ControlModifier);
        if (k->key() == Qt::Key_Escape || ctrlEnter) {
            // Esc / Ctrl+Enter end editing and keep the text (spec D2).
            finishEditing();
            return true;
        }
        const Qt::KeyboardModifiers mods = k->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        if (mods == Qt::ControlModifier
            && (k->key() == Qt::Key_B || k->key() == Qt::Key_I || k->key() == Qt::Key_U)) {
            const Prop prop = k->key() == Qt::Key_B ? Prop::Bold : k->key() == Qt::Key_I ? Prop::Italic : Prop::Underline;
            if (m_editor->textCursor().hasSelection()) {
                toggleSelectionFormat(prop);
            } else {
                // No selection: the whole text, like the button in the panel.
                m_canvas->applyProperty(prop, !m_obj->prop(prop).toBool());
                emit optionsChanged();
            }
            return true;
        }
    }
    return Tool::eventFilter(o, e);
}

void TextTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_editor) {
        finishEditing(); // a click outside ends editing and does nothing else
        return;
    }
    if (m_drag.begin(m_canvas, e->position()))
        return;
    if (const ObjectPtr o = m_canvas->objectAt(img); o && o->type() == Annotation::Type::Text) {
        beginEdit(std::static_pointer_cast<TextObject>(o->clone()), false);
        return;
    }
    m_canvas->setSelectedObjects({});
    m_creating = true;
    m_pressImg = img;
    m_dragRect = QRectF(img, img);
}

void TextTool::mouseMove(QMouseEvent *e, const QPointF &img)
{
    if (m_drag.active()) {
        m_drag.update(m_canvas, img, e->modifiers());
        return;
    }
    if (m_creating) {
        m_dragRect = QRectF(m_pressImg, img).normalized();
        m_dragEnd = img;
    }
}

void TextTool::mouseRelease(QMouseEvent *, const QPointF &)
{
    if (m_drag.active()) {
        m_drag.finish(m_canvas);
        return;
    }
    if (!m_creating)
        return;
    m_creating = false;
    auto obj = std::make_shared<TextObject>();
    if (m_id == ToolId::Bubble) {
        // Press = where the tail points, release = where the bubble goes.
        // A plain click puts the bubble up and to the right of the point.
        obj->bubble = true;
        tooldefaults::applyTo(ToolId::Bubble, *obj);
        obj->tail = m_pressImg;
        const QPointF end = m_dragRect.width() * m_canvas->scale() >= 16 || m_dragRect.height() * m_canvas->scale() >= 16
                                ? m_dragEnd
                                : m_pressImg + QPointF(40, -40 - obj->pixelSize * 2.0);
        const double pad = obj->padding();
        obj->transform = QTransform::fromTranslate(std::round(end.x() - pad), std::round(end.y() - pad));
        beginEdit(obj, true);
        return;
    }
    tooldefaults::applyTo(ToolId::Text, *obj);
    const double pad = obj->padding();
    // Click: automatic width. Drag: fixed width with wrapping (spec D2).
    if (m_dragRect.width() * m_canvas->scale() >= 16)
        obj->fixedWidth = std::max(double(obj->pixelSize), std::round(m_dragRect.width()));
    const QPointF tl = m_dragRect.width() * m_canvas->scale() >= 16 ? m_dragRect.topLeft() : m_pressImg;
    obj->transform = QTransform::fromTranslate(std::round(tl.x() - pad), std::round(tl.y() - pad));
    beginEdit(obj, true);
}

void TextTool::paintOverlay(QPainter &p)
{
    if (m_creating && m_dragRect.width() * m_canvas->scale() >= 16)
        drawDashedRect(p, m_canvas->toWidget(m_dragRect));
    if (m_editor) {
        // Show the box being edited.
        const QRectF box = m_canvas->viewTransform().mapRect(m_obj->boundingRect());
        p.setPen(QPen(QColor(0, 120, 215), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box.adjusted(-1, -1, 1, 1));
    } else {
        paintObjectSelection(p, m_canvas);
    }
}

QCursor TextTool::cursorAt(const QPointF &w, const QPointF &img)
{
    const int h = m_drag.handleAt(m_canvas, w);
    if (h >= 0)
        return ObjectDrag::cursorFor(m_canvas, h);
    const ObjectPtr o = m_canvas->objectAt(img);
    return o && o->type() == Annotation::Type::Text ? Qt::IBeamCursor : Qt::CrossCursor;
}

bool TextTool::formatsSelection(Prop prop) const
{
    return m_editor && m_editor->textCursor().hasSelection()
           && (prop == Prop::Bold || prop == Prop::Italic || prop == Prop::Underline || prop == Prop::Color);
}

void TextTool::toggleSelectionFormat(Prop prop)
{
    if (!m_editor)
        return;
    QTextCursor c = m_editor->textCursor();
    if (!c.hasSelection())
        return;
    // The state of the first selected character decides the new state.
    QTextCursor first(m_editor->document());
    first.setPosition(c.selectionStart() + 1);
    const QTextCharFormat cf = first.charFormat();
    QTextCharFormat f;
    if (prop == Prop::Bold) {
        const bool on = cf.hasProperty(QTextFormat::FontWeight) ? cf.fontWeight() >= QFont::DemiBold : m_obj->bold;
        f.setFontWeight(on ? QFont::Normal : QFont::Bold);
    } else if (prop == Prop::Italic) {
        const bool on = cf.hasProperty(QTextFormat::FontItalic) ? cf.fontItalic() : m_obj->italic;
        f.setFontItalic(!on);
    } else if (prop == Prop::Underline) {
        const bool on = cf.hasProperty(QTextFormat::TextUnderlineStyle) || cf.hasProperty(QTextFormat::FontUnderline)
                            ? cf.fontUnderline()
                            : m_obj->underline;
        f.setFontUnderline(!on);
    } else {
        return;
    }
    c.mergeCharFormat(f);
    m_dirty = true;
    updateEditorGeometry();
}

bool TextTool::applyProperty(Prop prop, const QVariant &v)
{
    if (!m_editor)
        return false;
    if (formatsSelection(prop)) {
        // A selection inside the edited text: only those characters (Etapa 4).
        if (prop == Prop::Color) {
            QTextCharFormat f;
            f.setForeground(v.value<QColor>());
            m_editor->textCursor().mergeCharFormat(f);
            m_dirty = true;
        } else {
            toggleSelectionFormat(prop);
        }
        emit optionsChanged(); // the panel keeps showing the whole text's values
        return true;
    }
    auto next = std::static_pointer_cast<TextObject>(m_obj->clone());
    next->setProp(prop, v);
    m_obj = next;
    m_dirty = true;
    styleEditor();
    m_canvas->update();
    return true;
}

bool TextTool::cancel()
{
    if (m_editor) {
        finishEditing();
        return true;
    }
    if (m_creating || m_drag.active()) {
        abort();
        return true;
    }
    if (!m_canvas->selectedObjects().isEmpty()) {
        m_canvas->setSelectedObjects({});
        return true;
    }
    return false;
}

void TextTool::abort()
{
    m_creating = false;
    m_drag.abort(m_canvas);
    finishEditing();
}

void TextTool::deactivate()
{
    abort();
}

// ================================================================ EyedropperTool

// ================================================================ BucketTool

void BucketTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton)
        return;
    Document *doc = m_canvas->document();
    const QPoint px(int(std::floor(img.x())), int(std::floor(img.y())));
    if (doc->isEmpty() || !canvasBounds(m_canvas).contains(px))
        return;
    QRect changed;
    const QImage filled = retouch::floodFill(doc->state().base, px, tooldefaults::value(id(), Prop::Color).value<QColor>(),
                                             tooldefaults::value(id(), Prop::Tolerance).toInt(),
                                             tooldefaults::value(id(), Prop::Contiguous).toBool(),
                                             tooldefaults::value(id(), Prop::Opacity).toDouble(), &changed);
    if (changed.isEmpty())
        return;
    DocState s = doc->state();
    s.base = filled;
    doc->commit(s, tr("Fill"));
}

// ================================================================ StraightenTool

double StraightenTool::angleFor(const QPointF &a, const QPointF &b)
{
    const double deg = std::atan2(b.y() - a.y(), b.x() - a.x()) * 180.0 / M_PI; // clockwise on screen
    const double target = std::round(deg / 90.0) * 90.0;
    return std::round((target - deg) * 100.0) / 100.0;
}

void StraightenTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton || m_canvas->document()->isEmpty())
        return;
    m_a = m_b = img;
    m_active = true;
}

void StraightenTool::mouseMove(QMouseEvent *, const QPointF &img)
{
    if (!m_active)
        return;
    m_b = img;
    m_canvas->update();
}

void StraightenTool::mouseRelease(QMouseEvent *, const QPointF &img)
{
    if (!m_active)
        return;
    m_active = false;
    m_b = img;
    m_canvas->update();
    if (QLineF(m_canvas->toWidget(m_a), m_canvas->toWidget(m_b)).length() < 8) {
        m_canvas->toast()->showMessage(tr("Drag along a line that should be level or upright."));
        return;
    }
    emit m_canvas->straightenRequested(angleFor(m_a, m_b));
}

bool StraightenTool::cancel()
{
    if (!m_active)
        return false;
    m_active = false;
    m_canvas->update();
    return true;
}

void StraightenTool::paintOverlay(QPainter &p)
{
    if (!m_active)
        return;
    const QPointF a = m_canvas->toWidget(m_a), b = m_canvas->toWidget(m_b);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::white, 3));
    p.drawLine(a, b);
    p.setPen(QPen(QColor(0, 120, 215), 1.5, Qt::DashLine));
    p.drawLine(a, b);
    const double angle = angleFor(m_a, m_b);
    const QString text = tr("Rotate by %1°").arg(QLocale().toString(angle, 'f', 1));
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    const QRectF tr_ = QFontMetricsF(f).boundingRect(text).adjusted(-6, -3, 6, 3).translated(b + QPointF(14, -10));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(tr_, 4, 4);
    p.setPen(Qt::white);
    p.drawText(tr_, Qt::AlignCenter, text);
}

void EyedropperTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPoint px(int(std::floor(img.x())), int(std::floor(img.y())));
    if (!canvasBounds(m_canvas).contains(px))
        return;
    // The visible result, annotations included (spec C3).
    const QImage one = docops::composite(m_canvas->document()->state(), QRect(px, QSize(1, 1)));
    QColor c = one.pixelColor(0, 0);
    c.setAlpha(255);
    m_canvas->pickColor(c);
}

// ---------------------------------------------------------------- RetouchTool

RetouchTool::RetouchTool(Canvas *canvas, ToolId id)
    : Tool(canvas)
    , m_id(id)
{
    // A new image invalidates the clone source.
    connect(canvas->document(), &Document::documentReset, this, [this] {
        m_hasSource = false;
        m_offsetValid = false;
    });
}

RetouchTool::~RetouchTool() = default;

double RetouchTool::size() const
{
    return tooldefaults::value(m_id, Prop::Size).toDouble();
}

void RetouchTool::setSource(const QPoint &p)
{
    m_source = p;
    m_hasSource = true;
    m_offsetValid = false;
    m_canvas->update();
}

void RetouchTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    const QPoint px(int(std::floor(img.x())), int(std::floor(img.y())));
    if (m_id == ToolId::Clone
        && (e->button() == Qt::RightButton
            || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)))) {
        // Ctrl+click or right click sets the source (spec D5).
        setSource(px);
        return;
    }
    if (e->button() != Qt::LeftButton || m_stroke)
        return;
    Document *doc = m_canvas->document();
    retouch::Stroke::Params p;
    p.diameter = size();
    p.hardness = tooldefaults::value(m_id, Prop::Hardness).toDouble();
    p.opacity = tooldefaults::value(m_id, Prop::Opacity).toDouble();
    switch (m_id) {
    case ToolId::Clone: {
        if (!m_hasSource) {
            m_canvas->toast()->showMessage(tr("Ctrl+click (or right-click) to set the source first."));
            return;
        }
        const bool aligned = tooldefaults::value(m_id, Prop::Aligned).toBool();
        if (!aligned || !m_offsetValid) {
            m_offset = m_source - px;
            m_offsetValid = true;
        }
        p.mode = retouch::Mode::Clone;
        p.cloneOffset = m_offset;
        break;
    }
    case ToolId::Eraser:
        p.mode = retouch::Mode::Erase;
        break;
    case ToolId::PixelBrush:
        p.mode = retouch::Mode::Pixelate;
        p.blurRadius = tooldefaults::value(m_id, Prop::Cell).toInt();
        break;
    default:
        p.mode = retouch::Mode::Blur;
        p.blurRadius = tooldefaults::value(m_id, Prop::Strength).toInt();
        break;
    }
    m_stroke = std::make_unique<retouch::Stroke>(doc->state().base, p);
    m_canvas->setBaseOverride(&m_stroke->result());
    m_canvas->baseChanged(m_stroke->lineTo(img));
}

void RetouchTool::mouseMove(QMouseEvent *, const QPointF &img)
{
    m_hover = img;
    m_hoverValid = true;
    if (m_stroke)
        m_canvas->baseChanged(m_stroke->lineTo(img));
}

void RetouchTool::mouseRelease(QMouseEvent *e, const QPointF &img)
{
    if (!m_stroke || e->button() != Qt::LeftButton)
        return;
    m_canvas->baseChanged(m_stroke->lineTo(img));
    const bool changed = !m_stroke->changedRect().isEmpty();
    DocState s = m_canvas->document()->state();
    s.base = m_stroke->takeResult();
    m_stroke.reset();
    m_canvas->setBaseOverride(nullptr);
    if (changed) {
        const QString text = m_id == ToolId::Clone    ? tr("Clone stamp")
                             : m_id == ToolId::Eraser ? tr("Erase")
                                                      : tr("Blur brush");
        m_canvas->document()->commit(s, text);
    }
    m_canvas->update();
}

bool RetouchTool::keyPress(QKeyEvent *e)
{
    // [ and ] change the brush size (spec D5). Match the text too: on many
    // layouts the brackets need AltGr and arrive with another key code.
    const bool smaller = e->key() == Qt::Key_BracketLeft || e->text() == QLatin1String("[");
    const bool larger = e->key() == Qt::Key_BracketRight || e->text() == QLatin1String("]");
    if (!smaller && !larger)
        return false;
    const double s = size();
    const double step = std::max(1.0, std::round(s * 0.1));
    const double next = std::clamp(smaller ? s - step : s + step, 1.0, 1000.0);
    tooldefaults::setValue(m_id, Prop::Size, next);
    emit optionsChanged();
    m_canvas->update();
    return true;
}

bool RetouchTool::cancel()
{
    if (m_stroke) {
        abort();
        return true;
    }
    return false;
}

void RetouchTool::abort()
{
    if (!m_stroke)
        return;
    m_stroke.reset();
    m_canvas->setBaseOverride(nullptr);
    m_canvas->update();
}

void RetouchTool::paintOverlay(QPainter &p)
{
    if (!m_hoverValid || !m_canvas->underMouse())
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    // Outline in view coordinates, so it stays thin at any zoom (spec D5).
    const QPointF c = m_canvas->toWidget(m_hover);
    const double r = std::max(1.0, size() / 2.0 * m_canvas->scale());
    p.setPen(QPen(QColor(255, 255, 255, 200), 1));
    p.drawEllipse(c, r, r);
    p.setPen(QPen(QColor(0, 0, 0, 200), 1, Qt::DashLine));
    p.drawEllipse(c, r, r);
    if (m_id == ToolId::Clone && m_hasSource) {
        const bool aligned = tooldefaults::value(m_id, Prop::Aligned).toBool();
        QPointF src;
        if (m_stroke || (aligned && m_offsetValid))
            src = m_canvas->toWidget(m_hover + QPointF(m_offset));
        else
            src = m_canvas->toWidget(QPointF(m_source) + QPointF(0.5, 0.5));
        for (const QColor &col : {QColor(255, 255, 255), QColor(0, 0, 0)}) {
            p.setPen(QPen(col, col == Qt::white ? 3 : 1));
            p.drawLine(src - QPointF(8, 0), src + QPointF(8, 0));
            p.drawLine(src - QPointF(0, 8), src + QPointF(0, 8));
        }
    }
    p.restore();
}

// ---------------------------------------------------------------- PolyTool

PolyTool::PolyTool(Canvas *canvas, ToolId id)
    : Tool(canvas)
    , m_id(id)
{
}

QPointF PolyTool::constrained(const QPointF &img, bool shift) const
{
    if (shift && m_obj && !m_obj->points.isEmpty())
        return snapAngle(m_obj->points.last(), img);
    return img;
}

void PolyTool::showPreview()
{
    if (!m_obj)
        return;
    auto preview = std::static_pointer_cast<PolylineObject>(m_obj->clone());
    preview->closed = false; // show the open path while drawing
    if (QLineF(preview->points.last(), m_hover).length() > 0.5)
        preview->points << m_hover;
    if (m_id == ToolId::Polygon && preview->points.size() >= 3)
        preview->closed = true;
    if (preview->points.size() >= 2)
        m_canvas->document()->setTransient(docops::addObjects(m_start, {preview}));
    else
        m_canvas->document()->setTransient(m_start);
}

void PolyTool::mousePress(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (!m_obj && m_drag.begin(m_canvas, e->position()))
        return;
    const QPointF p = constrained(img, e->modifiers() & Qt::ShiftModifier);
    if (!m_obj) {
        m_canvas->setSelectedObjects({});
        m_start = m_canvas->document()->state();
        m_obj = std::make_shared<PolylineObject>();
        m_obj->closed = m_id == ToolId::Polygon;
        tooldefaults::applyTo(m_id, *m_obj);
        m_obj->points << p;
        m_hover = p;
        showPreview();
        return;
    }
    // Clicking the first point closes a polygon.
    const double tol = 6.0 / m_canvas->scale();
    if (m_id == ToolId::Polygon && m_obj->points.size() >= 3 && QLineF(p, m_obj->points.first()).length() <= tol) {
        finish();
        return;
    }
    if (QLineF(p, m_obj->points.last()).length() > 0.5)
        m_obj->points << p;
    m_hover = p;
    showPreview();
}

void PolyTool::mouseMove(QMouseEvent *e, const QPointF &img)
{
    if (m_drag.active()) {
        m_drag.update(m_canvas, img, e->modifiers());
        return;
    }
    if (!m_obj)
        return;
    m_hover = constrained(img, e->modifiers() & Qt::ShiftModifier);
    showPreview();
}

void PolyTool::mouseRelease(QMouseEvent *, const QPointF &)
{
    if (m_drag.active())
        m_drag.finish(m_canvas);
}

void PolyTool::mouseDoubleClick(QMouseEvent *e, const QPointF &img)
{
    if (e->button() != Qt::LeftButton || !m_obj)
        return;
    // Usually the first click of the double-click has added the point already.
    const QPointF p = constrained(img, e->modifiers() & Qt::ShiftModifier);
    if (QLineF(p, m_obj->points.last()).length() > 2.0 / m_canvas->scale())
        m_obj->points << p;
    finish();
}

bool PolyTool::keyPress(QKeyEvent *e)
{
    if (!m_obj)
        return false;
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        finish();
        return true;
    case Qt::Key_Backspace:
        if (m_obj->points.size() > 1) {
            m_obj->points.removeLast();
            showPreview();
        } else {
            abort();
        }
        return true;
    default:
        return false;
    }
}

void PolyTool::finish()
{
    if (!m_obj)
        return;
    auto obj = m_obj;
    m_obj.reset();
    const int needed = obj->closed ? 3 : 2;
    Document *doc = m_canvas->document();
    if (obj->points.size() >= needed) {
        doc->setTransient(m_start);
        doc->commit(docops::addObjects(m_start, {obj}), tr("Draw"));
        m_canvas->setSelectedObjects({obj->id});
    } else {
        doc->setTransient(m_start);
    }
    m_canvas->update();
}

bool PolyTool::cancel()
{
    if (m_obj) {
        abort();
        return true;
    }
    if (!m_canvas->selectedObjects().isEmpty()) {
        m_canvas->setSelectedObjects({});
        return true;
    }
    return false;
}

void PolyTool::abort()
{
    if (m_drag.active())
        m_drag.abort(m_canvas);
    if (!m_obj)
        return;
    m_obj.reset();
    m_canvas->document()->setTransient(m_start);
    m_canvas->update();
}

void PolyTool::paintOverlay(QPainter &p)
{
    if (!m_obj) {
        paintObjectSelection(p, m_canvas);
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(30, 30, 30), 1));
    p.setBrush(Qt::white);
    for (const QPointF &pt : m_obj->points)
        p.drawEllipse(m_canvas->toWidget(pt), 3.5, 3.5);
    p.restore();
}

QCursor PolyTool::cursorAt(const QPointF &w, const QPointF &)
{
    if (!m_obj) {
        const int h = m_drag.handleAt(m_canvas, w);
        if (h >= 0)
            return ObjectDrag::cursorFor(m_canvas, h);
    }
    return Qt::CrossCursor;
}
