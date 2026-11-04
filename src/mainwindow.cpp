#include "mainwindow.h"

#include "application.h"
#include "canvas.h"
#include "exportdialog.h"
#include "imageio.h"
#include "propertypanel.h"
#include "project.h"
#include "recovery.h"
#include "filterdialog.h"
#include "batch.h"
#include "printing.h"
#include "capture.h"
#include "filters.h"
#include <QPrinter>
#include <QScreen>
#include <QRegularExpression>
#include "notespanel.h"
#include "icons.h"
#include "retouch.h"
#include "sizedialogs.h"
#include "tooldefaults.h"
#include "tools.h"
#include "settings.h"
#include "settingsdialog.h"
#include "shortcuts.h"
#include "thumbnails.h"
#include "toast.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPrinter>
#include <QRegularExpression>
#include <QMimeData>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QThreadPool>
#include <QUrl>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QGridLayout>
#include <QToolButton>
#include <functional>

namespace {
constexpr int kCascade = 16;

QIcon themeIcon(const char *name)
{
    return icons::get(QString::fromLatin1(name));
}

// Toolbar label from a menu text: drops "&", CJK-style "(&X)" accelerators
// and the trailing ellipsis ("開く(&O)…" -> "開く", "&Open…" -> "Open").
QString toolbarText(QString text)
{
    static const QRegularExpression cjkAccel(QStringLiteral("\\(&[^)]\\)"));
    text.remove(cjkAccel);
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QStringLiteral("…")))
        text.chop(1);
    else if (text.endsWith(QStringLiteral("...")))
        text.chop(3);
    return text.trimmed();
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_doc(new Document(this))
    , m_canvas(new Canvas(m_doc, this))
{
    setCentralWidget(m_canvas);
    setAcceptDrops(true);
    resize(1100, 760);
    m_doc->history().setLimitBytes(qint64(settings::historyLimitMB()) * 1024 * 1024);

    static int serial = 0;
    m_serial = ++serial;
    m_recoveryPath = recovery::pathFor(m_serial);
    m_autosaveTimer = new QTimer(this);
    // Save a few seconds after the first change, then at most that often
    // while editing continues (spec F7).
    m_autosaveTimer->setSingleShot(true);
    m_autosaveTimer->setInterval(5 * 1000);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] { autosaveNow(); });
    connect(m_doc, &Document::historyChanged, this, &MainWindow::onHistoryChanged);

    createActions();
    m_notes = new NotesPanel(m_doc, this);
    addDockWidget(Qt::RightDockWidgetArea, m_notes);
    m_notes->hide();
    a.notes = m_notes->toggleViewAction();
    a.notes->setText(tr("&Notes"));
    a.notes->setIconText(tr("Notes"));
    a.notes->setIcon(icons::get(QStringLiteral("notes")));
    a.notes->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N));
    a.notes->setToolTip(tr("Text notes about the image, Markdown"));
    a.notes->setObjectName(QStringLiteral("view.notes"));
    shortcuts::init(a.notes);
    addAction(a.notes);

    // Thumbnails of the current folder (Etapa 5).
    m_thumbs = new ThumbnailPanel([this](const QString &dir) { return folderImages(dir); }, this);
    addDockWidget(Qt::BottomDockWidgetArea, m_thumbs);
    m_thumbs->setVisible(QSettings().value(QStringLiteral("viewer/thumbnails"), false).toBool());
    a.thumbnails = m_thumbs->toggleViewAction();
    a.thumbnails->setText(tr("&Thumbnails"));
    a.thumbnails->setIconText(tr("Thumbnails"));
    a.thumbnails->setIcon(icons::get(QStringLiteral("view-thumbnails")));
    a.thumbnails->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    a.thumbnails->setToolTip(tr("Thumbnails of the images in the folder"));
    a.thumbnails->setObjectName(QStringLiteral("view.thumbnails"));
    shortcuts::init(a.thumbnails);
    addAction(a.thumbnails);
    connect(a.thumbnails, &QAction::triggered, this, [](bool on) {
        QSettings().setValue(QStringLiteral("viewer/thumbnails"), on);
    });
    connect(m_thumbs, &ThumbnailPanel::fileActivated, this, &MainWindow::showFolderImage);
    auto followDocument = [this] {
        m_thumbs->setCurrentFile(!m_doc->originPath().isEmpty() ? m_doc->originPath() : m_doc->filePath());
    };
    connect(m_doc, &Document::documentReset, this, followDocument);
    connect(m_doc, &Document::historyChanged, this, followDocument);
    createMenusAndToolbars();

    connect(m_doc, &Document::historyChanged, this, [this] {
        updateActions();
        updateTitle();
        updateStatus();
    });
    connect(m_doc, &Document::changed, this, &MainWindow::updateStatus);
    connect(m_doc, &Document::documentReset, this, &MainWindow::updateActions);
    connect(m_canvas, &Canvas::zoomChanged, this, &MainWindow::updateStatus);
    connect(m_canvas, &Canvas::selectionChanged, this, [this] {
        updateActions();
        updateToolOptions();
    });
    connect(m_doc, &Document::changed, this, &MainWindow::refreshPanelValues);
    connect(m_canvas, &Canvas::colorPicked, this, [this](const QColor &c) {
        m_canvas->toast()->showMessage(tr("Picked color %1").arg(c.name()));
        updateToolOptions();
    });
    for (int i = int(ToolId::Pointer); i <= int(ToolId::Bubble); ++i)
        connect(m_canvas->tool(ToolId(i)), &Tool::optionsChanged, this, &MainWindow::updateToolOptions);
    connect(m_canvas, &Canvas::toolChanged, this, [this](ToolId id) {
        for (QAction *act : m_toolGroup->actions()) {
            if (act->data().toInt() == int(id))
                act->setChecked(true);
        }
        updateToolOptions();
        updateActions();
    });
    connect(m_canvas, &Canvas::browseRequested, this, [this](int d) { browse(d); });
    connect(m_canvas, &Canvas::imageDoubleClicked, this, [this] { setFullScreenMode(!m_fullScreen); });
    connect(m_canvas, &Canvas::cursorMoved, this, [this](const QPoint &p, bool inside) {
        m_statusCursor->setText(inside ? QStringLiteral("%1, %2").arg(p.x()).arg(p.y()) : QString());
    });

    updateToolOptions();
    updateActions();
    updateTitle();
    updateStatus();
    m_canvas->setFocus();
}

// ================================================================ actions

