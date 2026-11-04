#pragma once

#include "tools.h"

#include <QImage>
#include <QSet>
#include <QWidget>
#include <map>
#include <memory>

class Document;
class Toast;

// The editing surface: view (zoom, pan) + tool dispatch + selection state.
// Zoom is expressed in DEVICE pixels per image pixel, so 100 % shows one image
// pixel on one physical screen pixel even on HiDPI screens (decision J3).
class Canvas : public QWidget
{
    Q_OBJECT
public:
    explicit Canvas(Document *doc, QWidget *parent = nullptr);
    ~Canvas() override;

    Document *document() const { return m_doc; }
    Toast *toast() const { return m_toast; }

    // ---- view ----
    double zoom() const { return m_zoom; }
    void setZoom(double zoom, const QPointF &anchorWidget);
    void setZoom(double zoom) { setZoom(zoom, rect().center()); }
    void zoomIn();
    void zoomOut();
    void fitToWindow(bool allowEnlarge = true);
    void actualSize();
    double scale() const; // widget logical px per image px
    QPointF toImage(const QPointF &widgetPos) const;
    QPointF toWidget(const QPointF &imgPos) const;
    QRectF toWidget(const QRectF &imgRect) const;
    QTransform viewTransform() const;
    QRectF visibleImageRect() const;

    // ---- selection ----
    QRect areaSelection() const { return m_area; }
    void setAreaSelection(const QRect &r);
    QSet<quint64> selectedObjects() const { return m_selected; }
    void setSelectedObjects(const QSet<quint64> &ids);
    ObjectList selectedObjectList() const; // in z-order
    // Topmost object at an image position, with a tolerance of a few screen pixels.
    ObjectPtr objectAt(const QPointF &imgPos) const;

    // Object currently edited in place (not painted by the canvas), 0 = none.
    quint64 hiddenObject() const { return m_hidden; }
    void setHiddenObject(quint64 id);
    // Start editing a text object in place (switches to the text tool).
    void editTextObject(quint64 id);
    TextTool *textTool() const;

    // Property change from the options bar (spec D1): goes to the text being
    // edited, otherwise to the selected objects; remembered as tool default.
    void applyProperty(Prop prop, const QVariant &value);
    // Eyedropper result: color for the previous tool and the selection.
    void pickColor(const QColor &c);
    ToolId previousTool() const { return m_previousTool; }

    // ---- tools ----
    void setTool(ToolId id);
    ToolId toolId() const { return m_tool->id(); }
    Tool *tool() const { return m_tool; }
    Tool *tool(ToolId id) const { return m_tools.at(id).get(); }
    void cancelInteraction();

    void setOverlayDirty() { update(); }

    // Retouch strokes paint into their own buffer; the canvas shows it instead
    // of the document's base until the stroke is committed (null = none).
    void setBaseOverride(const QImage *img);
    // Part of the shown base changed (updates the zoomed-out cache incrementally).
    void baseChanged(const QRect &imgRect);
    // View option "Hide annotations" (spec D5, Ctrl+H): changes nothing in the document.
    bool objectsHidden() const { return m_objectsHidden; }
    // Alignment grid over the image (view only), e.g. while rotating.
    void setGridVisible(bool on);
    // Near-black surroundings for full screen and slideshow (Etapa 5).
    void setDarkSurroundings(bool on) { m_dark = on; update(); }
    // Fitting also enlarges images smaller than the view (full screen option, Etapa 5).
    void setEnlargeSmallImages(bool on);
    bool enlargesSmallImages() const { return m_enlargeSmall; }
    // The view is fitted and follows size changes (until zooming or panning).
    bool isAutoFit() const { return m_autoFit; }
    // Hide the mouse pointer after a moment without movement (full screen).
    void setAutoHideCursor(bool on);
    bool gridVisible() const { return m_grid; }
    void setObjectsHidden(bool hidden);

signals:
    void zoomChanged(double zoom);
    void viewChanged();       // zoom or pan changed (text editor follows)
    void colorPicked(const QColor &c);
    void cursorMoved(const QPoint &imgPos, bool inside);
    void selectionChanged();
    void toolChanged(ToolId id);
    // The straighten tool measured a line: rotate by `degrees` (clockwise).
    void straightenRequested(double degrees);
    // Mouse wheel in "browse images" mode (Etapa 5): +1 next, -1 previous.
    void browseRequested(int delta);
    // Pointer tool: double-click outside annotations.
    void imageDoubleClicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void leaveEvent(QEvent *) override;
    bool event(QEvent *) override;

private:
    void onDocumentChanged();
    void onDocumentReset();
    void clampOrigin();
    void snapOrigin();
    void panBy(const QPointF &delta);
    void updateCursor(const QPointF &widgetPos);
    void drawBase(QPainter &p, const QRectF &imgW);
    void drawObjects(QPainter &p, const QRectF &imgW);

    Document *m_doc;
    Toast *m_toast;
    std::map<ToolId, std::unique_ptr<Tool>> m_tools;
    Tool *m_tool = nullptr;

    double m_zoom = 1.0;
    QPointF m_origin;           // widget position of image (0,0)
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPointF m_panLast;
    bool m_fitPending = true;

    QRect m_area;
    QSet<quint64> m_selected;
    quint64 m_hidden = 0;
    ToolId m_previousTool = ToolId::Pointer;

    const QImage *m_baseOverride = nullptr;
    bool m_objectsHidden = false;
    bool m_grid = false;
    QImage m_scaledCache;       // downscaled base for zoom < 100 %
    qint64 m_scaledKey = 0;
    double m_scaledZoom = 0;
    bool m_autoFit = false;
    class QTimer *m_hideCursorTimer = nullptr;
    bool m_cursorHidden = false;
    bool m_autoFitEnlarge = false;
    bool m_enlargeSmall = false;
    bool m_dark = false;
    int m_wheelBrowse = 0;
};
