#pragma once

#include <QDialog>
#include <QImage>

// Image information (Etapa 5): file, image and EXIF data incl. GPS position.
class ImageInfoDialog : public QDialog
{
    Q_OBJECT
public:
    // `path` may be empty (pasted image); `current` is the document's image now.
    ImageInfoDialog(const QString &path, const QImage &current, QWidget *parent = nullptr);

    // All shown values as "label: value" lines (also the "Copy" button).
    QString text() const { return m_text; }
    // OpenStreetMap link of the GPS position, empty without GPS.
    QString mapUrl() const { return m_mapUrl; }

private:
    QString m_text;
    QString m_mapUrl;
};