void MainWindow::createActions()
{
    auto make = [this](const QString &text, const QKeySequence &key, const char *icon,
                       std::function<void()> fn) {
        auto *act = new QAction(themeIcon(icon), text, this);
        act->setIconText(toolbarText(text));
        if (!key.isEmpty())
            act->setShortcut(key);
        connect(act, &QAction::triggered, this, std::move(fn));
        addAction(act); // shortcuts work even when the menu bar is hidden
        return act;
    };

    a.newFromClipboard = make(tr("New from Clipboard"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V),
                              "edit-paste", [this] { newFromClipboard(); });
    a.open = make(tr("&Open…"), QKeySequence::Open, "document-open", [this] { openDialog(); });
    a.save = make(tr("&Save"), QKeySequence::Save, "document-save", [this] { save(); });
    a.saveAs = make(tr("Save &As…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), "document-save-as",
                    [this] { saveAs(); });
    a.saveProject = make(tr("Save as &Project…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S), "document-save-project",
                         [this] { saveProjectAs(); });
    a.saveProject->setToolTip(tr("Save with editable annotations (.rolidan)"));
    a.settings = make(tr("Se&ttings…"), {}, "configure", [this] { showSettings(); });
    a.close = make(tr("&Close Window"), QKeySequence(Qt::CTRL | Qt::Key_W), "window-close", [this] { close(); });
    a.quit = make(tr("&Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q), "application-exit", [] {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (qobject_cast<MainWindow *>(w) && !w->close())
                return;
        }
    });

    a.undo = make(tr("&Undo"), QKeySequence(Qt::CTRL | Qt::Key_Z), "edit-undo", [this] {
        m_canvas->cancelInteraction();
        m_doc->undo();
    });
    a.redo = make(tr("&Redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), "edit-redo", [this] {
        m_canvas->cancelInteraction();
        m_doc->redo();
    });
    a.redo->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)});
    // Toolbar labels stay fixed while the menu text shows what will be undone.
    a.undo->setIconText(tr("Undo"));
    a.redo->setIconText(tr("Redo"));
    a.cut = make(tr("Cu&t Image Only"), QKeySequence(Qt::CTRL | Qt::Key_X), "edit-cut", [this] { cut(false); });
    a.cutAll = make(tr("Cut with Annotations"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), "edit-cut",
                    [this] { cut(true); });
    a.copy = make(tr("&Copy Image Only"), QKeySequence(Qt::CTRL | Qt::Key_C), "edit-copy", [this] { copy(false); });
    a.copyAll = make(tr("Copy with Annotations"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), "edit-copy",
                     [this] { copy(true); });
    a.copyResult = make(tr("Copy Re&sult"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C), "edit-copy",
                        [this] { copyResult(); });
    a.copyResult->setToolTip(tr("Copy the whole resulting image (Ctrl+Alt+C)"));
    a.paste = make(tr("&Paste"), QKeySequence(Qt::CTRL | Qt::Key_V), "edit-paste", [this] { paste(); });
    a.del = make(tr("&Delete"), QKeySequence(Qt::Key_Delete), "edit-delete", [this] { deleteSelection(); });
    a.selectAll = make(tr("Select &All"), QKeySequence(Qt::CTRL | Qt::Key_A), "edit-select-all",
                       [this] { m_canvas->tool()->selectAll(); m_canvas->update(); });

    a.cropToSelection = make(tr("Crop to &Selection"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K),
                             "transform-crop", [this] { cropToSelection(); });
    // Viewer-style single keys (IrfanView): L / R and < / > rotate, H / V flip.
    a.rotateLeft = make(tr("Rotate &Left"), QKeySequence(Qt::Key_L), "object-rotate-left",
                        [this] { transformDocument([](const DocState &s) { return docops::rotate90(s, false); },
                                                   tr("Rotate left")); });
    a.rotateRight = make(tr("Rotate &Right"), QKeySequence(Qt::Key_R), "object-rotate-right",
                         [this] { transformDocument([](const DocState &s) { return docops::rotate90(s, true); },
                                                    tr("Rotate right")); });
    a.rotateLeft->setShortcuts({QKeySequence(Qt::Key_L), QKeySequence(Qt::Key_Less)});
    a.rotateRight->setShortcuts({QKeySequence(Qt::Key_R), QKeySequence(Qt::Key_Greater)});
    a.rotate180 = make(tr("Rotate 1&80°"), {}, "", [this] { transformDocument(&docops::rotate180, tr("Rotate 180°")); });
    a.flipH = make(tr("Flip &Horizontally"), QKeySequence(Qt::Key_H), "object-flip-horizontal",
                   [this] { transformDocument([](const DocState &s) { return docops::flip(s, Qt::Horizontal); },
                                              tr("Flip horizontally")); });
    a.flipV = make(tr("Flip &Vertically"), QKeySequence(Qt::Key_V), "object-flip-vertical",
                   [this] { transformDocument([](const DocState &s) { return docops::flip(s, Qt::Vertical); },
                                              tr("Flip vertically")); });

    a.fit = make(tr("&Fit to Window"), QKeySequence(Qt::CTRL | Qt::Key_0), "zoom-fit-best",
                 [this] { m_canvas->fitToWindow(); });
    a.actual = make(tr("&Actual Size (100 %)"), QKeySequence(Qt::CTRL | Qt::Key_1), "zoom-original",
                    [this] { m_canvas->actualSize(); });
    a.zoomIn = make(tr("Zoom &In"), QKeySequence::ZoomIn, "zoom-in", [this] { m_canvas->zoomIn(); });
    a.zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    a.zoomOut = make(tr("Zoom &Out"), QKeySequence::ZoomOut, "zoom-out", [this] { m_canvas->zoomOut(); });

    // Tools: Shift + letter always selects the tool; the plain letter too,
    // unless a viewer action uses it (R, L, H, V, F).
    m_toolGroup = new QActionGroup(this);
    auto tool = [&](const QString &text, Qt::Key key, const char *icon, ToolId id, const QString &tip = {},
                    bool plain = true) {
        QAction *act = make(text, QKeySequence(key), icon, [this, id] { m_canvas->setTool(id); });
        QList<QKeySequence> keys;
        if (plain)
            keys << QKeySequence(key);
        keys << QKeySequence(Qt::SHIFT | key);
        act->setShortcuts(keys);
        act->setCheckable(true);
        act->setData(int(id));
        act->setProperty("tip", tip);
        act->setObjectName(QStringLiteral("tool.") + QLatin1String(toolName(id)));
        m_toolGroup->addAction(act);
        return act;
    };
    a.toolPointer = tool(tr("Pointer"), Qt::Key_V, "edit-select", ToolId::Pointer, {}, false);
    a.toolSelect = tool(tr("Select Area"), Qt::Key_M, "select-rectangular", ToolId::Select);
    a.toolCrop = tool(tr("Crop"), Qt::Key_Y, "transform-crop", ToolId::Crop);
    tool(tr("Text"), Qt::Key_T, "draw-text", ToolId::Text);
    tool(tr("Arrow"), Qt::Key_A, "draw-arrow", ToolId::Arrow);
    tool(tr("Line"), Qt::Key_L, "draw-line", ToolId::Line, {}, false);
    tool(tr("Rectangle"), Qt::Key_R, "draw-rectangle", ToolId::Rect, {}, false);
    tool(tr("Ellipse"), Qt::Key_O, "draw-ellipse", ToolId::Ellipse);
    tool(tr("Brush"), Qt::Key_B, "draw-brush", ToolId::Brush);
    tool(tr("Highlighter"), Qt::Key_H, "draw-highlight", ToolId::Highlighter, {}, false);
    tool(tr("Cover"), Qt::Key_K, "draw-cover", ToolId::Cover, tr("Opaque cover for sensitive data"));
    tool(tr("Color Picker"), Qt::Key_I, "color-picker", ToolId::Eyedropper);
    tool(tr("Numbered Marker"), Qt::Key_N, "draw-marker", ToolId::Marker, tr("Each click adds the next number"));
    tool(tr("Polyline"), Qt::Key_P, "draw-polyline", ToolId::Polyline,
         tr("Click to add points, double-click or Enter to finish"));
    tool(tr("Polygon"), Qt::Key_G, "draw-polygon", ToolId::Polygon,
         tr("Click to add points, click the first point or press Enter to close"));
    tool(tr("Bubble"), Qt::Key_Q, "draw-bubble", ToolId::Bubble,
         tr("Press where the tail points, drag to place the bubble"));
    tool(tr("Clone Stamp"), Qt::Key_S, "tool-clone", ToolId::Clone, tr("Ctrl+click or right-click sets the source"));
    tool(tr("Eraser"), Qt::Key_E, "draw-eraser", ToolId::Eraser,
         tr("Erases the image to transparency, not annotations"));
    tool(tr("Blur Brush"), Qt::Key_U, "tool-blur", ToolId::BlurBrush);
    tool(tr("Pixelate Brush"), Qt::Key_J, "tool-pixelate", ToolId::PixelBrush, tr("Mosaic where you paint"));
    tool(tr("Paint Bucket"), Qt::Key_F, "tool-bucket", ToolId::Bucket,
         tr("Fills an area of similar color in the image"), false);
    tool(tr("Straighten"), Qt::Key_W, "tool-straighten", ToolId::Straighten,
         tr("Drag along a line that should be level or upright"));
    a.toolPointer->setChecked(true);

    // Arrange (spec D1). Brackets are hard to reach on some layouts: Page Up/Down too.
    a.forward = make(tr("Bring &Forward"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight), "go-up",
                     [this] { arrange(int(docops::Order::Forward)); });
    a.forward->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_BracketRight), QKeySequence(Qt::CTRL | Qt::Key_PageUp)});
    a.backward = make(tr("Send &Backward"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), "go-down",
                      [this] { arrange(int(docops::Order::Backward)); });
    a.backward->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), QKeySequence(Qt::CTRL | Qt::Key_PageDown)});
    a.toFront = make(tr("Bring to F&ront"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight), "go-top",
                     [this] { arrange(int(docops::Order::ToFront)); });
    a.toFront->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight),
                             QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_PageUp)});
    a.toBack = make(tr("Send to Bac&k"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft), "go-bottom",
                    [this] { arrange(int(docops::Order::ToBack)); });
    a.toBack->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft),
                            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_PageDown)});
    a.duplicate = make(tr("Dup&licate"), QKeySequence(Qt::CTRL | Qt::Key_D), "edit-copy",
                       [this] { duplicateSelection(); });
    a.mergeSelected = make(tr("&Merge Selected into Image"), {}, "", [this] { mergeIntoImage(false); });
    a.mergeAll = make(tr("Merge &All Annotations into Image"), {}, "", [this] { mergeIntoImage(true); });

    a.resizeImage = make(tr("Resi&ze Image…"), QKeySequence(Qt::CTRL | Qt::Key_R), "transform-scale",
                         [this] { resizeImageDialog(); });
    a.resizeImage->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_R), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I)});
    a.canvasSize = make(tr("Ca&nvas Size…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_I), "",
                        [this] { canvasSizeDialog(); });
    a.blurArea = make(tr("&Blur Selection"), {}, "", [this] { areaEffect(0); });
    a.pixelateArea = make(tr("&Pixelate Selection"), {}, "", [this] { areaEffect(1); });
    a.coverArea = make(tr("C&over Selection"), {}, "", [this] { areaEffect(2); });
    a.hideObjects = make(tr("&Hide Annotations"), QKeySequence(Qt::CTRL | Qt::Key_H), "",
                         [] {});
    a.hideObjects->setCheckable(true);
    a.hideObjects->setToolTip(tr("Hide annotations to retouch the image under them (view only)"));
    connect(a.hideObjects, &QAction::toggled, m_canvas, &Canvas::setObjectsHidden);

    a.about = make(tr("&About Rolidan"), {}, "help-about", [this] { showAbout(); });

    // Adjustments and filters (Etapa 4), built from the filter registry.
    const QHash<QString, QKeySequence> filterKeys = {
        {QStringLiteral("enhanceColors"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)},
        {QStringLiteral("brightnessContrast"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B)},
        {QStringLiteral("hueSaturation"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U)},
        {QStringLiteral("levels"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)},
        {QStringLiteral("rotate"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R)},
    };
    for (const filters::Spec &spec : filters::all()) {
        const QString text = filters::displayName(spec) + (spec.params.isEmpty() ? QString() : QStringLiteral("…"));
        const QString id = spec.id;
        if (id == QLatin1String("noise")) {
            m_filterActions.append(make(tr("Add Noise…"), {}, "", [this] {
                runFilter(QStringLiteral("noise"), {{QStringLiteral("color"), 0}});
            }));
            m_filterActions.last()->setData(id);
            m_filterActions.append(make(tr("Add Color Noise…"), {}, "", [this] {
                runFilter(QStringLiteral("noise"), {{QStringLiteral("color"), 1}});
            }));
            m_filterActions.last()->setData(id);
            m_filterActions.last()->setProperty("filterColor", 1);
            continue;
        }
        QAction *act = make(text, filterKeys.value(id), "", [this, id] { runFilter(id); });
        act->setData(id);
        m_filterActions.append(act);
    }
    a.straightenDocument = make(tr("Straighten &Document Automatically…"), {}, "", [this] {
        if (m_doc->isEmpty())
            return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const double skew = filters::detectSkew(m_doc->state().base);
        QApplication::restoreOverrideCursor();
        if (skew == 0.0) {
            m_canvas->toast()->showMessage(tr("No tilted text lines found."));
            return;
        }
        runFilter(QStringLiteral("rotate"), {{QStringLiteral("angle"), std::round(-skew * 10.0) / 10.0},
                                             {QStringLiteral("canvas"), 1},
                                             {QStringLiteral("fill"), 1}});
    });
    a.straightenDocument->setToolTip(tr("Finds the angle of text lines in a scanned or photographed document"));
    connect(m_canvas, &Canvas::straightenRequested, this, [this](double degrees) {
        runFilter(QStringLiteral("rotate"), {{QStringLiteral("angle"), degrees}, {QStringLiteral("canvas"), 2}});
    });

    a.batch = make(tr("&Batch Convert…"), {}, "", [this] {
        BatchDialog dlg(this);
        const QString current = !m_doc->originPath().isEmpty() ? m_doc->originPath() : m_doc->filePath();
        if (!current.isEmpty())
            dlg.addFiles(folderImages(QFileInfo(current).absolutePath()));
        dlg.exec();
    });
    a.batch->setToolTip(tr("Convert, shrink or adjust many images at once"));
    a.print = make(tr("Pri&nt…"), QKeySequence::Print, "document-print", [this] { print(); });
    a.print->setToolTip(tr("Print the image: fit to page or actual size, with a preview"));
    a.capture = make(tr("Capt&ure Screen"), QKeySequence(Qt::Key_C), "camera-photo", [this] { captureScreen(); });
    a.capture->setIconText(tr("Screenshot"));
    a.capture->setToolTip(tr("Screenshot: Rolidan steps aside, takes the screenshot and comes back with it. "
                             "The arrow chooses whole screen or area, the field next to it the delay."));

    // Browsing the folder of the current image (Etapa 4).
    a.nextImage = make(tr("Ne&xt Image in Folder"), QKeySequence(Qt::Key_PageDown), "go-next", [this] { browse(1); });
    a.prevImage = make(tr("Pre&vious Image in Folder"), QKeySequence(Qt::Key_PageUp), "go-previous",
                       [this] { browse(-1); });
    a.firstImage = make(tr("F&irst Image in Folder"), QKeySequence(Qt::Key_Home), "", [this] { browse(-1000000); });
    a.lastImage = make(tr("&Last Image in Folder"), QKeySequence(Qt::Key_End), "", [this] { browse(1000000); });

    // Viewer (Etapa 5).
    a.fullScreen = make(tr("F&ull Screen"), QKeySequence(Qt::Key_F11), "view-fullscreen",
                        [this] { setFullScreenMode(!m_fullScreen); });
    a.fullScreen->setCheckable(true);
    a.fullScreen->setShortcuts({QKeySequence(Qt::Key_F11), QKeySequence(Qt::Key_F)}); // Enter: see keyPressEvent
    a.slideshow = make(tr("&Slideshow"), QKeySequence(Qt::Key_F5), "media-playback-start", [this] {
        if (isSlideshowRunning())
            stopSlideshow();
        else
            startSlideshow();
    });
    a.slideshow->setCheckable(true);
    a.slideshow->setToolTip(tr("Show the images of the folder one after another in full screen (F5)"));
    a.info = make(tr("Image In&formation…"), QKeySequence(Qt::CTRL | Qt::Key_I), "document-properties",
                  [this] { showImageInfo(); });
    a.info->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_I), QKeySequence(Qt::ALT | Qt::Key_Return)});
    a.rename = make(tr("R&ename File…"), QKeySequence(Qt::Key_F2), "edit-rename", [this] { renameDialog(); });
    a.trash = make(tr("Move File to Tras&h…"), QKeySequence(Qt::CTRL | Qt::Key_Delete), "user-trash",
                   [this] { trashCurrentFile(); });
    a.copyTo = make(tr("Cop&y File to Folder…"), QKeySequence(Qt::Key_F8), "edit-copy",
                    [this] { copyOrMoveDialog(false); });
    a.moveTo = make(tr("&Move File to Folder…"), QKeySequence(Qt::Key_F7), "go-jump",
                    [this] { copyOrMoveDialog(true); });
    a.showInFolder = make(tr("Show in File Mana&ger"), {}, "folder-open", [this] { showInFolder(); });
    a.clearRecent = make(tr("&Clear List"), {}, "edit-clear", [this] { settings::clearRecentFiles(); });
    a.enlargeSmall = make(tr("&Enlarge Small Images in Full Screen"), QKeySequence(Qt::Key_Z), "zoom-fit-best",
                          [this] { setEnlargeSmallImages(!settings::fullScreenEnlarge()); });
    a.enlargeSmall->setCheckable(true);
    a.enlargeSmall->setChecked(settings::fullScreenEnlarge());
    a.enlargeSmall->setToolTip(tr("In full screen, fit every image to the screen, small ones too (Z)"));
    a.shortcuts = make(tr("&Keyboard Shortcuts…"), {}, "preferences-desktop-keyboard", [this] { showShortcuts(); });

    // Stable ids for customizable shortcuts (settings keys, never translated).
    const QList<std::pair<QAction *, const char *>> ids = {
        {a.newFromClipboard, "file.newFromClipboard"}, {a.open, "file.open"}, {a.save, "file.save"},
        {a.saveAs, "file.saveAs"}, {a.saveProject, "file.saveProject"}, {a.batch, "file.batch"}, {a.print, "file.print"}, {a.capture, "file.capture"},
        {a.prevImage, "file.previousImage"}, {a.nextImage, "file.nextImage"}, {a.firstImage, "file.firstImage"},
        {a.lastImage, "file.lastImage"}, {a.info, "file.info"}, {a.rename, "file.rename"},
        {a.copyTo, "file.copyToFolder"}, {a.moveTo, "file.moveToFolder"}, {a.trash, "file.trash"},
        {a.showInFolder, "file.showInFolder"}, {a.settings, "file.settings"}, {a.shortcuts, "file.shortcuts"},
        {a.close, "file.close"}, {a.quit, "file.quit"},
        {a.undo, "edit.undo"}, {a.redo, "edit.redo"}, {a.cut, "edit.cut"}, {a.cutAll, "edit.cutAll"},
        {a.copy, "edit.copy"}, {a.copyAll, "edit.copyAll"}, {a.copyResult, "edit.copyResult"},
        {a.paste, "edit.paste"}, {a.del, "edit.delete"}, {a.selectAll, "edit.selectAll"},
        {a.duplicate, "edit.duplicate"},
        {a.forward, "arrange.forward"}, {a.backward, "arrange.backward"}, {a.toFront, "arrange.toFront"},
        {a.toBack, "arrange.toBack"},
        {a.cropToSelection, "image.cropToSelection"}, {a.rotateLeft, "image.rotateLeft"},
        {a.rotateRight, "image.rotateRight"}, {a.rotate180, "image.rotate180"}, {a.flipH, "image.flipHorizontally"},
        {a.flipV, "image.flipVertically"}, {a.straightenDocument, "image.straightenDocument"},
        {a.resizeImage, "image.resize"}, {a.canvasSize, "image.canvasSize"}, {a.blurArea, "image.blurSelection"},
        {a.pixelateArea, "image.pixelateSelection"}, {a.coverArea, "image.coverSelection"},
        {a.mergeSelected, "image.mergeSelected"}, {a.mergeAll, "image.mergeAll"},
        {a.fit, "view.fit"}, {a.actual, "view.actualSize"}, {a.zoomIn, "view.zoomIn"}, {a.zoomOut, "view.zoomOut"},
        {a.fullScreen, "view.fullScreen"}, {a.enlargeSmall, "view.enlargeSmall"}, {a.slideshow, "view.slideshow"}, {a.hideObjects, "view.hideAnnotations"},
        {a.about, "help.about"}};
    for (const auto &[act, id] : ids)
        act->setObjectName(QLatin1String(id));
    for (QAction *act : std::as_const(m_filterActions)) {
        QString id = QStringLiteral("filter.") + act->data().toString();
        if (act->property("filterColor").isValid())
            id += act->property("filterColor").toInt() ? QStringLiteral(".color") : QString();
        act->setObjectName(id);
    }
    for (QAction *act : customizableActions())
        shortcuts::init(act);
    updateShortcutTips();
}

