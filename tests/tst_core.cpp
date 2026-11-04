// Core tests: document model (spec C), copy/cut semantics (B3), clipboard
// decoding (B1), loading (B7), saving (F) and history (G1). Run headless:
// QT_QPA_PLATFORM=offscreen.
#include "annotation.h"
#include "printing.h"
#include "capture.h"
#include "clipboard.h"
#include "exif.h"
#include "exiffixture.h"
#include "settings.h"
#include "document.h"
#include "batch.h"
#include "filters.h"
#include "history.h"
#include "imageio.h"
#include "retouch.h"
#include "project.h"
#include "zip.h"

#include <QBuffer>
#include <algorithm>
#include <QElapsedTimer>
#include <QtEndian>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QMimeData>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QUrl>

namespace {

QImage solid(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

// Base with a distinct value per pixel, so geometric mappings can be checked.
QImage gradient(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgba(x * 10 % 256, y * 10 % 256, (x + y) % 256, 255));
    return img;
}

std::shared_ptr<ImageObject> imageObject(int w, int h, QColor c, QPointF pos)
{
    auto o = std::make_shared<ImageObject>(solid(w, h, c));
    o->transform = QTransform::fromTranslate(pos.x(), pos.y());
    return o;
}

QRgb px(const QImage &img, int x, int y)
{
    return img.convertToFormat(QImage::Format_ARGB32).pixel(x, y);
}

} // namespace

