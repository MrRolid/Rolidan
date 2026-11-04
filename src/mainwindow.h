#pragma once

#include "clipboard.h"
#include "document.h"
#include "imageio.h"

#include <QMainWindow>
#include <atomic>
#include <memory>

class Canvas;
class QAction;
class QActionGroup;
class QLabel;
class QToolBar;
class NotesPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    Document *document() const { return m_doc; }
    Canvas *canvas() const { return m_canvas; }
    NotesPanel *notesPanel() const { return m_notes; }

    bool openFile(const QString &path);          // into this window
    void openImage(const QImage &img, const QString &originPath, const QStringList &notes);
    void paste();                                // Ctrl+V (spec B1/B2)
    void insertContent(const clip::Content &c);  // shared by paste and drop
    void pasteWhenActive();                      // for --paste (spec B4: needs focus)

    // Copy / cut according to spec B3. `all` == with Shift.
    void copy(bool all);
    void cut(bool all);
    void copyResult();                           // Ctrl+Alt+C
    void deleteSelection();

    bool save();
    bool saveAs();
    bool saveProjectAs();
    bool saveProjectTo(const QString &path);
    // Crash recovery (spec F7).
    void autosaveNow(bool synchronous = false);
    void discardRecovery();
    bool restoreRecovery(const QString &path);
    QString recoveryPath() const { return m_recoveryPath; }

    // Viewer (Etapa 5, viewer.cpp)
    // The file the document shows on disk (saved file, else the opened one), empty if none.
    QString currentFile() const;
    // Next / previous image in the folder; `wrap` continues at the other end.
    bool browse(int delta, bool wrap = false);
    void setFullScreenMode(bool on);
    void setEnlargeSmallImages(bool on); // full screen option, remembered
    bool isFullScreenMode() const { return m_fullScreen; }
    void startSlideshow();
    void stopSlideshow();
    bool isSlideshowRunning() const { return m_slideshowTimer != nullptr; }
    bool isSlideshowPaused() const { return m_slideshowPaused; }
    void setSlideshowPaused(bool paused);
    void showImageInfo();
    bool renameCurrentFile(const QString &newName); // without a dialog (tests); false on failure
    void renameDialog();
    bool trashCurrentFile(bool ask = true);
    bool copyOrMoveCurrentFile(const QString &dir, bool move, bool ask = true);
    void copyOrMoveDialog(bool move);
    void showInFolder();
    void openRecent(const QString &path);
    // Show another image of the folder in this window (thumbnails), saving first if needed.
    bool showFolderImage(const QString &path);
    class ThumbnailPanel *thumbnails() const { return m_thumbs; }
    // Actions whose shortcuts the user can change (they have an objectName id).
    QList<QAction *> customizableActions() const;
    void showShortcuts();
    void print(); // Ctrl+P (Etapa 6)
    void captureScreen(); // C (Etapa 7)
    // Show a screenshot like Ctrl+V does (new document or a new object);
    // `chooseArea` starts the crop tool on a new document.
    void showCapture(const QImage &img, bool chooseArea);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    bool event(QEvent *) override;

private:
    void createActions();
    QWidget *createCaptureControls(QWidget *parent);
    void createMenusAndToolbars();
    void updateActions();
    void updateTitle();
    void updateStatus();
    void updateToolOptions();
    void showNotes(const QStringList &notes);
    void transformDocument(DocState (*op)(const DocState &), const QString &text);
    bool writeTo(const QString &path, const imageio::SaveOptions &options);
    bool maybeSave();
    void setClipboard(const QImage &img, const ObjectList &objects = {});
    void openDialog();
    void newFromClipboard();
    void cropToSelection();
    void showSettings();
    void showAbout();
    void arrange(int order);
    void duplicateSelection();
    void mergeIntoImage(bool all);
    void refreshPanelValues();
    void resizeImageDialog();
    void canvasSizeDialog();
    void areaEffect(int kind); // 0 blur, 1 pixelate, 2 cover
    QWidget *createSelectOptions(QWidget *parent);
    void onHistoryChanged();
    void runFilter(const QString &id, const QHash<QString, double> &overrides = {});
    QStringList folderImages(const QString &dir) const;
    void populateRecentMenu();
    void addRecent(const QString &path);
    void showAfterRemoval(const QStringList &before, int index); // next image after trash / move
    void slideshowStep();
    void updateShortcutTips();
    bool needsRecovery() const;

    int m_serial = 0;
    QString m_recoveryPath;
    class QTimer *m_autosaveTimer = nullptr;
    bool m_autosaveDirty = false;
    std::shared_ptr<std::atomic<bool>> m_autosaveCancel;
    bool m_coverWarned = false;

    Document *m_doc;
    Canvas *m_canvas;
    QToolBar *m_optionsBar = nullptr;
    QWidget *m_optionsWidget = nullptr;
    class PropertyPanel *m_panel = nullptr;
    class NotesPanel *m_notes = nullptr;
    QLabel *m_statusSize = nullptr;
    QLabel *m_statusZoom = nullptr;
    QLabel *m_statusCursor = nullptr;
    QLabel *m_statusState = nullptr;
    bool m_pasteOnActivate = false;

    // Viewer state
    class QMenu *m_recentMenu = nullptr;
    class ThumbnailPanel *m_thumbs = nullptr;
    bool m_fullScreen = false;
    bool m_wasMaximized = false;
    QList<QWidget *> m_hiddenForFullScreen;
    class QTimer *m_slideshowTimer = nullptr;
    bool m_slideshowPaused = false;
    bool m_slideshowStartedFullScreen = false;

    struct Actions
    {
        QAction *newFromClipboard, *open, *save, *saveAs, *saveProject, *settings, *close, *quit;
        QAction *undo, *redo, *cut, *cutAll, *copy, *copyAll, *copyResult, *paste, *del, *selectAll;
        QAction *cropToSelection, *rotateLeft, *rotateRight, *rotate180, *flipH, *flipV;
        QAction *fit, *actual, *zoomIn, *zoomOut;
        QAction *toolPointer, *toolSelect, *toolCrop;
        QAction *forward, *backward, *toFront, *toBack, *duplicate, *mergeSelected, *mergeAll;
        QAction *resizeImage, *canvasSize, *blurArea, *pixelateArea, *coverArea, *hideObjects;
        QAction *notes;
        QAction *batch, *print, *capture;
        QAction *straightenDocument, *nextImage, *prevImage, *firstImage, *lastImage;
        QAction *fullScreen, *slideshow, *info, *rename, *trash, *copyTo, *moveTo, *showInFolder, *clearRecent, *shortcuts, *enlargeSmall, *thumbnails;
        QAction *about;
    } a{};
    QActionGroup *m_toolGroup = nullptr;
    QList<QAction *> m_filterActions;
    bool m_captureBusy = false;
    std::unique_ptr<class QPrinter> m_printer; // keeps the chosen printer and paper
};