QList<QAction *> MainWindow::customizableActions() const
{
    QList<QAction *> out;
    for (QAction *act : actions())
        if (!act->objectName().isEmpty())
            out << act;
    return out;
}

void MainWindow::updateShortcutTips()
{
    // Tool tips show the current keys, so they follow customized shortcuts.
    for (QAction *act : m_toolGroup->actions()) {
        const QString keys = shortcuts::text(act->shortcuts());
        const QString name = act->iconText();
        QString tip = keys.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, keys);
        const QString extra = act->property("tip").toString();
        if (!extra.isEmpty())
            tip += QStringLiteral(": ") + extra;
        act->setToolTip(tip);
    }
}

void MainWindow::showShortcuts()
{
    ShortcutsDialog dlg(customizableActions(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w)) {
            shortcuts::reload(mw->customizableActions());
            mw->updateShortcutTips();
        }
    }
}

void MainWindow::createMenusAndToolbars()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addActions({a.newFromClipboard, a.capture, a.open});
    m_recentMenu = file->addMenu(themeIcon("document-open-recent"), tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::populateRecentMenu);
    populateRecentMenu();
    file->addSeparator();
    file->addActions({a.prevImage, a.nextImage, a.firstImage, a.lastImage});
    file->addSeparator();
    file->addActions({a.save, a.saveAs, a.saveProject});
    file->addAction(a.batch);
    file->addAction(a.print);
    file->addSeparator();
    file->addActions({a.info, a.rename, a.copyTo, a.moveTo, a.trash, a.showInFolder});
    file->addSeparator();
    file->addActions({a.settings, a.shortcuts});
    file->addSeparator();
    file->addActions({a.close, a.quit});

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({a.undo, a.redo});
    edit->addSeparator();
    edit->addActions({a.cut, a.cutAll, a.copy, a.copyAll, a.copyResult, a.paste, a.del});
    edit->addSeparator();
    edit->addAction(a.selectAll);
    edit->addAction(a.duplicate);

    QMenu *arrangeMenu = menuBar()->addMenu(tr("&Arrange"));
    arrangeMenu->addActions({a.forward, a.backward, a.toFront, a.toBack});

    QMenu *image = menuBar()->addMenu(tr("&Image"));
    image->addAction(a.cropToSelection);
    image->addSeparator();
    image->addActions({a.rotateLeft, a.rotateRight, a.rotate180});
    image->addSeparator();
    image->addActions({a.flipH, a.flipV});
    image->addSeparator();
    for (QAction *act : std::as_const(m_filterActions))
        if (filters::find(act->data().toString())->menu == filters::Menu::Image)
            image->addAction(act);
    image->addAction(a.straightenDocument);
    image->addSeparator();
    image->addActions({a.resizeImage, a.canvasSize});
    image->addSeparator();
    image->addAction(a.coverArea);
    image->addSeparator();
    image->addActions({a.mergeSelected, a.mergeAll});

    QMenu *colors = menuBar()->addMenu(tr("&Colors"));
    QMenu *filterMenu = menuBar()->addMenu(tr("Fi&lters"));
    for (QAction *act : std::as_const(m_filterActions)) {
        const filters::Spec *spec = filters::find(act->data().toString());
        if (spec->menu == filters::Menu::Adjust) {
            if (spec->id == QLatin1String("grayscale"))
                colors->addSeparator();
            colors->addAction(act);
        } else if (spec->menu == filters::Menu::Filters) {
            const bool groupStart = spec->id == QLatin1String("noise") || spec->id == QLatin1String("cleanDocument");
            if (groupStart && !filterMenu->actions().isEmpty() && filterMenu->actions().last()->data() != act->data())
                filterMenu->addSeparator();
            filterMenu->addAction(act);
        }
    }

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    tools->addActions(m_toolGroup->actions());

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addActions({a.fit, a.actual, a.zoomIn, a.zoomOut});
    view->addSeparator();
    view->addActions({a.fullScreen, a.enlargeSmall, a.slideshow});
    view->addSeparator();
    view->addAction(a.hideObjects);
    view->addActions({a.notes, a.thumbnails});

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(a.about);

    QToolBar *main = addToolBar(tr("Main"));
    main->setObjectName(QStringLiteral("mainToolbar"));
    main->setMovable(false);
    main->addActions({a.open, a.save});
    main->addSeparator();
    main->addActions({a.prevImage, a.nextImage});
    main->addSeparator();
    main->addActions({a.undo, a.redo});
    main->addSeparator();
    main->addAction(a.copyResult);
    main->addSeparator();
    main->addWidget(createCaptureControls(main));
    main->addSeparator();
    main->addActions({a.rotateLeft, a.rotateRight, a.flipH, a.flipV});
    main->addSeparator();
    main->addActions({a.fit, a.actual, a.fullScreen});
    main->addSeparator();
    main->addActions({a.notes, a.thumbnails});

    addToolBarBreak();
    m_optionsBar = addToolBar(tr("Tool Options"));
    m_optionsBar->setObjectName(QStringLiteral("toolOptions"));
    m_optionsBar->setMovable(false);

    QToolBar *toolsBar = new QToolBar(tr("Tools"), this);
    toolsBar->setObjectName(QStringLiteral("tools"));
    toolsBar->setMovable(false);
    // Two columns of icons (names in tooltips and the Tools menu): all tools
    // fit without an overflow menu even in a small window.
    auto *toolGrid = new QWidget(toolsBar);
    auto *gridLayout = new QGridLayout(toolGrid);
    gridLayout->setContentsMargins(2, 2, 2, 2);
    gridLayout->setSpacing(1);
    const QList<QAction *> toolActions = m_toolGroup->actions();
    for (qsizetype i = 0; i < toolActions.size(); ++i) {
        auto *b = new QToolButton(toolGrid);
        b->setDefaultAction(toolActions[i]);
        b->setAutoRaise(true);
        b->setIconSize(QSize(24, 24));
        b->setFocusPolicy(Qt::NoFocus);
        gridLayout->addWidget(b, int(i / 2), int(i % 2));
    }
    gridLayout->setRowStretch(gridLayout->rowCount(), 1);
    toolsBar->addWidget(toolGrid);
    addToolBar(Qt::LeftToolBarArea, toolsBar);

    m_statusSize = new QLabel(this);
    m_statusZoom = new QLabel(this);
    m_statusCursor = new QLabel(this);
    m_statusState = new QLabel(this);
    m_statusCursor->setMinimumWidth(90);
    statusBar()->addWidget(m_statusSize);
    statusBar()->addWidget(m_statusZoom);
    statusBar()->addWidget(m_statusCursor);
    statusBar()->addPermanentWidget(m_statusState);
}

