// Viewer functions of the main window (Etapa 5): folder browsing, recent
// files, full screen, slideshow, image information and file operations.
#include "mainwindow.h"

#include "application.h"
#include "canvas.h"
#include "imageinfo.h"
#include "settings.h"
#include "toast.h"
#include "tools.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

QString MainWindow::currentFile() const
{
    for (const QString &p : {m_doc->filePath(), m_doc->originPath()})
        if (!p.isEmpty() && QFileInfo(p).isFile())
            return QFileInfo(p).absoluteFilePath();
    return {};
}

// ---------------------------------------------------------------- browsing

bool MainWindow::browse(int delta, bool wrap)
{
    if (m_doc->isEmpty() || m_canvas->tool()->isBusy())
        return false;
    const QString current = !m_doc->originPath().isEmpty() ? m_doc->originPath() : m_doc->filePath();
    if (current.isEmpty())
        return false;
    const QStringList files = folderImages(QFileInfo(current).absolutePath());
    if (files.isEmpty())
        return false;
    const int n = int(files.size());
    int index = int(files.indexOf(QFileInfo(current).absoluteFilePath()));
    int target;
    if (index < 0) { // the current file was renamed or deleted
        index = 0;
        target = 0;
    } else if (wrap && std::abs(delta) < 1000000) {
        target = ((index + delta) % n + n) % n;
    } else {
        target = std::clamp(index + delta, 0, n - 1);
    }
    if (target == index && files.value(index) == QFileInfo(current).absoluteFilePath()) {
        if (!isSlideshowRunning())
            m_canvas->toast()->showMessage(delta > 0 ? tr("This is the last image in the folder")
                                                     : tr("This is the first image in the folder"));
        return false;
    }
    // Browsing replaces the shown image (a viewer, unlike opening): unsaved
    // changes are offered for saving first.
    if (!maybeSave())
        return false;
    // First / last: if unreadable, continue inwards; otherwise in the direction of travel.
    int step = delta > 0 ? 1 : -1;
    if (std::abs(delta) >= 1000000)
        step = -step;
    for (int k = 0, i = target; k < n; ++k, i += step) {
        if (i < 0 || i >= n) {
            if (!wrap)
                break;
            i = (i + n) % n;
        }
        if (i == index && k > 0)
            break;
        if (openFile(files[i])) {
            m_canvas->toast()->showMessage(
                QStringLiteral("%1 / %2 · %3").arg(i + 1).arg(n).arg(QFileInfo(files[i]).fileName()));
            return true;
        }
    }
    return false;
}

bool MainWindow::showFolderImage(const QString &path)
{
    if (m_canvas->tool()->isBusy() || !maybeSave())
        return false;
    if (!openFile(path))
        return false;
    const QStringList files = folderImages(QFileInfo(path).absolutePath());
    m_canvas->toast()->showMessage(QStringLiteral("%1 / %2 · %3")
                                       .arg(files.indexOf(QFileInfo(path).absoluteFilePath()) + 1)
                                       .arg(files.size())
                                       .arg(QFileInfo(path).fileName()));
    return true;
}

// ---------------------------------------------------------------- recent files

void MainWindow::addRecent(const QString &path)
{
    if (!path.isEmpty())
        settings::addRecentFile(path);
}

void MainWindow::populateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList files = settings::recentFiles();
    int i = 0;
    for (const QString &f : files) {
        ++i;
        const QFileInfo fi(f);
        // &1 … &9, &0 for the tenth: quick keyboard access
        const QString text = QStringLiteral("&%1  %2").arg(i % 10).arg(fi.fileName().replace(QLatin1Char('&'), QStringLiteral("&&")));
        QAction *act = m_recentMenu->addAction(text, this, [this, f] { openRecent(f); });
        act->setToolTip(QDir::toNativeSeparators(f));
        act->setStatusTip(QDir::toNativeSeparators(f));
    }
    if (files.isEmpty())
        m_recentMenu->addAction(tr("No recent files"))->setEnabled(false);
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(a.clearRecent);
    a.clearRecent->setEnabled(!files.isEmpty());
}

void MainWindow::openRecent(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        settings::removeRecentFile(path);
        QMessageBox::warning(this, tr("Open Recent"), tr("The file no longer exists:\n%1").arg(path));
        return;
    }
    // Decision J1: a non-empty document is never replaced.
    if (m_doc->isEmpty())
        openFile(path);
    else
        Application::instance()->openInNewWindow(path);
}

// ---------------------------------------------------------------- full screen

