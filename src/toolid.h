#pragma once

#include <QtGlobal>

// Tools (spec E). Values are stored in objects (Annotation::origin) and settings,
// so only append new ones.
enum class ToolId : quint8 {
    Pointer = 0,
    Select,
    Crop,
    Text,
    Arrow,
    Line,
    Rect,
    Ellipse,
    Brush,
    Highlighter,
    Cover,
    Eyedropper,
    Clone,       // clone stamp (spec D5)
    Eraser,      // pixel eraser (base only, to transparency)
    BlurBrush,   // blur brush
    Marker,      // numbered marker (Etapa 4)
    Polyline,    // polyline / polyline arrow
    Polygon,
    Bubble,      // speech bubble (text with a tail)
    PixelBrush,  // pixelate brush (mosaic where painted)
    Bucket,      // paint bucket: flood fill of the base
    Straighten,  // draw a line that should be level → rotate dialog
};

// Editable properties shared by tools (defaults) and objects (spec D1).
// Values are stored in settings by name, so only append new ones.
enum class Prop : quint8 {
    Color = 1,     // QColor: stroke / text / main color
    Width,         // double, px
    Opacity,       // double 0..1
    Fill,          // QColor, transparent = none
    HeadStart,     // bool
    HeadEnd,       // bool
    HeadSize,      // double px, 0 = automatic (4x width)
    Radius,        // double px (rounded rectangle)
    FontFamily,    // QString
    FontSize,      // int px
    Bold,          // bool
    Italic,        // bool
    Underline,     // bool
    Align,         // int Qt::AlignmentFlag (Left / HCenter / Right)
    Background,    // QColor, transparent = none
    Outline,       // QColor, transparent = none
    HighlightMode, // int: 0 freehand, 1 rectangle (tool only)
    Size,          // double px: brush diameter (retouch tools)
    Hardness,      // double 0..1
    Aligned,       // bool: clone source offset kept between strokes
    Strength,      // int px: blur radius (blur brush)
    Number,        // int: numbered marker
    Cell,          // int px: mosaic cell (pixelate brush)
    Tolerance,     // int 0..100 %: color similarity (bucket)
    Contiguous,    // bool: fill only the connected area (bucket)
};

const char *toolName(ToolId id);   // stable, used as settings key
const char *propName(Prop p);      // stable, used as settings key
