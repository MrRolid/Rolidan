#include "canvas.h"

#include "settings.h"
#include <QApplication>
#include <QTimer>

#include "document.h"
#include "tooldefaults.h"
#include "toast.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <cmath>

namespace {
constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 64.0;
const double kSteps[] = {0.02, 0.03, 0.05, 0.0625, 0.1, 0.125, 0.167, 0.25, 0.333, 0.5, 0.667,
                         1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0, 48.0, 64.0};
constexpr int kViewMargin = 24; // logical px around a fitted image
constexpr int kKeepVisible = 48;

QBrush checkerBrush()
{
    static QBrush brush = [] {
        QPixmap pm(16, 16);
        pm.fill(QColor(0xcc, 0xcc, 0xcc));
        QPainter p(&pm);
        p.fillRect(0, 0, 8, 8, QColor(0x99, 0x99, 0x99));
        p.fillRect(8, 8, 8, 8, QColor(0x99, 0x99, 0x99));
        return QBrush(pm);
    }();
    return brush;
}
} // namespace

Canvas::Canvas(Document *doc, QWidget *parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_toast(new Toast(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(200, 150);

    m_tools[ToolId::Pointer] = std::make_unique<PointerTool>(this);
    m_tools[ToolId::Select] = std::make_unique<SelectTool>(this);
    m_tools[ToolId::Crop] = std::make_unique<CropTool>(this);
    m_tools[ToolId::Text] = std::make_unique<TextTool>(this);
    m_tools[ToolId::Bubble] = std::make_unique<TextTool>(this, ToolId::Bubble);
    for (ToolId id : {ToolId::Polyline, ToolId::Polygon})
        m_tools[id] = std::make_unique<PolyTool>(this, id);
    for (ToolId id : {ToolId::Arrow, ToolId::Line, ToolId::Rect, ToolId::Ellipse, ToolId::Brush,
                      ToolId::Highlighter, ToolId::Cover, ToolId::Marker})
        m_tools[id] = std::make_unique<DrawTool>(this, id);
    m_tools[ToolId::Eyedropper] = std::make_unique<EyedropperTool>(this);
    for (ToolId id : {ToolId::Clone, ToolId::Eraser, ToolId::BlurBrush, ToolId::PixelBrush})
        m_tools[id] = std::make_unique<RetouchTool>(this, id);
    m_tools[ToolId::Bucket] = std::make_unique<BucketTool>(this);
    m_tools[ToolId::Straighten] = std::make_unique<StraightenTool>(this);
    m_tool = m_tools[ToolId::Pointer].get();

    connect(doc, &Document::changed, this, &Canvas::onDocumentChanged);
    connect(doc, &Document::documentReset, this, &Canvas::onDocumentReset);
}

Canvas::~Canvas() = default;

// ---------------------------------------------------------------- view

double Canvas::scale() const
{
    return m_zoom / devicePixelRatioF();
}

QTransform Canvas::viewTransform() const
{
    const double s = scale();
    return QTransform(s, 0, 0, s, m_origin.x(), m_origin.y());
}

QPointF Canvas::toImage(const QPointF &w) const
{
    return (w - m_origin) / scale();
}

QPointF Canvas::toWidget(const QPointF &i) const
{
    return m_origin + i * scale();
}

QRectF Canvas::toWidget(const QRectF &r) const
{
    return QRectF(toWidget(r.topLeft()), r.size() * scale());
}

QRectF Canvas::visibleImageRect() const
{
    const QRectF all(toImage(QPointF(0, 0)), toImage(QPointF(width(), height())));
    return all.intersected(QRectF(QPointF(0, 0), QSizeF(m_doc->size())));
}

void Canvas::setZoom(double zoom, const QPointF &anchor)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    const QPointF imgAnchor = toImage(anchor);
    m_autoFit = false;
    m_zoom = zoom;
    m_origin = anchor - imgAnchor * scale();
    clampOrigin();
    update();
    emit zoomChanged(m_zoom);
}

void Canvas::zoomIn()
{
    for (double s : kSteps) {
        if (s > m_zoom * 1.001) {
            setZoom(s);
            return;
        }
    }
}

void Canvas::zoomOut()
{
    for (auto it = std::rbegin(kSteps); it != std::rend(kSteps); ++it) {
        if (*it < m_zoom * 0.999) {
            setZoom(*it);
            return;
        }
    }
}

void Canvas::fitToWindow(bool allowEnlarge)
{
    const int margin = m_dark ? 0 : kViewMargin; // full screen: edge to edge
    if (m_doc->isEmpty() || width() <= 2 * margin || height() <= 2 * margin)
        return;
    const QSize s = m_doc->size();
    double z = std::min((width() - 2.0 * margin) / s.width(), (height() - 2.0 * margin) / s.height())
               * devicePixelRatioF();
    if (!allowEnlarge && !m_enlargeSmall)
        z = std::min(z, 1.0);
    m_autoFit = true; // follows the window size until the user zooms or pans
    m_autoFitEnlarge = allowEnlarge;
    m_zoom = std::clamp(z, kMinZoom, kMaxZoom);
    m_origin = QPointF((width() - s.width() * scale()) / 2, (height() - s.height() * scale()) / 2);
    snapOrigin();
    emit viewChanged();
    update();
    emit zoomChanged(m_zoom);
}

void Canvas::actualSize()
{
    const QPointF centerImg = toImage(rect().center());
    m_autoFit = false;
    m_zoom = 1.0;
    m_origin = QPointF(rect().center()) - centerImg * scale();
    clampOrigin();
    update();
    emit zoomChanged(m_zoom);
}

void Canvas::clampOrigin()
{
    if (m_doc->isEmpty())
        return;
    const QSizeF sz = QSizeF(m_doc->size()) * scale();
    // Keep the image centered when it fits, otherwise keep part of it visible.
    const int margin = m_dark ? 0 : kViewMargin;
    auto clampAxis = [margin](double origin, double extent, double view) {
        if (extent <= view - 2 * margin)
            return (view - extent) / 2;
        return std::clamp(origin, kKeepVisible - extent, view - kKeepVisible);
    };
    m_origin.setX(clampAxis(m_origin.x(), sz.width(), width()));
    m_origin.setY(clampAxis(m_origin.y(), sz.height(), height()));
    snapOrigin();
    emit viewChanged();
}

void Canvas::snapOrigin()
{
    // Align the image to physical pixels: at 100 % it stays perfectly sharp.
    const double dpr = devicePixelRatioF();
    m_origin = QPointF(std::round(m_origin.x() * dpr) / dpr, std::round(m_origin.y() * dpr) / dpr);
}

void Canvas::panBy(const QPointF &delta)
{
    const QPointF before = m_origin;
    m_origin += delta;
    clampOrigin();
    if ((m_origin - before).manhattanLength() > 0.5)
        m_autoFit = false; // a real pan (a fitted image cannot move)
    update();
}

void Canvas::setAutoHideCursor(bool on)
{
    if (on && !m_hideCursorTimer) {
        m_hideCursorTimer = new QTimer(this);
        m_hideCursorTimer->setSingleShot(true);
        m_hideCursorTimer->setInterval(1500);
        connect(m_hideCursorTimer, &QTimer::timeout, this, [this] {
            if (m_panning || m_tool->isBusy() || QApplication::mouseButtons() != Qt::NoButton)
                return;
            m_cursorHidden = true;
            setCursor(Qt::BlankCursor);
        });
        m_hideCursorTimer->start();
    } else if (!on && m_hideCursorTimer) {
        delete m_hideCursorTimer;
        m_hideCursorTimer = nullptr;
        if (m_cursorHidden) {
            m_cursorHidden = false;
            updateCursor(mapFromGlobal(QCursor::pos()));
        }
    }
}

void Canvas::setEnlargeSmallImages(bool on)
{
    if (on == m_enlargeSmall)
        return;
    m_enlargeSmall = on;
    if (m_autoFit && !m_doc->isEmpty())
        fitToWindow(m_autoFitEnlarge);
}

// ---------------------------------------------------------------- selection

void Canvas::setAreaSelection(const QRect &r)
{
    const QRect clamped = r.intersected(QRect(QPoint(0, 0), m_doc->size()));
    if (clamped == m_area)
        return;
    m_area = clamped;
    update();
    emit selectionChanged();
}

void Canvas::setSelectedObjects(const QSet<quint64> &ids)
{
    if (ids == m_selected)
        return;
    m_selected = ids;
    update();
    emit selectionChanged();
}

ObjectList Canvas::selectedObjectList() const
{
    ObjectList out;
    for (const auto &o : m_doc->state().objects) {
        if (m_selected.contains(o->id))
            out.append(o);
    }
    return out;
}

ObjectPtr Canvas::objectAt(const QPointF &imgPos) const
{
    const double tol = 5.0 / scale(); // 5 screen pixels
    const ObjectList &objects = m_doc->state().objects;
    for (auto it = objects.crbegin(); it != objects.crend(); ++it) {
        if ((*it)->id != m_hidden && (*it)->hitTest(imgPos, tol))
            return *it;
    }
    return {};
}

void Canvas::setHiddenObject(quint64 id)
{
    m_hidden = id;
    update();
}

TextTool *Canvas::textTool() const
{
    // The bubble tool is a text tool too; it edits when it is active.
    if (m_tool && m_tool->id() == ToolId::Bubble)
        return static_cast<TextTool *>(m_tool);
    return static_cast<TextTool *>(m_tools.at(ToolId::Text).get());
}

void Canvas::editTextObject(quint64 id)
{
    const ObjectPtr o = docops::findObject(m_doc->state(), id);
    if (!o || o->type() != Annotation::Type::Text)
        return;
    setTool(ToolId::Text);
    textTool()->beginEdit(std::static_pointer_cast<TextObject>(o->clone()), false);
}

void Canvas::applyProperty(Prop prop, const QVariant &value)
{
    const ToolId tid = m_tool->id();
    auto *text = dynamic_cast<TextTool *>(m_tool);
    const bool partOfText = text && text->formatsSelection(prop);
    if (!partOfText && tooldefaults::props(tid).contains(prop))
        tooldefaults::setValue(tid, prop, value);
    if (m_tool->applyProperty(prop, value))
        return;
    DocState s = m_doc->state();
    bool changed = false;
    for (auto &o : s.objects) {
        if (!m_selected.contains(o->id) || !o->props().contains(prop))
            continue;
        auto c = o->clone();
        c->setProp(prop, value);
        o = c;
        changed = true;
        if (tooldefaults::props(c->origin).contains(prop))
            tooldefaults::setValue(c->origin, prop, value);
    }
    // Continuous edits of one property merge into one undo step (spec D1).
    if (changed)
        m_doc->commit(s, tr("Change properties"), 2000 + int(prop));
}

void Canvas::pickColor(const QColor &c)
{
    ToolId back = m_previousTool;
    if (back == ToolId::Eyedropper || back == ToolId::Select || back == ToolId::Crop)
        back = ToolId::Pointer;
    setTool(back);
    applyProperty(Prop::Color, c);
    emit colorPicked(c);
}

// ---------------------------------------------------------------- tools

void Canvas::setTool(ToolId id)
{
    Tool *t = m_tools.at(id).get();
    if (t == m_tool)
        return;
    m_tool->deactivate();
    m_previousTool = m_tool->id();
    // Area tools work on pixels: drop the object selection so Delete/Ctrl+C
    // are not ambiguous.
    if (id == ToolId::Select || id == ToolId::Crop || id == ToolId::Clone || id == ToolId::Eraser
        || id == ToolId::BlurBrush || id == ToolId::PixelBrush || id == ToolId::Bucket || id == ToolId::Straighten)
        setSelectedObjects({});
    m_tool = t;
    m_tool->activate();
    unsetCursor();
    update();
    emit toolChanged(id);
}

void Canvas::setBaseOverride(const QImage *img)
{
    // The override starts as an exact copy of the base: keep the zoomed cache.
    const QImage &current = m_baseOverride ? *m_baseOverride : m_doc->state().base;
    if (img && m_scaledKey == current.cacheKey())
        m_scaledKey = img->cacheKey();
    m_baseOverride = img;
    update();
}

void Canvas::baseChanged(const QRect &r)
{
    if (r.isEmpty())
        return;
    const QImage &base = m_baseOverride ? *m_baseOverride : m_doc->state().base;
    if (m_zoom < 1.0 && !m_scaledCache.isNull() && qFuzzyCompare(m_scaledZoom, m_zoom)
        && m_scaledCache.size() != base.size()) {
        const double sx = double(m_scaledCache.width()) / base.width();
        const double sy = double(m_scaledCache.height()) / base.height();
        const QRect t = QRectF(r.x() * sx, r.y() * sy, r.width() * sx, r.height() * sy)
                            .toAlignedRect()
                            .adjusted(-1, -1, 1, 1)
                            .intersected(m_scaledCache.rect());
        const QRect src = QRectF(t.x() / sx, t.y() / sy, t.width() / sx, t.height() / sy)
                              .toAlignedRect()
                              .intersected(base.rect());
        if (!t.isEmpty() && !src.isEmpty()) {
            const qreal dpr = m_scaledCache.devicePixelRatio();
            m_scaledCache.setDevicePixelRatio(1.0);
            QPainter p(&m_scaledCache);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(t.topLeft(), base.copy(src).scaled(t.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            p.end();
            m_scaledCache.setDevicePixelRatio(dpr);
        }
        m_scaledKey = base.cacheKey();
    }
    update(toWidget(QRectF(r)).toAlignedRect().adjusted(-2, -2, 2, 2));
}

void Canvas::setObjectsHidden(bool hidden)
{
    m_objectsHidden = hidden;
    update();
}

void Canvas::cancelInteraction()
{
    // Spec B2: a drag in progress or a pending crop is cancelled; area and
    // object selections are kept.
    m_panning = false;
    m_tool->abort();
    if (m_tool->id() == ToolId::Crop)
        m_tool->cancel();
    update();
}

void Canvas::onDocumentChanged()
{
    // Keep selection valid after undo, crop, rotation...
    const QRect area = m_area.intersected(QRect(QPoint(0, 0), m_doc->size()));
    QSet<quint64> sel;
    for (const auto &o : m_doc->state().objects) {
        if (m_selected.contains(o->id))
            sel.insert(o->id);
    }
    if (area != m_area || sel != m_selected) {
        m_area = area;
        m_selected = sel;
        emit selectionChanged();
    }
    clampOrigin();
    update();
}

void Canvas::onDocumentReset()
{
    m_area = {};
    m_selected.clear();
    m_tool->abort();
    if (m_tool->id() == ToolId::Crop)
        m_tool->cancel();
    m_fitPending = true;
    if (isVisible() && width() > 0) {
        fitToWindow(false);
        m_fitPending = false;
    }
    emit selectionChanged();
    update();
}

// ---------------------------------------------------------------- painting

void Canvas::drawBase(QPainter &p, const QRectF &imgW)
{
    const QImage &base = m_baseOverride ? *m_baseOverride : m_doc->state().base;
    if (m_zoom < 1.0) {
        // Pre-scaled, high-quality copy; recomputed only when zoom or base change.
        if (m_scaledKey != base.cacheKey() || !qFuzzyCompare(m_scaledZoom, m_zoom)) {
            const QSize target(std::max(1, int(std::lround(base.width() * m_zoom))),
                               std::max(1, int(std::lround(base.height() * m_zoom))));
            m_scaledCache = base.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_scaledKey = base.cacheKey();
            m_scaledZoom = m_zoom;
        }
        m_scaledCache.setDevicePixelRatio(devicePixelRatioF());
        p.drawImage(imgW.topLeft(), m_scaledCache);
        return;
    }
    // Draw only the visible part; nearest-neighbour from 200 % (spec D7).
    const QRectF visible = visibleImageRect();
    if (visible.isEmpty())
        return;
    const QRect src = QRect(QPoint(int(std::floor(visible.left())), int(std::floor(visible.top()))),
                            QPoint(int(std::ceil(visible.right())) - 1, int(std::ceil(visible.bottom())) - 1))
                          .intersected(base.rect());
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 2.0);
    p.drawImage(toWidget(QRectF(src)), base, src);
}

void Canvas::drawObjects(QPainter &p, const QRectF &imgW)
{
    const ObjectList &objects = m_doc->state().objects;
    if (objects.isEmpty() || m_objectsHidden)
        return;
    const QTransform view = viewTransform();
    auto paintAll = [&](double opacity) {
        p.setOpacity(opacity);
        for (const auto &o : objects) {
            if (o->id == m_hidden)
                continue;
            p.save();
            p.setTransform(o->transform * view);
            o->paint(p);
            p.restore();
        }
        p.setOpacity(1.0);
    };
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 2.0);
    // Outside the canvas: faint, so objects can be found and moved back (spec C5).
    QRegion outside = QRegion(rect()) - QRegion(imgW.toAlignedRect());
    p.setClipRegion(outside);
    paintAll(0.35);
    p.setClipRect(imgW);
    paintAll(1.0);
    p.setClipping(false);
}

void Canvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), m_dark ? QColor(18, 18, 18) : palette().color(QPalette::Dark));
    if (m_doc->isEmpty()) {
        p.setPen(palette().color(QPalette::BrightText));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.3);
        p.setFont(f);
        p.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Press Ctrl+V to paste an image\nor drop a file here"));
        return;
    }
    const QRectF imgW = toWidget(QRectF(QPointF(0, 0), QSizeF(m_doc->size())));
    p.fillRect(imgW, checkerBrush());
    drawBase(p, imgW);
    drawObjects(p, imgW);

    p.setRenderHint(QPainter::Antialiasing, false);
    // Area selection is visible with every tool until cleared.
    if (!m_area.isEmpty() && m_tool->id() != ToolId::Select) {
        const QRectF r = toWidget(QRectF(m_area));
        p.setPen(QPen(Qt::white, 1));
        p.drawRect(r);
        p.setPen(QPen(Qt::black, 1, Qt::DashLine));
        p.drawRect(r);
    }
    if (m_grid) {
        // Alignment grid (rotation, straightening): view only.
        const double step = 48.0;
        p.setClipRect(imgW);
        QColor light(255, 255, 255, 110), dark(0, 0, 0, 90);
        for (double x = imgW.left() + step; x < imgW.right(); x += step) {
            p.setPen(dark);
            p.drawLine(QPointF(x, imgW.top()), QPointF(x, imgW.bottom()));
            p.setPen(light);
            p.drawLine(QPointF(x + 1, imgW.top()), QPointF(x + 1, imgW.bottom()));
        }
        for (double y = imgW.top() + step; y < imgW.bottom(); y += step) {
            p.setPen(dark);
            p.drawLine(QPointF(imgW.left(), y), QPointF(imgW.right(), y));
            p.setPen(light);
            p.drawLine(QPointF(imgW.left(), y + 1), QPointF(imgW.right(), y + 1));
        }
        p.setClipping(false);
    }
    m_tool->paintOverlay(p);
}

