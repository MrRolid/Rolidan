#pragma once

#include <QColor>
#include <QDialog>
#include <QPoint>
#include <QSize>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

// Image > Resize Image (spec C4: resampling; objects scale with the image).
// Modeled on IrfanView's Resize/Resample: size in px, cm or inches, percent of
// width and height, presets, resolution, resampling filter, sharpening.
class ResizeDialog : public QDialog
{
    Q_OBJECT
public:
    // `dpi`: resolution stored in the image, 0 = not set; `screen`: for the presets.
    explicit ResizeDialog(const QSize &current, double dpi = 0, const QSize &screen = {}, QWidget *parent = nullptr);
    QSize newSize() const;
    int filter() const;       // filters::Resample
    bool sharpen() const;
    // New resolution, 0 when unchanged.
    double newDpi() const;
    void accept() override;   // remembers the choices

    enum Unit { Pixels = 0, Centimeters, Inches };
    enum Preset { Half = 1, Double, FitScreen = 100, ScreenSize };
    void applyPreset(int indexInCombo);

    // Exposed for tests.
    QDoubleSpinBox *widthSpin() const { return m_width; }
    QDoubleSpinBox *heightSpin() const { return m_height; }
    QDoubleSpinBox *widthPercent() const { return m_widthPct; }
    QDoubleSpinBox *heightPercent() const { return m_heightPct; }
    QCheckBox *keepAspect() const { return m_keep; }
    QComboBox *unitCombo() const { return m_unit; }
    QComboBox *presetCombo() const { return m_preset; }
    QComboBox *filterCombo() const { return m_filter; }
    QSpinBox *dpiSpin() const { return m_dpi; }

private:
    void setPixels(double w, double h, QObject *source);
    double toPixels(double v) const;
    double fromPixels(double px) const;
    void unitChanged();
    void updateInfo();

    QSize m_current;
    double m_origDpi;
    QSize m_screen;
    double m_w, m_h; // new size in pixels
    QComboBox *m_preset;
    QDoubleSpinBox *m_width, *m_height, *m_widthPct, *m_heightPct;
    QComboBox *m_unit;
    QCheckBox *m_keep;
    QSpinBox *m_dpi;
    QComboBox *m_filter;
    QCheckBox *m_sharpen;
    QLabel *m_original, *m_info;
    bool m_updating = false;
};

// Image > Canvas Size (spec C4: anchor, fill with color or transparency).
class CanvasSizeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CanvasSizeDialog(const QSize &current, QWidget *parent = nullptr);
    QSize newSize() const;
    // Position of the old image in the new canvas.
    QPoint offset() const;
    QColor fill() const;

    // anchor index 0..8, row by row (0 = top left, 4 = center)
    void setAnchor(int index);
    QSpinBox *widthSpin() const { return m_width; }
    QSpinBox *heightSpin() const { return m_height; }

private:
    QSize m_current;
    QSpinBox *m_width;
    QSpinBox *m_height;
    QButtonGroup *m_anchor;
    QComboBox *m_fill;
    QColor m_custom = Qt::white;
};
