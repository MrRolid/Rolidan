#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Minimal ZIP container for the project format (spec F7). Entries are
// written uncompressed ("stored"): PNG data is compressed already, and this
// keeps Rolidan free of an extra dependency. Reading supports stored entries,
// i.e. files written by Rolidan.
namespace zip {

struct Entry
{
    QString name;
    QByteArray data;
};

quint32 crc32(const QByteArray &data);
QByteArray write(const QList<Entry> &entries);
// Returns false on a damaged / unsupported archive; `error` explains why.
bool read(const QByteArray &archive, QList<Entry> *entries, QString *error = nullptr);

} // namespace zip
