#include "thumbnails.h"

#include "document.h"
#include "project.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QImageReader>
#include <QListView>
#include <QPainter>
#include <QPointer>
#include <QStyle>
#include <QThreadPool>
#include <QTimer>

// ---------------------------------------------------------------- model

ThumbnailModel::ThumbnailModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_pool(new QThreadPool(this))
{
    m_pool->setMaxThreadCount(std::max(1, std::min(3, QThread::idealThreadCount() - 1)));
}

ThumbnailModel::~ThumbnailModel()
{
    m_pool->clear();
    m_pool->waitForDone();
}

QImage ThumbnailModel::makeThumbnail(const QString &path, int size)
{
    QImage img;
    if (project::isProjectFile(path)) {
        DocState s;
        QString error;
        if (project::load(path, &s, &error))
            img = docops::composite(s);
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize full = reader.size();
        if (full.isValid() && (full.width() > size * 2 || full.height() > size * 2)) {
            // Let the decoder scale (JPEG decodes at 1/2, 1/4, 1/8 directly).
            reader.setScaledSize(full.scaled(size * 2, size * 2, Qt::KeepAspectRatio));
        }
        img = reader.read();
    }
    if (img.isNull())
        return {};
    if (img.width() > size || img.height() > size)
        img = img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void ThumbnailModel::setFiles(const QStringList &files)
{
    if (files == m_files)
        return;
    beginResetModel();
    m_pool->clear();
    ++m_generation;
    m_files = files;
    // Keep what is still in the list (re-sorting, renaming another file).
    QHash<QString, QImage> keep;
    for (const QString &f : files)
        if (m_images.contains(f))
            keep.insert(f, m_images.value(f));
    m_images = keep;
    m_pending.clear();
    m_failed.clear();
    endResetModel();
}

void ThumbnailModel::setThumbnailSize(int px)
{
    if (px == m_size)
        return;
    beginResetModel();
    m_pool->clear();
    ++m_generation;
    m_size = px;
    m_images.clear();
    m_pending.clear();
    m_failed.clear();
    endResetModel();
}

int ThumbnailModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_files.size());
}

void ThumbnailModel::request(const QString &path) const
{
    if (m_pending.contains(path) || m_failed.contains(path))
        return;
    m_pending.insert(path);
    QPointer<ThumbnailModel> self(const_cast<ThumbnailModel *>(this));
    const int generation = m_generation, size = m_size;
    m_pool->start([self, path, generation, size] {
        const QImage img = makeThumbnail(path, size);
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, path, generation, img] {
                if (!self || generation != self->m_generation)
                    return;
                self->m_pending.remove(path);
                if (img.isNull())
                    self->m_failed.insert(path);
                else
                    self->m_images.insert(path, img);
                const int row = int(self->m_files.indexOf(path));
                if (row >= 0)
                    emit self->dataChanged(self->index(row), self->index(row), {Qt::DecorationRole});
            },
            Qt::QueuedConnection);
    });
}

QVariant ThumbnailModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_files.size())
        return {};
    const QString &path = m_files[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        return QFileInfo(path).fileName();
    case Qt::ToolTipRole:
        return QDir::toNativeSeparators(path);
    case Qt::UserRole:
        return path;
    case Qt::DecorationRole: {
        // A fixed-size square keeps the grid steady while thumbnails arrive.
        QImage canvas(m_size, m_size, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        QPainter p(&canvas);
        const auto it = m_images.constFind(path);
        if (it != m_images.constEnd()) {
            const QImage &t = *it;
            p.drawImage(QPoint((m_size - t.width()) / 2, (m_size - t.height()) / 2), t);
        } else {
            if (!m_failed.contains(path))
                request(path);
            QColor c = QApplication::palette().color(QPalette::Mid);
            c.setAlpha(70);
            p.fillRect(QRect(m_size / 8, m_size / 8, m_size * 3 / 4, m_size * 3 / 4), c);
        }
        p.end();
        return QPixmap::fromImage(canvas);
    }
    default:
        return {};
    }
}

// ---------------------------------------------------------------- panel

ThumbnailPanel::ThumbnailPanel(std::function<QStringList(const QString &)> lister, QWidget *parent)
    : QDockWidget(tr("Thumbnails"), parent)
    , m_lister(std::move(lister))
    , m_model(new ThumbnailModel(this))
    , m_view(new QListView(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_reload(new QTimer(this))
{
    setObjectName(QStringLiteral("thumbnailsDock"));
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea | Qt::LeftDockWidgetArea
                    | Qt::RightDockWidgetArea);
    m_view->setModel(m_model);
    m_view->setViewMode(QListView::IconMode);
    m_view->setFlow(QListView::LeftToRight);
    m_view->setWrapping(false);
    m_view->setMovement(QListView::Static);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setUniformItemSizes(true);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setIconSize(QSize(m_model->thumbnailSize(), m_model->thumbnailSize()));
    m_view->setGridSize(QSize(m_model->thumbnailSize() + 28, m_model->thumbnailSize() + 34));
    m_view->setTextElideMode(Qt::ElideMiddle);
    m_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setFocusPolicy(Qt::NoFocus); // keys stay with the canvas
    // One row: a filmstrip that leaves the space to the image.
    m_view->setFixedHeight(m_model->thumbnailSize() + 34 + m_view->style()->pixelMetric(QStyle::PM_ScrollBarExtent)
                           + 2 * m_view->frameWidth() + 4);
    setWidget(m_view);
    connect(m_view, &QListView::clicked, this, [this](const QModelIndex &i) {
        const QString path = i.data(Qt::UserRole).toString();
        if (!path.isEmpty() && path != m_current)
            emit fileActivated(path);
        else
            selectCurrent();
    });
    m_reload->setSingleShot(true);
    m_reload->setInterval(400);
    connect(m_reload, &QTimer::timeout, this, &ThumbnailPanel::refresh);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_reload, qOverload<>(&QTimer::start));
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible)
            refresh(); // nothing is loaded while hidden
    });
}

void ThumbnailPanel::setCurrentFile(const QString &currentFile)
{
    m_current = currentFile.isEmpty() ? QString() : QFileInfo(currentFile).absoluteFilePath();
    const QString dir = m_current.isEmpty() ? QString() : QFileInfo(m_current).absolutePath();
    if (dir != m_dir) {
        if (!m_watcher->directories().isEmpty())
            m_watcher->removePaths(m_watcher->directories());
        m_dir = dir;
        if (!m_dir.isEmpty())
            m_watcher->addPath(m_dir);
        refresh();
    } else {
        selectCurrent();
    }
}

void ThumbnailPanel::refresh()
{
    if (!isVisible()) {
        if (m_dir.isEmpty())
            m_model->setFiles({});
        return;
    }
    m_model->setFiles(m_dir.isEmpty() ? QStringList() : m_lister(m_dir));
    selectCurrent();
}

void ThumbnailPanel::selectCurrent()
{
    const int row = int(m_model->files().indexOf(m_current));
    if (row < 0) {
        m_view->clearSelection();
        return;
    }
    const QModelIndex i = m_model->index(row);
    m_view->setCurrentIndex(i);
    m_view->scrollTo(i, QAbstractItemView::PositionAtCenter);
}