void MainWindow::updateActions()
{
    const bool has = !m_doc->isEmpty();
    const bool objectsSelected = m_canvas->toolId() == ToolId::Pointer && !m_canvas->selectedObjects().isEmpty();
    const bool area = !m_canvas->areaSelection().isEmpty();
    a.undo->setEnabled(m_doc->canUndo());
    a.redo->setEnabled(m_doc->canRedo());
    a.undo->setText(m_doc->canUndo() ? tr("&Undo %1").arg(m_doc->history().undoText()) : tr("&Undo"));
    a.redo->setText(m_doc->canRedo() ? tr("&Redo %1").arg(m_doc->history().redoText()) : tr("&Redo"));
    for (QAction *act : {a.save, a.saveAs, a.saveProject, a.copy, a.copyAll, a.copyResult, a.selectAll, a.rotateLeft,
                         a.rotateRight, a.rotate180, a.flipH, a.flipV, a.fit, a.actual, a.zoomIn, a.zoomOut,
                         a.resizeImage, a.canvasSize})
        act->setEnabled(has);
    const bool anyObjects = !m_canvas->selectedObjects().isEmpty();
    a.cut->setEnabled(objectsSelected || area);
    a.cutAll->setEnabled(objectsSelected || area);
    a.del->setEnabled(anyObjects || area);
    for (QAction *act : {a.forward, a.backward, a.toFront, a.toBack, a.duplicate, a.mergeSelected})
        act->setEnabled(anyObjects);
    a.mergeAll->setEnabled(has && !m_doc->state().objects.isEmpty());
    a.cropToSelection->setEnabled(area);
    for (QAction *act : {a.blurArea, a.pixelateArea, a.coverArea})
        act->setEnabled(area);
    for (QAction *act : std::as_const(m_filterActions))
        act->setEnabled(has);
    a.straightenDocument->setEnabled(has);
    const bool fromFile = has && !(m_doc->filePath().isEmpty() && m_doc->originPath().isEmpty());
    for (QAction *act : {a.nextImage, a.prevImage, a.firstImage, a.lastImage, a.slideshow})
        act->setEnabled(fromFile || isSlideshowRunning());
    a.info->setEnabled(has);
    const bool onDisk = has && !currentFile().isEmpty();
    for (QAction *act : {a.rename, a.trash, a.copyTo, a.moveTo, a.showInFolder})
        act->setEnabled(onDisk);
}

void MainWindow::updateTitle()
{
    QString name = tr("Untitled");
    if (!m_doc->filePath().isEmpty())
        name = QFileInfo(m_doc->filePath()).fileName();
    setWindowTitle(QStringLiteral("%1[*] — Rolidan").arg(name));
    setWindowModified(m_doc->isModified());
}

void MainWindow::updateStatus()
{
    const QLocale loc;
    if (m_doc->isEmpty()) {
        m_statusSize->clear();
        m_statusZoom->clear();
        m_statusState->clear();
        return;
    }
    const QSize s = m_doc->size();
    m_statusSize->setText(QStringLiteral("%1 × %2 px").arg(s.width()).arg(s.height()));
    m_statusZoom->setText(loc.toString(m_canvas->zoom() * 100.0, 'f', m_canvas->zoom() < 0.1 ? 1 : 0)
                          + QStringLiteral(" %"));
    QStringList state;
    if (m_doc->isModified())
        state << tr("Unsaved changes");
    else if (!m_doc->filePath().isEmpty())
        state << tr("Saved");
    // Spec F6: annotations are lost on close unless saved as a project.
    if (m_doc->fileFormat() != project::kSuffix) {
        if (!m_doc->state().objects.isEmpty())
            state << tr("Annotations stay editable only until the window is closed — save as project (Ctrl+Alt+S) to keep them");
        else if (!m_doc->state().notes.isEmpty())
            state << tr("Notes are kept only in a project (Ctrl+Alt+S) or exported as .md");
    }
    m_statusState->setText(state.join(QStringLiteral(" · ")));
}

namespace {
// Holds the property panel in the options bar. A long panel (text tool,
// German labels) must never force the window wider: it scrolls instead.
class OptionsScroll : public QScrollArea
{
public:
    explicit OptionsScroll(QWidget *content, QWidget *parent)
        : QScrollArea(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        viewport()->setAutoFillBackground(false);
        content->setAutoFillBackground(false);
        setWidget(content);
    }

    QSize sizeHint() const override
    {
        widget()->ensurePolished();
        const QSize hint = widget()->sizeHint();
        return {hint.width(), contentHeight(hint.width() > width() && width() > 1)};
    }
    QSize minimumSizeHint() const override { return {1, contentHeight(false)}; }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QScrollArea::resizeEvent(e);
        // The scroll bar appears / disappears: ask the toolbar for a new height.
        const bool scrolls = widget()->sizeHint().width() > width();
        if (scrolls != m_scrolls) {
            m_scrolls = scrolls;
            updateGeometry();
        }
    }

private:
    int contentHeight(bool scrolls) const
    {
        widget()->ensurePolished();
        return widget()->sizeHint().height()
               + (scrolls ? style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this) : 0);
    }

    bool m_scrolls = false;
};

// Properties shown in the options bar and where their values come from.
struct PanelSource {
    QList<Prop> props;
    QHash<Prop, QVariant> values;
};

