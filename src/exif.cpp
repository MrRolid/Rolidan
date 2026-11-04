#include "exif.h"

#include <QList>
#include <QtEndian>
#include <cmath>

namespace exif {

namespace {

class Tiff
{
public:
    explicit Tiff(const QByteArray &d)
        : m_d(d)
    {
        if (d.size() < 8)
            return;
        if (d.startsWith("II"))
            m_le = true;
        else if (d.startsWith("MM"))
            m_le = false;
        else
            return;
        m_ok = u16(2) == 42;
    }

    bool ok() const { return m_ok; }
    quint32 firstIfd() const { return u32(4); }

    bool in(qint64 off, qint64 len) const { return off >= 0 && len >= 0 && off + len <= m_d.size(); }
    quint16 u16(qint64 off) const
    {
        if (!in(off, 2))
            return 0;
        const auto *p = reinterpret_cast<const uchar *>(m_d.constData() + off);
        return m_le ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
    }
    quint32 u32(qint64 off) const
    {
        if (!in(off, 4))
            return 0;
        const auto *p = reinterpret_cast<const uchar *>(m_d.constData() + off);
        return m_le ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
    }

    struct Entry
    {
        quint16 tag = 0, type = 0;
        quint32 count = 0;
        qint64 valueOffset = 0; // where the value bytes are
    };

    static int typeSize(quint16 type)
    {
        switch (type) {
        case 1: case 2: case 6: case 7: return 1;  // BYTE ASCII SBYTE UNDEFINED
        case 3: case 8: return 2;                  // SHORT SSHORT
        case 4: case 9: case 11: return 4;         // LONG SLONG FLOAT
        case 5: case 10: case 12: return 8;        // RATIONAL SRATIONAL DOUBLE
        default: return 0;
        }
    }

    QList<Entry> ifd(quint32 off) const
    {
        QList<Entry> out;
        const int n = u16(off);
        if (!in(off + 2, qint64(n) * 12) || n > 1000)
            return out;
        for (int i = 0; i < n; ++i) {
            const qint64 e = off + 2 + qint64(i) * 12;
            Entry en;
            en.tag = u16(e);
            en.type = u16(e + 2);
            en.count = u32(e + 4);
            const qint64 size = qint64(typeSize(en.type)) * en.count;
            if (typeSize(en.type) == 0 || en.count > 1000000)
                continue;
            en.valueOffset = size <= 4 ? e + 8 : qint64(u32(e + 8));
            if (!in(en.valueOffset, size))
                continue;
            out.append(en);
        }
        return out;
    }

    double number(const Entry &e, quint32 index = 0) const
    {
        if (index >= e.count)
            return 0;
        const qint64 o = e.valueOffset + qint64(index) * typeSize(e.type);
        switch (e.type) {
        case 1: case 7: return uchar(m_d[o]);
        case 6: return qint8(m_d[o]);
        case 3: return u16(o);
        case 8: return qint16(u16(o));
        case 4: return u32(o);
        case 9: return qint32(u32(o));
        case 5: {
            const quint32 den = u32(o + 4);
            return den ? double(u32(o)) / den : 0.0;
        }
        case 10: {
            const qint32 den = qint32(u32(o + 4));
            return den ? double(qint32(u32(o))) / den : 0.0;
        }
        default: return 0;
        }
    }