class TstCore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { imageio::initialize(); }

    // ---------------- document operations (spec C4)

    void rotateClockwiseMapsPixelsAndObjects()
    {
        DocState s{gradient(4, 2), {imageObject(1, 1, Qt::red, {3, 0})}};
        const DocState r = docops::rotate90(s, true);
        QCOMPARE(r.base.size(), QSize(2, 4));
        // (x,y) -> (h-1-y, x) for pixels
        QCOMPARE(px(r.base, 1, 3), px(s.base, 3, 0));
        QCOMPARE(px(r.base, 0, 0), px(s.base, 0, 1));
        // object at pixel (3,0) ends at pixel (1,3)
        QCOMPARE(r.objects.first()->boundingRect(), QRectF(1, 3, 1, 1));
        // composite agrees with the object position
        QCOMPARE(px(docops::composite(r), 1, 3), qRgba(255, 0, 0, 255));
    }

    void rotateCounterClockwiseIsInverse()
    {
        DocState s{gradient(5, 3), {imageObject(2, 1, Qt::blue, {1, 2})}};
        const DocState back = docops::rotate90(docops::rotate90(s, true), false);
        QCOMPARE(back.base, s.base);
        QCOMPARE(back.objects.first()->boundingRect(), s.objects.first()->boundingRect());
    }

    void rotate180AndFlip()
    {
        DocState s{gradient(4, 3), {imageObject(1, 1, Qt::red, {0, 0})}};
        const DocState r = docops::rotate180(s);
        QCOMPARE(px(r.base, 3, 2), px(s.base, 0, 0));
        QCOMPARE(r.objects.first()->boundingRect(), QRectF(3, 2, 1, 1));

        const DocState h = docops::flip(s, Qt::Horizontal);
        QCOMPARE(px(h.base, 3, 0), px(s.base, 0, 0));
        QCOMPARE(h.objects.first()->boundingRect(), QRectF(3, 0, 1, 1));

        const DocState v = docops::flip(s, Qt::Vertical);
        QCOMPARE(px(v.base, 0, 2), px(s.base, 0, 0));
        QCOMPARE(v.objects.first()->boundingRect(), QRectF(0, 2, 1, 1));
    }

    void cropTranslatesAndDropsOutsideObjects()
    {
        DocState s{gradient(10, 10),
                   {imageObject(2, 2, Qt::red, {4, 4}),      // inside
                    imageObject(4, 4, Qt::green, {7, 7}),    // partially inside
                    imageObject(1, 1, Qt::blue, {0, 0})}};   // outside
        const DocState c = docops::crop(s, QRect(3, 3, 5, 5));
        QCOMPARE(c.base.size(), QSize(5, 5));
        QCOMPARE(px(c.base, 0, 0), px(s.base, 3, 3));
        QCOMPARE(c.objects.size(), 2);
        QCOMPARE(c.objects[0]->boundingRect(), QRectF(1, 1, 2, 2));
        // partial object stays whole (clipped only when rendering)
        QCOMPARE(c.objects[1]->boundingRect(), QRectF(4, 4, 4, 4));
        QCOMPARE(docops::composite(c).size(), QSize(5, 5));
    }

    void expandCanvasShiftsContent()
    {
        DocState s{solid(10, 10, Qt::white), {imageObject(4, 4, Qt::red, {-2, 8})}};
        const DocState e = docops::expandCanvasToInclude(s, s.objects.first()->boundingRect());
        QCOMPARE(e.base.size(), QSize(12, 12));
        QCOMPARE(e.objects.first()->boundingRect(), QRectF(0, 8, 4, 4));
        QCOMPARE(qAlpha(px(e.base, 0, 0)), 0);       // new area transparent
        QCOMPARE(px(e.base, 2, 0), qRgba(255, 255, 255, 255));
    }

    // ---------------- copy / cut (spec B3, decision J5)

    void copySelectionBaseOnlyVsAll()
    {
        DocState s{solid(10, 10, Qt::white), {imageObject(2, 2, Qt::red, {4, 4})}};
        const QRect sel(3, 3, 4, 4);
        const QImage base = docops::copySelection(s, sel, docops::CopyMode::BaseOnly);
        const QImage all = docops::copySelection(s, sel, docops::CopyMode::All);
        QCOMPARE(base.size(), QSize(4, 4));
        QCOMPARE(px(base, 1, 1), qRgba(255, 255, 255, 255)); // annotation not included
        QCOMPARE(px(all, 1, 1), qRgba(255, 0, 0, 255));      // annotation included
    }

    void cutBaseOnlyKeepsObjectsCutAllRemovesInsideOnes()
    {
        DocState s{solid(10, 10, Qt::white),
                   {imageObject(2, 2, Qt::red, {4, 4}),     // fully inside
                    imageObject(4, 4, Qt::green, {5, 5})}}; // partially inside
        const QRect sel(3, 3, 4, 4);
        const DocState b = docops::cutSelection(s, sel, docops::CopyMode::BaseOnly);
        QCOMPARE(b.objects.size(), 2);
        QCOMPARE(qAlpha(px(b.base, 3, 3)), 0);
        QCOMPARE(px(b.base, 0, 0), qRgba(255, 255, 255, 255));
        QCOMPARE(px(s.base, 3, 3), qRgba(255, 255, 255, 255)); // original unchanged (history)

        const DocState a = docops::cutSelection(s, sel, docops::CopyMode::All);
        QCOMPARE(a.objects.size(), 1);
        QCOMPARE(a.objects.first()->boundingRect(), QRectF(5, 5, 4, 4));
    }

    void renderObjectsUsesUnionBounds()
    {
        const ObjectList objs{imageObject(2, 2, Qt::red, {1, 1}), imageObject(2, 2, Qt::blue, {5, 3})};
        QPoint tl;
        const QImage img = docops::renderObjects(objs, &tl);
        QCOMPARE(tl, QPoint(1, 1));
        QCOMPARE(img.size(), QSize(6, 4));
        QCOMPARE(qAlpha(px(img, 3, 0)), 0);
    }

    void objectsSerializeRoundTrip()
    {
        auto o = imageObject(3, 2, QColor(10, 20, 30), {7, 9});
        const ObjectList back = deserializeObjects(serializeObjects({o}));
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.first()->boundingRect(), QRectF(7, 9, 3, 2));
        QVERIFY(back.first()->id != o->id);
        QVERIFY(deserializeObjects("garbage").isEmpty());
    }

    // ---------------- history (spec G1)

    void historyUndoRedoAndMerge()
    {
        History h;
        DocState a{solid(2, 2, Qt::white), {}};
        h.reset(a);
        QVERIFY(h.isClean());
        DocState b = a;
        b.objects.append(imageObject(1, 1, Qt::red, {0, 0}));
        h.push("add", b);
        h.push("move", b, 7);
        h.push("move", b, 7); // merged
        QCOMPARE(h.count(), 3);
        h.closeMerge();
        h.push("move", b, 7);
        QCOMPARE(h.count(), 4);
        QVERIFY(!h.isClean());
        h.undo();
        h.undo();
        h.undo();
        QVERIFY(h.isClean());
        QCOMPARE(h.current().objects.size(), 0);
        h.redo();
        QCOMPARE(h.current().objects.size(), 1);
        h.push("other", a); // drops redo branch
        QVERIFY(!h.canRedo());
    }

    void historyMemoryLimitDropsOldest()
    {
        History h;
        h.setLimitBytes(3 * 100 * 100 * 4 + 1000);
        h.reset({solid(100, 100, Qt::white), {}});
        for (int i = 0; i < 6; ++i)
            h.push("paint", {solid(100, 100, QColor(i, i, i)), {}});
        QVERIFY(h.memoryUsage() <= h.limitBytes());
        QCOMPARE(h.count(), 3);
        QCOMPARE(px(h.current().base, 0, 0), qRgba(5, 5, 5, 255));
        // object-only steps share the base buffer and cost almost nothing
        History h2;
        const QImage big = solid(200, 200, Qt::white);
        h2.reset({big, {}});
        for (int i = 0; i < 50; ++i)
            h2.push("move", {big, {}});
        QVERIFY(h2.memoryUsage() < 200 * 200 * 4 + 50 * 1024);
    }

    // ---------------- clipboard decoding (spec B1)

    void decodePrefersPngAndKeepsAlpha()
    {
        QImage img = solid(3, 3, QColor(255, 0, 0, 128));
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        QMimeData m;
        m.setData("image/png", png);
        m.setText("ignored");
        const clip::Content c = clip::decode(&m);
        QCOMPARE(c.kind, clip::Content::Kind::Image);
        QCOMPARE(c.image.size(), QSize(3, 3));
        QVERIFY(qAbs(qAlpha(px(c.image, 1, 1)) - 128) <= 1);
    }

    void decodeUriListAndLinks()
    {
        QTemporaryDir dir;
        const QString f1 = dir.filePath(QStringLiteral("první obrázek.png")); // Czech file name
        const QString f2 = dir.filePath(QStringLiteral("druhý.png"));
        QVERIFY(solid(5, 4, Qt::green).save(f1));
        QVERIFY(solid(2, 2, Qt::blue).save(f2));

        QMimeData files;
        files.setUrls({QUrl::fromLocalFile(f1), QUrl::fromLocalFile(f2)});
        const clip::Content c = clip::decode(&files);
        QCOMPARE(c.kind, clip::Content::Kind::Image);
        QCOMPARE(c.image.size(), QSize(5, 4));
        QCOMPARE(c.fileCount, 2);
        QCOMPARE(c.filePath, f1);

        QMimeData link;
        link.setUrls({QUrl(QStringLiteral("https://example.com/a.png"))});
        QCOMPARE(clip::decode(&link).kind, clip::Content::Kind::Link);

        QMimeData textLink;
        textLink.setText(QStringLiteral("https://example.com/a.png"));
        QCOMPARE(clip::decode(&textLink).kind, clip::Content::Kind::Link);

        QMimeData text;
        text.setText(QStringLiteral("hello"));
        QCOMPARE(clip::decode(&text).kind, clip::Content::Kind::None);
        QCOMPARE(clip::decode(nullptr).kind, clip::Content::Kind::None);
    }

    void decodeOwnObjectsFirst()
    {
        QMimeData *m = clip::encode(solid(2, 2, Qt::red), {imageObject(2, 2, Qt::red, {3, 3})});
        const clip::Content c = clip::decode(m);
        QCOMPARE(c.kind, clip::Content::Kind::Objects);
        QCOMPARE(c.objects.first()->boundingRect(), QRectF(3, 3, 2, 2));
        delete m;
    }

    void decodeIgnoresDevicePixelRatio()
    {
        QImage img = solid(300, 200, Qt::red);
        img.setDevicePixelRatio(1.5);
        QMimeData m;
        m.setImageData(img);
        const clip::Content c = clip::decode(&m);
        QCOMPARE(c.image.size(), QSize(300, 200));
        QCOMPARE(c.image.devicePixelRatio(), 1.0);
    }

    // ---------------- loading (spec B7)

    void exifOrientationIsApplied()
    {
        QByteArray jpg;
        {
            QBuffer buf(&jpg);
            buf.open(QIODevice::WriteOnly);
            QVERIFY(solid(40, 20, Qt::red).save(&buf, "JPEG"));
        }
        // Insert an APP1 Exif segment with Orientation = 6 (rotate 90° CW).
        QByteArray tiff;
        tiff.append("MM\x00\x2A\x00\x00\x00\x08", 8);       // big-endian TIFF header, IFD at 8
        tiff.append("\x00\x01", 2);                          // 1 entry
        tiff.append("\x01\x12\x00\x03\x00\x00\x00\x01\x00\x06\x00\x00", 12); // Orientation SHORT 6
        tiff.append("\x00\x00\x00\x00", 4);                  // no next IFD
        QByteArray app1("\xFF\xE1", 2);
        const int len = 2 + 6 + tiff.size();
        app1.append(char(len >> 8)).append(char(len & 0xff));
        app1.append("Exif\x00\x00", 6).append(tiff);
        jpg.insert(2, app1);
        const imageio::LoadResult r = imageio::loadData(jpg);
        QVERIFY(r.ok());
        QCOMPARE(r.image.size(), QSize(20, 40));
    }

    void largeImageLoads()
    {
        // 12000 x 9000 exceeds Qt's default 256 MB allocation limit as ARGB32.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("big.png"));
        {
            QImage big(12000, 9000, QImage::Format_RGB888);
            big.fill(QColor(20, 40, 60));
            QImageWriter w(path, "png");
            w.setQuality(90); // low zlib level: fast
            QVERIFY(w.write(big));
        }
        const imageio::LoadResult r = imageio::loadFile(path);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.image.size(), QSize(12000, 9000));
    }

    void loadErrorsAreReported()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("broken.png"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not an image");
        f.close();
        QVERIFY(!imageio::loadFile(path).ok());
        QVERIFY(!imageio::loadFile(path).error.isEmpty());
        QVERIFY(!imageio::loadFile(dir.filePath(QStringLiteral("missing.png"))).ok());
    }

    // ---------------- saving (spec F)

    void resolveSavePathRules_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QByteArray>("filter");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QByteArray>("format");
        QTest::newRow("no suffix") << "/t/a" << QByteArray("png") << "/t/a.png" << QByteArray("png");
        QTest::newRow("trailing dot") << "/t/a." << QByteArray("jpeg") << "/t/a.jpg" << QByteArray("jpeg");
        QTest::newRow("supported wins") << "/t/a.jpg" << QByteArray("png") << "/t/a.jpg" << QByteArray("jpeg");
        QTest::newRow("upper case") << "/t/A.WEBP" << QByteArray("png") << "/t/A.WEBP" << QByteArray("webp");
        QTest::newRow("unsupported") << "/t/foto.xyz" << QByteArray("png") << "/t/foto.xyz.png" << QByteArray("png");
        QTest::newRow("dots in name") << "/t/v1.2" << QByteArray("png") << "/t/v1.2.png" << QByteArray("png");
    }

    void resolveSavePathRules()
    {
        QFETCH(QString, typed);
        QFETCH(QByteArray, filter);
        QFETCH(QString, path);
        QFETCH(QByteArray, format);
        QByteArray f;
        QCOMPARE(imageio::resolveSavePath(typed, filter, &f), path);
        QCOMPARE(f, format);
    }

    void pngExportMatchesComposite()
    {
        DocState s{gradient(30, 20), {imageObject(5, 5, QColor(0, 0, 255, 128), {10, 5})}};
        const QImage comp = docops::composite(s);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.png"));
        QString err;
        QVERIFY2(imageio::save(comp, path, imageio::SaveOptions{}, &err), qPrintable(err));
        const imageio::LoadResult back = imageio::loadFile(path);
        QVERIFY(back.ok());
        QCOMPARE(back.image.convertToFormat(QImage::Format_ARGB32), comp.convertToFormat(QImage::Format_ARGB32));
    }

    void webpLosslessIsExact()
    {
        if (imageio::formatForSuffix(QStringLiteral("webp")).isEmpty())
            QSKIP("WebP plugin not installed");
        const QImage comp = gradient(40, 30);
        imageio::SaveOptions o;
        o.format = "webp";
        o.webp.lossless = true;
        const imageio::LoadResult back = imageio::loadData(imageio::encode(comp, o));
        QVERIFY(back.ok());
        QCOMPARE(back.image.convertToFormat(QImage::Format_ARGB32), comp.convertToFormat(QImage::Format_ARGB32));
    }

    void jpegFlattensOnBackground()
    {
        imageio::SaveOptions o;
        o.format = "jpeg";
        o.jpeg.quality = 100;
        o.background = QColor(0, 0, 255);
        const imageio::LoadResult back = imageio::loadData(imageio::encode(solid(16, 16, Qt::transparent), o));
        QVERIFY(back.ok());
        const QRgb c = px(back.image, 8, 8);
        QVERIFY(qBlue(c) > 240 && qRed(c) < 15);
    }

    // Fine colored detail: one-pixel red/blue stripes on white.
    static QImage stripes(int w, int h)
    {
        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                img.setPixel(x, y, x % 2 ? qRgb(220, 0, 0) : (y % 2 ? qRgb(0, 0, 220) : qRgb(255, 255, 255)));
        return img;
    }

    static double meanError(const QImage &a, const QImage &b)
    {
        double sum = 0;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x) {
                const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
                sum += std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) + std::abs(qBlue(p) - qBlue(q));
            }
        return sum / (a.width() * a.height() * 3);
    }

    void jpegNativeOptions()
    {
        if (!codecs::haveJpeg())
            QSKIP("built without libjpeg headers");
        const QImage img = stripes(64, 64);
        imageio::SaveOptions o;
        o.format = "jpeg";
        o.jpeg.quality = 95;
        o.jpeg.subsampling = 0;
        const QByteArray full = imageio::encode(img, o);
        o.jpeg.subsampling = 2;
        const QByteArray quarter = imageio::encode(img, o);
        QVERIFY(!full.isEmpty() && !quarter.isEmpty());
        const QImage a = QImage::fromData(full, "jpeg"), b = QImage::fromData(quarter, "jpeg");
        QCOMPARE(a.size(), img.size());
        // 4:4:4 keeps colored detail much better than 4:2:0.
        QVERIFY2(meanError(img, a) * 2 < meanError(img, b),
                 qPrintable(QStringLiteral("%1 vs %2").arg(meanError(img, a)).arg(meanError(img, b))));
        // Progressive scans: SOF2 marker instead of SOF0.
        o.jpeg.progressive = true;
        const QByteArray prog = imageio::encode(img, o);
        QVERIFY(prog.contains(QByteArray("\xff\xc2", 2)));
        QVERIFY(!quarter.contains(QByteArray("\xff\xc2", 2)));
        QVERIFY(!QImage::fromData(prog, "jpeg").isNull());
        // Smoothing is applied (changes the output).
        o.jpeg.progressive = false;
        o.jpeg.smoothing = 100;
        QVERIFY(imageio::encode(img, o) != quarter);
    }

    void webpNativeOptions()
    {
        if (!codecs::haveWebp())
            QSKIP("built without libwebp");
        QImage img = stripes(64, 64);
        img.setPixel(0, 0, 0); // transparent pixel: alpha survives
        imageio::SaveOptions o;
        o.format = "webp";
        o.webp.lossless = true;
        o.webp.losslessLevel = 9;
        const QImage exact = QImage::fromData(imageio::encode(img, o), "webp");
        QCOMPARE(exact.convertToFormat(QImage::Format_ARGB32), img.convertToFormat(QImage::Format_ARGB32));
        o.webp.losslessLevel = 0;
        QVERIFY(!imageio::encode(img, o).isEmpty());
        o.webp.nearLossless = 40;
        QVERIFY(!imageio::encode(img, o).isEmpty());

        o.webp.lossless = false;
        for (int preset = 0; preset <= 5; ++preset) {
            o.webp.preset = preset;
            const QByteArray data = imageio::encode(img, o);
            QVERIFY2(!data.isEmpty(), qPrintable(QString::number(preset)));
            const QImage back = QImage::fromData(data, "webp");
            QCOMPARE(back.size(), img.size());
            QVERIFY(qAlpha(back.pixel(0, 0)) < 128);
        }
        o.webp.preset = 0;
        o.webp.quality = 10;
        const qsizetype low = imageio::encode(img, o).size();
        o.webp.quality = 100;
        o.webp.sharpYuv = true;
        o.webp.method = 6;
        QVERIFY(imageio::encode(img, o).size() > low);
    }

    void resampleFilters()
    {
        // A sharp black/white edge: Lanczos keeps it crisper than bilinear.
        QImage edge(100, 10, QImage::Format_ARGB32_Premultiplied);
        edge.fill(Qt::white);
        for (int y = 0; y < 10; ++y)
            for (int x = 50; x < 100; ++x)
                edge.setPixel(x, y, qRgb(0, 0, 0));
        auto ramp = [](const QImage &img) { // pixels between 10 % and 90 % gray
            int n = 0;
            for (int x = 0; x < img.width(); ++x) {
                const int g = qGray(img.pixel(x, 2));
                n += g > 25 && g < 230;
            }
            return n;
        };
        const QSize up(400, 40);
        const QImage lanczos = filters::resample(edge, up, filters::Resample::Lanczos);
        const QImage bilinear = filters::resample(edge, up, filters::Resample::Bilinear);
        QCOMPARE(lanczos.size(), up);
        QVERIFY(ramp(lanczos) < ramp(bilinear));
        // Softer filters spread the edge more: B-spline > Bell, Hermite has no overshoot.
        const QImage bell = filters::resample(edge, up, filters::Resample::Bell);
        const QImage bspline = filters::resample(edge, up, filters::Resample::BSpline);
        QVERIFY(ramp(bspline) >= ramp(bell));
        const QImage hermite = filters::resample(edge, up, filters::Resample::Hermite);
        for (int x = 0; x < hermite.width(); ++x) {
            const int g = qGray(hermite.pixel(x, 2));
            QVERIFY(g == 255 || g == 0 || (x > 150 && x < 250)); // flat areas stay exact
        }
        // Nearest neighbor doubles pixels exactly.
        QImage px(2, 1, QImage::Format_ARGB32_Premultiplied);
        px.setPixel(0, 0, qRgb(255, 0, 0));
        px.setPixel(1, 0, qRgb(0, 0, 255));
        const QImage n = filters::resample(px, QSize(4, 2), filters::Resample::Nearest);
        QCOMPARE(n.pixel(1, 1), qRgb(255, 0, 0));
        QCOMPARE(n.pixel(2, 0), qRgb(0, 0, 255));
        // Flat color and transparency survive every filter; shrinking averages.
        QImage flat(90, 60, QImage::Format_ARGB32_Premultiplied);
        flat.fill(QColor(10, 200, 30, 128));
        for (int f = 0; f <= 6; ++f) {
            const QImage r = filters::resample(flat, QSize(31, 17), filters::Resample(f)).convertToFormat(QImage::Format_ARGB32);
            const QRgb p = r.pixel(15, 8);
            QVERIFY2(std::abs(qAlpha(p) - 128) <= 1 && std::abs(qGreen(p) - 200) <= 3, qPrintable(QString::number(f)));
        }
        // Resolution: kept, or set without resampling.
        QImage dpi(50, 50, QImage::Format_ARGB32_Premultiplied);
        dpi.fill(Qt::white);
        dpi.setDotsPerMeterX(11811); // 300 dpi
        dpi.setDotsPerMeterY(11811);
        const DocState s{dpi, {}, {}};
        QCOMPARE(docops::resizeImage(s, QSize(25, 25), 0, true).base.dotsPerMeterX(), 11811);
        const DocState only = docops::resizeImage(s, QSize(50, 50), 0, false, 150);
        QCOMPARE(only.base.size(), QSize(50, 50));
        QCOMPARE(only.base.dotsPerMeterX(), int(std::lround(150 / 0.0254)));
    }

    void extendedFormatsRoundTrip()
    {
        QImage img(40, 30, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(200, 30, 30, 128));
        QTemporaryDir dir;
        const QList<QByteArray> writable = QImageWriter::supportedImageFormats();
        QList<QByteArray> offered;
        for (const auto &f : imageio::exportFormats())
            offered << f.format;
        QVERIFY(offered.contains("tiff")); // qt6-image-formats-plugins
        for (const QByteArray fmt : {"tiff", "avif", "heif", "jxl", "qoi"}) {
            QCOMPARE(offered.contains(fmt), writable.contains(fmt));
            if (!offered.contains(fmt))
                continue; // KImageFormats not installed
            imageio::SaveOptions o;
            o.format = fmt;
            o.quality = 100;
            const QString path = dir.filePath(QStringLiteral("x.") + QString::fromLatin1(fmt));
            QString error;
            QVERIFY2(imageio::save(img, path, o, &error), qPrintable(error));
            const imageio::LoadResult r = imageio::loadFile(path);
            QVERIFY2(r.ok(), fmt.constData());
            QCOMPARE(r.image.size(), img.size());
            QVERIFY2(r.image.hasAlphaChannel(), fmt.constData());
        }
        // LZW makes a flat TIFF much smaller.
        imageio::SaveOptions t;
        t.format = "tiff";
        const qsizetype lzw = imageio::encode(img, t).size();
        t.tiffLzw = false;
        QVERIFY(lzw > 0 && lzw < imageio::encode(img, t).size());
    }

    void failedSaveKeepsExistingFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("keep.png"));
        QVERIFY(solid(2, 2, Qt::red).save(path));
        const QByteArray before = [&] { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); }();
        imageio::SaveOptions o;
        o.format = "nonexistent-format";
        QString err;
        QVERIFY(!imageio::save(solid(4, 4, Qt::blue), path, o, &err));
        QVERIFY(!err.isEmpty());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);

        QVERIFY(!imageio::save(solid(4, 4, Qt::blue), QStringLiteral("/nonexistent-dir/x.png"),
                               imageio::SaveOptions{}, &err));
    }


    // ================================================================ stage 4: objects, project format

    DocState richDocument()
    {
        DocState s{gradient(300, 200), {}};
        auto img = imageObject(40, 20, Qt::blue, QPointF(10, 10));
        img->transform = QTransform::fromScale(1.5, 1.5) * QTransform::fromTranslate(10, 10);
        auto text = std::make_shared<TextObject>();
        text->text = QStringLiteral("Bublina ž 日本");
        text->bubble = true;
        text->tail = QPointF(30, 150);
        text->background = Qt::white;
        text->color = QColor(20, 20, 20);
        text->pixelSize = 18;
        text->transform = QTransform::fromTranslate(120, 20);
        text->spans = {TextSpan{0, 7, TextSpan::Bold | TextSpan::Color, TextSpan::Bold, QColor(200, 0, 0)}};
        auto line = std::make_shared<LineObject>();
        line->p1 = QPointF(10, 190);
        line->p2 = QPointF(200, 120);
        line->headStart = true;
        auto cover = std::make_shared<ShapeObject>();
        cover->style = ShapeObject::Style::Cover;
        cover->color = Qt::black;
        cover->rect = QRectF(220, 150, 60, 30);
        auto ellipse = std::make_shared<ShapeObject>();
        ellipse->kind = ShapeObject::Kind::Ellipse;
        ellipse->rect = QRectF(150, 100, 80, 50);
        ellipse->fill = QColor(0, 255, 0, 100);
        auto stroke = std::make_shared<StrokeObject>();
        stroke->highlighter = true;
        stroke->color = QColor(255, 235, 59);
        stroke->width = 14;
        stroke->points << QPointF(20, 60) << QPointF(80, 70) << QPointF(140, 60);
        auto marker = std::make_shared<MarkerObject>();
        marker->center = QPointF(260, 30);
        marker->number = 7;
        auto poly = std::make_shared<PolylineObject>();
        poly->points << QPointF(30, 100) << QPointF(80, 140) << QPointF(120, 100);
        poly->headEnd = true;
        auto polygon = std::make_shared<PolylineObject>();
        polygon->closed = true;
        polygon->fill = QColor(255, 0, 0, 80);
        polygon->points << QPointF(200, 60) << QPointF(240, 90) << QPointF(190, 100);
        s.objects = {img, text, line, cover, ellipse, stroke, marker, poly, polygon};
        return s;
    }

    void zipRoundTrip()
    {
        QCOMPARE(zip::crc32(QByteArrayLiteral("123456789")), 0xCBF43926u);
        const QList<zip::Entry> in = {{QStringLiteral("a.txt"), QByteArrayLiteral("hello")},
                                      {QStringLiteral("složka/ž.bin"), QByteArray(1000, '\x07')},
                                      {QStringLiteral("empty"), QByteArray()}};
        QList<zip::Entry> out;
        QVERIFY(zip::read(zip::write(in), &out));
        QCOMPARE(out.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(out[i].name, in[i].name);
            QCOMPARE(out[i].data, in[i].data);
        }
        QByteArray broken = zip::write(in);
        broken[40] = char(broken[40] ^ 0x55); // inside the first file's data
        QString err;
        QVERIFY(!zip::read(broken, &out, &err));
        QVERIFY(!zip::read(QByteArrayLiteral("not a zip"), &out, &err));
    }

    void projectRoundTripKeepsEverything()
    {
        const DocState s = richDocument();
        const QByteArray data = project::encode(s);
        DocState back;
        QString err;
        QVERIFY2(project::decode(data, &back, &err), qPrintable(err));
        QCOMPARE(back.base, s.base);
        QCOMPARE(back.objects.size(), s.objects.size());
        for (int i = 0; i < s.objects.size(); ++i) {
            QCOMPARE(back.objects[i]->type(), s.objects[i]->type());
            QCOMPARE(back.objects[i]->transform, s.objects[i]->transform);
            for (Prop p : s.objects[i]->props())
                QCOMPARE(back.objects[i]->prop(p), s.objects[i]->prop(p));
            QCOMPARE(back.objects[i]->visualRect(), s.objects[i]->visualRect());
        }
        QCOMPARE(docops::composite(back), docops::composite(s)); // pixel-identical result
        const auto *t = static_cast<const TextObject *>(back.objects[1].get());
        QCOMPARE(t->text, QStringLiteral("Bublina ž 日本"));
        QVERIFY(t->bubble);
        QCOMPARE(t->spans, static_cast<const TextObject *>(s.objects[1].get())->spans);
        QCOMPARE(static_cast<const MarkerObject *>(back.objects[6].get())->number, 7);
        QVERIFY(project::hasCovers(back));
        // The archive is a plain ZIP: document.json is readable JSON.
        QList<zip::Entry> entries;
        QVERIFY(zip::read(data, &entries));
        QCOMPARE(entries.first().name, QStringLiteral("document.json"));
        QVERIFY(entries.first().data.contains("\"format\": \"rolidan\""));
    }

    void readsExifFromJpegPngWebpAndTiff()
    {
        for (bool le : {true, false}) {
            const exif::Info e = exif::readTiff(fixture::sampleTiff(le));
            QVERIFY(e.valid);
            QCOMPARE(e.make, QStringLiteral("Canon"));
            QCOMPARE(e.model, QStringLiteral("Canon EOS R6"));
            QCOMPARE(e.lens, QStringLiteral("RF 35mm F1.8"));
            QCOMPARE(e.artist, QStringLiteral("Jana Novakova"));
            QCOMPARE(e.dateTaken, QStringLiteral("2024:05:01 12:30:45"));
            QCOMPARE(e.iso, 200);
            QVERIFY(qAbs(e.exposure - 1.0 / 250) < 1e-9);
            QVERIFY(qAbs(e.fNumber - 2.8) < 1e-9);
            QCOMPARE(e.focalLength, 35.0);
            QCOMPARE(e.flash, 16);
            QCOMPARE(e.orientation, 1);
            QVERIFY(e.hasGps);
            QVERIFY(qAbs(e.latitude - (50 + 5 / 60.0 + 15 / 3600.0)) < 1e-6);
            QVERIFY(qAbs(e.longitude - (14 + 25 / 60.0 + 17.04 / 3600.0)) < 1e-6);
            QVERIFY(e.hasAltitude);
            QCOMPARE(e.altitude, 235.0);
        }
        QCOMPARE(exif::formatExposure(1.0 / 250), QStringLiteral("1/250 s"));
        QCOMPARE(exif::formatExposure(2.5), QStringLiteral("2.5 s"));

        // Containers: JPEG APP1, PNG eXIf, WebP EXIF chunk.
        const QByteArray jpeg = fixture::jpegWithExif();
        QCOMPARE(exif::read(jpeg).model, QStringLiteral("Canon EOS R6"));
        QImage decoded;
        QVERIFY(decoded.loadFromData(jpeg)); // still a valid JPEG
        const QByteArray tiff = fixture::sampleTiff();
        QByteArray png("\x89PNG\r\n\x1a\n", 8);
        auto chunk = [](const QByteArray &type, const QByteArray &data) {
            QByteArray c(4, 0);
            qToBigEndian(quint32(data.size()), c.data());
            return c + type + data + QByteArray(4, 0); // CRC is not checked by the reader
        };
        png += chunk("IHDR", QByteArray(13, 1)) + chunk("eXIf", tiff) + chunk("IDAT", "x") + chunk("IEND", {});
        QCOMPARE(exif::read(png).iso, 200);
        QByteArray webpBody = QByteArray("WEBP") + "VP8X" + QByteArray("\x0a\0\0\0", 4) + QByteArray(10, 0);
        QByteArray len(4, 0);
        qToLittleEndian(quint32(tiff.size()), len.data());
        webpBody += QByteArray("EXIF") + len + tiff + (tiff.size() & 1 ? QByteArray(1, 0) : QByteArray());
        QByteArray riffLen(4, 0);
        qToLittleEndian(quint32(webpBody.size()), riffLen.data());
        QCOMPARE(exif::read(QByteArray("RIFF") + riffLen + webpBody).make, QStringLiteral("Canon"));

        // No EXIF, garbage and truncated data never crash.
        QImage plain(8, 8, QImage::Format_RGB32);
        plain.fill(Qt::red);
        QByteArray plainJpeg;
        QBuffer b(&plainJpeg);
        b.open(QIODevice::WriteOnly);
        plain.save(&b, "JPEG");
        QVERIFY(!exif::read(plainJpeg).valid);
        QVERIFY(!exif::read("garbage").valid);
        for (int cut = 0; cut < jpeg.size(); cut += 7)
            exif::read(jpeg.left(cut));
        QByteArray corrupt = tiff;
        for (int i = 8; i < corrupt.size(); i += 5)
            corrupt[i] = char(0xFF);
        exif::readTiff(corrupt);
    }

    void recentFilesList()
    {
        QTemporaryDir dir;
        settings::clearRecentFiles();
        QStringList paths;
        for (int i = 0; i < settings::kMaxRecent + 3; ++i) {
            paths << dir.filePath(QStringLiteral("r%1.png").arg(i));
            QVERIFY(solid(4, 4, Qt::red).save(paths.last()));
            settings::addRecentFile(paths.last());
        }
        settings::addRecentFile(paths[5]); // again: moves to the top, no duplicate
        QStringList r = settings::recentFiles();
        QCOMPARE(r.size(), settings::kMaxRecent);
        QCOMPARE(r.first(), paths[5]);
        QCOMPARE(r.count(paths[5]), 1);
        QFile::remove(paths.last()); // vanished files are skipped
        r = settings::recentFiles();
        QVERIFY(!r.contains(paths.last()));
        settings::removeRecentFile(paths[5]);
        QVERIFY(!settings::recentFiles().contains(paths[5]));
        settings::clearRecentFiles();
        QVERIFY(settings::recentFiles().isEmpty());
    }

    void screenshotPortalResponse()
    {
        QCOMPARE(ScreenCapture::requestPath(QStringLiteral(":1.42"), QStringLiteral("rolidan7")),
                 QStringLiteral("/org/freedesktop/portal/desktop/request/1_42/rolidan7"));
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("Screenshot.png"));
        QImage shot(64, 32, QImage::Format_RGB32);
        shot.fill(Qt::green);
        QVERIFY(shot.save(file));
        QString error;
        const QImage img = ScreenCapture::imageFromResponse(
            0, {{QStringLiteral("uri"), QUrl::fromLocalFile(file).toString()}}, &error);
        QCOMPARE(img.size(), QSize(64, 32));
        QVERIFY(error.isEmpty());
        // Cancelled: no image and no error message; other codes are errors.
        QVERIFY(ScreenCapture::imageFromResponse(1, {}, &error).isNull());
        QVERIFY(error.isEmpty());
        QVERIFY(ScreenCapture::imageFromResponse(2, {}, &error).isNull());
        QVERIFY(!error.isEmpty());
    }

    void printPlacement()
    {
        const QRectF page(0, 0, 2000, 1000); // device pixels at 200 dpi
        PrintLayout l;
        // Fit: as large as the page allows, centered.
        QCOMPARE(printing::targetRect(QSizeF(400, 100), page, 200, l), QRectF(0, 250, 2000, 500));
        // Actual size: 300 px at 100 dpi = 3 inches = 600 device pixels.
        l.size = PrintLayout::Size::Actual;
        l.dpi = 100;
        QCOMPARE(printing::targetRect(QSizeF(300, 100), page, 200, l).size(), QSizeF(600, 200));
        l.center = false;
        QCOMPARE(printing::targetRect(QSizeF(300, 100), page, 200, l).topLeft(), QPointF(0, 0));
        l.size = PrintLayout::Size::Scale;
        l.percent = 50;
        QCOMPARE(printing::targetRect(QSizeF(300, 100), page, 200, l).size(), QSizeF(300, 100));
        // Resolution stored in the image; 96 dpi means "not set".
        QImage img(10, 10, QImage::Format_RGB32);
        img.setDotsPerMeterX(int(std::lround(300 / 0.0254)));
        QVERIFY(std::abs(printing::imageDpi(img) - 300) < 0.1);
        img.setDotsPerMeterX(int(std::lround(96 / 0.0254)));
        QCOMPARE(printing::imageDpi(img), 0.0);
    }

    void rotatedShapes()
    {
        ShapeObject sh;
        sh.rect = QRectF(100, 100, 100, 40);
        sh.style = ShapeObject::Style::Cover;
        sh.rotate(30, QPointF(150, 120));
        QVERIFY(std::abs(sh.rotation() - 30.0) < 1e-6);
        QCOMPARE(sh.frameRect().size().toSize(), QSize(100, 40));
        // Hit test follows the rotated box: the far corner of the old box is outside.
        QVERIFY(sh.contains(QPointF(150, 120)));
        QVERIFY(!sh.contains(QPointF(101, 101)));
        QVERIFY(sh.contains(QTransform(sh.transform).map(QPointF(195, 135))));
        // Another 60°: upright again with swapped sides, no leftover transform.
        sh.rotate(60, QPointF(150, 120));
        QVERIFY(sh.transform.isIdentity());
        QCOMPARE(sh.rect.toRect(), QRect(130, 70, 40, 100));
        // Survives the clipboard format, mirroring and moving.
        sh.rotate(20, QPointF(150, 120));
        const ObjectList back = deserializeObjects(serializeObjects({std::make_shared<ShapeObject>(sh)}));
        QCOMPARE(back.size(), 1);
        QVERIFY(std::abs(back.first()->rotation() - 20.0) < 1e-6);
        auto m = back.first()->clone();
        m->applyDocumentTransform(QTransform::fromScale(-1, 1) * QTransform::fromTranslate(400, 0),
                                  DocTransformKind::Mirror);
        QVERIFY(std::abs(m->rotation() + 20.0) < 1e-6);
        QVERIFY(std::abs(m->boundingRect().center().x() - 250) < 1e-6);
        QVERIFY(m->transform.determinant() > 0);
        // Point geometry rotates its points.
        StrokeObject st;
        st.points = QPolygonF{QPointF(0, 0), QPointF(10, 0)};
        st.rotate(90, QPointF(0, 0));
        QVERIFY(QLineF(st.points[1], QPointF(0, 10)).length() < 1e-9);
        QCOMPARE(st.rotation(), 0.0);
    }

    void textSpansFormatPartsOfText()
    {
        auto plain = std::make_shared<TextObject>();
        plain->text = QStringLiteral("normal BOLD red under");
        plain->bold = false;
        plain->pixelSize = 24;
        plain->color = Qt::black;
        plain->transform = QTransform::fromTranslate(10, 10);
        auto rich = std::static_pointer_cast<TextObject>(plain->clone());
        rich->spans = {TextSpan{7, 4, TextSpan::Bold, TextSpan::Bold, {}},
                       TextSpan{12, 3, TextSpan::Color, 0, QColor(255, 0, 0)},
                       TextSpan{16, 5, TextSpan::Underline | TextSpan::Italic, TextSpan::Underline | TextSpan::Italic, {}}};
        // Document <-> spans round trip (the on-canvas editor uses this).
        QTextDocument doc;
        rich->fillDocument(&doc);
        QCOMPARE(rich->spansFrom(&doc), rich->spans);
        QTextDocument doc2;
        plain->fillDocument(&doc2);
        QVERIFY(plain->spansFrom(&doc2).isEmpty());
        // A span that repeats the object's own value is no span.
        auto bold = std::static_pointer_cast<TextObject>(plain->clone());
        bold->bold = true;
        QTextDocument doc3;
        bold->fillDocument(&doc3);
        TextObject::applySpans(&doc3, {TextSpan{0, 6, TextSpan::Bold, TextSpan::Bold, {}}});
        QVERIFY(bold->spansFrom(&doc3).isEmpty());
        QCOMPARE(rich->boldAt(8), true);
        QCOMPARE(rich->boldAt(2), false);
        // Rendering: bold is wider, red pixels exist only with the span.
        QVERIFY(rich->visualRect().width() > plain->visualRect().width());
        const QImage a = docops::composite(DocState{solid(500, 60, Qt::white), {plain}, {}});
        const QImage b = docops::composite(DocState{solid(500, 60, Qt::white), {rich}, {}});
        auto reds = [](const QImage &img) {
            int n = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QRgb c = img.pixel(x, y);
                    n += qRed(c) > 180 && qGreen(c) < 80 && qBlue(c) < 80;
                }
            return n;
        };
        QCOMPARE(reds(a), 0);
        QVERIFY(reds(b) > 20);
        // Clipboard and project keep the spans.
        const ObjectList back = deserializeObjects(serializeObjects({rich}));
        QCOMPARE(static_cast<const TextObject *>(back.first().get())->spans, rich->spans);
        DocState p;
        QString err;
        QVERIFY(project::decode(project::encode(DocState{solid(50, 50, Qt::white), {rich}, {}}), &p, &err));
        QCOMPARE(static_cast<const TextObject *>(p.objects.first().get())->spans, rich->spans);
        QCOMPARE(docops::composite(p), docops::composite(DocState{solid(50, 50, Qt::white), {rich}, {}}));
    }

    void notesInHistoryProjectAndTransforms()
    {
        Document doc;
        doc.reset(solid(40, 20, Qt::white));
        DocState s = doc.state();
        s.notes = QStringLiteral("# Úkol\n- opravit tlačítko 日本");
        doc.commit(s, QStringLiteral("notes"), 3000);
        s.notes += QStringLiteral("\n- další");
        doc.commit(s, QStringLiteral("notes"), 3000); // merged with the previous step
        QCOMPARE(doc.history().count(), 2);
        doc.undo();
        QVERIFY(doc.state().notes.isEmpty());
        doc.redo();
        QVERIFY(doc.state().notes.endsWith(QStringLiteral("další")));
        // Image operations keep the notes.
        QCOMPARE(docops::rotate90(doc.state(), true).notes, doc.state().notes);
        QCOMPARE(docops::crop(doc.state(), QRect(0, 0, 10, 10)).notes, doc.state().notes);
        QCOMPARE(docops::mergeIntoBase(doc.state(), {}).notes, doc.state().notes);
        // Saved in the project as notes.md (UTF-8), absent when empty.
        const QByteArray data = project::encode(doc.state());
        DocState back;
        QVERIFY(project::decode(data, &back, nullptr));
        QCOMPARE(back.notes, doc.state().notes);
        QList<zip::Entry> entries;
        QVERIFY(zip::read(data, &entries));
        QVERIFY(std::any_of(entries.begin(), entries.end(),
                            [](const zip::Entry &e) { return e.name == QStringLiteral("notes.md"); }));
        QVERIFY(zip::read(project::encode(DocState{solid(4, 4, Qt::red), {}, {}}), &entries));
        QVERIFY(std::none_of(entries.begin(), entries.end(),
                             [](const zip::Entry &e) { return e.name == QStringLiteral("notes.md"); }));
    }

    // ---------------------------------------------------------------- filters

    static QImage argb(const QImage &i) { return i.convertToFormat(QImage::Format_ARGB32); }

    void filterIdentities()
    {
        const QImage g = gradient(40, 30);
        QCOMPARE(argb(filters::brightnessContrast(g, 0, 0)), argb(g));
        QCOMPARE(argb(filters::hueSaturation(g, 0, 0, 0)), argb(g));
        QCOMPARE(argb(filters::levels(g, 0, 255, 1.0)), argb(g));
        QCOMPARE(argb(filters::invert(filters::invert(g))), argb(g));
        QCOMPARE(argb(filters::temperature(g, 0, 0)), argb(g));
        QCOMPARE(argb(filters::sepia(g, 0)), argb(g));
        QCOMPARE(argb(filters::gaussianBlur(g, 0.2)), argb(g));
        QCOMPARE(argb(filters::pixelate(g, 1)), argb(g));
        // Every registered filter runs and keeps the size; defaults are in range.
        for (const filters::Spec &spec : filters::all()) {
            const DocState out = filters::apply(spec, DocState{g, {}, QStringLiteral("n")}, {}, {});
            QVERIFY2(!out.base.isNull(), qPrintable(spec.id));
            QCOMPARE(out.base.format(), QImage::Format_ARGB32_Premultiplied);
            QCOMPARE(out.notes, QStringLiteral("n"));
            if (spec.id != QLatin1String("rotate"))
                QCOMPARE(out.base.size(), g.size());
            for (const filters::Param &p : spec.params)
                QVERIFY2(p.def >= p.min && p.def <= p.max, qPrintable(spec.id + p.key));
        }
    }

    void colorAdjustments()
    {
        const QImage red = solid(4, 4, QColor(200, 30, 30));
        const QRgb gray = argb(filters::hueSaturation(red, 0, -100, 0)).pixel(1, 1);
        QCOMPARE(qRed(gray), qGreen(gray));
        QCOMPARE(qGreen(gray), qBlue(gray));
        const QRgb cyan = argb(filters::hueSaturation(red, 180, 0, 0)).pixel(1, 1);
        QVERIFY(qRed(cyan) < qGreen(cyan) && qRed(cyan) < qBlue(cyan));
        QVERIFY(qRed(argb(filters::brightnessContrast(red, 50, 0)).pixel(0, 0)) > 200);
        QVERIFY(qGreen(argb(filters::brightnessContrast(red, 50, 0)).pixel(0, 0)) > 30);
        // Gamma brightens the midtones, keeps black and white.
        QImage ramp(3, 1, QImage::Format_ARGB32);
        ramp.setPixel(0, 0, qRgb(0, 0, 0));
        ramp.setPixel(1, 0, qRgb(128, 128, 128));
        ramp.setPixel(2, 0, qRgb(255, 255, 255));
        const QImage gm = argb(filters::brightnessContrast(ramp, 0, 0, 2.0));
        QCOMPARE(qRed(gm.pixel(0, 0)), 0);
        QVERIFY(qRed(gm.pixel(1, 0)) > 170);
        QCOMPARE(qRed(gm.pixel(2, 0)), 255);
        const QRgb warm = argb(filters::temperature(solid(2, 2, QColor(128, 128, 128)), 100, 0)).pixel(0, 0);
        QVERIFY(qRed(warm) > qBlue(warm));
        const QRgb g = argb(filters::grayscale(red)).pixel(0, 0);
        QVERIFY(qRed(g) == qGreen(g) && qGreen(g) == qBlue(g));
        const QRgb sep = argb(filters::sepia(solid(2, 2, QColor(100, 100, 100)), 100)).pixel(0, 0);
        QVERIFY(qRed(sep) > qGreen(sep) && qGreen(sep) > qBlue(sep));
        // Threshold and posterize produce only the allowed values.
        const QImage t = argb(filters::threshold(gradient(30, 30), 128));
        const QImage pz = argb(filters::posterize(gradient(30, 30), 3));
        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 30; ++x) {
                QVERIFY(qRed(t.pixel(x, y)) == 0 || qRed(t.pixel(x, y)) == 255);
                const int v = qRed(pz.pixel(x, y));
                QVERIFY(v == 0 || v == 128 || v == 127 || v == 255);
            }
        // Gamma above 1 brightens midtones, black/white points stretch.
        QVERIFY(qRed(argb(filters::levels(solid(2, 2, QColor(128, 128, 128)), 0, 255, 2.0)).pixel(0, 0)) > 170);
        QCOMPARE(qRed(argb(filters::levels(solid(2, 2, QColor(100, 100, 100)), 100, 200, 1.0)).pixel(0, 0)), 0);
        // Transparent pixels stay transparent.
        QCOMPARE(qAlpha(argb(filters::invert(solid(2, 2, Qt::transparent))).pixel(0, 0)), 0);
    }

    void enhanceColorsStretchesAndRemovesCast()
    {
        // Dull, bluish, low-contrast image: values 90..150, blue +30.
        QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const int v = 90 + x * 60 / 63;
                img.setPixel(x, y, qRgb(v, v, std::min(255, v + 30)));
            }
        const QImage out = argb(filters::enhanceColors(img, 100, 0, 0.5));
        QVERIFY(qRed(out.pixel(0, 5)) < 10);
        QVERIFY(qRed(out.pixel(63, 5)) > 245);
        const QRgb mid = out.pixel(32, 5);
        QVERIFY2(std::abs(qBlue(mid) - qRed(mid)) < 8, "color cast removed"); // gray again
        // Strength 0 and vibrance 0 change nothing.
        QCOMPARE(argb(filters::enhanceColors(img, 0, 0, 0.5)), argb(img));
        // Vibrance boosts a dull color's saturation.
        const QRgb dull = argb(filters::enhanceColors(solid(4, 4, QColor(140, 120, 120)), 0, 100, 0)).pixel(0, 0);
        QVERIFY(qRed(dull) - qGreen(dull) > 20);
    }

    void blurAndSharpen()
    {
        // Constant image is unchanged by blur; the mean is preserved.
        QCOMPARE(argb(filters::gaussianBlur(solid(50, 40, QColor(10, 200, 30)), 6)),
                 argb(solid(50, 40, QColor(10, 200, 30))));
        // Red square on transparency: blurred edges stay red (no dark halo).
        QImage sq(60, 60, QImage::Format_ARGB32_Premultiplied);
        sq.fill(Qt::transparent);
        QPainter(&sq).fillRect(20, 20, 20, 20, Qt::red);
        const QImage b = argb(filters::gaussianBlur(sq, 8));
        const QRgb edge = b.pixel(17, 30);
        QVERIFY(qAlpha(edge) > 20 && qAlpha(edge) < 230);
        QVERIFY(qRed(edge) > 240 && qGreen(edge) < 10);
        QVERIFY(qAlpha(b.pixel(30, 30)) > 200);
        // Sharpen increases contrast at an edge, threshold protects small differences.
        QImage step(40, 10, QImage::Format_ARGB32_Premultiplied);
        for (int x = 0; x < 40; ++x)
            for (int y = 0; y < 10; ++y)
                step.setPixel(x, y, x < 20 ? qRgb(80, 80, 80) : qRgb(160, 160, 160));
        const QImage sh = argb(filters::sharpen(step, 150, 2, 0));
        QVERIFY(qRed(sh.pixel(19, 5)) < 80);
        QVERIFY(qRed(sh.pixel(20, 5)) > 160);
        QCOMPARE(argb(filters::sharpen(step, 150, 2, 64)), argb(step));
    }

    void pixelateNoiseMedian()
    {
        const QImage px8 = argb(filters::pixelate(gradient(32, 32), 8));
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                QCOMPARE(px8.pixel(x, y), px8.pixel(0, 0));
        QVERIFY(px8.pixel(0, 0) != px8.pixel(8, 0));

        const QImage gray = solid(64, 64, QColor(128, 128, 128));
        const QImage mono = argb(filters::addNoise(gray, 30, false));
        const QImage color = argb(filters::addNoise(gray, 30, true));
        QCOMPARE(argb(filters::addNoise(gray, 30, true)), color); // deterministic
        double mean = 0;
        int colored = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const QRgb m = mono.pixel(x, y), c = color.pixel(x, y);
                QVERIFY(qRed(m) == qGreen(m) && qGreen(m) == qBlue(m));
                colored += qRed(c) != qGreen(c);
                mean += qRed(m);
            }
        QVERIFY(colored > 3000);
        QVERIFY(std::abs(mean / 4096 - 128) < 3);
        QVERIFY(mono != argb(gray));

        // Median removes isolated specks but keeps a straight edge.
        QImage speck = solid(20, 20, Qt::white);
        speck.setPixel(10, 10, qRgb(0, 0, 0));
        QPainter(&speck).fillRect(0, 15, 20, 5, Qt::black);
        const QImage med = argb(filters::median(speck, 1));
        QCOMPARE(med.pixel(10, 10), qRgb(255, 255, 255));
        QCOMPARE(med.pixel(5, 17), qRgb(0, 0, 0));
        QCOMPARE(med.pixel(5, 13), qRgb(255, 255, 255));
    }

    void cleanScannedDocument()
    {
        // Paper with a strong shadow from left to right and a yellow cast, dark text.
        QImage scan(300, 200, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 300; ++x) {
                const int l = 230 - x * 110 / 299; // 230 → 120
                scan.setPixel(x, y, qRgb(l, l, l * 8 / 10));
            }
        QPainter p(&scan);
        for (int line = 0; line < 6; ++line)
            p.fillRect(20, 20 + line * 28, 260, 6, QColor(40, 40, 30));
        p.end();
        const QImage out = argb(filters::cleanDocument(scan, 1, 100, 30, 75, false));
        // Paper is white everywhere, text stays dark, no color left.
        QVERIFY2(qRed(out.pixel(10, 10)) > 245, "light paper");
        QVERIFY2(qRed(out.pixel(290, 190)) > 240, qPrintable(QString::number(qRed(out.pixel(290, 190)))));
        QVERIFY(qRed(out.pixel(150, 22)) < 90);
        QVERIFY(qRed(out.pixel(290, 22)) < 120 || qRed(out.pixel(279, 22)) < 120);
        QCOMPARE(qRed(out.pixel(100, 100)), qBlue(out.pixel(100, 100)));
        const QImage bw = argb(filters::cleanDocument(scan, 2, 100, 30, 75, true));
        for (int y = 0; y < 200; y += 3)
            for (int x = 0; x < 300; x += 3)
                QVERIFY(qRed(bw.pixel(x, y)) == 0 || qRed(bw.pixel(x, y)) == 255);
        QCOMPARE(qRed(bw.pixel(150, 22)), 0);
        QCOMPARE(qRed(bw.pixel(250, 100)), 255);
    }

    void freeRotation()
    {
        DocState s{gradient(40, 20), {}, {}};
        auto rect = std::make_shared<ShapeObject>();
        rect->rect = QRectF(10, 5, 6, 4);
        auto line = std::make_shared<LineObject>();
        line->p1 = QPointF(0, 0);
        line->p2 = QPointF(40, 20);
        s.objects = {rect, line};
        // 0° keeps everything.
        const DocState same = filters::rotate(s, 0, 0, 0);
        QCOMPARE(argb(same.base), argb(s.base));
        // 90° expanded: size swaps, like the exact rotation.
        const DocState r90 = filters::rotate(s, 90, 0, 0);
        QCOMPARE(r90.base.size(), QSize(20, 40));
        // Arbitrary angle: canvas grows; boxes keep size, points rotate exactly.
        const DocState r = filters::rotate(s, 10.5, 0, 1);
        QVERIFY(r.base.width() > 40 && r.base.height() > 20);
        const auto *rr = static_cast<const ShapeObject *>(r.objects[0].get());
        QCOMPARE(rr->rect.size(), QSizeF(6, 4));
        const auto *rl = static_cast<const LineObject *>(r.objects[1].get());
        QVERIFY(std::abs(QLineF(rl->p1, rl->p2).length() - QLineF(QPointF(0, 0), QPointF(40, 20)).length()) < 1e-6);
        // White fill: corners are white and opaque.
        QCOMPARE(argb(r.base).pixel(0, 0), qRgb(255, 255, 255));
        // Keep size / crop: crop has no transparent corners.
        QCOMPARE(filters::rotate(s, 10.5, 1, 0).base.size(), QSize(40, 20));
        const DocState c = filters::rotate(DocState{solid(400, 300, Qt::red), {}, {}}, 7, 2, 0);
        QVERIFY(c.base.width() < 400 && c.base.height() < 300 && c.base.width() > 300);
        const QImage ca = argb(c.base);
        QVERIFY(qAlpha(ca.pixel(0, 0)) > 200);
        QVERIFY(qAlpha(ca.pixel(ca.width() - 1, ca.height() - 1)) > 200);
    }

    void detectsDocumentSkew()
    {
        auto page = [](double degrees) {
            QImage img(800, 600, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            QPainter p(&img);
            p.translate(400, 300);
            p.rotate(degrees);
            p.translate(-400, -300);
            for (int line = 0; line < 14; ++line)
                for (int word = 0; word < 9; ++word)
                    p.fillRect(80 + word * 72, 80 + line * 32, 50 + (word * 7 + line * 3) % 15, 9, Qt::black);
            return img;
        };
        QVERIFY(std::abs(filters::detectSkew(page(0))) < 0.15);
        QVERIFY2(std::abs(filters::detectSkew(page(3.0)) - 3.0) < 0.2,
                 qPrintable(QString::number(filters::detectSkew(page(3.0)))));
        QVERIFY(std::abs(filters::detectSkew(page(-1.7)) + 1.7) < 0.2);
        QCOMPARE(filters::detectSkew(solid(100, 100, Qt::white)), 0.0);
        // Rotating by the negative skew straightens the page.
        const DocState fixed = filters::rotate(DocState{page(3.0), {}, {}}, -filters::detectSkew(page(3.0)), 1, 1);
        QVERIFY(std::abs(filters::detectSkew(fixed.base)) < 0.2);
    }

    void filterOnSelectionOnly()
    {
        const DocState s{gradient(60, 40), {}, {}};
        const QRect sel(10, 10, 20, 15);
        const DocState out = filters::apply(*filters::find(QStringLiteral("blur")), s, sel,
                                            {{QStringLiteral("radius"), 6.0}});
        const QImage a = argb(s.base), b = argb(out.base);
        bool changedInside = false;
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 60; ++x) {
                if (!sel.contains(x, y))
                    QCOMPARE(b.pixel(x, y), a.pixel(x, y));
                else
                    changedInside |= b.pixel(x, y) != a.pixel(x, y);
            }
        QVERIFY(changedInside);
        // Blur inside uses context outside the selection: no hard edge darkening.
        QVERIFY(std::abs(qGreen(b.pixel(10, 20)) - qGreen(a.pixel(10, 20))) < 40);
    }

    void filterPerformance4K()
    {
        const QImage big = gradient(3840, 2160);
        QElapsedTimer t;
        t.start();
        filters::gaussianBlur(big, 20);
        const qint64 blur = t.restart();
        filters::hueSaturation(big, 30, 20, 0);
        const qint64 hue = t.restart();
        filters::median(big, 1);
        const qint64 med = t.restart();
        qInfo("4K: blur r20 %lld ms, hue/sat %lld ms, median r1 %lld ms", blur, hue, med);
#ifdef NDEBUG
        QVERIFY(blur < 1500);
        QVERIFY(hue < 1500);
#endif
    }

    void batchConvertsFiles()
    {
        QTemporaryDir dir;
        QImage big(800, 400, QImage::Format_ARGB32_Premultiplied);
        big.fill(QColor(200, 50, 50));
        QVERIFY(big.save(dir.filePath(QStringLiteral("velký.png"))));
        QVERIFY(project::save(DocState{solid(50, 40, Qt::blue), {}, {}}, dir.filePath(QStringLiteral("p.rolidan")),
                              nullptr));
        batch::Options o;
        o.outputDir = dir.filePath(QStringLiteral("out"));
        o.save.format = "jpeg";
        o.shrink = true;
        o.maxSize = QSize(200, 200);
        o.adjust = batch::Adjust::Grayscale;
        o.suffix = QStringLiteral("-web");
        QString out, err;
        QVERIFY2(batch::processFile(dir.filePath(QStringLiteral("velký.png")), o, &out, &err), qPrintable(err));
        QCOMPARE(QFileInfo(out).fileName(), QStringLiteral("velký-web.jpg"));
        const QImage back(out);
        QCOMPARE(back.size(), QSize(200, 100));
        QVERIFY(std::abs(qRed(back.pixel(50, 50)) - qBlue(back.pixel(50, 50))) < 4); // grayscale
        // Never overwrites: second run gets a number.
        QVERIFY(batch::processFile(dir.filePath(QStringLiteral("velký.png")), o, &out, &err));
        QCOMPARE(QFileInfo(out).fileName(), QStringLiteral("velký-web (2).jpg"));
        // Projects are converted with their annotations; small images are not enlarged.
        o.adjust = batch::Adjust::None;
        o.save.format = "png";
        QVERIFY(batch::processFile(dir.filePath(QStringLiteral("p.rolidan")), o, &out, &err));
        QCOMPARE(QImage(out).size(), QSize(50, 40));
        // Unreadable input reports an error.
        QFile junk(dir.filePath(QStringLiteral("junk.png")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not an image");
        junk.close();
        QVERIFY(!batch::processFile(junk.fileName(), o, &out, &err));
        QVERIFY(!err.isEmpty());
    }

    void projectRejectsForeignAndNewer()
    {
        DocState out;
        QString err;
        QVERIFY(!project::decode(QByteArrayLiteral("garbage"), &out, &err));
        QVERIFY(!err.isEmpty());
        const QByteArray json = R"({"format":"rolidan","version":99,"objects":[]})";
        const QByteArray z = zip::write({{QStringLiteral("document.json"), json}});
        QVERIFY(!project::decode(z, &out, &err));
        QVERIFY(err.contains(QStringLiteral("newer")));
    }

    void newObjectsFollowDocumentTransforms()
    {
        const DocState s = richDocument();
        const DocState r = docops::rotate90(s, true); // (x, y) -> (h - y, x), h = 200
        const auto *m = static_cast<const MarkerObject *>(r.objects[6].get());
        QCOMPARE(m->center, QPointF(200 - 30, 260));
        const auto *t = static_cast<const TextObject *>(r.objects[1].get());
        QCOMPARE(t->tail, QPointF(200 - 150, 30)); // the tail still points at the same spot
        const auto *pl = static_cast<const PolylineObject *>(r.objects[7].get());
        QCOMPARE(pl->points.first(), QPointF(200 - 100, 30));
        const DocState scaled = docops::resizeImage(s, QSize(600, 400));
        const auto *ms = static_cast<const MarkerObject *>(scaled.objects[6].get());
        QCOMPARE(ms->diameter, 64.0);
        QCOMPARE(static_cast<const PolylineObject *>(scaled.objects[7].get())->width, 8.0);
    }

    void markerAndPolylineDetails()
    {
        MarkerObject m;
        m.color = QColor(255, 235, 59);
        QCOMPARE(m.textColor(), QColor(Qt::black)); // readable on yellow
        m.color = QColor(0xe5, 0x1c, 0x23);
        QCOMPARE(m.textColor(), QColor(Qt::white));
        m.center = QPointF(50, 50);
        QVERIFY(m.contains(QPointF(60, 50)));
        QVERIFY(!m.contains(QPointF(70, 50)));

        PolylineObject p;
        p.points << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 100);
        QVERIFY(p.contains(QPointF(50, 1)));
        QVERIFY(!p.contains(QPointF(50, 50)));
        p.closed = true;
        p.fill = Qt::red;
        QVERIFY(p.contains(QPointF(80, 40)));
        QVERIFY(p.props().contains(Prop::Fill));
        QVERIFY(!p.props().contains(Prop::HeadEnd));
        // Clipboard format round trip (version 3) for the new types.
        const ObjectList list = deserializeObjects(serializeObjects(richDocument().objects));
        QCOMPARE(list.size(), 9);
        QVERIFY(static_cast<const TextObject *>(list[1].get())->bubble);
        QCOMPARE(static_cast<const PolylineObject *>(list[8].get())->points.size(), 3);
    }

    // ================================================================ history storage (G1)

    void historyStoresOnlyChangedRectangles()
    {
        // 30 small retouch steps on a 4000x3000 base must not cost 30 full images.
        const QImage base = gradient(4000, 3000);
        const qint64 full = base.sizeInBytes();
        History h;
        h.reset({base, {}});
        QList<QImage> states{base};
        QImage cur = base;
        for (int i = 0; i < 30; ++i) {
            cur = cur.copy();
            QPainter(&cur).fillRect(100 + i * 50, 200, 120, 80, QColor(i * 8, 0, 255 - i * 8));
            h.push(QStringLiteral("stroke"), {cur, {}});
            states.append(cur);
        }
        QVERIFY2(h.memoryUsage() < 3 * full, qPrintable(QString::number(h.memoryUsage() / double(full))));
        // Undo / redo reproduce every step pixel for pixel.
        for (int i = 29; i >= 0; --i)
            QCOMPARE(h.undo().base, states[i]);
        for (int i = 1; i <= 30; ++i)
            QCOMPARE(h.redo().base, states[i]);
        // Object-only steps and size changes.
        auto obj = imageObject(10, 10, Qt::red, QPointF(5, 5));
        h.push(QStringLiteral("add"), {cur, {obj}});
        const QImage small = cur.copy(0, 0, 100, 100);
        h.push(QStringLiteral("crop"), {small, {obj}});
        QCOMPARE(h.undo().base, cur);
        QCOMPARE(h.current().objects.size(), 1);
        QCOMPARE(h.undo().objects.size(), 0);
        QCOMPARE(h.redo().objects.size(), 1);
        QCOMPARE(h.redo().base, small);
    }

    void historyTrimKeepsReconstructableSteps()
    {
        const QImage base = gradient(1000, 1000);
        History h;
        h.reset({base, {}});
        QList<QImage> states{base};
        QImage cur = base;
        for (int i = 0; i < 20; ++i) {
            cur = cur.copy();
            QPainter(&cur).fillRect(i * 40, i * 40, 100, 100, QColor(255, i * 12, 0));
            h.push(QStringLiteral("s"), {cur, {}});
            states.append(cur);
        }
        h.setLimitBytes(base.sizeInBytes() * 2 + 400000); // forces dropping old steps
        QVERIFY(h.count() < 21);
        const int kept = h.index();
        for (int i = 0; i < kept; ++i)
            QCOMPARE(h.undo().base, states[20 - i - 1]);
        QVERIFY(!h.canUndo());
    }

    void historyDiffRect()
    {
        QImage a = gradient(50, 40), b = a.copy();
        QCOMPARE(History::diffRect(a, b), QRect());
        b.setPixel(7, 9, 0);
        b.setPixel(30, 22, 0);
        QCOMPARE(History::diffRect(a, b), QRect(QPoint(7, 9), QPoint(30, 22)));
    }

    // ================================================================ stage 3: retouch

    void stampOpacityIsACeilingForTheWholeStroke()
    {
        // Spec D5 / acceptance: 50 % opacity, many overlapping dabs -> never above 50 %.
        QImage base = solid(300, 200, Qt::black);
        QPainter(&base).fillRect(0, 0, 150, 200, Qt::white); // source half
        retouch::Stroke::Params p;
        p.mode = retouch::Mode::Clone;
        p.diameter = 40;
        p.hardness = 1.0;
        p.opacity = 0.5;
        p.cloneOffset = QPoint(-130, 0);
        retouch::Stroke st(base, p);
        // zig-zag back and forth over the same spot: hundreds of overlapping dabs
        for (int i = 0; i < 40; ++i)
            st.lineTo(QPointF(i % 2 ? 190 : 230, 80 + (i % 5)));
        const QImage out = st.result().convertToFormat(QImage::Format_ARGB32);
        int maxRed = 0, at128 = 0;
        for (int y = 0; y < 200; ++y)
            for (int x = 150; x < 300; ++x) {
                maxRed = std::max(maxRed, qRed(out.pixel(x, y)));
                at128 += qRed(out.pixel(x, y)) == 128;
            }
        QCOMPARE(maxRed, 128);
        QVERIFY(at128 > 500);
        QVERIFY(!st.changedRect().isEmpty());
    }

    void hardnessControlsTheEdge()
    {
        auto profile = [](double hardness) {
            retouch::Stroke::Params p;
            p.mode = retouch::Mode::Erase;
            p.diameter = 100;
            p.hardness = hardness;
            retouch::Stroke st(solid(200, 200, Qt::white), p);
            st.lineTo(QPointF(100, 100));
            QList<int> alpha;
            const QImage out = st.result().convertToFormat(QImage::Format_ARGB32);
            for (int x = 100; x < 160; ++x)
                alpha.append(qAlpha(out.pixel(x, 100)));
            return alpha;
        };
        // 0 %: smooth ramp, no visible edge.
        const QList<int> soft = profile(0.0);
        QCOMPARE(soft.first(), 0);
        QCOMPARE(soft.last(), 255);
        for (int i = 1; i < soft.size(); ++i) {
            QVERIFY(soft[i] >= soft[i - 1]);      // monotonic
            QVERIFY(soft[i] - soft[i - 1] <= 12); // no step
        }
        // 100 %: sharp edge, only fully erased or untouched pixels.
        for (int a : profile(1.0))
            QVERIFY(a == 0 || a == 255);
        QCOMPARE(retouch::brushAlpha(10, 50, 0.5), 1.0);
        QCOMPARE(retouch::brushAlpha(50, 50, 0.5), 0.0);
        QCOMPARE(retouch::brushAlpha(37.5, 50, 0.5), 0.5); // smoothstep midpoint
    }

    void stampSourceOutsideImageCopiesNothing()
    {
        const QImage base = gradient(200, 100);
        retouch::Stroke::Params p;
        p.mode = retouch::Mode::Clone;
        p.diameter = 60;
        p.hardness = 1.0;
        p.cloneOffset = QPoint(-50, 0); // target x < 50 -> source outside on the left
        retouch::Stroke st(base, p);
        st.lineTo(QPointF(40, 50));
        st.lineTo(QPointF(60, 50));
        const QImage out = st.result();
        for (int y = 30; y < 70; ++y) {
            for (int x = 10; x < 50; ++x)
                QCOMPARE(out.pixel(x, y), base.pixel(x, y)); // untouched, no artifacts
        }
        QCOMPARE(out.pixel(60, 50), base.pixel(10, 50)); // copied from inside
    }

    void stampCopiesAlpha()
    {
        QImage base = solid(100, 50, Qt::white);
        QPainter p0(&base);
        p0.setCompositionMode(QPainter::CompositionMode_Clear);
        p0.fillRect(0, 0, 50, 50, Qt::transparent);
        p0.end();
        retouch::Stroke::Params p;
        p.mode = retouch::Mode::Clone;
        p.diameter = 20;
        p.hardness = 1.0;
        p.cloneOffset = QPoint(-50, 0);
        retouch::Stroke st(base, p);
        st.lineTo(QPointF(75, 25));
        QCOMPARE(qAlpha(st.result().pixel(75, 25)), 0);
    }

    void blurBrushAndAreaEffects()
    {
        QImage checker(120, 120, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 120; ++y)
            for (int x = 0; x < 120; ++x)
                checker.setPixel(x, y, ((x / 2 + y / 2) % 2) ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
        // Area blur changes only the rectangle, and smooths it.
        const DocState s{checker, {}};
        const QRect r(30, 30, 40, 40);
        const DocState blurred = retouch::blurArea(s, r, 4);
        QCOMPARE(blurred.base.pixel(10, 10), checker.pixel(10, 10));
        QCOMPARE(blurred.base.pixel(29, 50), checker.pixel(29, 50));
        const int mid = qRed(blurred.base.pixel(50, 50));
        QVERIFY(mid > 90 && mid < 165);
        QCOMPARE(s.base.pixel(50, 50), checker.pixel(50, 50)); // history keeps the original
        // Mosaic: uniform cells aligned to the rectangle.
        const DocState mosaic = retouch::pixelateArea(s, QRect(32, 32, 40, 40), 10);
        for (int y = 32; y < 42; ++y)
            for (int x = 32; x < 42; ++x)
                QCOMPARE(mosaic.base.pixel(x, y), mosaic.base.pixel(32, 32));
        QCOMPARE(mosaic.base.pixel(31, 31), checker.pixel(31, 31));
        // Blur brush: smooths under the brush only.
        retouch::Stroke::Params p;
        p.mode = retouch::Mode::Blur;
        p.diameter = 20;
        p.hardness = 1.0;
        p.blurRadius = 4;
        retouch::Stroke st(checker, p);
        st.lineTo(QPointF(60, 60));
        const int b = qRed(st.result().pixel(60, 60));
        QVERIFY(b > 90 && b < 165);
        QCOMPARE(st.result().pixel(90, 90), checker.pixel(90, 90));
    }

    void stamp300pxOn4kKeepsUp()
    {
        // Acceptance: 300 px stamp on a 4K base at 60 fps -> < 16 ms per mouse move.
        const QImage base = gradient(3840, 2160);
        retouch::Stroke::Params p;
        p.mode = retouch::Mode::Clone;
        p.diameter = 300;
        p.hardness = 0.5;
        p.opacity = 0.8;
        p.cloneOffset = QPoint(-400, -300);
        QElapsedTimer t;
        t.start();
        retouch::Stroke st(base, p); // includes the per-stroke setup
        const qint64 setup = t.elapsed();
        double worst = 0;
        for (int i = 0; i < 150; ++i) {
            QElapsedTimer m;
            m.start();
            st.lineTo(QPointF(800 + i * 12, 900 + 200 * std::sin(i / 10.0)));
            worst = std::max(worst, m.nsecsElapsed() / 1e6);
        }
        qInfo("4K stamp 300 px: setup %lld ms, worst move %.2f ms", setup, worst);
#ifdef QT_NO_DEBUG
        QVERIFY2(worst < 16.0, qPrintable(QString::number(worst)));
#endif
    }

    // ================================================================ stage 3: size, canvas, export

    void resizeImageScalesObjects()
    {
        DocState s{solid(200, 100, Qt::white), {}};
        auto line = std::make_shared<LineObject>();
        line->p1 = QPointF(20, 20);
        line->p2 = QPointF(180, 80);
        line->width = 4;
        auto text = std::make_shared<TextObject>();
        text->text = QStringLiteral("A");
        text->pixelSize = 20;
        text->transform = QTransform::fromTranslate(100, 50);
        s.objects = {line, text};
        const DocState r = docops::resizeImage(s, QSize(400, 150)); // 2x, 1.5x
        QCOMPARE(r.base.size(), QSize(400, 150));
        const auto *l = static_cast<const LineObject *>(r.objects[0].get());
        QCOMPARE(l->p1, QPointF(40, 30));
        QCOMPARE(l->p2, QPointF(360, 120));
        QCOMPARE(l->width, 6.0); // smaller factor (spec C4)
        const auto *t = static_cast<const TextObject *>(r.objects[1].get());
        QCOMPARE(t->pixelSize, 30);
        QCOMPARE(t->transform.map(QPointF(0, 0)), QPointF(200, 75));
    }

    void canvasSizeAnchorsAndFillsOnlyNewArea()
    {
        QImage base = solid(100, 50, Qt::red);
        QPainter p(&base);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(0, 0, 10, 10, Qt::transparent); // existing transparency stays
        p.end();
        DocState s{base, {imageObject(10, 10, Qt::blue, QPointF(50, 20))}};
        const DocState r = docops::resizeCanvas(s, QSize(140, 90), QPoint(20, 20), Qt::white);
        QCOMPARE(r.base.size(), QSize(140, 90));
        QCOMPARE(r.base.pixel(5, 5), qRgba(255, 255, 255, 255));   // new area
        QCOMPARE(qAlpha(r.base.pixel(25, 25)), 0);                  // old transparency
        QCOMPARE(r.base.pixel(60, 50), qRgba(255, 0, 0, 255));     // old content moved
        QCOMPARE(r.objects.first()->boundingRect().topLeft(), QPointF(70, 40));
        // smaller canvas crops
        const DocState c = docops::resizeCanvas(s, QSize(50, 50), QPoint(-25, 0), Qt::transparent);
        QCOMPARE(c.base.size(), QSize(50, 50));
        QCOMPARE(c.objects.first()->boundingRect().topLeft(), QPointF(25, 20));
    }

    void icoHasAllSizesAsPng()
    {
        const QImage img = solid(300, 150, Qt::red); // not square
        const QByteArray ico = imageio::encodeIco(img, {256, 16, 48, 32});
        QVERIFY(ico.size() > 6);
        auto u16 = [&](int off) { return qFromLittleEndian<quint16>(ico.constData() + off); };
        auto u32 = [&](int off) { return qFromLittleEndian<quint32>(ico.constData() + off); };
        QCOMPARE(u16(0), quint16(0));
        QCOMPARE(u16(2), quint16(1));
        QCOMPARE(u16(4), quint16(4));
        const QList<int> expected = {16, 32, 48, 256};
        for (int i = 0; i < 4; ++i) {
            const int e = 6 + 16 * i;
            QCOMPARE(int(quint8(ico[e])), expected[i] == 256 ? 0 : expected[i]);
            QCOMPARE(u16(e + 6), quint16(32));
            const QByteArray png = ico.mid(int(u32(e + 12)), int(u32(e + 8)));
            const QImage entry = QImage::fromData(png, "png");
            QCOMPARE(entry.size(), QSize(expected[i], expected[i]));
            // centered on a transparent square
            QCOMPARE(qAlpha(entry.pixel(0, 0)), 0);
            QCOMPARE(entry.pixel(entry.width() / 2, entry.height() / 2), qRgba(255, 0, 0, 255));
        }
        // Qt's own ICO reader (if installed) sees all four images.
        QBuffer buf;
        buf.setData(ico);
        QImageReader reader(&buf, "ico");
        if (reader.canRead())
            QCOMPARE(reader.imageCount(), 4);
    }

    void bmpIs24BitWithBackground()
    {
        QTemporaryDir dir;
        QImage img = solid(10, 10, Qt::transparent);
        QPainter(&img).fillRect(0, 0, 5, 10, Qt::blue);
        imageio::SaveOptions o;
        o.format = "bmp";
        o.background = Qt::yellow;
        const QString path = dir.filePath(QStringLiteral("x.bmp"));
        QString err;
        QVERIFY2(imageio::save(img, path, o, &err), qPrintable(err));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray data = f.readAll();
        QCOMPARE(qFromLittleEndian<quint16>(data.constData() + 28), quint16(24));
        const QImage back(path);
        QCOMPARE(back.pixel(2, 2), qRgb(0, 0, 255));
        QCOMPARE(back.pixel(8, 2), qRgb(255, 255, 0));
        QVERIFY(imageio::formatForSuffix(QStringLiteral("ico")) == "ico");
        QVERIFY(imageio::formatForSuffix(QStringLiteral("BMP")) == "bmp");
    }

    void suggestedNameIsLanguageNeutral()
    {
        const QDateTime t(QDate(2026, 11, 3), QTime(8, 45, 12));
        QCOMPARE(imageio::suggestedFileName(t, "png"), QStringLiteral("image-2026-11-03-084512.png"));
        QCOMPARE(imageio::suggestedFileName(t, "jpeg"), QStringLiteral("image-2026-11-03-084512.jpg"));
    }
};

QTEST_MAIN(TstCore)
#include "tst_core.moc"
