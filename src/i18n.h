#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Localization (spec L).
namespace i18n {

struct Language
{
    QString code;      // e.g. "cs", "pt_BR"
    QString name;      // native name, e.g. "Čeština"
    bool builtIn = false;
};

inline const QStringList kBuiltIn = {QStringLiteral("cs"), QStringLiteral("de"),
                                     QStringLiteral("es"), QStringLiteral("ja")};

// Directories searched for external rolidan_<code>.qm files, in priority order.
QStringList externalDirectories();
// English + built-in + external languages found on disk.
QList<Language> available();
// Install translators for `code` (no-op for "en"). External files take
// precedence over built-in ones. Returns false if nothing was found.
bool install(const QString &code);

} // namespace i18n
