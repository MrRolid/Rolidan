#pragma once

#include <QAbstractListModel>
#include <QDockWidget>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <functional>

class QFileSystemWatcher;
class QListView;
class QThreadPool;
class QTimer;

// Thumbnails of the images in a folder, loaded in the background (Etapa 5).
class ThumbnailModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit ThumbnailModel(QObject *parent = nullptr);
    ~ThumbnailModel() override;

    void setFiles(const QStringList &files);
    QStringList files() const { return m_files; }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    // Number of thumbnails decoded so far (tests).
    int loadedCount() const { return int(m_images.size()); }
    void setThumbnailSize(int px);
    int thumbnailSize() const { return m_size; }

    // Decode a file into a thumbnail at most `size` px (also .rolidan projects).
    static QImage makeThumbnail(const QString &path, int size);

private:
    void request(const QString &path) const;

    QStringList m_files;
    int m_size = 96;
    mutable QHash<QString, QImage> m_images;
    mutable QSet<QString> m_pending;
    QSet<QString> m_failed;
    QThreadPool *m_pool;
    int m_generation = 0;
};

class ThumbnailPanel : public QDockWidget
{
    Q_OBJECT
public:
    // `lister` returns the images of a folder in browsing order.
    ThumbnailPanel(std::function<QStringList(const QString &)> lister, QWidget *parent = nullptr);

    // Show the folder of `currentFile` and highlight it (empty: clear).
    void setCurrentFile(const QString &currentFile);
    void refresh(); // re-read the folder (after rename, move, trash)
    QListView *view() const { return m_view; }
    ThumbnailModel *model() const { return m_model; }

signals:
    void fileActivated(const QString &path);

private:
    void selectCurrent();

    std::function<QStringList(const QString &)> m_lister;
    ThumbnailModel *m_model;
    QListView *m_view;
    QString m_dir;
    QString m_current;
    QFileSystemWatcher *m_watcher;
    QTimer *m_reload;
};
