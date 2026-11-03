#pragma once

#include "history.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

// Image adjustments and filters (Etapa 4). Pure functions on images, GUI-free
// and unit-tested; the generic FilterDialog builds its controls from the
// parameter descriptions below. Heavy filters use all CPU cores.
namespace filters {

struct Param
{
    enum class Type { Int, Double, Bool, Choice };
    QString key;
    const char *label;        // translatable (context "filters")
    Type type = Type::Int;
    double min = 0, max = 100, def = 0;
    int decimals = 0;         // Double
    const char *suffix = "";  // e.g. " px", " %", "°"
    QList<const char *> choices; // Choice, translatable
    const char *tip = "";
};

using Values = QHash<QString, double>;

enum class Menu { Adjust, Filters, Image };

struct Spec
{
    QString id;
    const char *name;         // menu text, translatable (context "filters")
    Menu menu = Menu::Filters;
    QList<Param> params;      // empty = applied at once, no dialog
    bool selection = true;    // limited to the area selection if there is one
    // Pixel filter on the area (Format_ARGB32_Premultiplied in and out).
    QImage (*image)(const QImage &, const Values &) = nullptr;
    // Context needed around a selection (blur radius etc.).
    int (*margin)(const Values &) = nullptr;
    // Whole-document operation (rotation: also moves objects).
    DocState (*document)(const DocState &, const Values &) = nullptr;
};

const QList<Spec> &all();
const Spec *find(const QString &id);
Values defaults(const Spec &spec);
QString displayName(const Spec &spec); // translated, without "…"

// Applies `spec` to the document: only inside `selection` if it is not empty
// and the filter supports it. One call = one undo step for the caller.
DocState apply(const Spec &spec, const DocState &s, const QRect &selection, const Values &v);

// ---- individual operations (exposed for tests and tools) ----
QImage brightnessContrast(const QImage &src, double brightness, double contrast); // -100..100
QImage hueSaturation(const QImage &src, double hue, double saturation, double lightness);
QImage enhanceColors(const QImage &src, double strength, double vibrance, double clipPercent);
QImage temperature(const QImage &src, double temperature, double tint);
QImage levels(const QImage &src, int black, int white, double gamma);
QImage grayscale(const QImage &src);
QImage sepia(const QImage &src, double strength);
QImage invert(const QImage &src);
QImage threshold(const QImage &src, int level);
QImage posterize(const QImage &src, int levels);
QImage gaussianBlur(const QImage &src, double radius);
QImage sharpen(const QImage &src, double amountPercent, double radius, int threshold);
QImage pixelate(const QImage &src, int cell);
QImage addNoise(const QImage &src, double amount, bool color, quint32 seed = 1);
QImage median(const QImage &src, int radius);
// Scanned document cleanup: mode 0 color, 1 grayscale, 2 black & white.
QImage cleanDocument(const QImage &src, int mode, double whiten, double ink, double bwThreshold, bool despeckle);

// Free rotation by `degrees` (clockwise). canvas: 0 expand to fit, 1 keep
// size, 2 crop to the largest rectangle without empty corners.
// fill: 0 transparent, 1 white, 2 black. Objects keep their orientation
// (texts stay readable); their positions and point geometry follow the image.
DocState rotate(const DocState &s, double degrees, int canvas, int fill);
// Skew of a document scan in degrees (text lines' angle, clockwise positive);
// rotate by the negative value to straighten. 0 if nothing was found.
double detectSkew(const QImage &src);

} // namespace filters