PanelSource panelSource(Canvas *canvas)
{
    PanelSource src;
    const ToolId tool = canvas->toolId();
    if ((tool == ToolId::Text || tool == ToolId::Bubble) && canvas->textTool()->isEditing()) {
        const TextObject *t = canvas->textTool()->editedObject();
        src.props = tooldefaults::props(ToolId::Text);
        for (Prop p : src.props)
            src.values[p] = t->prop(p);
        return src;
    }
    src.props = tooldefaults::props(tool);
    if (!src.props.isEmpty()) {
        for (Prop p : src.props)
            src.values[p] = tooldefaults::value(tool, p);
        return src;
    }
    if (tool != ToolId::Pointer)
        return src;
    // Pointer: properties common to all selected objects, values of the first.
    const ObjectList sel = canvas->selectedObjectList();
    if (sel.isEmpty())
        return src;
    src.props = sel.first()->props();
    for (const auto &o : sel) {
        const QList<Prop> theirs = o->props();
        src.props.removeIf([&](Prop p) { return !theirs.contains(p); });
    }
    for (Prop p : src.props)
        src.values[p] = sel.first()->prop(p);
    return src;
}
} // namespace

void MainWindow::updateToolOptions()
{
    const PanelSource src = panelSource(m_canvas);
    if (!src.props.isEmpty() && m_panel && m_panel->props() == src.props) {
        // Same layout: only refresh values (never delete a widget while it emits).
        m_panel->setValues(src.values);
        return;
    }
    if (m_optionsWidget) {
        m_optionsBar->clear();
        m_optionsWidget = nullptr;
        m_panel = nullptr;
    }
    QWidget *w = nullptr;
    if (!src.props.isEmpty()) {
        m_panel = new PropertyPanel(src.props, m_optionsBar);
        m_panel->setValues(src.values);
        connect(m_panel, &PropertyPanel::changed, m_canvas, &Canvas::applyProperty);
        w = new OptionsScroll(m_panel, m_optionsBar);
    } else if (m_canvas->toolId() == ToolId::Select) {
        w = createSelectOptions(m_optionsBar);
    } else {
        w = m_canvas->tool()->createOptions(m_optionsBar);
    }
    if (!w) {
        QString hint;
        switch (m_canvas->toolId()) {
        case ToolId::Pointer:
            hint = tr("Click to select an object, drag to move it, drag a corner to resize (Shift: free "
                      "aspect ratio), drag the round handle to rotate (Shift: 15° steps). Double-click a text "
                      "to edit it. Arrow keys move by 1 px, with Shift by 10 px.");
            break;
        case ToolId::Select:
            hint = tr("Drag to select an area. Ctrl+C copies the image only, Ctrl+Shift+C including "
                      "annotations. Esc clears the selection.");
            break;
        case ToolId::Eyedropper:
            hint = tr("Click to pick a color from the image; it is used by the previous tool.");
            break;
        default:
            break;
        }
        auto *label = new QLabel(hint, m_optionsBar);
        label->setContentsMargins(6, 0, 6, 0);
        label->setToolTip(hint);
        // A long hint (German!) must never force the window wider.
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setMinimumWidth(1);
        w = label;
    }
    m_optionsWidget = w;
    m_optionsBar->addWidget(w);
}

void MainWindow::refreshPanelValues()
{
    if (!m_panel)
        return;
    const PanelSource src = panelSource(m_canvas);
    if (src.props == m_panel->props())
        m_panel->setValues(src.values);
}

void MainWindow::arrange(int order)
{
    const QSet<quint64> ids = m_canvas->selectedObjects();
    if (ids.isEmpty())
        return;
    const DocState next = docops::reorder(m_doc->state(), ids, docops::Order(order));
    if (next.objects == m_doc->state().objects)
        return;
    m_doc->commit(next, tr("Arrange"));
}

void MainWindow::duplicateSelection()
{
    const QSet<quint64> ids = m_canvas->selectedObjects();
    if (ids.isEmpty())
        return;
    QSet<quint64> newIds;
    m_doc->commit(docops::duplicate(m_doc->state(), ids, QPointF(16, 16), &newIds), tr("Duplicate"));
    m_canvas->setSelectedObjects(newIds);
}

void MainWindow::mergeIntoImage(bool all)
{
    const QSet<quint64> ids = all ? QSet<quint64>() : m_canvas->selectedObjects();
    if (!all && ids.isEmpty())
        return;
    if (m_doc->state().objects.isEmpty())
        return;
    m_canvas->cancelInteraction();
    m_doc->commit(docops::mergeIntoBase(m_doc->state(), ids),
                  all ? tr("Merge all annotations") : tr("Merge into image"));
    m_canvas->setSelectedObjects({});
}

void MainWindow::resizeImageDialog()
{
    if (m_doc->isEmpty())
        return;
    m_canvas->cancelInteraction();
    QSize screenPx;
    if (QScreen *sc = screen())
        screenPx = (QSizeF(sc->size()) * sc->devicePixelRatio()).toSize(); // real pixels
    ResizeDialog dlg(m_doc->size(), printing::imageDpi(m_doc->state().base), screenPx, this);
    if (dlg.exec() != QDialog::Accepted || (dlg.newSize() == m_doc->size() && dlg.newDpi() <= 0))
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_canvas->setAreaSelection({});
    m_doc->commit(docops::resizeImage(m_doc->state(), dlg.newSize(), dlg.filter(), dlg.sharpen(), dlg.newDpi()),
                  dlg.newSize() == m_doc->size() ? tr("Change resolution") : tr("Resize image"));
    QApplication::restoreOverrideCursor();
    m_canvas->fitToWindow(false);
}

