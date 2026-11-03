#include "clipboard.h"

#include "imageio.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QUuid>

namespace clip {

static QByteArray ownerToken()
{
    static const QByteArray token = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    return token;
}

static bool loadInto(Content &c, const imageio::LoadResult &r)
{
    if (!r.ok())
        return false;
    c.kind = Content::Kind::Image;
    c.image = r.image;
    c.notes = r.notes;
    return true;
}

Content decode(const QMimeData *mime)
{
    Content c;
    if (!mime)
        return c;
    const QStringList formats = mime->formats();

    // 0. Our own objects (internal, keeps them editable).
    if (formats.contains(QLatin1String(kObjectsMime))) {
        c.objects = deserializeObjects(mime->data(QLatin1String(kObjectsMime)));
        if (!c.objects.isEmpty()) {
            c.kind = Content::Kind::Objects;
            return c;
        }
    }
    // 1. image/png keeps transparency.
    if (formats.contains(QLatin1String("image/png"))
        && loadInto(c, imageio::loadData(mime->data(QStringLiteral("image/png")), "png")))
        return c;
    // 2. Any other image/* Qt can decode.
    for (const QString &f : formats) {
        if (f.startsWith(QLatin1String("image/")) && f != QLatin1String("image/png")
            && loadInto(c, imageio::loadData(mime->data(f))))
            return c;
    }
    if (mime->hasImage()) {
        QStringList notes;
        QImage img = imageio::normalize(qvariant_cast<QImage>(mime->imageData()), &notes);
        if (!img.isNull()) {
            c.kind = Content::Kind::Image;
            c.image = img;
            c.notes = notes;
            return c;
        }
    }
    // 3./4. Local files: use the first loadable image.
    bool sawRemote = false;
    if (mime->hasUrls()) {
        QStringList files;
        for (const QUrl &u : mime->urls()) {
            if (u.isLocalFile())
                files << u.toLocalFile();
            else if (u.scheme().startsWith(QLatin1String("http")))
                sawRemote = true;
        }
        c.fileCount = files.size();
        for (const QString &f : files) {
            if (loadInto(c, imageio::loadFile(f))) {
                c.filePath = f;
                return c;
            }
        }
    }
    // 5. A link without image data: we never download (spec B1/5, H2).
    if (!sawRemote && mime->hasText()) {
        const QString t = mime->text().trimmed();
        sawRemote = !t.contains(QLatin1Char('\n'))
                    && (t.startsWith(QLatin1String("http://")) || t.startsWith(QLatin1String("https://")));
    }
    if (sawRemote) {
        c.kind = Content::Kind::Link;
        return c;
    }
    return c;
}

QMimeData *encode(const QImage &image, const ObjectList &objects)
{
    auto *mime = new QMimeData;
    mime->setImageData(image.convertToFormat(QImage::Format_ARGB32)); // offers image/png etc.
    if (!objects.isEmpty())
        mime->setData(QLatin1String(kObjectsMime), serializeObjects(objects));
    mime->setData(QLatin1String(kOwnerMime), ownerToken());
    return mime;
}

bool ownsClipboard()
{
    const QMimeData *m = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
    return m && m->data(QLatin1String(kOwnerMime)) == ownerToken();
}

} // namespace clip
