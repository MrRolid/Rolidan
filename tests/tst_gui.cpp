// GUI-level tests driving a real MainWindow on the offscreen platform:
// paste workflow (B1/B2), copy/cut shortcuts (B3), tools (D6), transforms (C4).
#include "application.h"
#include "canvas.h"
#include "exiffixture.h"
#include "imageinfo.h"
#include "shortcuts.h"
#include "thumbnails.h"
#include <QListView>
#include "clipboard.h"
#include "document.h"
#include "i18n.h"
#include "mainwindow.h"
#include "tools.h"
#include "printing.h"
#include "filters.h"
#include "capture.h"

#include <QPageLayout>
#include <QPrinter>
#include "toast.h"
#include "propertypanel.h"
#include "tooldefaults.h"
#include "sizedialogs.h"
#include "project.h"
#include "recovery.h"
#include "settings.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QTranslator>
#include <QMimeData>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolBar>
#include <QWidgetAction>
#include <QToolButton>
#include "exportdialog.h"
#include <QComboBox>
#include <QSlider>
#include <QRegularExpression>
#include "notespanel.h"
#include <QPlainTextEdit>
#include "filterdialog.h"
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QWheelEvent>
#include "batch.h"
#include <QSignalSpy>
#include <QTest>
#include <QTextEdit>
#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QInputMethodEvent>
#include <QSpinBox>
#include <QScrollArea>
#include <QCheckBox>
#include <QDoubleSpinBox>

namespace {
QImage solid(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}
QRgb px(const QImage &img, int x, int y)
{
    return img.convertToFormat(QImage::Format_ARGB32).pixel(x, y);
}
void putImage(const QImage &img)
{
    auto *m = new QMimeData;
    m->setImageData(img);
    QApplication::clipboard()->setMimeData(m);
}
QImage clipboardImage()
{
    return clip::decode(QApplication::clipboard()->mimeData()).image;
}
} // namespace

class TstGui : public QObject
{
    Q_OBJECT

    MainWindow *w = nullptr;
    QString shotDir;