void Canvas::setGridVisible(bool on)
{
    if (m_grid == on)
        return;
    m_grid = on;
    update();
}

// ---------------------------------------------------------------- input

void Canvas::updateCursor(const QPointF &w)
{
    if (m_panning) {
        setCursor(Qt::ClosedHandCursor);
    } else if (m_spaceHeld) {
        setCursor(Qt::OpenHandCursor);
    } else if (!m_doc->isEmpty()) {
        setCursor(m_tool->cursorAt(w, toImage(w)));
    } else {
        unsetCursor();
    }
}

void Canvas::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && m_spaceHeld)) {
        m_panning = true;
        m_panLast = e->position();
        updateCursor(e->position());
        return;
    }
    if (m_doc->isEmpty())
        return;
    m_doc->closeMerge();
    m_tool->mousePress(e, toImage(e->position()));
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_hideCursorTimer) {
        m_hideCursorTimer->start();
        if (m_cursorHidden) {
            m_cursorHidden = false;
            updateCursor(e->position());
        }
    }
    if (m_panning) {
        panBy(e->position() - m_panLast);
        m_panLast = e->position();
        return;
    }
    if (!m_doc->isEmpty()) {
        const QPointF img = toImage(e->position());
        const QPoint px(int(std::floor(img.x())), int(std::floor(img.y())));
        emit cursorMoved(px, QRect(QPoint(0, 0), m_doc->size()).contains(px));
        m_tool->mouseMove(e, img);
        updateCursor(e->position());
        update();
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning) {
        if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
            m_panning = false;
            updateCursor(e->position());
        }
        return;
    }
    if (m_doc->isEmpty())
        return;
    m_tool->mouseRelease(e, toImage(e->position()));
    updateCursor(e->position());
    update();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_doc->isEmpty() || m_spaceHeld) {
        mousePressEvent(e);
        return;
    }
    setFocus();
    m_tool->mouseDoubleClick(e, toImage(e->position()));
    update();
}

