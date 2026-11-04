#pragma once

#include "imageio.h"

#include <QString>
#include <QStringList>

// Typed access to persistent settings (QSettings, ~/.config/Rolidan/Rolidan.conf).
namespace settings {

QString language();                 // "en" default (decision J6)
void setLanguage(const QString &code);

bool keepClipboardAfterClose();     // decision J4, default true
void setKeepClipboardAfterClose(bool v);

bool clipboardLossWarned();
void setClipboardLossWarned(bool v);

int historyLimitMB();               // spec G1, default 1024
void setHistoryLimitMB(int mb);

QString lastDirectory();
void setLastDirectory(const QString &dir);

imageio::SaveOptions saveOptions(const QByteArray &format);
void setSaveOptions(const imageio::SaveOptions &o);

QByteArray lastExportFormat();
void setLastExportFormat(const QByteArray &f);

QString cropRatio();                // "free", "1:1", "4:3", "16:9", "w:h"
void setCropRatio(const QString &r);

// Viewer (Etapa 5)
QStringList recentFiles();          // newest first, at most kMaxRecent, existing files only
void addRecentFile(const QString &path);
void removeRecentFile(const QString &path);
void clearRecentFiles();
constexpr int kMaxRecent = 10;

double slideshowSeconds();          // default 4
void setSlideshowSeconds(double s);
bool slideshowLoop();               // default true
void setSlideshowLoop(bool v);

enum class Wheel { Scroll = 0, Browse = 1, Zoom = 2 };
Wheel wheelAction();                // mouse wheel without modifiers, default Scroll
void setWheelAction(Wheel w);

bool fullScreenEnlarge();          // full screen: enlarge small images to fit, default false
void setFullScreenEnlarge(bool v);

bool confirmTrash();                // default true
void setConfirmTrash(bool v);

QString copyMoveDirectory();        // last target of Copy to / Move to Folder
void setCopyMoveDirectory(const QString &dir);

} // namespace settings
