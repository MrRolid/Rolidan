#include "document.h"

#include "filters.h"

#include <QPainter>

namespace docops {

QRect canvasRect(const DocState &s)
{
    return QRect(QPoint(0, 0), s.base.size());
}

static void paintObjects(QPainter &p, const ObjectList &objects, const QTransform &view)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const auto &o : objects) {
        p.save();
        p.setTransform(o->transform * view);
        o->paint(p);
        p.restore();
    }
}

QImage composite(const DocState &s, QRect area, bool withObjects)
{
    if (area.isNull())
        area = canvasRect(s);
    QImage out(area.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(-area.topLeft(), s.base);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (withObjects) {
        // Objects never extend the canvas: clip to it (spec C4/C5).
        p.setClipRect(canvasRect(s).translated(-area.topLeft()));
        paintObjects(p, s.objects, QTransform::fromTranslate(-area.x(), -area.y()));
    }
    p.end();
    return out;
}

QImage renderObjects(const ObjectList &objects, QPoint *topLeftOut)
{
    QRectF bounds;
    for (const auto &o : objects)
        bounds |= o->visualRect();
    const QRect r = bounds.toAlignedRect();
    if (topLeftOut)
        *topLeftOut = r.topLeft();
    if (r.isEmpty())
        return {};
    QImage out(r.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    paintObjects(p, objects, QTransform::fromTranslate(-r.x(), -r.y()));
    p.end();
    return out;
}

static DocState transformed(const DocState &s, const QImage &newBase, const QTransform &t,
                            DocTransformKind kind)
{
    DocState out;
    out.base = newBase;
    out.notes = s.notes;
    for (const auto &o : s.objects) {
        auto c = o->clone();
        c->applyDocumentTransform(t, kind);
        out.objects.append(c);
    }
    return out;
}

DocState crop(const DocState &s, const QRect &r)
{
    const QRect rr = r.intersected(canvasRect(s));
    if (rr.isEmpty())
        return s;
    DocState out = transformed(s, s.base.copy(rr), QTransform::fromTranslate(-rr.x(), -rr.y()),
                               DocTransformKind::Translate);
    // Objects entirely outside the new canvas are deleted; partially
    // overlapping ones stay whole and are clipped at render time (spec C4).
    const QRectF canvas(QPointF(0, 0), QSizeF(rr.size()));
    out.objects.removeIf([&](const ObjectPtr &o) { return !o->visualRect().intersects(canvas); });
    return out;
}

DocState rotate90(const DocState &s, bool clockwise)
{
    const qreal w = s.base.width(), h = s.base.height();
    // QTransform(m11, m12, m21, m22, dx, dy): x' = m11 x + m21 y + dx, y' = m12 x + m22 y + dy
    const QTransform t = clockwise ? QTransform(0, 1, -1, 0, h, 0)   // (x,y) -> (h - y, x)
                                   : QTransform(0, -1, 1, 0, 0, w);  // (x,y) -> (y, w - x)
    const QImage base = s.base.transformed(QTransform().rotate(clockwise ? 90 : -90));
    return transformed(s, base, t, DocTransformKind::Rotate);
}

DocState rotate180(const DocState &s)
{
    const qreal w = s.base.width(), h = s.base.height();
    return transformed(s, s.base.mirrored(true, true), QTransform(-1, 0, 0, -1, w, h),
                       DocTransformKind::Rotate);
}

DocState flip(const DocState &s, Qt::Orientation orientation)
{
    const qreal w = s.base.width(), h = s.base.height();
    const bool horizontal = orientation == Qt::Horizontal;
    const QTransform t = horizontal ? QTransform(-1, 0, 0, 1, w, 0) : QTransform(1, 0, 0, -1, 0, h);
    return transformed(s, s.base.mirrored(horizontal, !horizontal), t, DocTransformKind::Mirror);
}

QImage copySelection(const DocState &s, const QRect &sel, CopyMode mode)
{
    const QRect r = sel.intersected(canvasRect(s));
    if (r.isEmpty())
        return {};
    return composite(s, r, mode == CopyMode::All);
}

DocState clearBase(const DocState &s, const QRect &sel)
{
    const QRect r = sel.intersected(canvasRect(s));
    if (r.isEmpty())
        return s;
    DocState out = s;
    out.base = s.base.copy(); // detach explicitly: history keeps the old buffer
    QPainter p(&out.base);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(r, Qt::transparent);
    p.end();
    return out;
}

DocState cutSelection(const DocState &s, const QRect &sel, CopyMode mode)
{
    DocState out = clearBase(s, sel);
    if (mode == CopyMode::All) {
        const QRectF r(sel);
        out.objects.removeIf([&](const ObjectPtr &o) { return r.contains(o->visualRect()); });
    }
    return out;
}

DocState expandCanvasToInclude(const DocState &s, const QRectF &r)
{
    const QRect current = canvasRect(s);
    const QRect target = current.united(r.toAlignedRect());
    if (target == current)
        return s;
    QImage base(target.size(), QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::transparent);
    QPainter p(&base);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(-target.topLeft(), s.base);
    p.end();
    return transformed(s, base, QTransform::fromTranslate(-target.x(), -target.y()),
                       DocTransformKind::Translate);
}

DocState resizeImage(const DocState &s, const QSize &size, int filter, bool sharpen, double dpi)
{
    if (size.isEmpty())
        return s;
    auto withDpi = [dpi, &s](QImage img) {
        if (dpi > 0) {
            const int dpm = int(std::lround(dpi / 0.0254));
            img.setDotsPerMeterX(dpm);
            img.setDotsPerMeterY(dpm);
        } else { // keep the original resolution
            img.setDotsPerMeterX(s.base.dotsPerMeterX());
            img.setDotsPerMeterY(s.base.dotsPerMeterY());
        }
        return img;
    };
    if (size == s.base.size()) {
        if (dpi <= 0)
            return s;
        DocState out = s; // resolution only
        out.base = withDpi(s.base.copy());
        return out;
    }
    QImage base = filters::resample(s.base, size, filters::Resample(std::clamp(filter, 0, 6)));
    if (sharpen)
        base = filters::sharpen(base, 40, 0.6, 2);
    base = withDpi(base);
    const QTransform t = QTransform::fromScale(double(size.width()) / s.base.width(),
                                               double(size.height()) / s.base.height());
    return transformed(s, base, t, DocTransformKind::Scale);
}

DocState resizeCanvas(const DocState &s, const QSize &size, const QPoint &offset, const QColor &fill)
{
    if (size.isEmpty())
        return s;
    QImage base(size, QImage::Format_ARGB32_Premultiplied);
    base.fill(fill.isValid() ? fill : QColor(Qt::transparent));
    QPainter p(&base);
    p.setCompositionMode(QPainter::CompositionMode_Source); // the fill only goes to the new area
    p.drawImage(offset, s.base);
    p.end();
    return transformed(s, base, QTransform::fromTranslate(offset.x(), offset.y()), DocTransformKind::Translate);
}

DocState addObjects(const DocState &s, const ObjectList &objects)
{
    DocState out = s;
    out.objects.append(objects);
    return out;
}

DocState reorder(const DocState &s, const QSet<quint64> &ids, Order order)
{
    DocState out = s;
    ObjectList &v = out.objects;
    switch (order) {
    case Order::ToFront:
    case Order::ToBack: {
        ObjectList moved, rest;
        for (const auto &o : v)
            (ids.contains(o->id) ? moved : rest).append(o);
        v = order == Order::ToFront ? rest + moved : moved + rest;
        break;
    }
    case Order::Forward:
        for (qsizetype i = v.size() - 2; i >= 0; --i) {
            if (ids.contains(v[i]->id) && !ids.contains(v[i + 1]->id))
                v.swapItemsAt(i, i + 1);
        }
        break;
    case Order::Backward:
        for (qsizetype i = 1; i < v.size(); ++i) {
            if (ids.contains(v[i]->id) && !ids.contains(v[i - 1]->id))
                v.swapItemsAt(i, i - 1);
        }
        break;
    }
    return out;
}

DocState duplicate(const DocState &s, const QSet<quint64> &ids, const QPointF &offset, QSet<quint64> *newIds)
{
    DocState out = s;
    for (const auto &o : s.objects) {
        if (!ids.contains(o->id))
            continue;
        auto c = o->clone();
        c->id = Annotation::nextId();
        c->applyDocumentTransform(QTransform::fromTranslate(offset.x(), offset.y()), DocTransformKind::Translate);
        out.objects.append(c);
        if (newIds)
            newIds->insert(c->id);
    }
    return out;
}

DocState mergeIntoBase(const DocState &s, const QSet<quint64> &ids)
{
    DocState merged{s.base, {}, {}};
    DocState out{QImage(), {}, s.notes};
    for (const auto &o : s.objects) {
        if (ids.isEmpty() || ids.contains(o->id))
            merged.objects.append(o);
        else
            out.objects.append(o);
    }
    if (merged.objects.isEmpty())
        return s;
    out.base = composite(merged);
    return out;
}

DocState removeObjects(const DocState &s, const QSet<quint64> &ids)
{
    DocState out = s;
    out.objects.removeIf([&](const ObjectPtr &o) { return ids.contains(o->id); });
    return out;
}

DocState replaceObject(const DocState &s, const ObjectPtr &obj)
{
    DocState out = s;
    for (auto &o : out.objects) {
        if (o->id == obj->id)
            o = obj;
    }
    return out;
}

ObjectPtr findObject(const DocState &s, quint64 id)
{
    for (const auto &o : s.objects) {
        if (o->id == id)
            return o;
    }
    return {};
}

ObjectPtr objectAt(const DocState &s, const QPointF &p)
{
    for (auto it = s.objects.crbegin(); it != s.objects.crend(); ++it) {
        if ((*it)->contains(p))
            return *it;
    }
    return {};
}

} // namespace docops

Document::Document(QObject *parent)
    : QObject(parent)
{
    m_history.reset({});
}

void Document::reset(const QImage &base, const QString &originPath)
{
    reset(DocState{base, {}, {}}, originPath);
}

void Document::reset(const DocState &state, const QString &originPath)
{
    m_state = state;
    m_history.reset(m_state);
    m_filePath = originPath;
    m_originPath = originPath;
    m_fileFormat.clear();
    m_originConfirmed = false;
    emit documentReset();
    emit changed();
    emit historyChanged();
}

void Document::commit(const DocState &s, const QString &text, int mergeKey)
{
    m_history.push(text, s, mergeKey);
    apply(s);
    emit historyChanged();
}

void Document::setTransient(const DocState &s)
{
    apply(s);
}

void Document::undo()
{
    if (!canUndo())
        return;
    apply(m_history.undo());
    emit historyChanged();
}

void Document::redo()
{
    if (!canRedo())
        return;
    apply(m_history.redo());
    emit historyChanged();
}

void Document::setSaved(const QString &path, const QByteArray &format)
{
    m_filePath = path;
    m_fileFormat = format;
    m_history.setClean();
    emit historyChanged();
}

void Document::relocate(const QString &from, const QString &to)
{
    bool changedPath = false;
    if (m_filePath == from) {
        m_filePath = to;
        changedPath = true;
    }
    if (m_originPath == from) {
        m_originPath = to;
        changedPath = true;
    }
    if (changedPath)
        emit historyChanged(); // title and actions follow the path
}

void Document::apply(const DocState &s)
{
    m_state = s;
    emit changed();
}