    QString text(const Entry &e) const
    {
        if (e.type != 2 && e.type != 7 && e.type != 1)
            return {};
        QByteArray b = m_d.mid(e.valueOffset, e.count);
        const qsizetype nul = b.indexOf('\0');
        if (nul >= 0)
            b.truncate(nul);
        return QString::fromUtf8(b).trimmed();
    }

private:
    const QByteArray &m_d;
    bool m_le = true;
    bool m_ok = false;
};

double gpsCoordinate(const Tiff &t, const Tiff::Entry &e)
{
    if (e.count < 3)
        return 0;
    return t.number(e, 0) + t.number(e, 1) / 60.0 + t.number(e, 2) / 3600.0;
}

} // namespace

Info readTiff(const QByteArray &data)
{
    Info info;
    const Tiff t(data);
    if (!t.ok())
        return info;
    quint32 exifIfd = 0, gpsIfd = 0;
    for (const Tiff::Entry &e : t.ifd(t.firstIfd())) {
        switch (e.tag) {
        case 0x010E: info.description = t.text(e); break;
        case 0x010F: info.make = t.text(e); break;
        case 0x0110: info.model = t.text(e); break;
        case 0x0112: info.orientation = int(t.number(e)); break;
        case 0x0131: info.software = t.text(e); break;
        case 0x0132: if (info.dateTaken.isEmpty()) info.dateTaken = t.text(e); break;
        case 0x013B: info.artist = t.text(e); break;
        case 0x8298: info.copyright = t.text(e); break;
        case 0x8769: exifIfd = quint32(t.number(e)); break;
        case 0x8825: gpsIfd = quint32(t.number(e)); break;
        default: break;
        }
    }
    info.valid = true;
    if (exifIfd) {
        for (const Tiff::Entry &e : t.ifd(exifIfd)) {
            switch (e.tag) {
            case 0x829A: info.exposure = t.number(e); break;
            case 0x829D: info.fNumber = t.number(e); break;
            case 0x8827: info.iso = int(t.number(e)); break;
            case 0x9003: info.dateTaken = t.text(e); break;
            case 0x9204: info.exposureBias = t.number(e); break;
            case 0x9209: info.flash = int(t.number(e)); break;
            case 0x920A: info.focalLength = t.number(e); break;
            case 0xA002: info.pixelWidth = int(t.number(e)); break;
            case 0xA003: info.pixelHeight = int(t.number(e)); break;
            case 0xA405: info.focalLength35 = int(t.number(e)); break;
            case 0xA434: info.lens = t.text(e); break;
            default: break;
            }
        }
    }
    if (gpsIfd) {
        QString latRef, lonRef;
        double lat = 0, lon = 0;
        bool haveLat = false, haveLon = false;
        int altRef = 0;
        for (const Tiff::Entry &e : t.ifd(gpsIfd)) {
            switch (e.tag) {
            case 1: latRef = t.text(e); break;
            case 2: lat = gpsCoordinate(t, e); haveLat = e.count >= 3; break;
            case 3: lonRef = t.text(e); break;
            case 4: lon = gpsCoordinate(t, e); haveLon = e.count >= 3; break;
            case 5: altRef = int(t.number(e)); break;
            case 6: info.altitude = t.number(e); info.hasAltitude = true; break;
            default: break;
            }
        }
        if (haveLat && haveLon && (lat != 0 || lon != 0)) {
            info.hasGps = true;
            info.latitude = latRef.startsWith(QLatin1Char('S')) ? -lat : lat;
            info.longitude = lonRef.startsWith(QLatin1Char('W')) ? -lon : lon;
        }
        if (altRef == 1)
            info.altitude = -info.altitude;
    }
    return info;
}

QByteArray extract(const QByteArray &f)
{
    auto be32 = [&](qsizetype o) {
        return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(f.constData() + o));
    };
    if (f.startsWith("II*") || f.startsWith("MM\0*"))
        return f;
    if (f.size() > 4 && uchar(f[0]) == 0xFF && uchar(f[1]) == 0xD8) { // JPEG
        qsizetype o = 2;
        while (o + 4 <= f.size() && uchar(f[o]) == 0xFF) {
            const uchar marker = uchar(f[o + 1]);
            if (marker == 0xD9 || marker == 0xDA) // end / start of scan
                break;
            const int len = (uchar(f[o + 2]) << 8) | uchar(f[o + 3]);
            if (len < 2)
                break;
            if (marker == 0xE1 && f.mid(o + 4, 6) == QByteArray("Exif\0\0", 6))
                return f.mid(o + 10, len - 8);
            o += 2 + len;
        }
        return {};
    }
    if (f.startsWith("\x89PNG\r\n\x1a\n")) {
        qsizetype o = 8;
        while (o + 12 <= f.size()) {
            const quint32 len = be32(o);
            const QByteArray type = f.mid(o + 4, 4);
            if (type == "eXIf")
                return f.mid(o + 8, len);
            if (type == "IDAT" || type == "IEND" || o + 12 + qsizetype(len) > f.size())
                break;
            o += 12 + len;
        }
        return {};
    }
    if (f.startsWith("RIFF") && f.mid(8, 4) == "WEBP") {
        qsizetype o = 12;
        while (o + 8 <= f.size()) {
            const quint32 len = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(f.constData() + o + 4));
            if (f.mid(o, 4) == "EXIF") {
                QByteArray d = f.mid(o + 8, len);
                if (d.startsWith(QByteArray("Exif\0\0", 6)))
                    d.remove(0, 6);
                return d;
            }
            o += 8 + len + (len & 1);
        }
    }
    return {};
}

Info read(const QByteArray &file)
{
    const QByteArray tiff = extract(file);
    return tiff.isEmpty() ? Info{} : readTiff(tiff);
}

QString formatExposure(double s)
{
    if (s <= 0)
        return {};
    if (s < 0.5) {
        const double inv = 1.0 / s;
        return QStringLiteral("1/%1 s").arg(std::lround(inv));
    }
    return QStringLiteral("%1 s").arg(QString::number(s, 'g', 3));
}

} // namespace exif
