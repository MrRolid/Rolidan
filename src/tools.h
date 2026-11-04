#pragma once

#include <optional>

#include "history.h"
#include "toolid.h"

#include <QCursor>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <memory>

class Canvas;
class QComboBox;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QSpinBox;
class QTextEdit;
class QWidget;
class TextObject;

class Tool : public QObject
{
    Q_OBJECT
public:
    explicit Tool(Canvas *canvas);

    virtual ToolId id() const = 0;
    // `img` is the cursor position in image coordinates (not rounded).
    virtual void mousePress(QMouseEvent *, const QPointF &img) { Q_UNUSED(img) }
    virtual void mouseMove(QMouseEvent *, const QPointF &img) { Q_UNUSED(img) }
    virtual void mouseRelease(QMouseEvent *, const QPointF &img) { Q_UNUSED(img) }
    virtual void mouseDoubleClick(QMouseEvent *e, const QPointF &img) { mousePress(e, img); }
    virtual bool keyPress(QKeyEvent *) { return false; }
    // Let the tool consume a property change (e.g. the text being edited).
    virtual bool applyProperty(Prop, const QVariant &) { return false; }
    virtual void paintOverlay(QPainter &) {}
    virtual QCursor cursorAt(const QPointF &widgetPos, const QPointF &img);
    virtual void activate() {}
    virtual void deactivate() {}
    // Abort an operation in progress (drag, pending crop). Returns true if
    // something was cancelled, so Esc can fall through otherwise.
    virtual bool cancel() { return false; }
    // Abort only a mouse drag in progress (undo, transforms, paste); keeps
    // selections. Default: nothing to abort.
    virtual void abort() {}
    virtual bool isBusy() const { return false; }
    virtual void selectAll() {}
    // Widget for the tool options bar (owned by the caller), or null.
    virtual QWidget *createOptions(QWidget *) { return nullptr; }

signals:
    void optionsChanged();

protected:
    Canvas *m_canvas;
};

// Shared editing of an integer, pixel-snapped rectangle with 8 handles (spec D6).
class RectEditor
{
public:
    enum Hit { None = -1, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Inside };

    QRect rect;           // image coordinates; empty = no rectangle
    double ratio = 0.0;   // width / height, 0 = free

    Hit hitTest(const Canvas *c, const QPointF &widgetPos) const;
    void begin(Hit hit, const QPointF &img);   // hit == None starts a new rectangle
    void update(const QPointF &img, bool shift, const QRect &bounds);
    void end() { m_mode = None; }
    bool dragging() const { return m_dragging; }
    void stop() { m_dragging = false; }
    void paintHandles(QPainter &p, const Canvas *c) const;
    static Qt::CursorShape cursorFor(Hit hit);
    // Enforce the ratio on `rect`, keeping its center, inside bounds.
    void applyRatio(const QRect &bounds);

private:
    Hit m_mode = None;
    bool m_dragging = false;
    QPoint m_pressPt;
    QRect m_pressRect;
};

// Dragging a handle of the single selected object (corners, line endpoints,
// text width), shared by the pointer and drawing tools (spec D1, D3).
class ObjectDrag
{
public:
    int handleAt(const Canvas *c, const QPointF &widgetPos) const;
    bool begin(Canvas *c, const QPointF &widgetPos);
    void update(Canvas *c, const QPointF &img, Qt::KeyboardModifiers mods);
    void finish(Canvas *c);
    void abort(Canvas *c);
    bool active() const { return m_active; }
    static QCursor cursorFor(const Canvas *c, int handle);
    // Size or angle next to the object while dragging.
    void paintLabel(QPainter &p, const Canvas *c) const;
    // Where the round rotation handle of `o` is (widget coordinates), if it has one.
    static std::optional<QPointF> rotateHandlePos(const Canvas *c, const Annotation &o);
    static constexpr int kRotateHandle = 1000;
    bool rotating() const { return m_active && m_handle == kRotateHandle; }

private:
    bool m_active = false;
    bool m_changed = false;
    DocState m_start;
    quint64 m_id = 0;
    int m_handle = -1;
    QRectF m_startRect;    // frame (unrotated box) at the start
    double m_startAngle = 0.0;
    QPointF m_pivot;       // center of the box
    double m_pressAngle = 0.0;
    double m_angle = 0.0;  // current angle while rotating
};

// Outline + handles of selected objects.
void paintObjectSelection(QPainter &p, const Canvas *c);

