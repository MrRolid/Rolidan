#pragma once

#include <QString>
#include <QStringList>

// Crash recovery (spec F7): every window periodically writes its unsaved
// document as a project file into ~/.local/state/rolidan/recovery/. Files are
// named <pid>-<window>.rolidan; at start-up, files of processes that no longer
// run are offered for restoring.
namespace recovery {

QString directory();
QString pathFor(int windowSerial);
// Recovery files left behind by processes that are not running any more.
QStringList orphans();

} // namespace recovery
