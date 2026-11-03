#include "recovery.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace recovery {

QString directory()
{
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation) + QStringLiteral("/recovery");
}

QString pathFor(int windowSerial)
{
    return QStringLiteral("%1/%2-%3.rolidan")
        .arg(directory())
        .arg(QCoreApplication::applicationPid())
        .arg(windowSerial);
}

QStringList orphans()
{
    QStringList out;
    const QDir dir(directory());
    const qint64 self = QCoreApplication::applicationPid();
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.rolidan")}, QDir::Files, QDir::Time)) {
        bool ok = false;
        const qint64 pid = fi.completeBaseName().section(QLatin1Char('-'), 0, 0).toLongLong(&ok);
        if (!ok || pid == self)
            continue;
        if (QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid)))
            continue; // still running (another Rolidan instance)
        out.append(fi.absoluteFilePath());
    }
    return out;
}

} // namespace recovery
