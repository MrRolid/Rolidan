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
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    bool event(QEvent *) override;

private:
    void createActions();
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
    void browse(int delta);
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
        QAction *batch;
        QAction *straightenDocument, *nextImage, *prevImage, *firstImage, *lastImage;
        QAction *about;
    } a{};
    QActionGroup *m_toolGroup = nullptr;
    QList<QAction *> m_filterActions;
};
