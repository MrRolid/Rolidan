#pragma once

#include "imageio.h"

#include <QString>

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

} // namespace settings