class PointerTool : public Tool
{
    Q_OBJECT
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Pointer; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    void mouseMove(QMouseEvent *, const QPointF &img) override;
    void mouseRelease(QMouseEvent *, const QPointF &img) override;
    bool keyPress(QKeyEvent *) override;
    void paintOverlay(QPainter &) override;
    QCursor cursorAt(const QPointF &widgetPos, const QPointF &img) override;
    bool cancel() override;
    void abort() override;
    bool isBusy() const override { return m_mode != Idle; }
    void selectAll() override;
    void deactivate() override;

    void mouseDoubleClick(QMouseEvent *, const QPointF &img) override;

private:
    enum Mode { Idle, Moving, Handle, Rubber };
    Mode m_mode = Idle;
    ObjectDrag m_drag;
    DocState m_start;
    QPointF m_pressImg;
    QRectF m_rubber;
    bool m_changed = false;
};

class SelectTool : public Tool
{
    Q_OBJECT
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Select; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    void mouseMove(QMouseEvent *, const QPointF &img) override;
    void mouseRelease(QMouseEvent *, const QPointF &img) override;
    void paintOverlay(QPainter &) override;
    QCursor cursorAt(const QPointF &widgetPos, const QPointF &img) override;
    bool cancel() override;
    void abort() override { m_editor.stop(); }
    bool isBusy() const override { return m_editor.dragging(); }
    void selectAll() override;

private:
    RectEditor m_editor;
};

