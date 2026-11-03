#pragma once

#include "history.h"

#include <QString>

// Project format (spec F7): a ZIP with base.png, document.json (objects,
// format version) and pasted images as PNG files. Keeps annotations editable
// across sessions; also used for crash recovery.
namespace project {

inline constexpr char kSuffix[] = "rolidan";
inline constexpr int kFormatVersion = 1;

// `fast`: lower PNG compression (crash-recovery snapshots).
QByteArray encode(const DocState &s, bool fast = false);
bool decode(const QByteArray &data, DocState *out, QString *error);

bool save(const DocState &s, const QString &path, QString *error, bool fast = false);
bool load(const QString &path, DocState *out, QString *error);
bool isProjectFile(const QString &path);
// True if the document hides image data under opaque covers (spec H1:
// the project file still contains the original pixels).
bool hasCovers(const DocState &s);

} // namespace project