void MainWindow::canvasSizeDialog()
{
    if (m_doc->isEmpty())
        return;
    m_canvas->cancelInteraction();
    CanvasSizeDialog dlg(m_doc->size(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.newSize() == m_doc->size() && dlg.offset().isNull())
        return;
    m_canvas->setAreaSelection({});
    m_doc->commit(docops::resizeCanvas(m_doc->state(), dlg.newSize(), dlg.offset(), dlg.fill()), tr("Canvas size"));
    m_canvas->fitToWindow(false);
}

namespace {
const char kBlurKey[] = "tools/select/blurRadius";
const char kMosaicKey[] = "tools/select/mosaicCell";
} // namespace

void MainWindow::areaEffect(int kind)
{
    const QRect sel = m_canvas->areaSelection().intersected(QRect(QPoint(0, 0), m_doc->size()));
    if (sel.isEmpty())
        return;
    m_canvas->cancelInteraction();
    QSettings settings;
    switch (kind) {
    case 0:
        m_doc->commit(retouch::blurArea(m_doc->state(), sel, settings.value(kBlurKey, 10).toInt()),
                      tr("Blur selection"));
        break;
    case 1:
        m_doc->commit(retouch::pixelateArea(m_doc->state(), sel, settings.value(kMosaicKey, 12).toInt()),
                      tr("Pixelate selection"));
        break;
    default: {
        auto cover = std::make_shared<ShapeObject>();
        cover->kind = ShapeObject::Kind::Rect;
        cover->style = ShapeObject::Style::Cover;
        tooldefaults::applyTo(ToolId::Cover, *cover);
        cover->rect = QRectF(sel);
        m_doc->commit(docops::addObjects(m_doc->state(), {cover}), tr("Cover selection"));
        break;
    }
    }
}

void MainWindow::runFilter(const QString &id, const filters::Values &overrides)
{
    const filters::Spec *spec = filters::find(id);
    if (!spec || m_doc->isEmpty())
        return;
    m_canvas->cancelInteraction();
    if (spec->params.isEmpty()) {
        FilterDialog::applyDirect(*spec, m_canvas);
        return;
    }
    FilterDialog dlg(*spec, m_canvas, overrides, this);
    dlg.exec();
}

QStringList MainWindow::folderImages(const QString &dir) const
{
    QStringList globs;
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        globs << QStringLiteral("*.") + QString::fromLatin1(f);
    globs << QStringLiteral("*.") + QLatin1String(project::kSuffix);
    QStringList files = QDir(dir).entryList(globs, QDir::Files | QDir::Readable, QDir::NoSort);
    // Natural order: img2 before img10 (QCollator's numeric mode needs ICU
    // and a non-C locale, so compare digit runs ourselves).
    auto naturalLess = [](const QString &a, const QString &b) {
        qsizetype i = 0, j = 0;
        while (i < a.size() && j < b.size()) {
            if (a[i].isDigit() && b[j].isDigit()) {
                qsizetype ie = i, je = j;
                while (ie < a.size() && a[ie].isDigit())
                    ++ie;
                while (je < b.size() && b[je].isDigit())
                    ++je;
                const QString na = a.mid(i, ie - i).remove(QRegularExpression(QStringLiteral("^0+(?=\\d)")));
                const QString nb = b.mid(j, je - j).remove(QRegularExpression(QStringLiteral("^0+(?=\\d)")));
                if (na.size() != nb.size())
                    return na.size() < nb.size();
                if (na != nb)
                    return na < nb;
                i = ie;
                j = je;
                continue;
            }
            const int c = QString::compare(QString(a[i]), QString(b[j]), Qt::CaseInsensitive);
            if (c != 0)
                return QString::localeAwareCompare(QString(a[i]).toLower(), QString(b[j]).toLower()) < 0;
            ++i;
            ++j;
        }
        return a.size() - i < b.size() - j;
    };
    std::sort(files.begin(), files.end(), naturalLess);
    for (QString &f : files)
        f = QDir(dir).filePath(f);
    return files;
}

QWidget *MainWindow::createSelectOptions(QWidget *parent)
{
    auto *w = new QWidget;
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(4, 0, 4, 0);
    lay->setSpacing(4);
    QSettings settings;
    auto button = [&](QAction *act) {
        auto *b = new QToolButton(w);
        b->setDefaultAction(act);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setText(toolbarText(act->text()));
        lay->addWidget(b);
    };
    auto spin = [&](const char *key, int def, int min, int max, const QString &tip) {
        auto *sb = new QSpinBox(w);
        sb->setRange(min, max);
        sb->setValue(settings.value(QLatin1String(key), def).toInt());
        sb->setSuffix(QStringLiteral(" px"));
        sb->setToolTip(tip);
        sb->setObjectName(QLatin1String(key));
        connect(sb, &QSpinBox::valueChanged, this, [key](int v) { QSettings().setValue(QLatin1String(key), v); });
        lay->addWidget(sb);
    };
    button(a.blurArea);
    spin(kBlurKey, 10, 1, 200, tr("Blur radius"));
    button(a.pixelateArea);
    spin(kMosaicKey, 12, 2, 500, tr("Mosaic cell size"));
    button(a.coverArea);
    // Spec H1: blur and mosaic can sometimes be reversed.
    auto *note = new QLabel(tr("For passwords and other sensitive data use Cover."), w);
    note->setEnabled(false);
    note->setContentsMargins(8, 0, 0, 0);
    lay->addWidget(note);
    lay->addStretch();
    w->setToolTip(tr("Drag to select an area. Ctrl+C copies the image only, Ctrl+Shift+C including "
                     "annotations. Esc clears the selection."));
    return new OptionsScroll(w, parent);
}

// ================================================================ open / paste

bool MainWindow::openFile(const QString &path)
{
    if (project::isProjectFile(path)) {
        DocState s;
        QString error;
        if (!project::load(path, &s, &error)) {
            QMessageBox::warning(this, tr("Open"), error);
            return false;
        }
        m_canvas->setTool(ToolId::Pointer);
        m_doc->reset(s, {});
        m_doc->setSaved(QFileInfo(path).absoluteFilePath(), project::kSuffix);
        settings::setLastDirectory(QFileInfo(path).absolutePath());
        addRecent(path);
        updateTitle();
        updateStatus();
        return true;
    }
    const imageio::LoadResult r = imageio::loadFile(path);
    if (!r.ok()) {
        QMessageBox::warning(this, tr("Open"), r.error);
        return false;
    }
    settings::setLastDirectory(QFileInfo(path).absolutePath());
    openImage(r.image, QFileInfo(path).absoluteFilePath(), r.notes);
    addRecent(path);
    return true;
}

void MainWindow::openImage(const QImage &img, const QString &originPath, const QStringList &notes)
{
    m_canvas->setTool(ToolId::Pointer);
    m_doc->reset(img, originPath);
    showNotes(notes);
    updateTitle();
}

void MainWindow::showNotes(const QStringList &notes)
{
    if (!notes.isEmpty())
        m_canvas->toast()->showMessage(notes.join(QLatin1Char('\n')));
}

void MainWindow::paste()
{
    insertContent(clip::decode(QApplication::clipboard()->mimeData(QClipboard::Clipboard)));
}

void MainWindow::pasteWhenActive()
{
    // Wayland only gives clipboard access to the focused window (spec B4).
    if (isActiveWindow()) {
        QTimer::singleShot(0, this, &MainWindow::paste);
        return;
    }
    m_pasteOnActivate = true;
    // X11 without focus still works: fall back after a moment.
    QTimer::singleShot(1500, this, [this] {
        if (m_pasteOnActivate) {
            m_pasteOnActivate = false;
            paste();
        }
    });
}

bool MainWindow::event(QEvent *e)
{
    if (e->type() == QEvent::WindowActivate && m_pasteOnActivate) {
        m_pasteOnActivate = false;
        QTimer::singleShot(0, this, &MainWindow::paste);
    }
    return QMainWindow::event(e);
}

void MainWindow::insertContent(const clip::Content &c)
{
    m_canvas->cancelInteraction(); // spec B2: a pending operation is cancelled
    Toast *toast = m_canvas->toast();
    switch (c.kind) {
    case clip::Content::Kind::None:
        toast->showMessage(tr("There is no image in the clipboard"));
        return;
    case clip::Content::Kind::Link:
        toast->showMessage(tr("The clipboard contains a link, not an image. In the browser, use "
                              "“Copy Image”."));
        return;
    case clip::Content::Kind::Image:
    case clip::Content::Kind::Objects:
        break;
    }

    if (m_doc->isEmpty()) {
        QImage img = c.image;
        if (c.kind == clip::Content::Kind::Objects)
            img = docops::renderObjects(c.objects);
        openImage(img, c.filePath, c.notes);
        if (c.fileCount > 1)
            toast->showMessage(tr("Opened 1 of %n files", nullptr, c.fileCount));
        return;
    }

    // An image pasted while an area is selected goes into that rectangle
    // (Etapa 4): fitted with its aspect ratio; the toast offers stretching.
    const QRect area = m_canvas->areaSelection();
    if (c.kind == clip::Content::Kind::Image && !area.isEmpty() && !c.image.isNull()) {
        auto obj = std::make_shared<ImageObject>(c.image);
        const QSizeF fitted = QSizeF(c.image.size()).scaled(QSizeF(area.size()), Qt::KeepAspectRatio);
        const QPointF tl = QRectF(area).center() - QPointF(fitted.width(), fitted.height()) / 2.0;
        obj->transform = QTransform::fromScale(fitted.width() / c.image.width(), fitted.height() / c.image.height())
                         * QTransform::fromTranslate(tl.x(), tl.y());
        m_doc->commit(docops::addObjects(m_doc->state(), {obj}), tr("Paste into selection"));
        m_canvas->setTool(ToolId::Pointer);
        m_canvas->setSelectedObjects({obj->id});
        const quint64 id = obj->id;
        toast->showMessage(tr("Pasted into the selection, aspect ratio kept."), tr("Stretch"), [this, id, area] {
            const ObjectPtr o = docops::findObject(m_doc->state(), id);
            if (!o)
                return;
            auto cl = std::static_pointer_cast<ImageObject>(o->clone());
            cl->transform = QTransform::fromScale(double(area.width()) / cl->image.width(),
                                                  double(area.height()) / cl->image.height())
                            * QTransform::fromTranslate(area.x(), area.y());
            m_doc->commit(docops::replaceObject(m_doc->state(), cl), tr("Stretch"));
        });
        return;
    }

    // Insert as object(s), 1:1 (spec B2).
    ObjectList objects;
    if (c.kind == clip::Content::Kind::Objects) {
        objects = c.objects;
    } else {
        auto obj = std::make_shared<ImageObject>(c.image);
        const QPointF center = m_canvas->visibleImageRect().center();
        obj->transform = QTransform::fromTranslate(std::round(center.x() - c.image.width() / 2.0),
                                                   std::round(center.y() - c.image.height() / 2.0));
        objects.append(obj);
    }
    // Cascade repeated pastes by 16 px so they do not hide each other.
    auto occupied = [this](const QPointF &tl) {
        for (const auto &o : m_doc->state().objects) {
            if ((o->boundingRect().topLeft() - tl).manhattanLength() < 0.5)
                return true;
        }
        return false;
    };
    QPointF offset;
    const QPointF tl0 = objects.first()->boundingRect().topLeft();
    for (int i = 0; i < 100 && occupied(tl0 + offset); ++i)
        offset += QPointF(kCascade, kCascade);
    QSet<quint64> ids;
    QRectF bounds;
    bool larger = false;
    const QSize canvasSize = m_doc->size();
    for (auto &o : objects) {
        if (!offset.isNull()) {
            auto cl = o->clone();
            cl->applyDocumentTransform(QTransform::fromTranslate(offset.x(), offset.y()), DocTransformKind::Translate);
            o = cl;
        }
        ids.insert(o->id);
        bounds |= o->boundingRect();
        larger |= o->boundingRect().width() > canvasSize.width() || o->boundingRect().height() > canvasSize.height();
    }
    m_doc->commit(docops::addObjects(m_doc->state(), objects), tr("Paste"));
    m_canvas->setTool(ToolId::Pointer);
    m_canvas->setSelectedObjects(ids);

    if (larger) {
        toast->showMessage(tr("The pasted image is larger than the canvas."), tr("Expand Canvas"), [this, ids] {
            QRectF r;
            for (const auto &o : m_doc->state().objects) {
                if (ids.contains(o->id))
                    r |= o->boundingRect();
            }
            if (!r.isEmpty())
                m_doc->commit(docops::expandCanvasToInclude(m_doc->state(), r), tr("Expand canvas"));
        });
    } else if (!c.notes.isEmpty()) {
        showNotes(c.notes);
    } else if (c.fileCount > 1) {
        toast->showMessage(tr("Inserted 1 of %n files", nullptr, c.fileCount));
    }
}

void MainWindow::openDialog()
{
    QStringList globs;
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        globs << QStringLiteral("*.") + QString::fromLatin1(f);
    globs << QStringLiteral("*.") + QLatin1String(project::kSuffix);
    const QString filter = tr("Images and projects (%1)").arg(globs.join(QLatin1Char(' '))) + QStringLiteral(";;")
                           + tr("Rolidan projects (*.%1)").arg(QLatin1String(project::kSuffix))
                           + QStringLiteral(";;") + tr("All files (*)");
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Image"), settings::lastDirectory(), filter);
    if (path.isEmpty())
        return;
    // Decision J1: a non-empty document is never replaced.
    if (m_doc->isEmpty())
        openFile(path);
    else
        Application::instance()->openInNewWindow(path);
}

void MainWindow::newFromClipboard()
{
    if (m_doc->isEmpty())
        paste();
    else
        Application::instance()->newWindowFromClipboard(); // decision J1
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    const QMimeData *m = e->mimeData();
    bool ok = m->hasUrls() || m->hasImage();
    for (const QString &f : m->formats())
        ok |= f.startsWith(QLatin1String("image/"));
    if (ok)
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    // A dropped project opens (in a new window if this one has a document).
    for (const QUrl &url : e->mimeData()->urls()) {
        if (url.isLocalFile() && NotesPanel::isNotesFile(url.toLocalFile())) {
            e->acceptProposedAction();
            if (m_doc->isEmpty())
                m_canvas->toast()->showMessage(tr("Open or paste an image first, then drop the notes on it"));
            else
                m_notes->importFile(url.toLocalFile());
            return;
        }
        if (url.isLocalFile() && project::isProjectFile(url.toLocalFile())) {
            e->acceptProposedAction();
            if (m_doc->isEmpty())
                openFile(url.toLocalFile());
            else
                Application::instance()->openInNewWindow(url.toLocalFile());
            return;
        }
    }
    clip::Content c = clip::decode(e->mimeData());
    e->acceptProposedAction();
    if (c.kind == clip::Content::Kind::None) {
        m_canvas->toast()->showMessage(tr("The dropped data contains no image"));
        return;
    }
    if (!c.filePath.isEmpty())
        settings::setLastDirectory(QFileInfo(c.filePath).absolutePath());
    insertContent(c);
    activateWindow();
}

// ================================================================ clipboard out (spec B3)

void MainWindow::setClipboard(const QImage &img, const ObjectList &objects)
{
    if (img.isNull())
        return;
    QApplication::clipboard()->setMimeData(clip::encode(img, objects), QClipboard::Clipboard);
}

void MainWindow::copy(bool all)
{
    if (m_doc->isEmpty())
        return;
    Toast *toast = m_canvas->toast();
    const ObjectList objs = m_canvas->toolId() == ToolId::Pointer ? m_canvas->selectedObjectList() : ObjectList();
    if (!objs.isEmpty()) {
        setClipboard(docops::renderObjects(objs), objs);
        toast->showMessage(tr("Copied %n object(s)", nullptr, objs.size()));
        return;
    }
    const QRect sel = m_canvas->areaSelection();
    if (!sel.isEmpty()) {
        const QImage img = docops::copySelection(m_doc->state(), sel,
                                                 all ? docops::CopyMode::All : docops::CopyMode::BaseOnly);
        setClipboard(img);
        toast->showMessage(all ? tr("Copied selection with annotations")
                               : tr("Copied selection — image only, without annotations"));
        return;
    }
    // Nothing selected: the whole result, annotations included (spec B3 note).
    copyResult();
}

void MainWindow::cut(bool all)
{
    if (m_doc->isEmpty())
        return;
    const ObjectList objs = m_canvas->toolId() == ToolId::Pointer ? m_canvas->selectedObjectList() : ObjectList();
    if (!objs.isEmpty()) {
        setClipboard(docops::renderObjects(objs), objs);
        QSet<quint64> ids;
        for (const auto &o : objs)
            ids.insert(o->id);
        m_doc->commit(docops::removeObjects(m_doc->state(), ids), tr("Cut"));
        return;
    }
    const QRect sel = m_canvas->areaSelection();
    if (sel.isEmpty()) {
        m_canvas->toast()->showMessage(tr("Nothing is selected"));
        return;
    }
    const auto mode = all ? docops::CopyMode::All : docops::CopyMode::BaseOnly;
    setClipboard(docops::copySelection(m_doc->state(), sel, mode));
    m_doc->commit(docops::cutSelection(m_doc->state(), sel, mode), tr("Cut"));
    m_canvas->toast()->showMessage(all ? tr("Cut selection with annotations")
                                       : tr("Cut selection — image only, annotations kept"));
}

void MainWindow::copyResult()
{
    if (m_doc->isEmpty())
        return;
    const QImage img = docops::composite(m_doc->state());
    setClipboard(img);
    m_canvas->toast()->showMessage(tr("Copied the whole image (%1 × %2)").arg(img.width()).arg(img.height()));
}

void MainWindow::deleteSelection()
{
    const ObjectList objs = m_canvas->selectedObjectList();
    if (!objs.isEmpty()) {
        QSet<quint64> ids;
        for (const auto &o : objs)
            ids.insert(o->id);
        m_doc->commit(docops::removeObjects(m_doc->state(), ids), tr("Delete"));
        return;
    }
    const QRect sel = m_canvas->areaSelection();
    if (!sel.isEmpty())
        m_doc->commit(docops::clearBase(m_doc->state(), sel), tr("Delete"));
}

// ================================================================ image operations

void MainWindow::transformDocument(DocState (*op)(const DocState &), const QString &text)
{
    if (m_doc->isEmpty())
        return;
    m_canvas->cancelInteraction();
    m_canvas->setAreaSelection({});
    m_doc->commit(op(m_doc->state()), text);
}

void MainWindow::cropToSelection()
{
    const QRect sel = m_canvas->areaSelection();
    if (sel.isEmpty())
        return;
    m_canvas->cancelInteraction();
    m_canvas->setAreaSelection({});
    m_doc->commit(docops::crop(m_doc->state(), sel), tr("Crop"));
}

// ================================================================ saving (spec F)

bool MainWindow::writeTo(const QString &path, const imageio::SaveOptions &options)
{
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = imageio::save(docops::composite(m_doc->state()), path, options, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        // Spec F3: the document stays intact, the target file untouched.
        QMessageBox::critical(this, tr("Save failed"), error);
        return false;
    }
    m_doc->setSaved(path, options.format);
    addRecent(path);
    settings::setLastDirectory(QFileInfo(path).absolutePath());
    settings::setLastExportFormat(options.format);
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)), 4000);
    updateTitle();
    return true;
}

