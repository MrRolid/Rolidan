#pragma once

#include "imageio.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QThreadPool;
class QTimer;

// Format options shown on "Save As" (spec F5). Remembers the choice in settings.
class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    ExportDialog(const QImage &composite, const imageio::SaveOptions &options, QWidget *parent = nullptr);
    ~ExportDialog() override;

    imageio::SaveOptions options() const;
    // True if the dialog has something to configure for this format.
    static bool hasOptions(const QByteArray &format, const QImage &composite);

    // Exposed for tests.
    QComboBox *webpMode() const { return m_webpMode; }
    QSlider *qualitySlider() const { return m_quality; }
    QComboBox *subsampling() const { return m_subsampling; }
    QLabel *estimateLabel() const { return m_estimate; }

private:
    QSlider *addSlider(const QString &label, int min, int max, int value, const QString &tip = {});
    void scheduleEstimate();
    void updateEstimate();
    void estimateReady(int generation, const QByteArray &data, const QImage &preview);
    void updateVisibility();
    void updateBackgroundButton();

    QImage m_image;
    imageio::SaveOptions m_options;
    bool m_hasAlpha;
    QFormLayout *m_form = nullptr;

    // JPEG / WebP lossy
    QSlider *m_quality = nullptr;
    // JPEG
    QComboBox *m_subsampling = nullptr;
    QCheckBox *m_progressive = nullptr;
    QCheckBox *m_optimize = nullptr;
    QSpinBox *m_smoothing = nullptr;
    // WebP
    QComboBox *m_webpMode = nullptr;
    QComboBox *m_preset = nullptr;
    QSlider *m_method = nullptr;
    QSlider *m_alphaQuality = nullptr;
    QCheckBox *m_sharpYuv = nullptr;
    QSlider *m_losslessLevel = nullptr;
    QSpinBox *m_nearLossless = nullptr;
    QCheckBox *m_exact = nullptr;
    QList<QWidget *> m_lossyRows, m_losslessRows;

    QList<QCheckBox *> m_icoSizes;
    QPushButton *m_saveButton = nullptr;
    QSlider *m_compression = nullptr;
    QPushButton *m_background = nullptr;
    QLabel *m_estimate = nullptr;
    QLabel *m_preview = nullptr;
    QTimer *m_timer = nullptr;
    int m_generation = 0;
    QThreadPool *m_pool = nullptr; // estimates run here; waited for on destruction
};