void Canvas::wheelEvent(QWheelEvent *e)
{
    if (m_doc->isEmpty())
        return;
    const settings::Wheel mode = settings::wheelAction();
    const bool plain = !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier));
    if ((e->modifiers() & Qt::ControlModifier) || (plain && mode == settings::Wheel::Zoom)) {
        const double steps = e->angleDelta().y() / 120.0;
        setZoom(m_zoom * std::pow(1.25, steps), e->position());
        e->accept();
        return;
    }
    if (plain && mode == settings::Wheel::Browse) {
        // One image per notch; touchpads send small deltas that add up.
        m_wheelBrowse += e->angleDelta().y();
        while (std::abs(m_wheelBrowse) >= 120) {
            const int dir = m_wheelBrowse > 0 ? -1 : 1; // wheel down = next image
            m_wheelBrowse += dir * 120;
            emit browseRequested(dir);
        }
        e->accept();
        return;
    }
    QPointF d = !e->pixelDelta().isNull() ? QPointF(e->pixelDelta())
                                          : QPointF(e->angleDelta()) / 120.0 * 48.0;
    if ((e->modifiers() & Qt::ShiftModifier) && qFuzzyIsNull(d.x()))
        d = QPointF(d.y(), 0);
    panBy(d);
    e->accept();
}

