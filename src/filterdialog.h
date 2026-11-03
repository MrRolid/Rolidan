#pragma once

#include "filters.h"

#include <QDialog>

class Canvas;
class Document;
class QCheckBox;
class QFormLayout;
class QLabel;
class QThreadPool;
class QTimer;

// Generic dialog for an adjustment / filter (filters::Spec): controls built
// from the parameter list, live preview on the canvas (computed in the
// background), one undo step on OK. Values are remembered in settings.
class FilterDialog : public QDialog
{
    Q_OBJECT
public:
    // `overrides` replace remembered values (e.g. "color" = 1 for color noise).
    FilterDialog(const filters::Spec &spec, Canvas *canvas, const filters::Values &overrides = {},
                 QWidget *parent = nullptr);
    ~FilterDialog() override;

    filters::Values values() const { return m_values; }
    void setValue(const QString &key, double value);
    // The result for the current values (computes it if the preview is not ready).
    DocState result();
    bool previewReady() const { return m_readyGeneration == m_generation; }

    void accept() override;
    void reject() override;

    // Applies a filter without parameters at once (one undo step).
    static void applyDirect(const filters::Spec &spec, Canvas *canvas);

signals:
    void valueChanged(const QString &key, double value); // keeps the widgets in sync

private:
    void changed();
    void compute();
    void showPreview();
    void saveValues() const;

    const filters::Spec &m_spec;
    Canvas *m_canvas;
    Document *m_doc;
    DocState m_original;
    QRect m_selection;
    filters::Values m_values;
    DocState m_result;
    int m_generation = 0;
    int m_readyGeneration = -1;
    QCheckBox *m_previewBox = nullptr;
    QLabel *m_busy = nullptr;
    QTimer *m_timer = nullptr;
    QThreadPool *m_pool = nullptr;
    bool m_hadGrid = false;
};
