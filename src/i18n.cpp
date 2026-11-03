#include "i18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QStandardPaths>
#include <QTranslator>

namespace i18n {

QStringList externalDirectories()
{
    return {
        QDir::homePath() + QStringLiteral("/.local/share/rolidan/translations"),
        QStringLiteral("/usr/share/rolidan/translations"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
    };
}

static QString nativeName(const QString &code)
{
    if (code == QLatin1String("en"))
        return QStringLiteral("English");
    const QLocale loc(code);
    if (loc.language() == QLocale::C)
        return code;
    QString n = loc.nativeLanguageName();
    if (code.contains(QLatin1Char('_')))
        n += QStringLiteral(" (%1)").arg(loc.nativeTerritoryName());
    return n.isEmpty() ? code : n.left(1).toUpper() + n.mid(1);
}

QList<Language> available()
{
    QList<Language> out{{QStringLiteral("en"), nativeName(QStringLiteral("en")), true}};
    QStringList seen{QStringLiteral("en")};
    for (const QString &c : kBuiltIn) {
        out.append({c, nativeName(c), true});
        seen << c;
    }
    for (const QString &dir : externalDirectories()) {
        const QStringList files = QDir(dir).entryList({QStringLiteral("rolidan_*.qm")}, QDir::Files);
        for (const QString &f : files) {
            const QString code = f.mid(8, f.size() - 8 - 3);
            if (!code.isEmpty() && !seen.contains(code)) {
                out.append({code, nativeName(code), false});
                seen << code;
            }
        }
    }
    return out;
}

static bool loadFirst(const QString &base, const QString &code)
{
    auto *t = new QTranslator(QCoreApplication::instance());
    QStringList dirs = externalDirectories();
    dirs << QStringLiteral(":/i18n");
    for (const QString &dir : dirs) {
        if (t->load(base + QLatin1Char('_') + code, dir)) {
            QCoreApplication::installTranslator(t);
            return true;
        }
    }
    delete t;
    return false;
}

bool install(const QString &code)
{
    if (code.isEmpty() || code == QLatin1String("en"))
        return true;
    QLocale::setDefault(QLocale(code));
    loadFirst(QStringLiteral("qtbase"), code); // standard dialogs; optional
    return loadFirst(QStringLiteral("rolidan"), code);
}

} // namespace i18n
