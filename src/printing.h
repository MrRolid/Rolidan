#pragma once

#include <QDialog>
#include <QImage>
#include <QRectF>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPrinter;
class QPrintPreviewWidget;
class QRadioButton;
class QSpinBox;
class QPainter;

// Printing (Etapa 6): how the image is placed on the page.
struct PrintLayout
{
    enum class Size { Fit, Actual, Scale };
    enum class Orientation { Auto, Portrait, Landscape };
    Size size = Size::Fit;
    double dpi = 96.0;       // Actual: image pixels per inch on paper
    int percent = 100;       // Scale: % of the actual size
    bool center = true;      // otherwise the top-left corner of the printable area
    bool annotations = true;
    Orientation orientation = Orientation::Auto;
    double marginMm = 10.0;

    static PrintLayout load();
    void save() const;
};

namespace printing {
// Where an image of `imagePx` goes inside the printable area `page` (device
// pixels at `deviceDpi`). The result may be larger than the page (Actual, Scale).
QRectF targetRect(const QSizeF &imagePx, const QRectF &page, double deviceDpi, const PrintLayout &layout);
// The resolution an image says it has, or 0 when it does not (96 dpi = not set).
double imageDpi(const QImage &image);
// Apply orientation and margins of `layout` to the printer for `imagePx`.
void setupPage(QPrinter *printer, const QSize &imagePx, const PrintLayout &layout);
// Paint one page.
void paint(QPrinter *printer, const QImage &image, const PrintLayout &layout);
} // namespace printing

class PrintDialog : public QDialog
{
    Q_OBJECT
public:
    // `withAnnotations` is the composite, `base` the image alone.
    PrintDialog(const QImage &withAnnotations, const QImage &base, QPrinter *printer, QWidget *parent = nullptr);

    PrintLayout printLayout() const;
    void setPrintLayout(const PrintLayout &layout);
    QPrintPreviewWidget *preview() const { return m_preview; }
    // Print with the current settings, without the system dialog (tests, PDF).
    void printNow();
    QImage printedImage() const;

private:
    void changed();
    void choosePrinter();
    void pageSetup();

    QImage m_full, m_base;
    QPrinter *m_printer;
    QPrintPreviewWidget *m_preview;
    QRadioButton *m_fit, *m_actual, *m_scale;
    QDoubleSpinBox *m_dpi, *m_margin;
    QSpinBox *m_percent;
    QCheckBox *m_center, *m_annotations;
    QComboBox *m_orientation;
    bool m_updating = false;
};
