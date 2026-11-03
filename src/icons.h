#pragma once

#include <QIcon>
#include <QString>

// Built-in vector icons for tools and toolbar actions. Drawn with QPainter in
// the palette's text color, so they are sharp at any scale, follow light and
// dark themes and need no icon theme or SVG plugin.
namespace icons {

// Icon for an action name (freedesktop-style, e.g. "document-save" or
// "draw-arrow"). Falls back to the icon theme for names without a drawing.
QIcon get(const QString &name);
// True if `name` has a built-in drawing.
bool has(const QString &name);

} // namespace icons
