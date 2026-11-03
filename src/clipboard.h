#pragma once

#include "annotation.h"

#include <QImage>
#include <QStringList>

class QMimeData;

namespace clip {

inline constexpr char kObjectsMime[] = "application/x-rolidan-objects";
inline constexpr char kOwnerMime[] = "application/x-rolidan-owner";

// What a clipboard / drop offer contains, decided by the priority in spec B1.
struct Content
{
    enum class Kind {
        None,       // nothing usable
        Objects,    // Rolidan objects (copied from Rolidan)
        Image,      // image data or a single local image file
        Link,       // http(s) link without image data
    };
    Kind kind = Kind::None;
    QImage image;
    ObjectList objects;
    int fileCount = 0;      // number of local files offered (B1/4)
    QString filePath;       // set when the image came from a local file
    QStringList notes;      // conversion notes from loading
};

Content decode(const QMimeData *mime);

// Build the mime data for an outgoing image; `objects` (optional) are attached
// in the internal format so pasting back into Rolidan keeps them editable.
QMimeData *encode(const QImage &image, const ObjectList &objects = {});

// True while the system clipboard still holds data we put there (spec B4).
bool ownsClipboard();

} // namespace clip