class CropTool : public Tool
{
    Q_OBJECT
public:
    explicit CropTool(Canvas *canvas);
    ToolId id() const override { return ToolId::Crop; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    void mouseMove(QMouseEvent *, const QPointF &img) override;
    void mouseRelease(QMouseEvent *, const QPointF &img) override;
    bool keyPress(QKeyEvent *) override;
    void paintOverlay(QPainter &) override;
    QCursor cursorAt(const QPointF &widgetPos, const QPointF &img) override;
    void activate() override;
    void deactivate() override;
    bool cancel() override;
    void abort() override { m_editor.stop(); }
    bool isBusy() const override { return !m_editor.rect.isEmpty(); }
    void selectAll() override;
    QWidget *createOptions(QWidget *parent) override;

    QRect cropRect() const { return m_editor.rect; }
    void setCropRect(const QRect &r);
    bool apply();

private:
    void setRatioFromString(const QString &r);
    void syncSpins();

    RectEditor m_editor;
    QSpinBox *m_x = nullptr, *m_y = nullptr, *m_w = nullptr, *m_h = nullptr;
    QComboBox *m_ratio = nullptr;
    bool m_syncing = false;
};

// Line, arrow, rectangle, ellipse, brush, highlighter, cover (spec D3–D5, 8).
class DrawTool : public Tool
{
    Q_OBJECT
public:
    DrawTool(Canvas *canvas, ToolId id);
    ToolId id() const override { return m_id; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    void mouseMove(QMouseEvent *, const QPointF &img) override;
    void mouseRelease(QMouseEvent *, const QPointF &img) override;
    void paintOverlay(QPainter &) override;
    QCursor cursorAt(const QPointF &widgetPos, const QPointF &img) override;
    bool cancel() override;
    void abort() override;
    bool isBusy() const override { return m_obj != nullptr || m_drag.active(); }

private:
    std::shared_ptr<Annotation> create(const QPointF &img) const;
    void updateGeometry(const QPointF &img, bool shift);
    bool isValid() const;

    ToolId m_id;
    ObjectDrag m_drag;
    DocState m_start;
    QPointF m_pressImg;
    std::shared_ptr<Annotation> m_obj;
};

// Text annotations edited directly on the canvas (spec D2). Editing uses a
// real text widget so input methods, dead keys and text undo work.
class TextTool : public Tool
{
    Q_OBJECT
public:
    explicit TextTool(Canvas *canvas, ToolId id = ToolId::Text);
    ~TextTool() override;
    ToolId id() const override { return m_id; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    void mouseMove(QMouseEvent *, const QPointF &img) override;
    void mouseRelease(QMouseEvent *, const QPointF &img) override;
    void paintOverlay(QPainter &) override;
    QCursor cursorAt(const QPointF &widgetPos, const QPointF &img) override;
    bool applyProperty(Prop, const QVariant &) override;
    bool cancel() override;
    void abort() override;
    void deactivate() override;
    bool isBusy() const override { return isEditing() || m_creating || m_drag.active(); }

    bool isEditing() const { return m_editor != nullptr; }
    void beginEdit(const std::shared_ptr<TextObject> &obj, bool isNew);
    void finishEditing();
    QTextEdit *editor() const { return m_editor; }
    const TextObject *editedObject() const { return m_obj.get(); }
    // True when `prop` would change only the selected part of the edited text.
    bool formatsSelection(Prop prop) const;
    // Toggles bold / italic / underline of the selected characters (Ctrl+B/I/U).
    void toggleSelectionFormat(Prop prop);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void styleEditor();
    void updateEditorGeometry();

    ToolId m_id;
    ObjectDrag m_drag;
    QTextEdit *m_editor = nullptr;
    std::shared_ptr<TextObject> m_obj;
    QString m_originalText;
    QList<TextSpan> m_originalSpans;
    bool m_isNew = false;
    bool m_dirty = false;
    bool m_creating = false;
    QPointF m_pressImg;
    QRectF m_dragRect;
    QPointF m_dragEnd;
};

// Polyline / polyline arrow / polygon (Etapa 4): click adds a point, the last
// segment follows the mouse, double-click or Enter finishes, Backspace removes
// the last point, Esc cancels. Shift: 15° steps. Clicking the first point
// closes a polygon.
class PolyTool : public Tool
{
    Q_OBJECT
public:
    PolyTool(Canvas *canvas, ToolId id);
    ToolId id() const override { return m_id; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    void mouseMove(QMouseEvent *, const QPointF &img) override;
    void mouseRelease(QMouseEvent *, const QPointF &img) override;
    void mouseDoubleClick(QMouseEvent *, const QPointF &img) override;
    bool keyPress(QKeyEvent *) override;
    void paintOverlay(QPainter &) override;
    QCursor cursorAt(const QPointF &widgetPos, const QPointF &img) override;
    bool cancel() override;
    void abort() override;
    void deactivate() override { finish(); }
    bool isBusy() const override { return m_obj != nullptr || m_drag.active(); }
    // Commit what has been drawn so far (if valid).
    void finish();

private:
    void showPreview();
    QPointF constrained(const QPointF &img, bool shift) const;

    ToolId m_id;
    ObjectDrag m_drag;
    DocState m_start;
    std::shared_ptr<PolylineObject> m_obj; // fixed points only
    QPointF m_hover;
};

// Picks a color from the visible result and gives it to the previous tool (spec 8).
class EyedropperTool : public Tool
{
    Q_OBJECT
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Eyedropper; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    QCursor cursorAt(const QPointF &, const QPointF &) override { return Qt::CrossCursor; }
};

// Paint bucket (Etapa 4): flood fill of the base with the tool color.
class BucketTool : public Tool
{
    Q_OBJECT
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Bucket; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    bool applyProperty(Prop, const QVariant &) override { return true; }
    QCursor cursorAt(const QPointF &, const QPointF &) override { return Qt::CrossCursor; }
};

// Straighten (Etapa 4): drag along something that should be level or
// upright; on release the canvas asks for a rotation by the angle needed.
class StraightenTool : public Tool
{
    Q_OBJECT
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Straighten; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    void mouseMove(QMouseEvent *, const QPointF &img) override;
    void mouseRelease(QMouseEvent *, const QPointF &img) override;
    void paintOverlay(QPainter &) override;
    QCursor cursorAt(const QPointF &, const QPointF &) override { return Qt::CrossCursor; }
    bool cancel() override;
    void abort() override { m_active = false; }
    bool isBusy() const override { return m_active; }
    // Rotation (clockwise degrees) that makes the line from a to b level or upright.
    static double angleFor(const QPointF &a, const QPointF &b);

private:
    QPointF m_a, m_b;
    bool m_active = false;
};

namespace retouch { class Stroke; }

// Brush tools working on the base pixels (spec C3, D5): clone stamp, pixel
// eraser and blur brush. One stroke = one undo step.
class RetouchTool : public Tool
{
    Q_OBJECT
public:
    RetouchTool(Canvas *canvas, ToolId id);
    ~RetouchTool() override;
    ToolId id() const override { return m_id; }
    void mousePress(QMouseEvent *, const QPointF &img) override;
    void mouseMove(QMouseEvent *, const QPointF &img) override;
    void mouseRelease(QMouseEvent *, const QPointF &img) override;
    bool keyPress(QKeyEvent *) override;
    bool applyProperty(Prop, const QVariant &) override { return true; }
    void paintOverlay(QPainter &) override;
    QCursor cursorAt(const QPointF &, const QPointF &) override { return Qt::CrossCursor; }
    bool cancel() override;
    void abort() override;
    void deactivate() override { abort(); }
    bool isBusy() const override { return m_stroke != nullptr; }

    bool hasSource() const { return m_hasSource; }
    QPoint source() const { return m_source; }
    void setSource(const QPoint &p);

private:
    double size() const;

    ToolId m_id;
    std::unique_ptr<retouch::Stroke> m_stroke;
    QPoint m_source;
    bool m_hasSource = false;
    QPoint m_offset;             // source - target of the current / last stroke
    bool m_offsetValid = false;
    QPointF m_hover;
    bool m_hoverValid = false;
};
