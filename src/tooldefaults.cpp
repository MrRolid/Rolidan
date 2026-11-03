#include "tooldefaults.h"

#include <QGuiApplication>
#include <QSettings>

namespace tooldefaults {

namespace {
const QColor kRed(0xe5, 0x1c, 0x23);

QVariant builtIn(ToolId tool, Prop prop)
{
    switch (prop) {
    case Prop::Color:
        switch (tool) {
        case ToolId::Highlighter: return QColor(0xff, 0xeb, 0x3b);
        case ToolId::Cover: return QColor(Qt::black);
        case ToolId::Bubble: return QColor(0x21, 0x21, 0x21);
        default: return kRed;
        }
    case Prop::Width:
        switch (tool) {
        case ToolId::Highlighter: return 18.0;
        case ToolId::Rect:
        case ToolId::Ellipse: return 3.0;
        default: return 4.0;
        }
    case Prop::Opacity: return tool == ToolId::Highlighter ? 0.8 : 1.0;
    case Prop::Fill: return QColor(Qt::transparent);
    case Prop::HeadStart: return false;
    case Prop::HeadEnd: return tool == ToolId::Arrow;
    case Prop::HeadSize: return 0.0;
    case Prop::Radius: return 0.0;
    case Prop::FontFamily: return QGuiApplication::font().family();
    case Prop::FontSize: return tool == ToolId::Bubble ? 20 : 24;
    case Prop::Bold: return tool != ToolId::Bubble;
    case Prop::Italic: return false;
    case Prop::Underline: return false;
    case Prop::Align: return int(Qt::AlignLeft);
    case Prop::Background: return tool == ToolId::Bubble ? QColor(Qt::white) : QColor(Qt::transparent);
    case Prop::Outline: return QColor(Qt::transparent);
    case Prop::HighlightMode: return 0;
    case Prop::Size: return tool == ToolId::Eraser ? 30.0 : tool == ToolId::Marker ? 32.0 : 40.0;
    case Prop::Hardness: return tool == ToolId::Eraser ? 0.8 : 0.5;
    case Prop::Aligned: return true;
    case Prop::Strength: return 6;
    case Prop::Number: return 1;
    case Prop::Cell: return 12;
    case Prop::Tolerance: return 15;
    case Prop::Contiguous: return true;
    }
    return {};
}

QString key(ToolId tool, Prop prop)
{
    return QStringLiteral("tools/%1/%2").arg(QLatin1String(toolName(tool)), QLatin1String(propName(prop)));
}

QSettings &store()
{
    static QSettings s;
    return s;
}
} // namespace

QList<Prop> props(ToolId tool)
{
    switch (tool) {
    case ToolId::Text:
    case ToolId::Bubble:
        return {Prop::Color, Prop::FontFamily, Prop::FontSize, Prop::Bold, Prop::Italic, Prop::Underline,
                Prop::Align, Prop::Background, Prop::Outline, Prop::Opacity};
    case ToolId::Arrow:
    case ToolId::Line:
        return {Prop::Color, Prop::Width, Prop::Opacity, Prop::HeadStart, Prop::HeadEnd, Prop::HeadSize};
    case ToolId::Rect: return {Prop::Color, Prop::Width, Prop::Fill, Prop::Opacity, Prop::Radius};
    case ToolId::Ellipse: return {Prop::Color, Prop::Width, Prop::Fill, Prop::Opacity};
    case ToolId::Brush: return {Prop::Color, Prop::Width, Prop::Opacity};
    case ToolId::Highlighter: return {Prop::HighlightMode, Prop::Color, Prop::Width, Prop::Opacity};
    case ToolId::Cover: return {Prop::Color};
    case ToolId::Marker: return {Prop::Color, Prop::Size, Prop::Opacity};
    case ToolId::Polyline:
        return {Prop::Color, Prop::Width, Prop::Opacity, Prop::HeadStart, Prop::HeadEnd, Prop::HeadSize};
    case ToolId::Polygon: return {Prop::Color, Prop::Width, Prop::Fill, Prop::Opacity};
    case ToolId::Clone: return {Prop::Size, Prop::Hardness, Prop::Opacity, Prop::Aligned};
    case ToolId::Eraser: return {Prop::Size, Prop::Hardness, Prop::Opacity};
    case ToolId::BlurBrush: return {Prop::Size, Prop::Hardness, Prop::Strength, Prop::Opacity};
    case ToolId::PixelBrush: return {Prop::Size, Prop::Hardness, Prop::Cell, Prop::Opacity};
    case ToolId::Bucket: return {Prop::Color, Prop::Tolerance, Prop::Contiguous, Prop::Opacity};
    default: return {};
    }
}

QVariant value(ToolId tool, Prop prop)
{
    const QVariant def = builtIn(tool, prop);
    const QVariant v = store().value(key(tool, prop));
    if (!v.isValid())
        return def;
    QVariant converted = v;
    if (converted.convert(def.metaType()))
        return converted;
    return def;
}

void setValue(ToolId tool, Prop prop, const QVariant &v)
{
    if (prop == Prop::Color || prop == Prop::Fill || prop == Prop::Background || prop == Prop::Outline)
        store().setValue(key(tool, prop), v.value<QColor>().name(QColor::HexArgb));
    else
        store().setValue(key(tool, prop), v);
}

void applyTo(ToolId tool, Annotation &obj)
{
    obj.origin = tool;
    for (Prop p : obj.props())
        obj.setProp(p, value(tool, p));
}

void resetAll()
{
    store().remove(QStringLiteral("tools"));
}

} // namespace tooldefaults
