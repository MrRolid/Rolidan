#pragma once

#include "annotation.h"
#include "history.h"

#include <QObject>
#include <QRect>
#include <QSet>

// Pure operations on a document state. All coordinates are image pixels (spec C2).
namespace docops {

enum class CopyMode { BaseOnly, All }; // spec B3: without Shift / with Shift

QRect canvasRect(const DocState &s);

// Render base + objects for `area` (default: whole canvas). Result is
// ARGB32_Premultiplied, size == area.size(). This is THE render function used
// by export and clipboard (spec C5).
QImage composite(const DocState &s, QRect area = {}, bool withObjects = true);
// Render only the given objects into the union of their bounding rects.
QImage renderObjects(const ObjectList &objects, QPoint *topLeftOut = nullptr);

DocState crop(const DocState &s, const QRect &r);
DocState rotate90(const DocState &s, bool clockwise);
DocState rotate180(const DocState &s);
DocState flip(const DocState &s, Qt::Orientation orientation); // Horizontal = mirror left/right

QImage copySelection(const DocState &s, const QRect &sel, CopyMode mode);
// Clears the base inside `sel` to transparency; in All mode also removes
// objects lying entirely inside `sel`.
DocState cutSelection(const DocState &s, const QRect &sel, CopyMode mode);
DocState clearBase(const DocState &s, const QRect &sel);

// Grow the canvas (transparent fill) so it contains `r`; shifts content if
// `r` extends to negative coordinates.
DocState expandCanvasToInclude(const DocState &s, const QRectF &r);
// Resample the base to `size`; objects scale with it, line widths and font
// sizes by the smaller factor (spec C4).
// `filter`: filters::Resample (0 Lanczos … 4 nearest); `sharpen` after resampling;
// `dpi` > 0 stores a new resolution (also alone, without changing the size).
DocState resizeImage(const DocState &s, const QSize &size, int filter = 0, bool sharpen = false, double dpi = 0);
// New canvas of `size`; the old base is placed at `offset`, the rest filled
// with `fill` (may be transparent). Objects move by the same vector (spec C4).
DocState resizeCanvas(const DocState &s, const QSize &size, const QPoint &offset, const QColor &fill);

DocState addObjects(const DocState &s, const ObjectList &objects);

enum class Order { Forward, Backward, ToFront, ToBack };
DocState reorder(const DocState &s, const QSet<quint64> &ids, Order order);
// Copies of `ids` shifted by `offset`; new ids are returned in *newIds.
DocState duplicate(const DocState &s, const QSet<quint64> &ids, const QPointF &offset, QSet<quint64> *newIds);
// Paint the given objects (all if `ids` is empty) into the base and remove
// them (spec C3 "Sloučit").
DocState mergeIntoBase(const DocState &s, const QSet<quint64> &ids = {});
DocState removeObjects(const DocState &s, const QSet<quint64> &ids);
DocState replaceObject(const DocState &s, const ObjectPtr &obj);
ObjectPtr findObject(const DocState &s, quint64 id);
// Topmost object whose outline contains p, or null.
ObjectPtr objectAt(const DocState &s, const QPointF &p);

} // namespace docops

// A document = current state + history + file information.
class Document : public QObject
{
    Q_OBJECT
public:
    explicit Document(QObject *parent = nullptr);

    bool isEmpty() const { return m_state.base.isNull(); }
    const DocState &state() const { return m_state; }
    QSize size() const { return m_state.base.size(); }

    // Start a fresh document (clears history).
    void reset(const QImage &base, const QString &originPath = {});
    // Start from a full state (opened project): objects included, no undo step.
    void reset(const DocState &state, const QString &originPath = {});

    // Record a new state as one history step.
    void commit(const DocState &s, const QString &text, int mergeKey = 0);
    // Update the visible state without recording (live preview while dragging).
    void setTransient(const DocState &s);
    // End merging of consecutive steps (e.g. after a series of nudges).
    void closeMerge() { m_history.closeMerge(); }

    History &history() { return m_history; }
    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    void undo();
    void redo();

    bool isModified() const { return !isEmpty() && !m_history.isClean(); }
    void setSaved(const QString &path, const QByteArray &format);
    // The file was renamed or moved outside the document (Etapa 5): follow it.
    void relocate(const QString &from, const QString &to);

    QString filePath() const { return m_filePath; }
    QByteArray fileFormat() const { return m_fileFormat; }
    // Path of the file the document was opened from (spec F2), empty if pasted.
    QString originPath() const { return m_originPath; }
    bool overwriteOriginConfirmed() const { return m_originConfirmed; }
    void setOverwriteOriginConfirmed(bool v) { m_originConfirmed = v; }

signals:
    void changed();          // state changed (content)
    void historyChanged();   // undo/redo availability or modified flag changed
    void documentReset();    // a new document was loaded

private:
    void apply(const DocState &s);

    DocState m_state;
    History m_history;
    QString m_filePath;
    QByteArray m_fileFormat;
    QString m_originPath;
    bool m_originConfirmed = false;
};