void MainWindow::setFullScreenMode(bool on)
{
    if (on == m_fullScreen)
        return;
    m_fullScreen = on;
    a.fullScreen->setChecked(on);
    if (on) {
        m_wasMaximized = isMaximized();
        m_hiddenForFullScreen.clear();
        QList<QWidget *> bars{menuBar(), statusBar()};
        for (QToolBar *t : findChildren<QToolBar *>(Qt::FindDirectChildrenOnly))
            bars << t;
        for (QDockWidget *d : findChildren<QDockWidget *>(Qt::FindDirectChildrenOnly))
            bars << d;
        for (QWidget *w : bars) {
            if (w->isVisible()) {
                m_hiddenForFullScreen << w;
                w->hide();
            }
        }
        m_canvas->setDarkSurroundings(true);
        m_canvas->setEnlargeSmallImages(settings::fullScreenEnlarge());
        m_canvas->setAutoHideCursor(true);
        showFullScreen();
        m_canvas->fitToWindow(false); // and again when the window has its full size
        m_canvas->toast()->showMessage(tr("Full screen · F11 or Esc to leave"));
    } else {
        for (QWidget *w : std::as_const(m_hiddenForFullScreen))
            w->show();
        m_hiddenForFullScreen.clear();
        m_canvas->setDarkSurroundings(false);
        m_canvas->setEnlargeSmallImages(false);
        m_canvas->setAutoHideCursor(false);
        if (m_wasMaximized)
            showMaximized();
        else
            showNormal();
        m_canvas->fitToWindow(false);
    }
    m_canvas->setFocus();
}

// ---------------------------------------------------------------- slideshow

void MainWindow::setEnlargeSmallImages(bool on)
{
    settings::setFullScreenEnlarge(on);
    a.enlargeSmall->setChecked(on);
    if (m_fullScreen) {
        m_canvas->setEnlargeSmallImages(on);
        m_canvas->fitToWindow(false);
    }
    m_canvas->toast()->showMessage(on ? tr("Full screen: small images are enlarged to fit the screen")
                                      : tr("Full screen: small images are shown at actual size"));
}

void MainWindow::startSlideshow()
{
    if (isSlideshowRunning())
        return;
    const QString current = !m_doc->originPath().isEmpty() ? m_doc->originPath() : m_doc->filePath();
    if (m_doc->isEmpty() || current.isEmpty() || folderImages(QFileInfo(current).absolutePath()).size() < 2) {
        m_canvas->toast()->showMessage(tr("Open an image from a folder with more images to start a slideshow"));
        return;
    }
    if (!maybeSave())
        return;
    m_slideshowStartedFullScreen = !m_fullScreen;
    setFullScreenMode(true);
    m_slideshowPaused = false;
    m_slideshowTimer = new QTimer(this);
    m_slideshowTimer->setSingleShot(true);
    m_slideshowTimer->setInterval(int(settings::slideshowSeconds() * 1000));
    connect(m_slideshowTimer, &QTimer::timeout, this, &MainWindow::slideshowStep);
    m_slideshowTimer->start();
    m_canvas->installEventFilter(this);
    a.slideshow->setChecked(true);
    m_canvas->toast()->showMessage(tr("Slideshow · Space pauses, arrows browse, Esc ends"));
}

void MainWindow::stopSlideshow()
{
    if (!isSlideshowRunning())
        return;
    m_canvas->removeEventFilter(this);
    m_slideshowTimer->stop();
    m_slideshowTimer->deleteLater();
    m_slideshowTimer = nullptr;
    m_slideshowPaused = false;
    a.slideshow->setChecked(false);
    if (m_slideshowStartedFullScreen)
        setFullScreenMode(false);
}

void MainWindow::setSlideshowPaused(bool paused)
{
    if (!isSlideshowRunning() || paused == m_slideshowPaused)
        return;
    m_slideshowPaused = paused;
    if (paused) {
        m_slideshowTimer->stop();
        m_canvas->toast()->showMessage(tr("Slideshow paused · Space continues"));
    } else {
        m_slideshowTimer->start();
        m_canvas->toast()->showMessage(tr("Slideshow continues"));
    }
}

void MainWindow::slideshowStep()
{
    if (!isSlideshowRunning())
        return;
    if (!browse(1, settings::slideshowLoop())) {
        stopSlideshow();
        m_canvas->toast()->showMessage(tr("End of slideshow"));
        return;
    }
    if (m_slideshowTimer && !m_slideshowPaused)
        m_slideshowTimer->start();
}

