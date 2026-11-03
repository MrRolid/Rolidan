#include "zip.h"

#include <QtEndian>
#include <array>

namespace zip {

namespace {
constexpr quint32 kLocalSig = 0x04034b50;
constexpr quint32 kCentralSig = 0x02014b50;
constexpr quint32 kEndSig = 0x06054b50;
constexpr quint16 kUtf8Flag = 0x0800;
// 1980-01-01 00:00 in DOS format: dates are irrelevant for a project file.
constexpr quint16 kDosTime = 0;
constexpr quint16 kDosDate = (0 << 9) | (1 << 5) | 1;

const std::array<quint32, 256> &table()
{
    static const std::array<quint32, 256> t = [] {
        std::array<quint32, 256> r{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            r[i] = c;
        }
        return r;
    }();
    return t;
}

void put16(QByteArray &b, quint16 v)
{
    char d[2];
    qToLittleEndian(v, d);
    b.append(d, 2);
}

void put32(QByteArray &b, quint32 v)
{
    char d[4];
    qToLittleEndian(v, d);
    b.append(d, 4);
}

quint16 get16(const QByteArray &b, qsizetype off)
{
    return qFromLittleEndian<quint16>(b.constData() + off);
}

quint32 get32(const QByteArray &b, qsizetype off)
{
    return qFromLittleEndian<quint32>(b.constData() + off);
}
} // namespace

quint32 crc32(const QByteArray &data)
{
    const auto &t = table();
    quint32 c = 0xFFFFFFFFu;
    for (char ch : data)
        c = t[(c ^ quint8(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

QByteArray write(const QList<Entry> &entries)
{
    QByteArray out, central;
    for (const Entry &e : entries) {
        const QByteArray name = e.name.toUtf8();
        const quint32 crc = crc32(e.data);
        const quint32 offset = quint32(out.size());
        put32(out, kLocalSig);
        put16(out, 10);            // version needed: 1.0 (stored)
        put16(out, kUtf8Flag);
        put16(out, 0);             // method: stored
        put16(out, kDosTime);
        put16(out, kDosDate);
        put32(out, crc);
        put32(out, quint32(e.data.size()));
        put32(out, quint32(e.data.size()));
        put16(out, quint16(name.size()));
        put16(out, 0);             // extra length
        out.append(name);
        out.append(e.data);

        put32(central, kCentralSig);
        put16(central, 0x031E);    // made by: Unix, 3.0
        put16(central, 10);
        put16(central, kUtf8Flag);
        put16(central, 0);
        put16(central, kDosTime);
        put16(central, kDosDate);
        put32(central, crc);
        put32(central, quint32(e.data.size()));
        put32(central, quint32(e.data.size()));
        put16(central, quint16(name.size()));
        put16(central, 0);         // extra
        put16(central, 0);         // comment
        put16(central, 0);         // disk
        put16(central, 0);         // internal attributes
        put32(central, quint32(0100644) << 16); // external: -rw-r--r--
        put32(central, offset);
        central.append(name);
    }
    const quint32 centralOffset = quint32(out.size());
    out.append(central);
    put32(out, kEndSig);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(entries.size()));
    put16(out, quint16(entries.size()));
    put32(out, quint32(central.size()));
    put32(out, centralOffset);
    put16(out, 0); // comment length
    return out;
}

bool read(const QByteArray &a, QList<Entry> *entries, QString *error)
{
    auto fail = [error](const char *msg) {
        if (error)
            *error = QString::fromLatin1(msg);
        return false;
    };
    // End of central directory: search backwards (an archive comment may follow).
    qsizetype end = -1;
    for (qsizetype i = a.size() - 22; i >= 0 && i >= a.size() - 22 - 65535; --i) {
        if (get32(a, i) == kEndSig) {
            end = i;
            break;
        }
    }
    if (end < 0)
        return fail("not a ZIP archive");
    const int count = get16(a, end + 10);
    qsizetype pos = get32(a, end + 16);
    entries->clear();
    for (int i = 0; i < count; ++i) {
        if (pos + 46 > a.size() || get32(a, pos) != kCentralSig)
            return fail("damaged central directory");
        const quint16 method = get16(a, pos + 10);
        const quint32 crc = get32(a, pos + 16);
        const quint32 csize = get32(a, pos + 20);
        const quint32 usize = get32(a, pos + 24);
        const quint16 nameLen = get16(a, pos + 28);
        const quint16 extraLen = get16(a, pos + 30);
        const quint16 commentLen = get16(a, pos + 32);
        const quint32 local = get32(a, pos + 42);
        const QString name = QString::fromUtf8(a.mid(pos + 46, nameLen));
        pos += 46 + nameLen + extraLen + commentLen;
        if (method != 0 || csize != usize)
            return fail("compressed entries are not supported");
        if (qsizetype(local) + 30 > a.size() || get32(a, local) != kLocalSig)
            return fail("damaged entry");
        const qsizetype dataPos = qsizetype(local) + 30 + get16(a, local + 26) + get16(a, local + 28);
        if (dataPos + qsizetype(csize) > a.size())
            return fail("truncated archive");
        Entry e{name, a.mid(dataPos, csize)};
        if (crc32(e.data) != crc)
            return fail("checksum mismatch");
        entries->append(e);
    }
    return true;
}

} // namespace zip