    void key(QKeySequence seq)
    {
        const QKeyCombination k = seq[0];
        QTest::keyClick(w->canvas(), k.key(), k.keyboardModifiers());
        QCoreApplication::processEvents();
    }
    QPoint widgetPos(QPointF img) { return w->canvas()->toWidget(img).toPoint(); }
    void drag(QPointF fromImg, QPointF toImg, Qt::KeyboardModifiers mod = {})
    {
        Canvas *c = w->canvas();
        QTest::mousePress(c, Qt::LeftButton, mod, widgetPos(fromImg));
        QTest::mouseMove(c, widgetPos((fromImg + toImg) / 2));
        QTest::mouseMove(c, widgetPos(toImg));
        QTest::mouseRelease(c, Qt::LeftButton, mod, widgetPos(toImg));
    }
    void shot(const QString &name)
    {
        QCoreApplication::processEvents(); // pending layouts
        if (!shotDir.isEmpty())
            w->grab().save(shotDir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    void path(const QList<QPointF> &pts, Qt::KeyboardModifiers mod = {})
    {
        Canvas *c = w->canvas();
        QTest::mousePress(c, Qt::LeftButton, mod, widgetPos(pts.first()));
        for (qsizetype i = 1; i < pts.size(); ++i) {
            // several intermediate moves per segment, like a real mouse
            for (int k = 1; k <= 8; ++k)
                QTest::mouseMove(c, widgetPos(pts[i - 1] + (pts[i] - pts[i - 1]) * (k / 8.0)));
        }
        QTest::mouseRelease(c, Qt::LeftButton, mod, widgetPos(pts.last()));
    }
    void newDoc(int width, int height, QColor color)
    {
        putImage(solid(width, height, color));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
    }
    void tool(Qt::Key k)
    {
        key(QKeySequence(Qt::SHIFT | k)); // Shift + letter always selects a tool
    }
    QTextEdit *editor() { return w->canvas()->textTool()->editor(); }
    QImage result() { return docops::composite(w->document()->state()).convertToFormat(QImage::Format_ARGB32); }
    // Type a text with the text tool at an image position and finish with Esc.
    void typeText(QPointF at, const QString &text)
    {
        tool(Qt::Key_T);
        QTest::mouseClick(w->canvas(), Qt::LeftButton, {}, widgetPos(at));
        QVERIFY(editor());
        QTest::keyClicks(editor(), text);
        QTest::keyClick(editor(), Qt::Key_Escape);
        QVERIFY(!editor());
    }

private slots:
    void exportDialogCodecOptions()
    {
        QImage img(300, 200, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        imageio::SaveOptions o;
        o.format = "webp";
        {
            ExportDialog dlg(img, o);
            QVERIFY(dlg.webpMode());
            QTRY_VERIFY(dlg.estimateLabel()->text().contains(QRegularExpression(QStringLiteral("\\d"))));
            dlg.webpMode()->setCurrentIndex(1);
            QVERIFY(dlg.options().webp.lossless);
            QVERIFY(!dlg.qualitySlider()->isVisibleTo(&dlg) || !codecs::haveWebp());
            dlg.webpMode()->setCurrentIndex(0);
            dlg.qualitySlider()->setValue(42);
            QCOMPARE(dlg.options().webp.quality, 42);
            QVERIFY(!dlg.options().webp.lossless);
            dlg.show();
            QTRY_VERIFY(dlg.estimateLabel()->text().contains(QRegularExpression(QStringLiteral("\\d"))));
            if (!shotDir.isEmpty())
                dlg.grab().save(shotDir + QStringLiteral("/export-webp.png"));
        }
        o.format = "jpeg";
        ExportDialog dlg(img, o);
        if (codecs::haveJpeg()) {
            QVERIFY(dlg.subsampling());
            dlg.subsampling()->setCurrentIndex(0);
            QCOMPARE(dlg.options().jpeg.subsampling, 0);
        }
        dlg.show();
        QTRY_VERIFY(dlg.estimateLabel()->text().contains(QRegularExpression(QStringLiteral("\\d"))));
        if (!shotDir.isEmpty())
            dlg.grab().save(shotDir + QStringLiteral("/export-jpeg.png"));
        // Closing while an estimate runs must be safe.
        dlg.qualitySlider()->setValue(10);
        QTest::qWait(200);
    }

    void toolbarIconsAreDrawn()
    {
        // Every toolbar action has a visible icon (built-in drawings, spec: icons for tools).
        QImage sheet(10 * 56, 4 * 56, QImage::Format_ARGB32);
        sheet.fill(Qt::white);
        QPainter sp(&sheet);
        int n = 0;
        QList<QAction *> actions;
        for (QToolBar *bar : w->findChildren<QToolBar *>())
            actions += bar->actions();
        for (QToolButton *b : w->findChildren<QToolButton *>())
            if (b->defaultAction() && !actions.contains(b->defaultAction()))
                actions += b->defaultAction();
        {
            for (QAction *act : std::as_const(actions)) {
                if (act->isSeparator() || act->text().isEmpty() || qobject_cast<QWidgetAction *>(act))
                    continue;
                const QImage img = act->icon().pixmap(QSize(48, 48)).toImage();
                QVERIFY2(!img.isNull(), qPrintable(act->text()));
                int ink = 0;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        ink += qAlpha(img.pixel(x, y)) > 100;
                QVERIFY2(ink > 40, qPrintable(act->text()));
                sp.drawImage(QPoint((n % 10) * 56 + 4, (n / 10) * 56 + 4), img);
                ++n;
            }
        }
        sp.end();
        QVERIFY(n >= 30);
        if (!shotDir.isEmpty())
            sheet.save(shotDir + QStringLiteral("/icons.png"));
    }

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        shotDir = qEnvironmentVariable("ROLIDAN_SHOTS");
    }

    void init()
    {
        w = Application::instance()->newWindow();
        w->resize(1000, 700);
        QVERIFY(QTest::qWaitForWindowExposed(w));
        tooldefaults::resetAll();
    }

    void cleanup()
    {
        w->document()->reset(QImage()); // avoid the unsaved-changes dialog
        w->close();
        QCoreApplication::processEvents();
    }

    void emptyWindowThenPasteCreatesDocument()
    {
        shot(QStringLiteral("01-empty"));
        QVERIFY(w->document()->isEmpty());
        putImage(solid(640, 400, QColor(30, 120, 200)));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        QCOMPARE(w->document()->size(), QSize(640, 400));
        QVERIFY(!w->document()->isModified()); // fresh document from clipboard
        shot(QStringLiteral("02-pasted"));
    }

    void secondPasteInsertsSelectedObject()
    {
        putImage(solid(640, 400, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        putImage(solid(100, 50, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        Document *d = w->document();
        QCOMPARE(d->state().objects.size(), 1);
        QCOMPARE(d->size(), QSize(640, 400));
        QCOMPARE(w->canvas()->selectedObjects().size(), 1);
        const QRectF b = d->state().objects.first()->boundingRect();
        QCOMPARE(b.size(), QSizeF(100, 50));
        QCOMPARE(b.center(), QPointF(320, 200)); // centered in the visible area
        // third paste cascades by 16 px
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        QCOMPARE(d->state().objects.last()->boundingRect().topLeft(), b.topLeft() + QPointF(16, 16));
        shot(QStringLiteral("03-object"));
        // one undo step per paste
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QCOMPARE(d->state().objects.size(), 1);
    }

    void largerPasteOffersExpandCanvas()
    {
        putImage(solid(200, 100, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        putImage(solid(300, 50, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        QVERIFY(w->canvas()->toast()->text().contains(QStringLiteral("larger")));
        QCOMPARE(w->document()->size(), QSize(200, 100)); // nothing automatic
        shot(QStringLiteral("04-larger"));
    }

    void moveObjectWithMouseAndArrows()
    {
        putImage(solid(400, 300, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        putImage(solid(40, 40, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        Document *d = w->document();
        const QPointF tl = d->state().objects.first()->boundingRect().topLeft();
        drag(tl + QPointF(20, 20), tl + QPointF(70, 50));
        const QPointF moved = d->state().objects.first()->boundingRect().topLeft();
        QVERIFY(qAbs(moved.x() - (tl.x() + 50)) <= 1 && qAbs(moved.y() - (tl.y() + 30)) <= 1);
        QCOMPARE(moved, QPointF(std::round(moved.x()), std::round(moved.y()))); // pixel aligned
        const int steps = d->history().index();
        for (int i = 0; i < 5; ++i)
            QTest::keyClick(w->canvas(), Qt::Key_Right);
        QCOMPARE(d->state().objects.first()->boundingRect().left(), moved.x() + 5);
        QCOMPARE(d->history().index(), steps + 1); // nudges merge into one step
    }

    void copyShortcutsFollowB3()
    {
        putImage(solid(200, 200, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        putImage(solid(40, 40, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V)); // object at 80..120
        // Area selection over the object.
        key(QKeySequence(Qt::Key_M));
        QCOMPARE(w->canvas()->toolId(), ToolId::Select);
        drag(QPointF(70, 70), QPointF(130, 130));
        QCOMPARE(w->canvas()->areaSelection(), QRect(70, 70, 60, 60));
        shot(QStringLiteral("05-selection"));

        key(QKeySequence(Qt::CTRL | Qt::Key_C));
        QImage img = clipboardImage();
        QCOMPARE(img.size(), QSize(60, 60));
        QCOMPARE(px(img, 30, 30), qRgba(255, 255, 255, 255)); // image only

        key(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
        QCOMPARE(px(clipboardImage(), 30, 30), qRgba(255, 0, 0, 255)); // with annotations

        key(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
        QCOMPARE(clipboardImage().size(), QSize(200, 200)); // whole result

        key(QKeySequence(Qt::CTRL | Qt::Key_X)); // base only, object kept
        QCOMPARE(w->document()->state().objects.size(), 1);
        QCOMPARE(qAlpha(px(w->document()->state().base, 100, 100)), 0);
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        key(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X)); // all: object inside removed
        QCOMPARE(w->document()->state().objects.size(), 0);

        // Esc clears the selection; Ctrl+C then copies the whole result.
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        key(QKeySequence(Qt::Key_Escape));
        QVERIFY(w->canvas()->areaSelection().isEmpty());
        key(QKeySequence(Qt::CTRL | Qt::Key_C));
        img = clipboardImage();
        QCOMPARE(img.size(), QSize(200, 200));
        QCOMPARE(px(img, 100, 100), qRgba(255, 0, 0, 255));
        QVERIFY(clip::ownsClipboard());
    }

    void copiedObjectsPasteBackEditable()
    {
        putImage(solid(200, 200, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        putImage(solid(20, 20, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        key(QKeySequence(Qt::CTRL | Qt::Key_C)); // pointer tool, object selected
        QVERIFY(QApplication::clipboard()->mimeData()->formats().contains(QLatin1String(clip::kObjectsMime)));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        QCOMPARE(w->document()->state().objects.size(), 2);
    }

    void cropToolWithEnter()
    {
        putImage(solid(300, 200, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        putImage(solid(20, 20, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V)); // at 140..160, 90..110
        key(QKeySequence(Qt::Key_Y));
        QCOMPARE(w->canvas()->toolId(), ToolId::Crop);
        drag(QPointF(100, 50), QPointF(250, 150));
        auto *crop = static_cast<CropTool *>(w->canvas()->tool());
        QCOMPARE(crop->cropRect(), QRect(100, 50, 150, 100));
        shot(QStringLiteral("06-crop"));
        QTest::keyClick(w->canvas(), Qt::Key_Return);
        QCOMPARE(w->document()->size(), QSize(150, 100));
        QCOMPARE(w->document()->state().objects.first()->boundingRect().topLeft(), QPointF(40, 40));
        QVERIFY(w->document()->isModified());
    }

    void cropEscapeCancels()
    {
        putImage(solid(300, 200, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        key(QKeySequence(Qt::Key_Y));
        drag(QPointF(10, 10), QPointF(100, 100));
        QTest::keyClick(w->canvas(), Qt::Key_Escape);
        QTest::keyClick(w->canvas(), Qt::Key_Return);
        QCOMPARE(w->document()->size(), QSize(300, 200));
    }

    void rotateAndFlipKeepObjectsAttached()
    {
        putImage(solid(300, 200, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        putImage(solid(20, 10, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        key(QKeySequence(Qt::Key_R));
        Document *d = w->document();
        QCOMPARE(d->size(), QSize(200, 300));
        const QImage comp = docops::composite(d->state());
        const QRectF b = d->state().objects.first()->boundingRect();
        QCOMPARE(b.size(), QSizeF(10, 20));
        QCOMPARE(px(comp, int(b.center().x()), int(b.center().y())), qRgba(255, 0, 0, 255));
        key(QKeySequence(Qt::Key_L));
        QCOMPARE(d->size(), QSize(300, 200));
        QCOMPARE(d->state().objects.first()->boundingRect(), QRectF(140, 95, 20, 10));
        shot(QStringLiteral("07-rotated"));
    }

    void resizeObjectKeepsAspect()
    {
        putImage(solid(400, 400, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        putImage(solid(40, 20, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V)); // 180..220, 190..210
        drag(QPointF(220, 210), QPointF(260, 215)); // bottom-right handle
        const QRectF b = w->document()->state().objects.first()->boundingRect();
        QCOMPARE(b.topLeft(), QPointF(180, 190));
        QCOMPARE(b.width() / b.height(), 2.0);
        QCOMPARE(b.width(), 80.0);
    }

    void rotateObjectWithMouse()
    {
        putImage(solid(400, 400, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        putImage(solid(40, 20, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V)); // 180..220, 190..210, selected
        Canvas *c = w->canvas();
        const auto handle = ObjectDrag::rotateHandlePos(c, *w->document()->state().objects.first());
        QVERIFY(handle);
        const QPoint from = handle->toPoint();
        // From straight up to up-right: 45° clockwise, Shift snaps to 15° steps.
        const QPoint to = widgetPos(QPointF(243, 160));
        QTest::mousePress(c, Qt::LeftButton, Qt::ShiftModifier, from);
        for (const QPoint &pt : {(from + to) / 2, to}) { // QTest::mouseMove sends no modifiers
            QMouseEvent move(QEvent::MouseMove, QPointF(pt), c->mapToGlobal(QPointF(pt)), Qt::NoButton,
                             Qt::LeftButton, Qt::ShiftModifier);
            QCoreApplication::sendEvent(c, &move);
        }
        shot(QStringLiteral("35-rotate-object"));
        QTest::mouseRelease(c, Qt::LeftButton, Qt::ShiftModifier, to);
        ObjectPtr o = w->document()->state().objects.first();
        QVERIFY(std::abs(o->rotation() - 45.0) < 1e-6);
        QVERIFY(std::abs(o->boundingRect().center().x() - 200) < 1e-6);
        QCOMPARE(o->frameRect().size().toSize(), QSize(40, 20));
        // Resizing a rotated object keeps its angle and its proportions.
        const QPointF br = o->handlePoints()[2];
        const QPointF dir = br - o->boundingRect().center();
        drag(br, br + dir * 0.5);
        o = w->document()->state().objects.first();
        QVERIFY(std::abs(o->rotation() - 45.0) < 1e-6);
        const QSizeF f = o->frameRect().size();
        QVERIFY(std::abs(f.width() / f.height() - 2.0) < 0.05);
        QVERIFY(std::abs(f.width() - 50) < 1.0); // the corner moved by a quarter of the diagonal
        // Each drag is one undo step.
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QCOMPARE(w->document()->state().objects.first()->rotation(), 0.0);
    }

    void screenCaptureOpensForEditing()
    {
        QAction *capture = w->findChild<QAction *>(QStringLiteral("file.capture"));
        QVERIFY(capture);
        QCOMPARE(capture->shortcut(), QKeySequence(Qt::Key_C));
        // Toolbar: the button with its menu and the delay next to it.
        auto *button = w->findChild<QToolButton *>(QStringLiteral("captureButton"));
        QVERIFY(button && button->menu());
        auto *delay = w->findChild<QSpinBox *>(QStringLiteral("captureDelay"));
        QVERIFY(delay);
        delay->setValue(3);
        QCOMPARE(CaptureOptions::load().delaySeconds, 3);
        delay->setValue(0);
        QImage shot(800, 500, QImage::Format_ARGB32_Premultiplied);
        shot.fill(Qt::darkCyan);
        // An empty window takes the screenshot as its image; without the portal
        // the area is chosen afterwards with the crop tool.
        w->showCapture(shot, true);
        QCOMPARE(w->document()->state().base.size(), QSize(800, 500));
        QVERIFY(w->document()->filePath().isEmpty());
        QCOMPARE(w->canvas()->toolId(), ToolId::Crop);
        // With an image open, the screenshot is inserted like Ctrl+V.
        w->showCapture(shot, false);
        QCOMPARE(w->document()->state().objects.size(), 1);
        QSettings().remove(QStringLiteral("capture"));
    }

    void printToPdf()
    {
        newDoc(600, 300, Qt::blue);
        QVERIFY(w->findChild<QAction *>(QStringLiteral("file.print"))->isEnabled());
        QCOMPARE(w->findChild<QAction *>(QStringLiteral("file.print"))->shortcut(), QKeySequence(QKeySequence::Print));
        QTemporaryDir dir;
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(dir.filePath(QStringLiteral("out.pdf")));
        const DocState &s = w->document()->state();
        PrintDialog dlg(QImage(), s.base, &printer);
        PrintLayout l;
        l.size = PrintLayout::Size::Actual;
        l.dpi = 150;
        l.orientation = PrintLayout::Orientation::Auto;
        dlg.setPrintLayout(l);
        QCOMPARE(dlg.printLayout().dpi, 150.0);
        QCOMPARE(printer.pageLayout().orientation(), QPageLayout::Landscape); // wide image
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        if (!shotDir.isEmpty())
            dlg.grab().save(shotDir + QStringLiteral("/36-print.png"));
        dlg.printNow();
        QFile pdf(dir.filePath(QStringLiteral("out.pdf")));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.readAll().startsWith("%PDF"));
        QSettings().remove(QStringLiteral("print"));
    }

    void zoomShortcuts()
    {
        putImage(solid(4000, 3000, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        Canvas *c = w->canvas();
        QVERIFY(c->zoom() < 1.0); // large image fits on open
        key(QKeySequence(Qt::CTRL | Qt::Key_1));
        QCOMPARE(c->zoom(), 1.0);
        key(QKeySequence(Qt::CTRL | Qt::Key_0));
        QVERIFY(c->zoom() < 1.0);
        shot(QStringLiteral("08-fit"));
    }

    void builtInTranslationsAreEmbedded()
    {
        // Spec L: cs/de/es/ja work without any external files, incl. Qt's own dialogs.
        const QHash<QString, QString> expected{{"cs", "Oříznout"}, {"de", "Zuschneiden"},
                                               {"es", "Recortar"}, {"ja", "切り抜き"}};
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            QTranslator t;
            QVERIFY2(t.load(QStringLiteral("rolidan_") + it.key(), QStringLiteral(":/i18n")), qPrintable(it.key()));
            QCOMPARE(t.translate("CropTool", "Crop"), it.value());
            QTranslator qt;
            QVERIFY2(qt.load(QStringLiteral("qtbase_") + it.key(), QStringLiteral(":/i18n")), qPrintable(it.key()));
        }
    }

    void externalTranslationIsListed()
    {
        QTemporaryDir home;
        qputenv("HOME", home.path().toLocal8Bit());
        const QString dir = home.path() + QStringLiteral("/.local/share/rolidan/translations");
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(QFile::copy(QStringLiteral(":/i18n/rolidan_cs.qm"), dir + QStringLiteral("/rolidan_pl.qm")));
        bool found = false;
        for (const auto &l : i18n::available())
            found |= l.code == QLatin1String("pl") && !l.builtIn;
        QVERIFY(found);
    }

    void menuAcceleratorsAreUniqueInEveryLanguage()
    {
        auto accel = [](const QString &text) -> QChar {
            for (qsizetype i = 0; i + 1 < text.size(); ++i) {
                if (text[i] != QLatin1Char('&'))
                    continue;
                if (text[i + 1] == QLatin1Char('&')) {
                    ++i;
                    continue;
                }
                return text[i + 1].toLower();
            }
            return {};
        };
        QStringList problems;
        std::function<void(const QString &, const QList<QAction *> &, const QString &)> check;
        check = [&](const QString &lang, const QList<QAction *> &actions, const QString &where) {
            QHash<QChar, QString> seen;
            for (QAction *act : actions) {
                if (act->isSeparator() || !act->isVisible())
                    continue;
                const QChar c = accel(act->text());
                if (!c.isNull()) {
                    if (seen.contains(c))
                        problems << QStringLiteral("%1 %2: '%3' and '%4'").arg(lang, where, seen[c], act->text());
                    seen[c] = act->text();
                }
                if (act->menu())
                    check(lang, act->menu()->actions(), where + QLatin1Char('/') + act->text());
            }
        };
        QStringList langs{QStringLiteral("en")};
        langs += i18n::kBuiltIn;
        for (const QString &lang : langs) {
            QTranslator tr;
            if (lang != QLatin1String("en")) {
                QVERIFY(tr.load(QStringLiteral(":/i18n/rolidan_%1.qm").arg(lang)));
                QCoreApplication::installTranslator(&tr);
            }
            MainWindow *mw = Application::instance()->newWindow();
            check(lang, mw->menuBar()->actions(), QStringLiteral("menu bar"));
            mw->close();
            QCoreApplication::removeTranslator(&tr);
            QCoreApplication::processEvents();
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    }

    void keepsServingClipboardAfterLastWindowCloses()
    {
        // Decision J4: with the setting on (default), closing the last window while
        // Rolidan owns the clipboard keeps the process alive until someone else
        // takes the clipboard.
        putImage(solid(50, 50, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        key(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
        QVERIFY(clip::ownsClipboard());
        w->document()->reset(QImage());
        w->close();
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(Application::instance()->windowCount(), 0);
        QVERIFY(Application::instance()->isLingering());
        QApplication::clipboard()->setText(QStringLiteral("another application"));
        QCoreApplication::processEvents();
        QVERIFY(!Application::instance()->isLingering());
        w = Application::instance()->newWindow(); // for cleanup()
    }

    // ================================================================ stage 2

    void everyDrawingToolCreatesOneUndoableObject()
    {
        newDoc(600, 400, Qt::white);
        Document *d = w->document();
        struct Case { Qt::Key key; Annotation::Type type; };
        const QList<Case> cases = {{Qt::Key_A, Annotation::Type::Line}, {Qt::Key_L, Annotation::Type::Line},
                                   {Qt::Key_R, Annotation::Type::Shape}, {Qt::Key_O, Annotation::Type::Shape},
                                   {Qt::Key_B, Annotation::Type::Stroke}, {Qt::Key_H, Annotation::Type::Stroke},
                                   {Qt::Key_K, Annotation::Type::Shape}};
        int y = 30;
        for (const Case &c : cases) {
            const qsizetype before = d->state().objects.size();
            tool(c.key);
            path({QPointF(40, y), QPointF(140, y + 30), QPointF(240, y + 10)});
            QCOMPARE(d->state().objects.size(), before + 1);
            QCOMPARE(d->state().objects.last()->type(), c.type);
            QCOMPARE(w->canvas()->selectedObjects(), QSet<quint64>{d->state().objects.last()->id});
            y += 50;
        }
        const auto *arrow = static_cast<const LineObject *>(d->state().objects[0].get());
        QVERIFY(arrow->headEnd);
        QVERIFY(!static_cast<const LineObject *>(d->state().objects[1].get())->headEnd);
        QVERIFY(static_cast<const StrokeObject *>(d->state().objects[5].get())->highlighter);
        QCOMPARE(static_cast<const ShapeObject *>(d->state().objects[6].get())->style, ShapeObject::Style::Cover);
        shot(QStringLiteral("10-tools"));
        for (qsizetype i = cases.size(); i > 0; --i)
            key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QVERIFY(d->state().objects.isEmpty());
    }

    void shiftConstrainsAngleAndSquare()
    {
        newDoc(400, 400, Qt::white);
        tool(Qt::Key_L);
        drag(QPointF(50, 50), QPointF(250, 70), Qt::ShiftModifier);
        const auto *l = static_cast<const LineObject *>(w->document()->state().objects.last().get());
        QCOMPARE(l->p2.y(), l->p1.y()); // 5.7° snaps to 0°
        tool(Qt::Key_R);
        drag(QPointF(50, 100), QPointF(150, 160), Qt::ShiftModifier);
        const auto *r = static_cast<const ShapeObject *>(w->document()->state().objects.last().get());
        QCOMPARE(r->rect.width(), r->rect.height());
    }

    void coverIsFullyOpaque()
    {
        newDoc(200, 100, Qt::red);
        tool(Qt::Key_K);
        drag(QPointF(20, 20), QPointF(120, 80));
        const QImage img = result();
        for (QPoint p : {QPoint(21, 21), QPoint(70, 50), QPoint(119, 79)})
            QCOMPARE(img.pixel(p), qRgba(0, 0, 0, 255));
        QCOMPARE(img.pixel(19, 50), qRgba(255, 0, 0, 255)); // pixel-aligned edge
        QCOMPARE(img.pixel(120, 50), qRgba(255, 0, 0, 255));
    }

    void highlighterHasOneToneWhereStrokeOverlapsItself()
    {
        // Spec D4: a stroke crossing itself must not get darker at the crossing.
        newDoc(200, 120, Qt::white);
        tool(Qt::Key_H);
        path({QPointF(20, 60), QPointF(180, 60), QPointF(100, 15), QPointF(100, 105)});
        const QImage img = result();
        const QRgb single = img.pixel(40, 60);
        QVERIFY(single != qRgba(255, 255, 255, 255));
        QCOMPARE(img.pixel(100, 60), single); // crossing
        QCOMPARE(img.pixel(100, 95), single);
        // Multiply: dark content under the highlighter stays dark.
        shot(QStringLiteral("11-highlighter"));
    }

    void typoFixedByDoubleClickAfterFiveOtherEdits()
    {
        newDoc(600, 400, Qt::white);
        Document *d = w->document();
        typeText(QPointF(50, 50), QStringLiteral("Helo"));
        QCOMPARE(d->state().objects.size(), 1);
        const quint64 textId = d->state().objects.first()->id;
        tool(Qt::Key_R);
        for (int i = 0; i < 5; ++i)
            drag(QPointF(50 + i * 100, 200), QPointF(120 + i * 100, 260));
        QCOMPARE(d->state().objects.size(), 6);

        tool(Qt::Key_V);
        QTest::mouseDClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(62, 62)));
        QVERIFY(editor());
        QCOMPARE(editor()->toPlainText(), QStringLiteral("Helo"));
        editor()->selectAll();
        QTest::keyClicks(editor(), QStringLiteral("Hello"));
        QTest::keyClick(editor(), Qt::Key_Escape);
        QVERIFY(!editor());
        auto text = [&] {
            return static_cast<const TextObject *>(docops::findObject(d->state(), textId).get())->text;
        };
        QCOMPARE(text(), QStringLiteral("Hello"));
        QCOMPARE(d->state().objects.size(), 6);
        // The fix is one undo step; the five rectangles stay.
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QCOMPARE(text(), QStringLiteral("Helo"));
        QCOMPARE(d->state().objects.size(), 6);
    }

    void textAcceptsInputMethodAndDeadKeys()
    {
        newDoc(400, 200, Qt::white);
        tool(Qt::Key_T);
        QTest::mouseClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(30, 30)));
        QVERIFY(editor());
        {   // IME (fcitx/ibus): preedit, then commit
            QInputMethodEvent pre(QStringLiteral("にほんご"), {});
            QCoreApplication::sendEvent(editor(), &pre);
            QInputMethodEvent commit;
            commit.setCommitString(QStringLiteral("日本語"));
            QCoreApplication::sendEvent(editor(), &commit);
        }
        {   // dead key / compose: ´ + e = é
            QInputMethodEvent pre(QStringLiteral("´"), {});
            QCoreApplication::sendEvent(editor(), &pre);
            QInputMethodEvent commit;
            commit.setCommitString(QStringLiteral("é"));
            QCoreApplication::sendEvent(editor(), &commit);
        }
        QTest::keyClick(editor(), Qt::Key_Return); // Enter = new line
        {   // non-ASCII key with text (Czech keyboard)
            QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral("ž"));
            QCoreApplication::sendEvent(editor(), &press);
        }
        QTest::keyClick(editor(), Qt::Key_Return, Qt::ControlModifier); // Ctrl+Enter finishes
        QVERIFY(!editor());
        const auto *t = static_cast<const TextObject *>(w->document()->state().objects.last().get());
        QCOMPARE(t->text, QStringLiteral("日本語é\nž"));
        shot(QStringLiteral("12-text"));
    }

    void formatsSelectedCharacters()
    {
        newDoc(500, 200, Qt::white);
        typeText(QPointF(30, 30), QStringLiteral("one two three"));
        tool(Qt::Key_V);
        QTest::mouseDClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(36, 40)));
        QVERIFY(editor());
        QTextCursor c = editor()->textCursor();
        c.setPosition(4);
        c.setPosition(7, QTextCursor::KeepAnchor);
        editor()->setTextCursor(c);
        QTest::keyClick(editor(), Qt::Key_B, Qt::ControlModifier);
        QTest::keyClick(editor(), Qt::Key_U, Qt::ControlModifier);
        // A color from the panel goes to the selection only.
        w->canvas()->applyProperty(Prop::Color, QColor(0, 0, 255));
        QTest::keyClick(editor(), Qt::Key_Escape);
        QVERIFY(!editor());
        const auto *t = static_cast<const TextObject *>(w->document()->state().objects.last().get());
        QCOMPARE(t->text, QStringLiteral("one two three"));
        QVERIFY(t->bold); // text is bold by default; the selection is not
        QCOMPARE(t->color.blue() == 255, false); // the whole text keeps its color
        QCOMPARE(t->spans.size(), 1);
        QCOMPARE(t->spans[0].start, 4);
        QCOMPARE(t->spans[0].length, 3);
        QCOMPARE(int(t->spans[0].set), TextSpan::Bold | TextSpan::Underline | TextSpan::Color);
        QCOMPARE(int(t->spans[0].on), int(TextSpan::Underline));
        QCOMPARE(t->spans[0].color, QColor(0, 0, 255));
        QCOMPARE(tooldefaults::value(ToolId::Text, Prop::Bold).toBool(), true); // defaults untouched
        shot(QStringLiteral("13-text-spans"));
        // Undo returns to the unformatted text.
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QVERIFY(static_cast<const TextObject *>(w->document()->state().objects.last().get())->spans.isEmpty());
        // Without a selection Ctrl+B switches the whole text.
        QTest::mouseDClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(36, 40)));
        QVERIFY(editor());
        QTest::keyClick(editor(), Qt::Key_B, Qt::ControlModifier);
        QTest::keyClick(editor(), Qt::Key_Escape);
        t = static_cast<const TextObject *>(w->document()->state().objects.last().get());
        QVERIFY(!t->bold);
        QVERIFY(t->spans.isEmpty());
        tooldefaults::setValue(ToolId::Text, Prop::Bold, true);
    }

    void emptiedTextIsDeleted()
    {
        newDoc(300, 200, Qt::white);
        typeText(QPointF(30, 30), QStringLiteral("x"));
        QCOMPARE(w->document()->state().objects.size(), 1);
        tool(Qt::Key_V);
        QTest::mouseDClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(36, 40)));
        QVERIFY(editor());
        editor()->clear();
        QTest::keyClick(editor(), Qt::Key_Escape);
        QVERIFY(w->document()->state().objects.isEmpty());
    }

    void spinBoxChangesAreOneUndoStep()
    {
        newDoc(400, 300, Qt::white);
        tool(Qt::Key_R);
        drag(QPointF(50, 50), QPointF(200, 150));
        Document *d = w->document();
        auto width = [&] { return static_cast<const ShapeObject *>(d->state().objects.last().get())->width; };
        QCOMPARE(width(), 3.0);
        auto *spin = w->findChild<QSpinBox *>(QStringLiteral("width"));
        QVERIFY(spin);
        for (int v = 4; v <= 12; ++v)
            spin->setValue(v); // what dragging / holding the arrow does
        QCOMPARE(width(), 12.0);
        QCOMPARE(tooldefaults::value(ToolId::Rect, Prop::Width).toDouble(), 12.0); // remembered
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QCOMPARE(width(), 3.0);
        QCOMPARE(d->state().objects.size(), 1);
    }

    void pointerPanelEditsSelectedObjects()
    {
        newDoc(400, 300, Qt::white);
        tool(Qt::Key_A);
        drag(QPointF(50, 50), QPointF(200, 150));
        tool(Qt::Key_V);
        auto *spin = w->findChild<QSpinBox *>(QStringLiteral("width"));
        QVERIFY(spin); // panel of the selected arrow
        QCOMPARE(spin->value(), 4);
        spin->setValue(9);
        QCOMPARE(static_cast<const LineObject *>(w->document()->state().objects.last().get())->width, 9.0);
        key(QKeySequence(Qt::Key_Escape));
        QVERIFY(!w->findChild<QSpinBox *>(QStringLiteral("width")));
    }

    void transformsKeepEveryObjectTypeAttached()
    {
        // Spec C4 for every object type: rotate and flip move objects with the image.
        newDoc(400, 300, Qt::white);
        Document *d = w->document();
        typeText(QPointF(30, 30), QStringLiteral("Text"));
        tool(Qt::Key_A); drag(QPointF(150, 40), QPointF(250, 90));
        tool(Qt::Key_R); drag(QPointF(40, 120), QPointF(120, 180));
        tool(Qt::Key_O); drag(QPointF(150, 120), QPointF(260, 200));
        tool(Qt::Key_B); path({QPointF(280, 40), QPointF(350, 80), QPointF(300, 150)});
        tool(Qt::Key_H); path({QPointF(40, 250), QPointF(200, 250)});
        tool(Qt::Key_K); drag(QPointF(300, 200), QPointF(380, 280));
        putImage(solid(30, 20, Qt::blue));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        QCOMPARE(d->state().objects.size(), 8);
        const DocState orig = d->state();
        auto centers = [&] {
            QList<QPointF> c;
            for (const auto &o : d->state().objects)
                c.append(o->visualRect().center());
            return c;
        };
        const QList<QPointF> before = centers();
        auto near = [](QPointF a, QPointF b) { return QLineF(a, b).length() < 1.5; };

        key(QKeySequence(Qt::Key_R)); // rotate right: (x, y) -> (h - y, x)
        QCOMPARE(d->size(), QSize(300, 400));
        QList<QPointF> after = centers();
        for (int i = 0; i < before.size(); ++i)
            QVERIFY2(near(after[i], QPointF(300 - before[i].y(), before[i].x())), qPrintable(QString::number(i)));
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));

        d->commit(docops::flip(d->state(), Qt::Horizontal), QStringLiteral("flip"));
        after = centers();
        for (int i = 0; i < before.size(); ++i)
            QVERIFY2(near(after[i], QPointF(400 - before[i].x(), before[i].y())), qPrintable(QString::number(i)));
        // Text is never mirrored (decision J2), only moved.
        const auto &text = d->state().objects.first();
        QCOMPARE(text->type(), Annotation::Type::Text);
        QVERIFY(text->transform.determinant() > 0);
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QCOMPARE(centers(), before);
        QCOMPARE(d->state().objects, orig.objects);
    }

    void exportMatchesViewAtActualSize()
    {
        // Spec D: what you see at 100 % is what gets exported.
        newDoc(300, 200, QColor(240, 240, 240));
        typeText(QPointF(20, 20), QStringLiteral("Export"));
        tool(Qt::Key_A); drag(QPointF(40, 150), QPointF(250, 60));
        tool(Qt::Key_H); path({QPointF(20, 120), QPointF(280, 130)});
        tool(Qt::Key_O); drag(QPointF(150, 20), QPointF(280, 100));
        tool(Qt::Key_V);
        w->canvas()->setSelectedObjects({});
        w->canvas()->actualSize();
        QCoreApplication::processEvents();
        QCOMPARE(w->canvas()->scale(), 1.0);
        const QImage view = w->canvas()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const QPoint o = w->canvas()->toWidget(QPointF(0, 0)).toPoint();
        const QImage exported = result();
        int diff = 0;
        for (int y = 0; y < exported.height(); ++y)
            for (int x = 0; x < exported.width(); ++x) {
                const QRgb a = exported.pixel(x, y), b = view.pixel(o.x() + x, o.y() + y);
                if (std::abs(qRed(a) - qRed(b)) > 2 || std::abs(qGreen(a) - qGreen(b)) > 2
                    || std::abs(qBlue(a) - qBlue(b)) > 2)
                    ++diff;
            }
        QVERIFY2(diff == 0, qPrintable(QStringLiteral("%1 pixels differ").arg(diff)));
    }

    void arrangeDuplicateAndMerge()
    {
        newDoc(300, 200, Qt::white);
        Document *d = w->document();
        tool(Qt::Key_R); drag(QPointF(20, 20), QPointF(100, 100));
        tool(Qt::Key_O); drag(QPointF(60, 60), QPointF(160, 160));
        const quint64 rect = d->state().objects[0]->id, ellipse = d->state().objects[1]->id;
        tool(Qt::Key_V);
        w->canvas()->setSelectedObjects({rect});
        key(QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
        QCOMPARE(d->state().objects.last()->id, rect);
        key(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft));
        QCOMPARE(d->state().objects.first()->id, rect);
        key(QKeySequence(Qt::CTRL | Qt::Key_PageUp));
        QCOMPARE(d->state().objects.last()->id, rect);

        key(QKeySequence(Qt::CTRL | Qt::Key_D));
        QCOMPARE(d->state().objects.size(), 3);
        const auto &copy = d->state().objects.last();
        QVERIFY(copy->id != rect && copy->id != ellipse);
        QCOMPARE(w->canvas()->selectedObjects(), QSet<quint64>{copy->id});
        QCOMPARE(copy->visualRect().topLeft(),
                 docops::findObject(d->state(), rect)->visualRect().topLeft() + QPointF(16, 16));

        // Merge the bottom object only (looks the same), then everything.
        QCOMPARE(d->state().objects.first()->id, ellipse);
        w->canvas()->setSelectedObjects({ellipse});
        const QImage before = result();
        for (QAction *a : w->findChildren<QAction *>()) {
            if (a->text() == QStringLiteral("&Merge Selected into Image"))
                a->trigger();
        }
        QCOMPARE(d->state().objects.size(), 2);
        QCOMPARE(result(), before); // looks the same, but is now pixels
        for (QAction *a : w->findChildren<QAction *>()) {
            if (a->text() == QStringLiteral("Merge &All Annotations into Image"))
                a->trigger();
        }
        QVERIFY(d->state().objects.isEmpty());
        QCOMPARE(result(), before);
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QCOMPARE(d->state().objects.size(), 2);
    }

    void deleteWorksInAnyTool()
    {
        newDoc(300, 200, Qt::white);
        tool(Qt::Key_R);
        drag(QPointF(20, 20), QPointF(100, 100));
        QCOMPARE(w->document()->state().objects.size(), 1);
        key(QKeySequence(Qt::Key_Delete)); // still in the rectangle tool
        QVERIFY(w->document()->state().objects.isEmpty());
    }

    void eyedropperFeedsPreviousTool()
    {
        QImage img = solid(200, 100, Qt::white);
        QPainter(&img).fillRect(100, 0, 100, 100, QColor(10, 120, 200));
        putImage(img);
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        tool(Qt::Key_R);
        tool(Qt::Key_I);
        QTest::mouseClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(150, 50)));
        QCOMPARE(w->canvas()->toolId(), ToolId::Rect);
        QCOMPARE(tooldefaults::value(ToolId::Rect, Prop::Color).value<QColor>(), QColor(10, 120, 200));
        drag(QPointF(20, 20), QPointF(80, 80));
        QCOMPARE(static_cast<const ShapeObject *>(w->document()->state().objects.last().get())->color,
                 QColor(10, 120, 200));
    }

    void toolDefaultsSurviveRestart()
    {
        tooldefaults::setValue(ToolId::Arrow, Prop::Color, QColor(1, 2, 3));
        tooldefaults::setValue(ToolId::Arrow, Prop::Width, 7.0);
        QSettings().sync();
        QSettings fresh;
        QCOMPARE(fresh.value(QStringLiteral("tools/arrow/color")).toString(), QStringLiteral("#ff010203"));
        QCOMPARE(tooldefaults::value(ToolId::Arrow, Prop::Color).value<QColor>(), QColor(1, 2, 3));
        QCOMPARE(tooldefaults::value(ToolId::Arrow, Prop::Width).toDouble(), 7.0);
        QCOMPARE(tooldefaults::value(ToolId::Line, Prop::Width).toDouble(), 4.0); // per tool
    }

    // ================================================================ stage 3

    QImage patternImage(int w, int h)
    {
        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                img.setPixel(x, y, qRgba(x % 256, y % 256, (x * 7 + y * 3) % 256, 255));
        return img;
    }
    QAction *action(const QString &text)
    {
        for (QAction *a : w->findChildren<QAction *>())
            if (a->text() == text)
                return a;
        return nullptr;
    }

    void cloneStampAlignedAndNonAligned()
    {
        putImage(patternImage(300, 300));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        tooldefaults::setValue(ToolId::Clone, Prop::Size, 1.0);
        tooldefaults::setValue(ToolId::Clone, Prop::Hardness, 1.0);
        tooldefaults::setValue(ToolId::Clone, Prop::Opacity, 1.0);
        tooldefaults::setValue(ToolId::Clone, Prop::Aligned, true);
        tool(Qt::Key_S);
        QCOMPARE(w->canvas()->toolId(), ToolId::Clone);
        Document *d = w->document();
        const QImage orig = d->state().base;
        auto px = [&](int x, int y) { return d->state().base.pixel(x, y); };
        auto click = [&](QPointF p, Qt::KeyboardModifiers m = {}) {
            QTest::mouseClick(w->canvas(), Qt::LeftButton, m, widgetPos(p));
        };
        // Without a source: a hint, nothing changes.
        click(QPointF(100.25, 100.25));
        QVERIFY(w->canvas()->toast()->text().contains(QStringLiteral("source")));
        QVERIFY(!d->canUndo() || d->state().base == orig);

        click(QPointF(20.25, 20.25), Qt::ControlModifier); // source
        click(QPointF(100.25, 100.25));                     // offset (-80, -80)
        QCOMPARE(px(100, 100), orig.pixel(20, 20));
        // Aligned: the next stroke keeps the offset.
        click(QPointF(150.25, 150.25));
        QCOMPARE(px(150, 150), orig.pixel(70, 70));
        // One stroke = one undo step.
        key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QCOMPARE(px(150, 150), orig.pixel(150, 150));
        QCOMPARE(px(100, 100), orig.pixel(20, 20));

        // Not aligned: every stroke starts at the original source.
        tooldefaults::setValue(ToolId::Clone, Prop::Aligned, false);
        click(QPointF(200.25, 200.25));
        QCOMPARE(px(200, 200), orig.pixel(20, 20));
        click(QPointF(250.25, 120.25));
        QCOMPARE(px(250, 120), orig.pixel(20, 20));
        // Right click also sets the source.
        QTest::mouseClick(w->canvas(), Qt::RightButton, {}, widgetPos(QPointF(5.25, 5.25)));
        click(QPointF(60.25, 60.25));
        QCOMPARE(px(60, 60), orig.pixel(5, 5));
    }

    void cloneStrokeSourceIsBaseBeforeStroke()
    {
        // Spec C3: the stamp reads the base as it was before the stroke, so a
        // stroke running into its own source does not repeat itself.
        QImage img(200, 20, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter(&img).fillRect(0, 0, 10, 20, Qt::red);
        putImage(img);
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        tooldefaults::setValue(ToolId::Clone, Prop::Size, 1.0);
        tooldefaults::setValue(ToolId::Clone, Prop::Hardness, 1.0);
        tool(Qt::Key_S);
        QTest::mouseClick(w->canvas(), Qt::LeftButton, Qt::ControlModifier, widgetPos(QPointF(0.25, 10.25)));
        path({QPointF(10.25, 10.25), QPointF(60.25, 10.25)});
        const QImage b = w->document()->state().base;
        QCOMPARE(b.pixel(15, 10), qRgba(255, 0, 0, 255)); // copied red (source x = 5)
        QCOMPARE(b.pixel(40, 10), qRgba(255, 255, 255, 255)); // source x = 30 was white before
    }

    void eraserMakesBaseTransparentAndKeepsObjects()
    {
        newDoc(200, 100, Qt::white);
        tool(Qt::Key_R);
        drag(QPointF(10, 10), QPointF(60, 60));
        tooldefaults::setValue(ToolId::Eraser, Prop::Hardness, 1.0);
        tool(Qt::Key_E);
        QVERIFY(w->canvas()->selectedObjects().isEmpty());
        path({QPointF(100, 50), QPointF(150, 50)});
        const QImage b = w->document()->state().base;
        QCOMPARE(qAlpha(b.pixel(125, 50)), 0);
        QCOMPARE(qAlpha(b.pixel(125, 10)), 255);
        QCOMPARE(w->document()->state().objects.size(), 1);
        shot(QStringLiteral("20-eraser"));
    }

    void bracketsChangeBrushSize()
    {
        newDoc(200, 100, Qt::white);
        tool(Qt::Key_E);
        const double before = tooldefaults::value(ToolId::Eraser, Prop::Size).toDouble();
        QTest::keyClick(w->canvas(), Qt::Key_BracketRight);
        const double larger = tooldefaults::value(ToolId::Eraser, Prop::Size).toDouble();
        QVERIFY(larger > before);
        auto *spin = w->findChild<QSpinBox *>(QStringLiteral("size"));
        QVERIFY(spin);
        QCOMPARE(spin->value(), int(larger));
        QTest::keyClick(w->canvas(), Qt::Key_BracketLeft);
        QTest::keyClick(w->canvas(), Qt::Key_BracketLeft);
        QVERIFY(tooldefaults::value(ToolId::Eraser, Prop::Size).toDouble() < before);
        QVERIFY(w->findChild<QSpinBox *>(QStringLiteral("hardness")));
        QVERIFY(w->findChild<QSpinBox *>(QStringLiteral("opacity")));
    }

    void hideAnnotationsIsViewOnly()
    {
        newDoc(200, 100, Qt::white);
        tool(Qt::Key_K);
        drag(QPointF(10, 10), QPointF(100, 90));
        const int steps = w->document()->history().index();
        key(QKeySequence(Qt::CTRL | Qt::Key_H));
        QVERIFY(w->canvas()->objectsHidden());
        const QImage view = w->canvas()->grab().toImage();
        const QPoint c = widgetPos(QPointF(50, 50));
        QCOMPARE(QColor(view.pixel(c)), QColor(Qt::white)); // the cover is not drawn
        QCOMPARE(w->document()->history().index(), steps);
        QCOMPARE(w->document()->state().objects.size(), 1);
        QCOMPARE(qRed(result().pixel(50, 50)), 0); // export unaffected
        key(QKeySequence(Qt::CTRL | Qt::Key_H));
        QVERIFY(!w->canvas()->objectsHidden());
    }

    void selectionBlurPixelateCover()
    {
        putImage(patternImage(200, 200));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        Document *d = w->document();
        const QImage orig = d->state().base;
        tool(Qt::Key_M);
        QVERIFY(w->findChild<QSpinBox *>(QStringLiteral("tools/select/blurRadius")));
        drag(QPointF(40, 40), QPointF(100, 100));
        QVERIFY(!w->canvas()->areaSelection().isEmpty());
        action(QStringLiteral("&Pixelate Selection"))->trigger();
        QVERIFY(d->state().base.pixel(60, 60) != orig.pixel(60, 60));
        QCOMPARE(d->state().base.pixel(150, 150), orig.pixel(150, 150));
        action(QStringLiteral("&Blur Selection"))->trigger();
        shot(QStringLiteral("21-select"));
        action(QStringLiteral("C&over Selection"))->trigger();
        QCOMPARE(d->state().objects.size(), 1);
        QCOMPARE(d->state().objects.first()->boundingRect(), QRectF(w->canvas()->areaSelection()));
        QCOMPARE(result().pixel(70, 70), qRgba(0, 0, 0, 255));
        for (int i = 0; i < 3; ++i)
            key(QKeySequence(Qt::CTRL | Qt::Key_Z));
        QCOMPARE(d->state().base, orig);
    }

    void resizeAndCanvasDialogs()
    {
        newDoc(200, 100, Qt::white);
        QSettings().remove(QStringLiteral("resize"));
        ResizeDialog rd(QSize(200, 100), 0, QSize(1920, 1080));
        rd.widthPercent()->setValue(50);
        QCOMPARE(rd.newSize(), QSize(100, 50));
        QCOMPARE(rd.heightPercent()->value(), 50.0); // aspect kept
        rd.widthSpin()->setValue(300);
        QCOMPARE(rd.heightSpin()->value(), 150.0);
        QCOMPARE(rd.widthPercent()->value(), 150.0);
        rd.keepAspect()->setChecked(false);
        rd.heightSpin()->setValue(40);
        QCOMPARE(rd.newSize(), QSize(300, 40));
        // Percent of width and height separately.
        rd.widthPercent()->setValue(25);
        rd.heightPercent()->setValue(200);
        QCOMPARE(rd.newSize(), QSize(50, 200));
        // Units: 96 dpi, 2.54 cm = 96 px.
        rd.unitCombo()->setCurrentIndex(ResizeDialog::Centimeters);
        rd.widthSpin()->setValue(2.54);
        QCOMPARE(rd.newSize().width(), 96);
        rd.unitCombo()->setCurrentIndex(ResizeDialog::Pixels);
        // Presets: fit inside 1920 × 1080 with the aspect ratio kept, half, screen.
        rd.keepAspect()->setChecked(true);
        rd.applyPreset(rd.presetCombo()->findText(QStringLiteral("1920 × 1080 (Full HD)")));
        QCOMPARE(rd.newSize(), QSize(1920, 960));
        rd.applyPreset(rd.presetCombo()->findData(int(ResizeDialog::Half)));
        QCOMPARE(rd.newSize(), QSize(100, 50)); // half of the original, like IrfanView
        QCOMPARE(rd.widthPercent()->value(), 50.0);
        rd.applyPreset(rd.presetCombo()->findData(int(ResizeDialog::FitScreen)));
        QCOMPARE(rd.newSize(), QSize(1920, 960));
        rd.applyPreset(rd.presetCombo()->findData(int(ResizeDialog::ScreenSize)));
        QCOMPARE(rd.newSize(), QSize(1920, 1080));
        QCOMPARE(rd.newDpi(), 0.0);
        QCOMPARE(rd.filterCombo()->count(), 7); // like IrfanView
        rd.filterCombo()->setCurrentIndex(rd.filterCombo()->findData(int(filters::Resample::Bell)));
        QCOMPARE(rd.filter(), int(filters::Resample::Bell));
        rd.dpiSpin()->setValue(300);
        QCOMPARE(rd.newDpi(), 300.0);
        if (!shotDir.isEmpty()) {
            rd.show();
            QVERIFY(QTest::qWaitForWindowExposed(&rd));
            rd.grab().save(shotDir + QStringLiteral("/37-resize.png"));
        }

        CanvasSizeDialog cd(QSize(200, 100));
        cd.widthSpin()->setValue(300);
        cd.heightSpin()->setValue(160);
        QCOMPARE(cd.offset(), QPoint(50, 30)); // centered by default
        cd.setAnchor(0);
        QCOMPARE(cd.offset(), QPoint(0, 0));
        cd.setAnchor(8);
        QCOMPARE(cd.offset(), QPoint(100, 60));
        QCOMPARE(cd.fill().alpha(), 0); // transparent by default
    }

    // ================================================================ stage 4

    void markersNumberThemselves()
    {
        newDoc(400, 200, Qt::white);
        tool(Qt::Key_N);
        Document *d = w->document();
        for (int i = 0; i < 3; ++i)
            QTest::mouseClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(50 + i * 60, 100)));
        auto number = [&](int i) { return static_cast<const MarkerObject *>(d->state().objects[i].get())->number; };
        QCOMPARE(d->state().objects.size(), 3);
        QCOMPARE(number(0), 1);
        QCOMPARE(number(2), 3);
        // The panel of a selected marker can change its number.
        tool(Qt::Key_V);
        w->canvas()->setSelectedObjects({d->state().objects[1]->id});
        auto *spin = w->findChild<QSpinBox *>(QStringLiteral("number"));
        QVERIFY(spin);
        spin->setValue(9);
        QCOMPARE(number(1), 9);
        tool(Qt::Key_N);
        QTest::mouseClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(300, 150)));
        QCOMPARE(number(3), 10); // next after the highest
        shot(QStringLiteral("30-markers"));
    }

    void polylineAndPolygonTools()
    {
        newDoc(400, 300, Qt::white);
        Document *d = w->document();
        Canvas *c = w->canvas();
        tool(Qt::Key_P);
        QTest::mouseClick(c, Qt::LeftButton, {}, widgetPos(QPointF(20, 20)));
        QTest::mouseMove(c, widgetPos(QPointF(100, 40)));
        QTest::mouseClick(c, Qt::LeftButton, {}, widgetPos(QPointF(100, 40)));
        QTest::mouseClick(c, Qt::LeftButton, {}, widgetPos(QPointF(150, 120)));
        QCOMPARE(d->state().objects.size(), 1); // live preview (not committed yet)
        QVERIFY(!d->canUndo());
        QTest::mouseDClick(c, Qt::LeftButton, {}, widgetPos(QPointF(200, 100)));
        QCOMPARE(d->state().objects.size(), 1);
        const auto *pl = static_cast<const PolylineObject *>(d->state().objects.first().get());
        QCOMPARE(pl->points.size(), 4);
        QVERIFY(!pl->closed);
        QVERIFY(d->canUndo());

        // Backspace removes the last point, Enter finishes; Esc cancels.
        QTest::mouseClick(c, Qt::LeftButton, {}, widgetPos(QPointF(20, 200)));
        QTest::mouseClick(c, Qt::LeftButton, {}, widgetPos(QPointF(60, 220)));
        QTest::mouseClick(c, Qt::LeftButton, {}, widgetPos(QPointF(90, 200)));
        key(QKeySequence(Qt::Key_Backspace));
        key(QKeySequence(Qt::Key_Return));
        QCOMPARE(d->state().objects.size(), 2);
        QCOMPARE(static_cast<const PolylineObject *>(d->state().objects.last().get())->points.size(), 2);
        QTest::mouseClick(c, Qt::LeftButton, {}, widgetPos(QPointF(300, 200)));
        QTest::mouseClick(c, Qt::LeftButton, {}, widgetPos(QPointF(350, 220)));
        key(QKeySequence(Qt::Key_Escape));
        QCOMPARE(d->state().objects.size(), 2);

        // Polygon: clicking the first point closes it.
        tool(Qt::Key_G);
        for (QPointF p : {QPointF(250, 30), QPointF(350, 40), QPointF(320, 120), QPointF(250, 30)})
            QTest::mouseClick(c, Qt::LeftButton, {}, widgetPos(p));
        QCOMPARE(d->state().objects.size(), 3);
        const auto *pg = static_cast<const PolylineObject *>(d->state().objects.last().get());
        QVERIFY(pg->closed);
        QCOMPARE(pg->points.size(), 3);

        // Every point has a handle.
        tool(Qt::Key_V);
        c->setSelectedObjects({pg->id});
        drag(QPointF(350, 40), QPointF(380, 60));
        QCOMPARE(static_cast<const PolylineObject *>(d->state().objects.last().get())->points[1], QPointF(380, 60));
        shot(QStringLiteral("31-polyline"));
    }

    void bubbleToolPointsTheTail()
    {
        newDoc(400, 300, Qt::white);
        tool(Qt::Key_Q);
        drag(QPointF(60, 250), QPointF(200, 60));
        QVERIFY(editor());
        QTest::keyClicks(editor(), QStringLiteral("Hello"));
        QTest::keyClick(editor(), Qt::Key_Escape);
        const auto *t = static_cast<const TextObject *>(w->document()->state().objects.last().get());
        QVERIFY(t->bubble);
        QCOMPARE(t->tail, QPointF(60, 250));
        QCOMPARE(t->text, QStringLiteral("Hello"));
        QVERIFY(t->visualRect().contains(QPointF(60, 249))); // tail is part of the object
        QCOMPARE(result().pixel(205, 54), qRgba(255, 255, 255, 255)); // white body above the text
        shot(QStringLiteral("32-bubble"));
    }

    void pasteIntoSelectionFitsAndStretches()
    {
        newDoc(400, 300, Qt::white);
        tool(Qt::Key_M);
        drag(QPointF(100, 100), QPointF(300, 200)); // 200 x 100
        putImage(solid(50, 50, Qt::red));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        Document *d = w->document();
        QCOMPARE(d->state().objects.size(), 1);
        QCOMPARE(d->state().objects.first()->boundingRect(), QRectF(150, 100, 100, 100)); // fitted, centered
        QVERIFY(w->canvas()->toast()->text().contains(QStringLiteral("aspect")));
        w->canvas()->toast()->findChild<QPushButton *>()->click();
        QCOMPARE(d->state().objects.first()->boundingRect(), QRectF(100, 100, 200, 100)); // stretched
    }

    void projectSaveAndReopen()
    {
        newDoc(300, 200, Qt::white);
        tool(Qt::Key_A);
        drag(QPointF(20, 20), QPointF(200, 150));
        tool(Qt::Key_N);
        QTest::mouseClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(250, 50)));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("projekt ž.rolidan"));
        QVERIFY(w->saveProjectTo(path));
        QVERIFY(!w->document()->isModified());
        QCOMPARE(w->document()->fileFormat(), QByteArray("rolidan"));
        for (QLabel *l : w->statusBar()->findChildren<QLabel *>()) // no "lost on close" note any more
            QVERIFY(!l->text().contains(QStringLiteral("only until the window is closed")));
        const QImage before = result();

        MainWindow *other = Application::instance()->newWindow();
        QVERIFY(other->openFile(path));
        QCOMPARE(other->document()->state().objects.size(), 2);
        QCOMPARE(docops::composite(other->document()->state()).convertToFormat(QImage::Format_ARGB32), before);
        QVERIFY(!other->document()->isModified());
        QVERIFY(other->document()->state().objects.first()->type() == Annotation::Type::Line);
        other->close();
        QCoreApplication::processEvents();
    }

    void crashRecoveryWritesAndRestores()
    {
        newDoc(200, 100, Qt::white);
        tool(Qt::Key_R);
        drag(QPointF(20, 20), QPointF(120, 80));
        const QString path = w->recoveryPath();
        QVERIFY(path.startsWith(recovery::directory()));
        w->autosaveNow(true);
        QVERIFY(QFile::exists(path));
        // Simulate a crashed process: rename to a dead pid.
        const QString orphan = recovery::directory() + QStringLiteral("/999999999-1.rolidan");
        QFile::remove(orphan);
        QVERIFY(QFile::copy(path, orphan));
        QVERIFY(recovery::orphans().contains(orphan));
        QVERIFY(!recovery::orphans().contains(path)); // our own file is not an orphan

        MainWindow *restored = Application::instance()->newWindow();
        QVERIFY(restored->restoreRecovery(orphan));
        QVERIFY(!QFile::exists(orphan));
        QCOMPARE(restored->document()->state().objects.size(), 1);
        QVERIFY(restored->document()->isModified());
        restored->document()->reset(QImage());
        restored->close();

        // Closing the window on purpose removes its recovery file.
        w->document()->reset(QImage());
        w->close();
        QCoreApplication::processEvents();
        QVERIFY(!QFile::exists(path));
        w = Application::instance()->newWindow();
    }

    void recoveryForUntouchedPaste()
    {
        // A pasted image without a file is worth recovering even when unchanged.
        newDoc(50, 40, Qt::white);
        QVERIFY(!w->document()->isModified());
        const QString path = w->recoveryPath();
        w->autosaveNow(true);
        QVERIFY(QFile::exists(path));
        // An image opened from a file and unchanged is not.
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("a.png"));
        QImage img(30, 20, QImage::Format_RGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(file));
        QVERIFY(w->openFile(file));
        QVERIFY(!QFile::exists(path));
        w->autosaveNow(true);
        QVERIFY(!QFile::exists(path));
    }

    void notesPanel()
    {
        newDoc(120, 80, Qt::white);
        NotesPanel *panel = w->notesPanel();
        QVERIFY(panel);
        QVERIFY(!panel->isVisible());
        panel->show();
        panel->editor()->setFocus();
        QTest::keyClicks(panel->editor(), "Hello");
        QCOMPARE(w->document()->state().notes, QStringLiteral("Hello"));
        QVERIFY(w->document()->isModified());
        // Typing is one undo step; document undo restores the editor.
        w->document()->closeMerge();
        w->document()->undo();
        QCOMPARE(panel->editor()->toPlainText(), QString());
        w->document()->redo();
        QCOMPARE(panel->editor()->toPlainText(), QStringLiteral("Hello"));

        // Import: empty notes are replaced without asking; then appended programmatically.
        QTemporaryDir dir;
        const QString md = dir.filePath(QStringLiteral("poznámky.md"));
        {
            QFile f(md);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# Nadpis\r\n\n- bod ž\n");
        }
        w->document()->undo();
        QVERIFY(panel->importFile(md));
        QCOMPARE(w->document()->state().notes, QStringLiteral("# Nadpis\n\n- bod ž\n"));
        panel->importText(QStringLiteral("dále"), true);
        QVERIFY(w->document()->state().notes.endsWith(QStringLiteral("- bod ž\n\ndále")));
        panel->previewAction()->setChecked(true);
        if (!shotDir.isEmpty()) {
            panel->importText(QStringLiteral("# Úkoly\n\n- **opravit** tlačítko\n- poslat [odkaz](https://example.com)\n"), false);
            shot(QStringLiteral("notes"));
        }
        panel->previewAction()->setChecked(false);

        // Export writes UTF-8 with a final newline.
        const QString out = dir.filePath(QStringLiteral("out.md"));
        QVERIFY(panel->exportFile(out));
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()).trimmed(), w->document()->state().notes.trimmed());

        // A project keeps them and shows the panel when reopened.
        const QString proj = dir.filePath(QStringLiteral("n.rolidan"));
        QVERIFY(w->saveProjectTo(proj));
        MainWindow *other = Application::instance()->newWindow();
        other->show();
        QVERIFY(other->openFile(proj));
        QCOMPARE(other->document()->state().notes, w->document()->state().notes);
        QVERIFY(other->notesPanel()->isVisible());
        other->close();
        QCoreApplication::processEvents();
        panel->hide();
    }

    void filterDialogPreviewApplyCancel()
    {
        newDoc(200, 120, QColor(100, 100, 100));
        const DocState before = w->document()->state();
        const filters::Spec *spec = filters::find(QStringLiteral("brightnessContrast"));
        {
            FilterDialog dlg(*spec, w->canvas());
            dlg.show();
            dlg.setValue(QStringLiteral("brightness"), 60);
            QTRY_VERIFY(dlg.previewReady());
            // Live preview on the canvas, not yet in the history.
            QVERIFY(qRed(w->document()->state().base.pixel(5, 5)) > 150);
            QVERIFY(!w->document()->canUndo());
            if (!shotDir.isEmpty())
                dlg.grab().save(shotDir + QStringLiteral("/filter-dialog.png"));
            dlg.reject();
        }
        QCOMPARE(w->document()->state().base, before.base); // cancel restores
        {
            FilterDialog dlg(*spec, w->canvas());
            dlg.setValue(QStringLiteral("brightness"), 60);
            dlg.accept(); // result not ready yet: computed synchronously
        }
        QVERIFY(qRed(w->document()->state().base.pixel(5, 5)) > 150);
        QVERIFY(w->document()->canUndo());
        QCOMPARE(w->document()->history().undoText(), filters::displayName(*spec));
        // The value is remembered for the next time.
        FilterDialog again(*spec, w->canvas());
        QCOMPARE(again.values().value(QStringLiteral("brightness")), 60.0);
        again.setValue(QStringLiteral("brightness"), 0);
        again.reject();
        QSettings().remove(QStringLiteral("filters"));
        w->document()->undo();
        QCOMPARE(w->document()->state().base, before.base);

        // Selection: only the area changes; parameterless filters apply at once.
        w->canvas()->setAreaSelection(QRect(0, 0, 50, 50));
        FilterDialog::applyDirect(*filters::find(QStringLiteral("invert")), w->canvas());
        QCOMPARE(qRed(w->document()->state().base.pixel(10, 10)), 155);
        QCOMPARE(qRed(w->document()->state().base.pixel(100, 100)), 100);
        w->canvas()->setAreaSelection({});
    }

    void filterMenusAndRotateDialog()
    {
        newDoc(300, 200, Qt::white);
        QStringList menus;
        for (QAction *m : w->menuBar()->actions())
            menus << m->text();
        QVERIFY(menus.contains(QStringLiteral("&Colors")));
        QVERIFY(menus.contains(QStringLiteral("Fi&lters")));
        // Rotation dialog: grid on while open, fine angle, objects follow.
        const filters::Spec *rot = filters::find(QStringLiteral("rotate"));
        {
            FilterDialog dlg(*rot, w->canvas(), {{QStringLiteral("angle"), 12.3}});
            QVERIFY(w->canvas()->gridVisible());
            QCOMPARE(dlg.values().value(QStringLiteral("angle")), 12.3);
            auto *spin = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("angle"));
            QVERIFY(spin);
            QCOMPARE(spin->decimals(), 1);
            QCOMPARE(spin->value(), 12.3);
            dlg.show();
            QTRY_VERIFY(dlg.previewReady());
            if (!shotDir.isEmpty()) {
                shot(QStringLiteral("rotate-preview"));
                dlg.grab().save(shotDir + QStringLiteral("/rotate-dialog.png"));
            }
            dlg.accept();
        }
        QVERIFY(!w->canvas()->gridVisible());
        QVERIFY(w->document()->size().width() > 300);
    }

    void bucketStraightenPixelBrush()
    {
        newDoc(100, 80, Qt::white);
        {
            DocState s = w->document()->state();
            QPainter(&s.base).fillRect(50, 0, 50, 80, Qt::black);
            w->document()->commit(s, QStringLiteral("half"));
        }
        tooldefaults::setValue(ToolId::Bucket, Prop::Color, QColor(Qt::red));
        tool(Qt::Key_F);
        QCOMPARE(w->canvas()->toolId(), ToolId::Bucket);
        QTest::mouseClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(10.25, 10.25)));
        QCOMPARE(w->document()->state().base.pixel(20, 70), qRgb(255, 0, 0));
        QCOMPARE(w->document()->state().base.pixel(70, 10), qRgb(0, 0, 0)); // other area untouched
        QCOMPARE(w->document()->history().undoText(), QStringLiteral("Fill"));

        // Straighten: a line tilted by ~5° asks for a -5° rotation.
        double requested = 999;
        auto conn = connect(w->canvas(), &Canvas::straightenRequested, this, [&](double d) { requested = d; });
        QCOMPARE(StraightenTool::angleFor(QPointF(0, 0), QPointF(100, 0)), 0.0);
        QVERIFY(std::abs(StraightenTool::angleFor(QPointF(0, 0), QPointF(100, 8.75)) + 5.0) < 0.01);
        QVERIFY(std::abs(StraightenTool::angleFor(QPointF(10, 0), QPointF(13, 60)) - 2.86) < 0.01); // nearly upright
        // Run it through the UI without opening the modal dialog.
        QObject::disconnect(w->canvas(), SIGNAL(straightenRequested(double)), w, nullptr);
        tool(Qt::Key_W);
        drag(QPointF(10.25, 40.25), QPointF(90.25, 47.25));
        QVERIFY(std::abs(requested + 5.0) < 0.1);
        disconnect(conn);

        // Pixelate brush: cells inside the stroke become uniform.
        newDoc(120, 80, Qt::white);
        {
            DocState s = w->document()->state();
            s.base = QImage(120, 80, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < 80; ++y)
                for (int x = 0; x < 120; ++x)
                    s.base.setPixel(x, y, qRgb(x * 2, y * 3, 100));
            w->document()->commit(s, QStringLiteral("gradient"));
        }
        tooldefaults::setValue(ToolId::PixelBrush, Prop::Cell, 10);
        tooldefaults::setValue(ToolId::PixelBrush, Prop::Hardness, 1.0);
        tooldefaults::setValue(ToolId::PixelBrush, Prop::Size, 60.0);
        tool(Qt::Key_J);
        drag(QPointF(40.25, 40.25), QPointF(70.25, 40.25));
        const QImage b = w->document()->state().base;
        QCOMPARE(b.pixel(50, 40), b.pixel(59, 49));     // same cell
        QVERIFY(b.pixel(50, 40) != b.pixel(60, 40));    // next cell
        QCOMPARE(b.pixel(2, 2), qRgb(4, 6, 100));        // outside the stroke unchanged
    }

    void browsesFolder()
    {
        QTemporaryDir dir;
        for (const char *name : {"img10.png", "img2.png", "img1.png", "notes.txt"}) {
            QImage img(20 + int(qstrlen(name)), 10, QImage::Format_RGB32);
            img.fill(Qt::blue);
            if (QByteArray(name).endsWith(".png"))
                QVERIFY(img.save(dir.filePath(QString::fromLatin1(name))));
            else
                QFile(dir.filePath(QString::fromLatin1(name))).open(QIODevice::WriteOnly);
        }
        w->document()->reset(QImage());
        QVERIFY(w->openFile(dir.filePath(QStringLiteral("img1.png"))));
        key(QKeySequence(Qt::Key_PageDown));
        QCOMPARE(QFileInfo(w->document()->originPath()).fileName(), QStringLiteral("img2.png")); // numeric order
        key(QKeySequence(Qt::Key_PageDown));
        QCOMPARE(QFileInfo(w->document()->originPath()).fileName(), QStringLiteral("img10.png"));
        key(QKeySequence(Qt::Key_PageDown)); // last: stays, says so
        QCOMPARE(QFileInfo(w->document()->originPath()).fileName(), QStringLiteral("img10.png"));
        QVERIFY(w->canvas()->toast()->text().contains(QStringLiteral("last")));
        key(QKeySequence(Qt::Key_Home));
        QCOMPARE(QFileInfo(w->document()->originPath()).fileName(), QStringLiteral("img1.png"));
        key(QKeySequence(Qt::Key_PageUp));
        QCOMPARE(QFileInfo(w->document()->originPath()).fileName(), QStringLiteral("img1.png"));
        w->document()->reset(QImage());
    }

    // ---------------- viewer (Etapa 5)

    QStringList makeFolder(const QTemporaryDir &dir, int count)
    {
        QStringList out;
        for (int i = 1; i <= count; ++i) {
            QImage img(30 + i, 20, QImage::Format_RGB32);
            img.fill(QColor(40 * i, 90, 160));
            out << dir.filePath(QStringLiteral("p%1.png").arg(i));
            img.save(out.last());
        }
        return out;
    }
    QString shownName() { return QFileInfo(w->document()->originPath()).fileName(); }

    void recentFilesMenu()
    {
        QTemporaryDir dir;
        const QStringList files = makeFolder(dir, 2);
        settings::clearRecentFiles();
        QVERIFY(w->openFile(files[0]));
        QVERIFY(w->openFile(files[1]));
        QCOMPARE(settings::recentFiles().first(), files[1]);
        QMenu *recent = nullptr;
        for (QMenu *m : w->findChildren<QMenu *>())
            if (m->title().contains(QStringLiteral("Recent")))
                recent = m;
        QVERIFY(recent);
        emit recent->aboutToShow();
        QStringList texts;
        for (QAction *act : recent->actions())
            texts << act->text();
        QVERIFY(texts.value(0).endsWith(QStringLiteral("p2.png")));
        QVERIFY(texts.value(1).endsWith(QStringLiteral("p1.png")));
        settings::clearRecentFiles();
    }

    void browsingWrapsAndWheelBrowses()
    {
        QTemporaryDir dir;
        makeFolder(dir, 3);
        QVERIFY(w->openFile(dir.filePath(QStringLiteral("p3.png"))));
        QVERIFY(w->browse(1, true));
        QCOMPARE(shownName(), QStringLiteral("p1.png"));
        QVERIFY(w->browse(-1, true));
        QCOMPARE(shownName(), QStringLiteral("p3.png"));
        settings::setWheelAction(settings::Wheel::Browse);
        auto wheel = [&](int delta) {
            const QPointF pos(w->canvas()->width() / 2.0, w->canvas()->height() / 2.0);
            QWheelEvent ev(pos, w->canvas()->mapToGlobal(pos), {}, QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(w->canvas(), &ev);
        };
        wheel(120); // up = previous
        QCOMPARE(shownName(), QStringLiteral("p2.png"));
        wheel(-60);
        QCOMPARE(shownName(), QStringLiteral("p2.png")); // half a notch: nothing yet
        wheel(-60);
        QCOMPARE(shownName(), QStringLiteral("p3.png"));
        settings::setWheelAction(settings::Wheel::Scroll);
    }

    void fullScreenAndSlideshow()
    {
        QTemporaryDir dir;
        makeFolder(dir, 3);
        QVERIFY(w->openFile(dir.filePath(QStringLiteral("p1.png"))));
        key(QKeySequence(Qt::Key_F11));
        QVERIFY(w->isFullScreenMode());
        QVERIFY(!w->menuBar()->isVisible());
        QTest::keyClick(w->canvas(), Qt::Key_Escape); // nothing to cancel: leaves full screen
        QVERIFY(!w->isFullScreenMode());
        QVERIFY(w->menuBar()->isVisible());

        settings::setSlideshowSeconds(0.5);
        settings::setSlideshowLoop(true);
        key(QKeySequence(Qt::Key_F5));
        QVERIFY(w->isSlideshowRunning());
        QVERIFY(w->isFullScreenMode());
        QTRY_COMPARE_WITH_TIMEOUT(shownName(), QStringLiteral("p2.png"), 3000);
        QTest::keyClick(w->canvas(), Qt::Key_Space);
        QVERIFY(w->isSlideshowPaused());
        QTest::qWait(800);
        QCOMPARE(shownName(), QStringLiteral("p2.png")); // paused
        QTest::keyClick(w->canvas(), Qt::Key_Right);
        QCOMPARE(shownName(), QStringLiteral("p3.png"));
        QTest::keyClick(w->canvas(), Qt::Key_Right); // loops
        QCOMPARE(shownName(), QStringLiteral("p1.png"));
        QTest::keyClick(w->canvas(), Qt::Key_Left);
        QCOMPARE(shownName(), QStringLiteral("p3.png"));
        shot(QStringLiteral("30-slideshow"));
        QTest::keyClick(w->canvas(), Qt::Key_Escape);
        QVERIFY(!w->isSlideshowRunning());
        QVERIFY(!w->isFullScreenMode());

        // Editing ends a slideshow.
        key(QKeySequence(Qt::Key_F5));
        QVERIFY(w->isSlideshowRunning());
        key(QKeySequence(Qt::Key_R));
        QVERIFY(!w->isSlideshowRunning());
        settings::setSlideshowSeconds(4);
    }

    void renameCopyMoveFiles()
    {
        QTemporaryDir dir, other;
        makeFolder(dir, 3);
        settings::clearRecentFiles();
        QVERIFY(w->openFile(dir.filePath(QStringLiteral("p2.png"))));
        QVERIFY(w->renameCurrentFile(QStringLiteral("holiday.png")));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("holiday.png"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("p2.png"))));
        QCOMPARE(QFileInfo(w->currentFile()).fileName(), QStringLiteral("holiday.png"));
        QVERIFY(w->windowTitle().contains(QStringLiteral("holiday.png")));
        QCOMPARE(QFileInfo(settings::recentFiles().value(0)).fileName(), QStringLiteral("holiday.png"));
        QVERIFY(!w->renameCurrentFile(QStringLiteral("a/b.png")));

        QVERIFY(w->copyOrMoveCurrentFile(other.path(), false, false));
        QVERIFY(QFileInfo::exists(other.filePath(QStringLiteral("holiday.png"))));
        QVERIFY(w->copyOrMoveCurrentFile(other.path(), false, false)); // exists: keeps both
        QVERIFY(QFileInfo::exists(other.filePath(QStringLiteral("holiday (2).png"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("holiday.png"))));

        // Move: the file leaves the folder, the next image is shown.
        QVERIFY(w->openFile(dir.filePath(QStringLiteral("p1.png"))));
        QVERIFY(w->copyOrMoveCurrentFile(other.path(), true, false));
        QVERIFY(QFileInfo::exists(other.filePath(QStringLiteral("p1.png"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("p1.png"))));
        QCOMPARE(shownName(), QStringLiteral("p3.png")); // natural order: holiday, p1, p3
        settings::clearRecentFiles();
    }

    void trashShowsNextImage()
    {
        // Inside the test-mode data directory, so the file lands in its trash.
        const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        QDir(base).mkpath(QStringLiteral("trash-test"));
        QTemporaryDir dir(base + QStringLiteral("/trash-test/XXXXXX"));
        const QStringList files = makeFolder(dir, 2);
        QVERIFY(w->openFile(files[0]));
        if (!w->trashCurrentFile(false))
            QSKIP("no trash available here");
        QVERIFY(!QFileInfo::exists(files[0]));
        QCOMPARE(shownName(), QStringLiteral("p2.png"));
        QVERIFY(w->trashCurrentFile(false));
        QVERIFY(w->document()->isEmpty()); // the folder is empty now
        QDir(base + QStringLiteral("/Trash")).removeRecursively();
        QDir(base + QStringLiteral("/trash-test")).removeRecursively();
    }

    void fullScreenBrowsingAndFit()
    {
        QTemporaryDir dir;
        makeFolder(dir, 3); // small images: 31..33 × 20 px
        settings::setFullScreenEnlarge(false);
        QVERIFY(w->openFile(dir.filePath(QStringLiteral("p1.png"))));
        QTest::keyClick(w->canvas(), Qt::Key_Return);
        QVERIFY(w->isFullScreenMode());
        QTRY_VERIFY(w->canvas()->isAutoFit());
        QCOMPARE(w->canvas()->zoom(), 1.0); // small image: actual size
        // Arrows browse, no automatic advance.
        QTest::keyClick(w->canvas(), Qt::Key_Right);
        QCOMPARE(shownName(), QStringLiteral("p2.png"));
        QTest::keyClick(w->canvas(), Qt::Key_Down);
        QCOMPARE(shownName(), QStringLiteral("p3.png"));
        QTest::keyClick(w->canvas(), Qt::Key_Left);
        QCOMPARE(shownName(), QStringLiteral("p2.png"));
        QTest::qWait(600);
        QCOMPARE(shownName(), QStringLiteral("p2.png"));
        QVERIFY(!w->isSlideshowRunning());
        // Z: enlarge small images to the screen, remembered.
        key(QKeySequence(Qt::Key_Z));
        QVERIFY(settings::fullScreenEnlarge());
        QVERIFY(w->canvas()->zoom() > 5.0);
        QTest::keyClick(w->canvas(), Qt::Key_Right);
        QVERIFY(w->canvas()->zoom() > 5.0); // also for the next image
        shot(QStringLiteral("33-fullscreen-enlarged"));
        key(QKeySequence(Qt::Key_Z));
        QVERIFY(!settings::fullScreenEnlarge());
        QCOMPARE(w->canvas()->zoom(), 1.0);
        // The pointer hides after a moment without movement.
        QTRY_COMPARE_WITH_TIMEOUT(w->canvas()->cursor().shape(), Qt::BlankCursor, 3000);
        QTest::mouseMove(w->canvas(), QPoint(40, 40));
        QVERIFY(w->canvas()->cursor().shape() != Qt::BlankCursor);
        QTest::keyClick(w->canvas(), Qt::Key_Escape);
        QVERIFY(!w->isFullScreenMode());
        // Outside full screen the arrows do not browse; small images are not enlarged.
        QTest::keyClick(w->canvas(), Qt::Key_Right);
        QCOMPARE(shownName(), QStringLiteral("p3.png"));
        // Double-click on the image toggles full screen.
        const QPoint center = widgetPos(QPointF(15, 10));
        QTest::mouseDClick(w->canvas(), Qt::LeftButton, {}, center);
        QVERIFY(w->isFullScreenMode());
        QTest::mouseDClick(w->canvas(), Qt::LeftButton, {}, widgetPos(QPointF(15, 10)));
        QVERIFY(!w->isFullScreenMode());
    }

    void thumbnailPanel()
    {
        QTemporaryDir dir;
        const QStringList files = makeFolder(dir, 3);
        // A project gets a thumbnail of its composite.
        QVERIFY(project::save(DocState{solid(300, 150, Qt::green), {}, {}}, dir.filePath(QStringLiteral("p4.rolidan")),
                              nullptr));
        QVERIFY(w->openFile(files[0]));
        ThumbnailPanel *panel = w->thumbnails();
        key(QKeySequence(Qt::CTRL | Qt::Key_T));
        QVERIFY(panel->isVisible());
        ThumbnailModel *m = panel->model();
        QCOMPARE(m->rowCount(), 4);
        for (int i = 0; i < m->rowCount(); ++i)
            m->data(m->index(i), Qt::DecorationRole); // what painting the view does
        QTRY_COMPARE_WITH_TIMEOUT(m->loadedCount(), 4, 5000);
        QCOMPARE(panel->view()->currentIndex().row(), 0);
        const QImage t = ThumbnailModel::makeThumbnail(dir.filePath(QStringLiteral("p4.rolidan")), 96);
        QCOMPARE(t.size(), QSize(96, 48));
        // A click shows that image; browsing moves the highlight.
        const QRect r = panel->view()->visualRect(m->index(1));
        QTest::mouseClick(panel->view()->viewport(), Qt::LeftButton, {}, r.center());
        QCOMPARE(shownName(), QStringLiteral("p2.png"));
        key(QKeySequence(Qt::Key_PageDown));
        QCOMPARE(panel->view()->currentIndex().row(), 2);
        shot(QStringLiteral("34-thumbnails"));
        // A renamed file appears under its new name.
        QVERIFY(w->renameCurrentFile(QStringLiteral("zz.png")));
        QTRY_VERIFY_WITH_TIMEOUT(m->files().contains(dir.filePath(QStringLiteral("zz.png"))), 3000);
        QTRY_COMPARE(panel->view()->currentIndex().data(Qt::UserRole).toString(), dir.filePath(QStringLiteral("zz.png")));
        key(QKeySequence(Qt::CTRL | Qt::Key_T));
        QVERIFY(!panel->isVisible());
        QSettings().remove(QStringLiteral("viewer/thumbnails"));
    }

    void fitFollowsWindowSizeUntilZoomed()
    {
        putImage(solid(2000, 1000, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        QVERIFY(w->canvas()->isAutoFit());
        const double z = w->canvas()->zoom();
        w->resize(1400, 900);
        QTRY_VERIFY(w->canvas()->zoom() > z); // bigger window: refitted
        key(QKeySequence(Qt::CTRL | Qt::Key_1));
        QVERIFY(!w->canvas()->isAutoFit());
        w->resize(900, 600);
        QTest::qWait(50);
        QCOMPARE(w->canvas()->zoom(), 1.0); // the user's zoom stays
    }

    void viewerKeysLikeIrfanView()
    {
        putImage(solid(300, 200, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        Document *d = w->document();
        key(QKeySequence(Qt::Key_R));
        QCOMPARE(d->size(), QSize(200, 300));
        key(QKeySequence(Qt::Key_L));
        QCOMPARE(d->size(), QSize(300, 200));
        key(QKeySequence(Qt::Key_Greater));
        QCOMPARE(d->size(), QSize(200, 300));
        key(QKeySequence(Qt::Key_Less));
        QCOMPARE(d->size(), QSize(300, 200));
        const int undoBefore = d->history().index();
        key(QKeySequence(Qt::Key_H));
        key(QKeySequence(Qt::Key_V));
        QCOMPARE(d->history().index(), undoBefore + 2);
        QVERIFY(d->history().undoText().contains(QStringLiteral("vertically")));
        // F, Enter and F11 toggle full screen.
        key(QKeySequence(Qt::Key_F));
        QVERIFY(w->isFullScreenMode());
        QTest::keyClick(w->canvas(), Qt::Key_Return);
        QVERIFY(!w->isFullScreenMode());
        QTest::keyClick(w->canvas(), Qt::Key_Enter);
        QVERIFY(w->isFullScreenMode());
        key(QKeySequence(Qt::Key_F11));
        QVERIFY(!w->isFullScreenMode());
        // Enter still confirms a crop first.
        key(QKeySequence(Qt::Key_Y));
        QCOMPARE(w->canvas()->toolId(), ToolId::Crop);
        drag(QPointF(10, 10), QPointF(110, 60));
        QTest::keyClick(w->canvas(), Qt::Key_Return);
        QCOMPARE(d->size(), QSize(100, 50));
        QVERIFY(!w->isFullScreenMode());
        // Tools with letters used by the viewer: Shift + letter.
        for (auto [k, id] : {std::pair{Qt::Key_R, ToolId::Rect}, {Qt::Key_L, ToolId::Line},
                             {Qt::Key_H, ToolId::Highlighter}, {Qt::Key_F, ToolId::Bucket},
                             {Qt::Key_V, ToolId::Pointer}, {Qt::Key_T, ToolId::Text}}) {
            key(QKeySequence(Qt::SHIFT | k));
            QCOMPARE(w->canvas()->toolId(), id);
        }
        key(QKeySequence(Qt::Key_T)); // plain letters still work where free
        QCOMPARE(w->canvas()->toolId(), ToolId::Text);
        QAction *resize = nullptr;
        for (QAction *act : w->actions())
            if (act->objectName() == QLatin1String("image.resize"))
                resize = act;
        QVERIFY(resize);
        QVERIFY(resize->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_R)));
    }

    void customKeyboardShortcuts()
    {
        QSettings().remove(QStringLiteral("shortcuts"));
        {
            ShortcutsDialog dlg(w->customizableActions(), w);
            QVERIFY(dlg.conflicts().isEmpty()); // the defaults never clash
            dlg.setKeys(QStringLiteral("image.flipHorizontally"), 0, QKeySequence(Qt::Key_R)); // R is rotate
            QVERIFY(!dlg.conflicts().isEmpty());
            dlg.setKeys(QStringLiteral("image.flipHorizontally"), 0, QKeySequence(Qt::Key_X));
            dlg.setKeys(QStringLiteral("image.flipHorizontally"), 1, QKeySequence());
            dlg.setKeys(QStringLiteral("tool.rect"), 1, QKeySequence(Qt::Key_D));
            QVERIFY(dlg.conflicts().isEmpty());
            if (!shotDir.isEmpty()) {
                dlg.show();
                QVERIFY(QTest::qWaitForWindowExposed(&dlg));
                dlg.grab().save(shotDir + QStringLiteral("/32-shortcuts.png"));
            }
            dlg.accept();
        }
        w->activateWindow(); // a shown dialog took the focus
        QVERIFY(QTest::qWaitForWindowActive(w));
        putImage(solid(300, 200, Qt::white));
        key(QKeySequence(Qt::CTRL | Qt::Key_V));
        const int before = w->document()->history().index();
        key(QKeySequence(Qt::Key_H)); // no longer bound
        QCOMPARE(w->document()->history().index(), before);
        QAction *flipH = nullptr;
        for (QAction *act : w->actions())
            if (act->objectName() == QLatin1String("image.flipHorizontally"))
                flipH = act;
        QCOMPARE(flipH->shortcuts(), QList<QKeySequence>{QKeySequence(Qt::Key_X)});
        key(QKeySequence(Qt::Key_X));
        QCOMPARE(w->document()->history().index(), before + 1);
        key(QKeySequence(Qt::Key_D));
        QCOMPARE(w->canvas()->toolId(), ToolId::Rect);
        // A new window gets the customized keys, and the tooltip shows them.
        MainWindow *w2 = Application::instance()->newWindow();
        QAction *rect = nullptr;
        for (QAction *act : w2->actions())
            if (act->objectName() == QLatin1String("tool.rect"))
                rect = act;
        QVERIFY(rect);
        QCOMPARE(rect->shortcuts().value(1), QKeySequence(Qt::Key_D));
        QVERIFY(rect->toolTip().contains(QStringLiteral("D")));
        w2->close();
        QSettings().remove(QStringLiteral("shortcuts"));
        shortcuts::reload(w->customizableActions());
        QVERIFY(!shortcuts::current(rect).contains(QKeySequence(Qt::Key_D)));
    }

    void imageInfoShowsExif()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("photo.jpg"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(fixture::jpegWithExif());
        f.close();
        QVERIFY(w->openFile(path));
        ImageInfoDialog dlg(w->currentFile(), w->document()->state().base, w);
        const QString t = dlg.text();
        QVERIFY2(t.contains(QStringLiteral("Canon EOS R6")), qPrintable(t));
        QVERIFY(t.contains(QStringLiteral("1/250 s")));
        QVERIFY2(t.contains(QStringLiteral("f/2.8")) || t.contains(QStringLiteral("f/2,8")), qPrintable(t));
        QVERIFY(t.contains(QStringLiteral("Transparency: no")));
        QVERIFY(t.contains(QStringLiteral("64 × 48 px")));
        QVERIFY(t.contains(QStringLiteral("JPEG")));
        QVERIFY(t.contains(QStringLiteral("50.087500° N")));
        QVERIFY(dlg.mapUrl().contains(QStringLiteral("mlat=50.087500")));
        QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("showOnMap")));
        if (!shotDir.isEmpty()) {
            dlg.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dlg));
            dlg.grab().save(shotDir + QStringLiteral("/31-image-info.png"));
        }
        // A pasted image: no file, still basic information.
        ImageInfoDialog pasted(QString(), solid(10, 5, Qt::red), w);
        QVERIFY(pasted.text().contains(QStringLiteral("10 × 5 px")));
        QVERIFY(pasted.mapUrl().isEmpty());
    }

    void batchDialogRuns()
    {
        QTemporaryDir dir;
        QStringList inputs;
        for (int i = 0; i < 3; ++i) {
            QImage img(60, 40, QImage::Format_RGB32);
            img.fill(QColor(i * 80, 100, 100));
            inputs << dir.filePath(QStringLiteral("s%1.png").arg(i));
            QVERIFY(img.save(inputs.last()));
        }
        BatchDialog dlg;
        dlg.addFiles(inputs);
        dlg.addFiles(inputs); // no duplicates
        QCOMPARE(dlg.files().size(), 3);
        QVERIFY(dlg.options().outputDir.endsWith(QStringLiteral("converted")));
        dlg.setOutputDir(dir.filePath(QStringLiteral("out")));
        QSignalSpy done(&dlg, &BatchDialog::finished);
        dlg.show();
        if (!shotDir.isEmpty())
            dlg.grab().save(shotDir + QStringLiteral("/batch.png"));
        dlg.start();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.first().at(0).toInt(), 3);
        QCOMPARE(QDir(dir.filePath(QStringLiteral("out"))).entryList(QDir::Files).size(), 3);
        QVERIFY(!dlg.isRunning());
    }

    void dropOfLinkShowsMessage()
    {
        QMimeData m;
        m.setUrls({QUrl(QStringLiteral("https://example.com/x.png"))});
        w->insertContent(clip::decode(&m));
        QVERIFY(w->canvas()->toast()->text().contains(QStringLiteral("link")));
        QVERIFY(w->document()->isEmpty());
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Keep the user's real settings untouched.
    QTemporaryDir config;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, config.path());
    Application app(argc, argv);
    TstGui t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_gui.moc"