bool MainWindow::eventFilter(QObject *o, QEvent *e)
{
    // While a slideshow runs, its keys win over tools and shortcuts.
    if (o == m_canvas && isSlideshowRunning()
        && (e->type() == QEvent::KeyPress || e->type() == QEvent::ShortcutOverride)) {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
            return QMainWindow::eventFilter(o, e);
        int step = 0;
        switch (k->key()) {
        case Qt::Key_Space: break;
        case Qt::Key_Escape: break;
        case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_PageDown: step = 1; break;
        case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_PageUp: step = -1; break;
        default: return QMainWindow::eventFilter(o, e);
        }
        if (e->type() == QEvent::ShortcutOverride) {
            e->accept(); // deliver as a key press instead of triggering a shortcut
            return true;
        }
        if (k->key() == Qt::Key_Escape) {
            stopSlideshow();
        } else if (k->key() == Qt::Key_Space) {
            if (!k->isAutoRepeat())
                setSlideshowPaused(!m_slideshowPaused);
        } else {
            browse(step, settings::slideshowLoop());
            if (!m_slideshowPaused)
                m_slideshowTimer->start(); // a full interval for the new image
        }
        return true;
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    // Esc that nothing else wanted (the canvas passes it on).
    if (e->key() == Qt::Key_Escape && e->modifiers() == Qt::NoModifier) {
        if (isSlideshowRunning()) {
            stopSlideshow();
            return;
        }
        if (m_fullScreen) {
            setFullScreenMode(false);
            return;
        }
    }
    // Full screen without slideshow: arrows that no tool used browse the folder.
    if (m_fullScreen && !isSlideshowRunning() && e->modifiers() == Qt::NoModifier) {
        switch (e->key()) {
        case Qt::Key_Right: case Qt::Key_Down: browse(1); return;
        case Qt::Key_Left: case Qt::Key_Up: browse(-1); return;
        default: break;
        }
    }
    // Enter that no tool used (nothing to confirm): full screen, like IrfanView.
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && e->modifiers() == Qt::NoModifier
        && !m_doc->isEmpty() && m_canvas->areaSelection().isEmpty() && !m_canvas->tool()->isBusy()
        && !isSlideshowRunning()) {
        setFullScreenMode(!m_fullScreen);
        return;
    }
    QMainWindow::keyPressEvent(e);
}

// ---------------------------------------------------------------- information

void MainWindow::showImageInfo()
{
    ImageInfoDialog dlg(currentFile(), m_doc->state().base, this);
    dlg.exec();
}

// ---------------------------------------------------------------- file operations

namespace {
QString uniqueTarget(const QString &dir, const QString &name)
{
    const QFileInfo fi(name);
    const QString base = fi.completeBaseName(), suffix = fi.suffix();
    QString candidate = QDir(dir).filePath(name);
    for (int i = 2; QFileInfo::exists(candidate); ++i)
        candidate = QDir(dir).filePath(suffix.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(i)
                                                        : QStringLiteral("%1 (%2).%3").arg(base).arg(i).arg(suffix));
    return candidate;
}
} // namespace

bool MainWindow::renameCurrentFile(const QString &newName)
{
    const QString from = currentFile();
    const QString name = newName.trimmed();
    if (from.isEmpty() || name.isEmpty() || name.contains(QLatin1Char('/')) || name == QLatin1String(".")
        || name == QLatin1String(".."))
        return false;
    const QString to = QFileInfo(from).dir().filePath(name);
    if (to == from)
        return true;
    if (QFileInfo::exists(to)) {
        QMessageBox::warning(this, tr("Rename"), tr("A file named “%1” already exists.").arg(name));
        return false;
    }
    if (!QFile::rename(from, to)) {
        QMessageBox::warning(this, tr("Rename"), tr("Could not rename the file."));
        return false;
    }
    m_doc->relocate(from, to);
    settings::removeRecentFile(from);
    addRecent(to);
    updateTitle();
    updateActions();
    return true;
}

void MainWindow::renameDialog()
{
    const QString from = currentFile();
    if (from.isEmpty())
        return;
    QInputDialog dlg(this);
    dlg.setWindowTitle(tr("Rename"));
    dlg.setLabelText(tr("New name:"));
    const QString name = QFileInfo(from).fileName();
    dlg.setTextValue(name);
    dlg.resize(420, dlg.sizeHint().height());
    // Select the name without the extension, like file managers do.
    QTimer::singleShot(0, &dlg, [&dlg, name] {
        if (auto *edit = dlg.findChild<QLineEdit *>()) {
            const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
            edit->setSelection(0, int(dot > 0 ? dot : name.size()));
        }
    });
    if (dlg.exec() == QDialog::Accepted)
        renameCurrentFile(dlg.textValue());
}

void MainWindow::showAfterRemoval(const QStringList &before, int index)
{
    QStringList left = before;
    if (index >= 0 && index < left.size())
        left.removeAt(index);
    // Continue with the next image (or the previous one at the end), like a viewer.
    for (int i = std::min(index, int(left.size()) - 1); i >= 0 && i < left.size(); ++i) {
        if (QFileInfo::exists(left[i]) && openFile(left[i]))
            return;
    }
    m_canvas->setTool(ToolId::Pointer);
    m_doc->reset(QImage());
    updateTitle();
    updateStatus();
    updateActions();
}

