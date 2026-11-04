#pragma once
// Builds small EXIF blocks for tests: camera, exposure and GPS fields.

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QtEndian>

namespace fixture {

class TiffWriter
{
public:
    explicit TiffWriter(bool littleEndian) : m_le(littleEndian) {}

    struct Field
    {
        quint16 tag, type;
        quint32 count;
        QByteArray data; // value bytes in the file's byte order
    };

    QByteArray u16(quint16 v) const
    {
        QByteArray b(2, 0);
        if (m_le)
            qToLittleEndian(v, b.data());
        else
            qToBigEndian(v, b.data());
        return b;
    }
    QByteArray u32(quint32 v) const
    {
        QByteArray b(4, 0);
        if (m_le)
            qToLittleEndian(v, b.data());
        else
            qToBigEndian(v, b.data());
        return b;
    }
    Field ascii(quint16 tag, const QByteArray &s) const { return {tag, 2, quint32(s.size() + 1), s + '\0'}; }
    Field shortv(quint16 tag, quint16 v) const { return {tag, 3, 1, u16(v)}; }
    Field longv(quint16 tag, quint32 v) const { return {tag, 4, 1, u32(v)}; }
    Field rational(quint16 tag, QList<QPair<quint32, quint32>> v) const
    {
        QByteArray d;
        for (auto [n, den] : v)
            d += u32(n) + u32(den);
        return {tag, 5, quint32(v.size()), d};
    }

    // Writes IFDs in order; `link` maps an IFD index to the (tag, target IFD index)
    // pointer stored in it (Exif / GPS sub-IFDs).
    QByteArray build(QList<QList<Field>> ifds, QList<std::tuple<int, quint16, int>> links) const
    {
        // Pointers first get placeholders so the sizes are known.
        for (auto [from, tag, to] : links)
            ifds[from].append(longv(tag, 0));
        for (auto &ifd : ifds)
            std::sort(ifd.begin(), ifd.end(), [](const Field &a, const Field &b) { return a.tag < b.tag; });
        QList<quint32> offsets;
        quint32 pos = 8;
        for (const auto &ifd : ifds) {
            offsets << pos;
            pos += 2 + 12 * ifd.size() + 4;
            for (const Field &f : ifd)
                if (f.data.size() > 4)
                    pos += f.data.size() + (f.data.size() & 1);
        }
        for (auto [from, tag, to] : links)
            for (Field &f : ifds[from])
                if (f.tag == tag)
                    f.data = u32(offsets[to]);
        QByteArray out = m_le ? QByteArray("II") : QByteArray("MM");
        out += u16(42) + u32(8);
        for (int i = 0; i < ifds.size(); ++i) {
            const auto &ifd = ifds[i];
            quint32 extra = offsets[i] + 2 + 12 * ifd.size() + 4;
            QByteArray table = u16(quint16(ifd.size())), blobs;
            for (const Field &f : ifd) {
                table += u16(f.tag) + u16(f.type) + u32(f.count);
                if (f.data.size() <= 4) {
                    table += f.data + QByteArray(4 - f.data.size(), 0);
                } else {
                    table += u32(extra + blobs.size());
                    blobs += f.data;
                    if (f.data.size() & 1)
                        blobs += '\0';
                }
            }
            table += u32(0);
            out += table + blobs;
        }
        return out;
    }

private:
    bool m_le;
};

// Canon-like photo: 1/250 s, f/2.8, ISO 200, 35 mm, Prague (50.0875 N, 14.4214 E), 235 m.
inline QByteArray sampleTiff(bool littleEndian = true)
{
    TiffWriter t(littleEndian);
    QList<TiffWriter::Field> ifd0{t.ascii(0x010F, "Canon"), t.ascii(0x0110, "Canon EOS R6"),
                                  t.shortv(0x0112, 1), t.ascii(0x0131, "Test Suite 1.0"),
                                  t.ascii(0x013B, "Jana Novakova")};
    QList<TiffWriter::Field> exifIfd{t.rational(0x829A, {{1, 250}}), t.rational(0x829D, {{28, 10}}),
                                     t.shortv(0x8827, 200), t.ascii(0x9003, "2024:05:01 12:30:45"),
                                     t.shortv(0x9209, 16), t.rational(0x920A, {{35, 1}}),
                                     t.ascii(0xA434, "RF 35mm F1.8")};
    QList<TiffWriter::Field> gps{t.ascii(1, "N"), t.rational(2, {{50, 1}, {5, 1}, {1500, 100}}),
                                 t.ascii(3, "E"), t.rational(4, {{14, 1}, {25, 1}, {1704, 100}}),
                                 {5, 1, 1, QByteArray(1, 0)}, t.rational(6, {{235, 1}})};
    return t.build({ifd0, exifIfd, gps}, {{0, 0x8769, 1}, {0, 0x8825, 2}});
}

// A JPEG image with the sample EXIF block in APP1.
inline QByteArray jpegWithExif(const QByteArray &tiff = sampleTiff())
{
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(QColor(90, 140, 200));
    QByteArray jpeg;
    QBuffer buf(&jpeg);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPEG", 90);
    const QByteArray payload = QByteArray("Exif\0\0", 6) + tiff;
    QByteArray app1("\xFF\xE1", 2);
    const int len = int(payload.size()) + 2;
    app1 += char(len >> 8);
    app1 += char(len & 0xFF);
    app1 += payload;
    return jpeg.left(2) + app1 + jpeg.mid(2);
}

} // namespace fixture
