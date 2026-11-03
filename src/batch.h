#pragma once

#include "filters.h"
#include "imageio.h"

#include <QDialog>
#include <QSize>
#include <QStringList>
#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;

// Batch conversion (Etapa 4): many images → one format, optional shrinking
// and one adjustment, written next to each other into an output folder.
namespace batch {

enum class Adjust { None, EnhanceColors, Grayscale, CleanDocument, StraightenDocument };

struct Options
{
    QString outputDir;
    imageio::SaveOptions save;     // format + codec options
    bool shrink = false;
    QSize maxSize{1920, 1920};     // fit inside, never enlarged
    Adjust adjust = Adjust::None;
    filters::Values adjustValues;  // parameters of the adjustment (defaults if empty)
    QString suffix;                // appended to the base name, e.g. "-web"
};

// Converts one file. Never overwrites: an existing target gets " (2)", " (3)"…
// Returns false and sets *error on failure.
bool processFile(const QString &input, const Options &o, QString *outputPath, QString *error);

} // namespace batch

class BatchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BatchDialog(QWidget *parent = nullptr);
    ~BatchDialog() override;

    void addFiles(const QStringList &paths);
    QStringList files() const;
    batch::Options options() const;
    void setOutputDir(const QString &dir);
    // Starts the conversion (also used by tests); finished() is emitted at the end.
    void start();
    bool isRunning() const { return m_running; }

signals:
    void finished(int converted, int failed);

private:
    void updateState();
    void chooseOptions();

    QListWidget *m_list;
    QLineEdit *m_output;
    QComboBox *m_format;
    QPushButton *m_formatOptions;
    QCheckBox *m_shrink;
    QSpinBox *m_maxW, *m_maxH;
    QComboBox *m_adjust;
    QLineEdit *m_suffix;
    QProgressBar *m_progress;
    QLabel *m_status;
    QPushButton *m_startButton;
    QPushButton *m_closeButton;
    imageio::SaveOptions m_saveOptions;
    bool m_running = false;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