bool Canvas::event(QEvent *e)
{
    if (e->type() == QEvent::NativeGesture) {
        auto *g = static_cast<QNativeGestureEvent *>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture && !m_doc->isEmpty()) {
            setZoom(m_zoom * (1.0 + g->value()), g->position());
            return true;
        }
    }
    if (e->type() == QEvent::ShortcutOverride) {
        // Let the active tool consume Enter / arrows before window shortcuts.
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() == Qt::Key_Escape || k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

void Canvas::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && !m_tool->isBusy()) {
        m_spaceHeld = true;
        updateCursor(mapFromGlobal(QCursor::pos()));
        return;
    }
    if (!m_doc->isEmpty() && m_tool->keyPress(e)) {
        update();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        if (!m_tool->cancel()) {
            if (m_area.isEmpty()) {
                e->ignore(); // nothing to cancel: the window may use it (leave full screen)
                return;
            }
            setAreaSelection({});
        }
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void Canvas::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceHeld = false;
        updateCursor(mapFromGlobal(QCursor::pos()));
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void Canvas::resizeEvent(QResizeEvent *e)
{
    if ((m_fitPending || m_autoFit) && !m_doc->isEmpty()) {
        fitToWindow(m_fitPending ? false : m_autoFitEnlarge); // e.g. entering full screen
        m_fitPending = false;
        return;
    }
    m_origin += QPointF(e->size().width() - e->oldSize().width(),
                        e->size().height() - e->oldSize().height())
                / 2.0;
    if (e->oldSize().isValid())
        clampOrigin();
}

void Canvas::leaveEvent(QEvent *)
{
    emit cursorMoved({}, false);
}
