#pragma once

#include <QByteArray>
#include <QString>

// Minimal EXIF reader (Etapa 5, image information): JPEG APP1, PNG eXIf,
// WebP EXIF chunk and plain TIFF. Only the fields Rolidan shows are decoded.
namespace exif {

struct Info
{
    bool valid = false;
    QString make, model, lens, software, artist, copyright, description;
    QString dateTaken;          // "2024:05:01 12:30:00" as stored
    double exposure = 0;        // seconds
    double fNumber = 0;
    int iso = 0;
    double focalLength = 0;     // mm
    int focalLength35 = 0;      // mm, 35 mm equivalent
    double exposureBias = 0;    // EV
    int flash = -1;             // raw EXIF value, -1 = unknown
    int orientation = 0;        // 1-8, 0 = unknown
    int pixelWidth = 0, pixelHeight = 0;
    bool hasGps = false;
    double latitude = 0, longitude = 0; // degrees, south / west negative
    bool hasAltitude = false;
    double altitude = 0;        // m
};

// Parse the EXIF block of an image file's contents.
Info read(const QByteArray &file);
// Parse a raw TIFF structure ("II*\0" / "MM\0*").
Info readTiff(const QByteArray &tiff);
// Find the TIFF structure inside a JPEG, PNG or WebP file (empty if none).
QByteArray extract(const QByteArray &file);

// "1/250 s", "2.5 s"
QString formatExposure(double seconds);

} // namespace exif
