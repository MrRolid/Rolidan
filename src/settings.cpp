#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace settings {

static QSettings &s()
{
    static QSettings instance;
    return instance;
}

QString language() { return s().value("ui/language", QStringLiteral("en")).toString(); }
void setLanguage(const QString &code) { s().setValue("ui/language", code); }

bool keepClipboardAfterClose() { return s().value("clipboard/keepAfterClose", true).toBool(); }
void setKeepClipboardAfterClose(bool v) { s().setValue("clipboard/keepAfterClose", v); }

bool clipboardLossWarned() { return s().value("clipboard/lossWarned", false).toBool(); }
void setClipboardLossWarned(bool v) { s().setValue("clipboard/lossWarned", v); }

int historyLimitMB() { return qBound(64, s().value("history/limitMB", 1024).toInt(), 65536); }
void setHistoryLimitMB(int mb) { s().setValue("history/limitMB", mb); }

QString lastDirectory()
{
    const QString d = s().value("files/lastDirectory").toString();
    return !d.isEmpty() && QDir(d).exists() ? d : QDir::homePath();
}
void setLastDirectory(const QString &dir) { s().setValue("files/lastDirectory", dir); }

imageio::SaveOptions saveOptions(const QByteArray &format)
{
    imageio::SaveOptions o;
    o.format = format;
    o.pngCompression = s().value("export/png/compression", o.pngCompression).toInt();
    auto i = [](const char *key, int def) { return s().value(key, def).toInt(); };
    auto b = [](const char *key, bool def) { return s().value(key, def).toBool(); };
    o.jpeg.quality = i("export/jpeg/quality", o.jpeg.quality);
    o.jpeg.subsampling = i("export/jpeg/subsampling", o.jpeg.subsampling);
    o.jpeg.progressive = b("export/jpeg/progressive", o.jpeg.progressive);
    o.jpeg.optimize = b("export/jpeg/optimize", o.jpeg.optimize);
    o.jpeg.smoothing = i("export/jpeg/smoothing", o.jpeg.smoothing);
    o.webp.lossless = b("export/webp/lossless", o.webp.lossless);
    o.webp.quality = i("export/webp/quality", o.webp.quality);
    o.webp.preset = i("export/webp/preset", o.webp.preset);
    o.webp.method = i("export/webp/method", o.webp.method);
    o.webp.alphaQuality = i("export/webp/alphaQuality", o.webp.alphaQuality);
    o.webp.sharpYuv = b("export/webp/sharpYuv", o.webp.sharpYuv);
    o.webp.losslessLevel = i("export/webp/losslessLevel", o.webp.losslessLevel);
    o.webp.nearLossless = i("export/webp/nearLossless", o.webp.nearLossless);
    o.webp.exact = b("export/webp/exact", o.webp.exact);
    o.background = QColor::fromString(s().value("export/background", QStringLiteral("#ffffff")).toString());
    const QStringList ico = s().value("export/ico/sizes").toStringList();
    if (!ico.isEmpty()) {
        o.icoSizes.clear();
        for (const QString &v : ico)
            o.icoSizes.append(v.toInt());
    }
    if (!o.background.isValid())
        o.background = Qt::white;
    o.tiffLzw = b("export/tiff/lzw", o.tiffLzw);
    if (imageio::hasQuality(format))
        o.quality = s().value("export/" + QString::fromLatin1(format) + "/quality", o.quality).toInt();
    return o;
}

void setSaveOptions(const imageio::SaveOptions &o)
{
    s().setValue("export/png/compression", o.pngCompression);
    s().setValue("export/tiff/lzw", o.tiffLzw);
    if (imageio::hasQuality(o.format))
        s().setValue("export/" + QString::fromLatin1(o.format) + "/quality", o.quality);
    s().setValue("export/jpeg/quality", o.jpeg.quality);
    s().setValue("export/jpeg/subsampling", o.jpeg.subsampling);
    s().setValue("export/jpeg/progressive", o.jpeg.progressive);
    s().setValue("export/jpeg/optimize", o.jpeg.optimize);
    s().setValue("export/jpeg/smoothing", o.jpeg.smoothing);
    s().setValue("export/webp/lossless", o.webp.lossless);
    s().setValue("export/webp/quality", o.webp.quality);
    s().setValue("export/webp/preset", o.webp.preset);
    s().setValue("export/webp/method", o.webp.method);
    s().setValue("export/webp/alphaQuality", o.webp.alphaQuality);
    s().setValue("export/webp/sharpYuv", o.webp.sharpYuv);
    s().setValue("export/webp/losslessLevel", o.webp.losslessLevel);
    s().setValue("export/webp/nearLossless", o.webp.nearLossless);
    s().setValue("export/webp/exact", o.webp.exact);
    s().setValue("export/background", o.background.name());
    QStringList ico;
    for (int v : o.icoSizes)
        ico << QString::number(v);
    s().setValue("export/ico/sizes", ico);
}

QByteArray lastExportFormat() { return s().value("export/lastFormat", QByteArray("png")).toByteArray(); }
void setLastExportFormat(const QByteArray &f) { s().setValue("export/lastFormat", f); }

QString cropRatio() { return s().value("tools/crop/ratio", QStringLiteral("free")).toString(); }
void setCropRatio(const QString &r) { s().setValue("tools/crop/ratio", r); }

QStringList recentFiles()
{
    QStringList out;
    for (const QString &f : s().value("files/recent").toStringList())
        if (QFileInfo::exists(f) && !out.contains(f))
            out.append(f);
    return out.mid(0, kMaxRecent);
}
void addRecentFile(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    QStringList l = s().value("files/recent").toStringList();
    l.removeAll(abs);
    l.prepend(abs);
    s().setValue("files/recent", l.mid(0, kMaxRecent * 2)); // a few spares for vanished files
}
void removeRecentFile(const QString &path)
{
    QStringList l = s().value("files/recent").toStringList();
    l.removeAll(QFileInfo(path).absoluteFilePath());
    s().setValue("files/recent", l);
}
void clearRecentFiles() { s().remove("files/recent"); }

double slideshowSeconds() { return qBound(0.5, s().value("viewer/slideshowSeconds", 4.0).toDouble(), 3600.0); }
void setSlideshowSeconds(double v) { s().setValue("viewer/slideshowSeconds", v); }
bool slideshowLoop() { return s().value("viewer/slideshowLoop", true).toBool(); }
void setSlideshowLoop(bool v) { s().setValue("viewer/slideshowLoop", v); }

Wheel wheelAction() { return Wheel(qBound(0, s().value("viewer/wheel", 0).toInt(), 2)); }
void setWheelAction(Wheel w) { s().setValue("viewer/wheel", int(w)); }

bool fullScreenEnlarge() { return s().value("viewer/fullScreenEnlarge", false).toBool(); }
void setFullScreenEnlarge(bool v) { s().setValue("viewer/fullScreenEnlarge", v); }

bool confirmTrash() { return s().value("files/confirmTrash", true).toBool(); }
void setConfirmTrash(bool v) { s().setValue("files/confirmTrash", v); }

QString copyMoveDirectory() { return s().value("files/copyMoveDirectory").toString(); }
void setCopyMoveDirectory(const QString &dir) { s().setValue("files/copyMoveDirectory", dir); }

} // namespace settings