bool MainWindow::save()
{
    if (m_doc->isEmpty())
        return false;
    const QString path = m_doc->filePath();
    if (path.isEmpty())
        return saveAs();
    QByteArray format = m_doc->fileFormat();
    if (format == project::kSuffix || project::isProjectFile(path))
        return saveProjectTo(path);
    if (format.isEmpty())
        format = imageio::formatForSuffix(QFileInfo(path).suffix());
    if (format.isEmpty())
        return saveAs(); // opened from a format we cannot write (yet)
    const imageio::SaveOptions options = settings::saveOptions(format);

    // Spec F2: first overwrite of the file the document was opened from.
    if (path == m_doc->originPath() && !m_doc->overwriteOriginConfirmed()) {
        QMessageBox box(QMessageBox::Question, tr("Overwrite Original"),
                        tr("Overwrite the original file “%1”? Annotations will be merged into "
                           "the image.")
                            .arg(QFileInfo(path).fileName()),
                        QMessageBox::NoButton, this);
        if (imageio::isLossy(options))
            box.setInformativeText(tr("The file will be compressed lossily again."));
        QPushButton *overwrite = box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
        QPushButton *saveAsBtn = box.addButton(tr("Save As…"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        auto *dontAsk = new QCheckBox(tr("Do not ask again for this document"), &box);
        box.setCheckBox(dontAsk);
        box.exec();
        if (box.clickedButton() == saveAsBtn)
            return saveAs();
        if (box.clickedButton() != overwrite)
            return false;
        if (dontAsk->isChecked())
            m_doc->setOverwriteOriginConfirmed(true);
    }
    return writeTo(path, options);
}

bool MainWindow::saveAs()
{
    if (m_doc->isEmpty())
        return false;
    const QList<imageio::FormatInfo> formats = imageio::exportFormats();
    if (formats.isEmpty()) {
        QMessageBox::critical(this, tr("Save As"), tr("No image format plugins are available."));
        return false;
    }
    QByteArray initialFormat = m_doc->fileFormat();
    if (initialFormat.isEmpty() && !m_doc->filePath().isEmpty())
        initialFormat = imageio::formatForSuffix(QFileInfo(m_doc->filePath()).suffix());
    if (initialFormat.isEmpty())
        initialFormat = settings::lastExportFormat();

    QStringList filters;
    QString selectedFilter;
    for (const auto &f : formats) {
        filters << imageio::nameFilter(f);
        if (f.format == initialFormat)
            selectedFilter = filters.last();
    }
    if (selectedFilter.isEmpty()) {
        selectedFilter = filters.first();
        initialFormat = formats.first().format;
    }

    QString start;
    if (!m_doc->filePath().isEmpty()) {
        const QFileInfo fi(m_doc->filePath());
        QString name = fi.completeBaseName() + QLatin1Char('.');
        for (const auto &f : formats) {
            if (f.format == initialFormat)
                name += f.suffixes.first();
        }
        start = QDir(fi.absolutePath()).filePath(name);
    } else {
        start = QDir(settings::lastDirectory())
                    .filePath(imageio::suggestedFileName(QDateTime::currentDateTime(), initialFormat));
    }

    const QString typed = QFileDialog::getSaveFileName(this, tr("Save As"), start, filters.join(QStringLiteral(";;")),
                                                       &selectedFilter, QFileDialog::DontConfirmOverwrite);
    if (typed.isEmpty())
        return false;
    QByteArray filterFormat = formats.first().format;
    for (int i = 0; i < formats.size(); ++i) {
        if (filters[i] == selectedFilter)
            filterFormat = formats[i].format;
    }
    QByteArray format;
    const QString path = imageio::resolveSavePath(typed, filterFormat, &format);

    if (QFileInfo::exists(path)
        && QMessageBox::question(this, tr("Save As"),
                                 tr("“%1” already exists. Replace it?").arg(QFileInfo(path).fileName()),
                                 QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
               != QMessageBox::Yes)
        return false;

    imageio::SaveOptions options = settings::saveOptions(format);
    const QImage result = docops::composite(m_doc->state());
    if (ExportDialog::hasOptions(format, result)) {
        ExportDialog dlg(result, options, this);
        if (dlg.exec() != QDialog::Accepted)
            return false;
        options = dlg.options();
        settings::setSaveOptions(options);
    }
    if (QFileInfo(path).absoluteFilePath() == m_doc->originPath())
        m_doc->setOverwriteOriginConfirmed(true); // explicitly chosen by the user
    return writeTo(path, options);
}

bool MainWindow::maybeSave()
{
    if (!m_doc->isModified())
        return true;
    const auto ret = QMessageBox::warning(this, tr("Unsaved Changes"),
                                          tr("The image has unsaved changes. Save them before closing?"),
                                          QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                          QMessageBox::Save);
    if (ret == QMessageBox::Save)
        return save();
    return ret == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    m_canvas->cancelInteraction();
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    // Decision J4 disabled: tell the user once that the clipboard will be emptied.
    if (Application::instance()->windowCount() == 1 && !settings::keepClipboardAfterClose()
        && clip::ownsClipboard() && !settings::clipboardLossWarned()) {
        QMessageBox::information(this, tr("Clipboard"),
                                 tr("The copied image will disappear from the clipboard when Rolidan "
                                    "quits. You can keep it available in Settings (“Keep clipboard "
                                    "contents after closing the window”)."));
        settings::setClipboardLossWarned(true);
    }
    discardRecovery(); // closed on purpose: nothing to recover
    e->accept();
}

MainWindow::~MainWindow()
{
    discardRecovery();
}

void MainWindow::onHistoryChanged()
{
    if (isSlideshowRunning() && m_doc->isModified()) {
        stopSlideshow(); // editing ends the slideshow
        m_canvas->toast()->showMessage(tr("Slideshow ended because the image was edited"));
    }
    if (needsRecovery()) {
        m_autosaveDirty = true;
        if (!m_autosaveTimer->isActive())
            m_autosaveTimer->start();
    } else
        discardRecovery(); // saved (or back to the saved state)
}

bool MainWindow::needsRecovery() const
{
    // A pasted image without a file is lost on a crash even when untouched:
    // the clipboard owner (often the screenshot tool) may be gone too.
    return !m_doc->isEmpty() && (m_doc->isModified() || m_doc->filePath().isEmpty());
}

void MainWindow::autosaveNow(bool synchronous)
{
    if (!m_autosaveDirty || !needsRecovery())
        return;
    m_autosaveDirty = false;
    const DocState state = m_doc->state(); // shared, immutable: safe to read in a thread
    const QString path = m_recoveryPath;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_autosaveCancel = cancel;
    auto job = [state, path, cancel] {
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (cancel->load())
            return;
        project::save(state, path, nullptr, true);
        if (cancel->load()) // discarded while writing
            QFile::remove(path);
    };
    if (synchronous)
        job();
    else
        QThreadPool::globalInstance()->start(job);
}

void MainWindow::discardRecovery()
{
    m_autosaveDirty = false;
    if (m_autosaveCancel)
        m_autosaveCancel->store(true);
    QFile::remove(m_recoveryPath);
}

bool MainWindow::restoreRecovery(const QString &path)
{
    DocState s;
    QString error;
    if (!project::load(path, &s, &error)) {
        m_canvas->toast()->showMessage(tr("The unsaved document could not be restored: %1").arg(error));
        return false;
    }
    const QDateTime when = QFileInfo(path).lastModified();
    m_doc->reset(DocState{s.base, {}, {}}, {});
    m_doc->commit(s, tr("Restore"));
    m_canvas->toast()->showMessage(tr("Restored the unsaved document from %1.")
                                       .arg(QLocale().toString(when, QLocale::ShortFormat)));
    QFile::remove(path);
    updateTitle();
    return true;
}

bool MainWindow::saveProjectAs()
{
    if (m_doc->isEmpty())
        return false;
    QString start;
    if (!m_doc->filePath().isEmpty())
        start = QFileInfo(m_doc->filePath()).absolutePath() + QLatin1Char('/')
                + QFileInfo(m_doc->filePath()).completeBaseName() + QLatin1Char('.') + QLatin1String(project::kSuffix);
    else
        start = QDir(settings::lastDirectory())
                    .filePath(QFileInfo(imageio::suggestedFileName(QDateTime::currentDateTime(), "png")).completeBaseName()
                              + QLatin1Char('.') + QLatin1String(project::kSuffix));
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"), start,
                                                tr("Rolidan projects (*.%1)").arg(QLatin1String(project::kSuffix)));
    if (path.isEmpty())
        return false;
    if (!project::isProjectFile(path))
        path += QLatin1Char('.') + QLatin1String(project::kSuffix);
    return saveProjectTo(path);
}

bool MainWindow::saveProjectTo(const QString &path)
{
    // Spec H1: the project keeps the original pixels under covers.
    if (project::hasCovers(m_doc->state()) && !m_coverWarned) {
        const auto answer = QMessageBox::warning(
            this, tr("Save Project"),
            tr("The project file keeps the original image under covers, so the covered data can be "
               "recovered from it. Share exported images, not project files."),
            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save);
        if (answer != QMessageBox::Save)
            return false;
        m_coverWarned = true;
    }
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = project::save(m_doc->state(), path, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, tr("Save failed"), error);
        return false;
    }
    m_doc->setSaved(path, project::kSuffix);
    addRecent(path);
    settings::setLastDirectory(QFileInfo(path).absolutePath());
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)), 4000);
    updateTitle();
    updateStatus();
    return true;
}