bool MainWindow::trashCurrentFile(bool ask)
{
    const QString path = currentFile();
    if (path.isEmpty() || m_canvas->tool()->isBusy())
        return false;
    const bool modified = m_doc->isModified();
    if (ask && (settings::confirmTrash() || modified)) {
        QMessageBox box(QMessageBox::Question, tr("Move to Trash"),
                        tr("Move “%1” to the trash?").arg(QFileInfo(path).fileName()), QMessageBox::NoButton, this);
        if (modified)
            box.setInformativeText(tr("Unsaved changes in this window will be lost."));
        QPushButton *yes = box.addButton(tr("Move to Trash"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(yes);
        auto *dontAsk = new QCheckBox(tr("Don’t ask again"), &box);
        box.setCheckBox(dontAsk);
        box.exec();
        if (box.clickedButton() != yes)
            return false;
        if (dontAsk->isChecked())
            settings::setConfirmTrash(false);
    }
    const QString dir = QFileInfo(path).absolutePath();
    const QStringList before = folderImages(dir);
    const int index = int(before.indexOf(path));
    if (!QFile::moveToTrash(path)) {
        QMessageBox::warning(this, tr("Move to Trash"),
                             tr("Could not move the file to the trash. It may be on a drive without a trash "
                                "folder, or you lack permission."));
        return false;
    }
    settings::removeRecentFile(path);
    m_doc->setSaved(m_doc->filePath(), m_doc->fileFormat()); // nothing left to save there
    showAfterRemoval(before, index);
    m_canvas->toast()->showMessage(tr("Moved to the trash: %1").arg(QFileInfo(path).fileName()));
    return true;
}

bool MainWindow::copyOrMoveCurrentFile(const QString &dir, bool move, bool ask)
{
    const QString path = currentFile();
    if (path.isEmpty() || dir.isEmpty() || !QFileInfo(dir).isDir())
        return false;
    const QString title = move ? tr("Move to Folder") : tr("Copy to Folder");
    if (QFileInfo(dir).absoluteFilePath() == QFileInfo(path).absolutePath()) {
        QMessageBox::information(this, title, tr("The file is already in this folder."));
        return false;
    }
    if (move && m_doc->isModified() && ask && !maybeSave())
        return false;
    const QString name = QFileInfo(path).fileName();
    QString target = QDir(dir).filePath(name);
    if (QFileInfo::exists(target)) {
        QMessageBox box(QMessageBox::Question, title, tr("“%1” already exists in the target folder.").arg(name),
                        QMessageBox::NoButton, this);
        QPushButton *both = box.addButton(tr("Keep Both"), QMessageBox::AcceptRole);
        QPushButton *replace = box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(both);
        if (ask)
            box.exec();
        if (ask && box.clickedButton() == replace) {
            if (!QFile::remove(target)) {
                QMessageBox::warning(this, title, tr("Could not replace the existing file."));
                return false;
            }
        } else if (!ask || box.clickedButton() == both) {
            target = uniqueTarget(dir, name);
        } else {
            return false;
        }
    }
    settings::setCopyMoveDirectory(QFileInfo(dir).absoluteFilePath());
    if (!move) {
        if (!QFile::copy(path, target)) {
            QMessageBox::warning(this, title, tr("Could not copy the file."));
            return false;
        }
        m_canvas->toast()->showMessage(tr("Copied to %1").arg(QDir::toNativeSeparators(target)));
        return true;
    }
    const QStringList before = folderImages(QFileInfo(path).absolutePath());
    const int index = int(before.indexOf(path));
    if (!QFile::rename(path, target)) { // also works across drives (copy + remove)
        QMessageBox::warning(this, title, tr("Could not move the file."));
        return false;
    }
    settings::removeRecentFile(path);
    m_doc->relocate(path, target);
    showAfterRemoval(before, index);
    m_canvas->toast()->showMessage(tr("Moved to %1").arg(QDir::toNativeSeparators(target)));
    return true;
}

void MainWindow::copyOrMoveDialog(bool move)
{
    const QString path = currentFile();
    if (path.isEmpty())
        return;
    QString start = settings::copyMoveDirectory();
    if (start.isEmpty() || !QFileInfo(start).isDir())
        start = QFileInfo(path).absolutePath();
    const QString dir = QFileDialog::getExistingDirectory(
        this, move ? tr("Move “%1” to Folder").arg(QFileInfo(path).fileName())
                   : tr("Copy “%1” to Folder").arg(QFileInfo(path).fileName()),
        start);
    if (!dir.isEmpty())
        copyOrMoveCurrentFile(dir, move);
}

void MainWindow::showInFolder()
{
    const QString path = currentFile();
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}
