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
class ResizeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ResizeDialog(const QSize &current, QWidget *parent = nullptr);
    QSize newSize() const;

    // Exposed for tests.
    QSpinBox *widthSpin() const { return m_width; }
    QSpinBox *heightSpin() const { return m_height; }
    QDoubleSpinBox *percentSpin() const { return m_percent; }
    QCheckBox *keepAspect() const { return m_keep; }

private:
    void widthChanged(int w);
    void heightChanged(int h);
    void percentChanged(double p);
    void updateInfo();

    QSize m_current;
    QSpinBox *m_width;
    QSpinBox *m_height;
    QDoubleSpinBox *m_percent;
    QCheckBox *m_keep;
    QLabel *m_info;
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