void MainWindow::showSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (auto *mw = qobject_cast<MainWindow *>(w)) {
                mw->document()->history().setLimitBytes(qint64(settings::historyLimitMB()) * 1024 * 1024);
                mw->a.enlargeSmall->setChecked(settings::fullScreenEnlarge());
                if (mw->m_fullScreen) {
                    mw->m_canvas->setEnlargeSmallImages(settings::fullScreenEnlarge());
                    mw->m_canvas->fitToWindow(false);
                }
            }
        }
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Rolidan"),
                       tr("<b>Rolidan %1</b><p>Fast image editor: paste, annotate, save.</p>")
                               .arg(QApplication::applicationVersion())
                           + tr("<p>Free software under the GNU General Public License version 3 "
                                "(GPL-3.0). It comes with ABSOLUTELY NO WARRANTY.</p>"
                                "<p>Built with Qt %1 (LGPL-3.0), libjpeg-turbo and libwebp (BSD).</p>")
                                 .arg(QString::fromLatin1(qVersion())));
}

void MainWindow::print()
{
    if (m_doc->isEmpty())
        return;
    if (!m_printer) {
        m_printer = std::make_unique<QPrinter>(QPrinter::HighResolution);
        m_printer->setDocName(QFileInfo(currentFile()).completeBaseName());
    }
    const DocState &s = m_doc->state();
    // What you see is what gets printed: hidden annotations stay hidden.
    const QImage full = s.objects.isEmpty() || m_canvas->objectsHidden() ? QImage() : docops::composite(s);
    PrintDialog dlg(full, s.base, m_printer.get(), this);
    dlg.exec();
}

void MainWindow::captureScreen()
{
    if (m_captureBusy)
        return;
    const CaptureOptions o = CaptureOptions::load();
    m_captureBusy = true;
    // Get out of the way: minimize on X11. On Wayland a program cannot bring
    // itself back from the taskbar, so the window is hidden and shown again.
    const bool hide = o.hideWindow && isVisible();
    const Qt::WindowStates state = windowState();
    const bool x11 = QGuiApplication::platformName() == QLatin1String("xcb");
    if (hide) {
        if (x11)
            showMinimized();
        else
            setVisible(false);
    }
    // The compositor needs a moment to remove the window (and its animation).
    const int wait = o.delaySeconds * 1000 + (hide ? 600 : 150);
    QTimer::singleShot(wait, this, [this, o, hide, state, x11] {
        auto *cap = new ScreenCapture(this);
        auto restore = [this, hide, state, x11, cap] {
            cap->deleteLater();
            m_captureBusy = false;
            if (hide) {
                if (x11) {
                    setWindowState(state & ~Qt::WindowMinimized);
                    show();
                } else {
                    setWindowState(state);
                    setVisible(true);
                }
                raise();
                activateWindow();
            }
        };
        connect(cap, &ScreenCapture::captured, this, [this, cap, o, restore](const QImage &img) {
            const bool chooseArea = o.interactive && cap->direct();
            restore();
            showCapture(img, chooseArea);
        });
        connect(cap, &ScreenCapture::failed, this, [this, restore](const QString &message) {
            restore();
            if (!message.isEmpty())
                QMessageBox::warning(this, tr("Capture Screen"), message);
        });
        cap->start(o.interactive);
    });
}

void MainWindow::showCapture(const QImage &img, bool chooseArea)
{
    const bool wasEmpty = m_doc->isEmpty();
    clip::Content c;
    c.kind = clip::Content::Kind::Image;
    c.image = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // Like Ctrl+V: a new document, or a new object in the current image.
    insertContent(c);
    if (chooseArea && wasEmpty) {
        m_canvas->setTool(ToolId::Crop);
        m_canvas->toast()->showMessage(tr("Drag to choose the area, Enter crops"));
    }
}

QWidget *MainWindow::createCaptureControls(QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *lay = new QHBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    auto *button = new QToolButton(box);
    button->setObjectName(QStringLiteral("captureButton"));
    button->setDefaultAction(a.capture);
    button->setPopupMode(QToolButton::MenuButtonPopup);
    button->setAutoRaise(true);
    if (auto *bar = qobject_cast<QToolBar *>(parent))
        button->setIconSize(bar->iconSize());
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); // say what it is
    auto *menu = new QMenu(button);
    const CaptureOptions o = CaptureOptions::load();
    auto *group = new QActionGroup(menu);
    QAction *whole = menu->addAction(tr("&Whole screen"));
    QAction *area = menu->addAction(tr("&Area or window"));
    for (QAction *x : {whole, area}) {
        x->setCheckable(true);
        group->addAction(x);
    }
    (o.interactive ? area : whole)->setChecked(true);
    area->setToolTip(ScreenCapture::canGrabDirectly() && !ScreenCapture::portalAvailable()
                         ? tr("The whole screen is captured, then the crop tool lets you choose the area")
                         : tr("The desktop lets you choose an area or a window"));
    menu->addSeparator();
    QAction *hide = menu->addAction(tr("&Minimize Rolidan while capturing"));
    hide->setCheckable(true);
    hide->setChecked(o.hideWindow);
    auto store = [whole, area, hide] {
        Q_UNUSED(whole);
        CaptureOptions c = CaptureOptions::load();
        c.interactive = area->isChecked();
        c.hideWindow = hide->isChecked();
        c.save();
    };
    connect(group, &QActionGroup::triggered, box, store);
    connect(hide, &QAction::toggled, box, store);
    button->setMenu(menu);
    lay->addWidget(button);

    auto *delay = new QSpinBox(box);
    delay->setObjectName(QStringLiteral("captureDelay"));
    delay->setRange(0, 60);
    delay->setSuffix(tr(" s"));
    delay->setSpecialValueText(tr("Now"));
    delay->setValue(o.delaySeconds);
    delay->setToolTip(tr("Delay before the screenshot is taken"));
    delay->setFocusPolicy(Qt::ClickFocus); // keys stay with the canvas
    connect(delay, &QSpinBox::valueChanged, box, [](int v) {
        CaptureOptions c = CaptureOptions::load();
        c.delaySeconds = v;
        c.save();
    });
    lay->addWidget(delay);
    return box;
}
