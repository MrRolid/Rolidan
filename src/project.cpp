#include "project.h"

#include "imageio.h"
#include "zip.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace project {

namespace {
QString tr(const char *text)
{
    return QCoreApplication::translate("project", text);
}

const char *typeName(Annotation::Type t)
{
    switch (t) {
    case Annotation::Type::Image: return "image";
    case Annotation::Type::Text: return "text";
    case Annotation::Type::Line: return "line";
    case Annotation::Type::Shape: return "shape";
    case Annotation::Type::Stroke: return "stroke";
    case Annotation::Type::Marker: return "marker";
    case Annotation::Type::Polyline: return "polyline";
    }
    return "unknown";
}

bool isColorProp(Prop p)
{
    return p == Prop::Color || p == Prop::Fill || p == Prop::Background || p == Prop::Outline;
}

QList<Prop> allProps()
{
    QList<Prop> out;
    for (int i = int(Prop::Color); i <= int(Prop::Contiguous); ++i)
        out.append(Prop(i));
    return out;
}

QJsonArray point(const QPointF &p)
{
    return {p.x(), p.y()};
}

QPointF toPoint(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    return a.size() == 2 ? QPointF(a[0].toDouble(), a[1].toDouble()) : QPointF();
}

QJsonArray points(const QPolygonF &poly)
{
    QJsonArray a;
    for (const QPointF &p : poly)
        a.append(point(p));
    return a;
}

QPolygonF toPoints(const QJsonValue &v)
{
    QPolygonF poly;
    for (const QJsonValue &p : v.toArray())
        poly << toPoint(p);
    return poly;
}

QByteArray png(const QImage &img, bool fast)
{
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, "png");
    w.setQuality(fast ? 90 : 30); // Qt: higher quality = less compression
    w.write(img.convertToFormat(QImage::Format_ARGB32));
    return data;
}
} // namespace

bool isProjectFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String(kSuffix), Qt::CaseInsensitive) == 0;
}

bool hasCovers(const DocState &s)
{
    for (const auto &o : s.objects) {
        if (o->type() == Annotation::Type::Shape
            && static_cast<const ShapeObject *>(o.get())->style == ShapeObject::Style::Cover)
            return true;
    }
    return false;
}

QByteArray encode(const DocState &s, bool fast)
{
    QList<zip::Entry> files;
    QJsonArray objects;
    int imageNo = 0;
    for (const auto &o : s.objects) {
        QJsonObject j;
        j[QStringLiteral("type")] = QLatin1String(typeName(o->type()));
        j[QStringLiteral("origin")] = QLatin1String(toolName(o->origin));
        const QTransform &t = o->transform;
        j[QStringLiteral("transform")] = QJsonArray{t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()};
        QJsonObject props;
        for (Prop p : o->props()) {
            const QVariant v = o->prop(p);
            props[QLatin1String(propName(p))] =
                isColorProp(p) ? QJsonValue(v.value<QColor>().name(QColor::HexArgb)) : QJsonValue::fromVariant(v);
        }
        j[QStringLiteral("props")] = props;
        switch (o->type()) {
        case Annotation::Type::Image: {
            const QString name = QStringLiteral("images/%1.png").arg(++imageNo);
            files.append({name, png(static_cast<const ImageObject *>(o.get())->image, fast)});
            j[QStringLiteral("image")] = name;
            break;
        }
        case Annotation::Type::Text: {
            const auto *x = static_cast<const TextObject *>(o.get());
            j[QStringLiteral("text")] = x->text;
            j[QStringLiteral("fixedWidth")] = x->fixedWidth;
            if (!x->spans.isEmpty()) {
                QJsonArray spans;
                for (const TextSpan &sp : x->spans) {
                    QJsonObject js;
                    js[QStringLiteral("start")] = sp.start;
                    js[QStringLiteral("length")] = sp.length;
                    if (sp.set & TextSpan::Bold)
                        js[QStringLiteral("bold")] = bool(sp.on & TextSpan::Bold);
                    if (sp.set & TextSpan::Italic)
                        js[QStringLiteral("italic")] = bool(sp.on & TextSpan::Italic);
                    if (sp.set & TextSpan::Underline)
                        js[QStringLiteral("underline")] = bool(sp.on & TextSpan::Underline);
                    if (sp.set & TextSpan::Color)
                        js[QStringLiteral("color")] = sp.color.name(QColor::HexArgb);
                    spans.append(js);
                }
                j[QStringLiteral("spans")] = spans;
            }
            if (x->bubble) {
                j[QStringLiteral("bubble")] = true;
                j[QStringLiteral("tail")] = point(x->tail);
            }
            break;
        }
        case Annotation::Type::Line: {
            const auto *x = static_cast<const LineObject *>(o.get());
            j[QStringLiteral("p1")] = point(x->p1);
            j[QStringLiteral("p2")] = point(x->p2);
            break;
        }
        case Annotation::Type::Shape: {
            const auto *x = static_cast<const ShapeObject *>(o.get());
            j[QStringLiteral("rect")] = QJsonArray{x->rect.x(), x->rect.y(), x->rect.width(), x->rect.height()};
            j[QStringLiteral("kind")] = x->kind == ShapeObject::Kind::Ellipse ? QStringLiteral("ellipse") : QStringLiteral("rect");
            j[QStringLiteral("style")] = x->style == ShapeObject::Style::Cover       ? QStringLiteral("cover")
                                         : x->style == ShapeObject::Style::Highlight ? QStringLiteral("highlight")
                                                                                     : QStringLiteral("outline");
            break;
        }
        case Annotation::Type::Stroke: {
            const auto *x = static_cast<const StrokeObject *>(o.get());
            j[QStringLiteral("points")] = points(x->points);
            j[QStringLiteral("highlighter")] = x->highlighter;
            break;
        }
        case Annotation::Type::Marker:
            j[QStringLiteral("center")] = point(static_cast<const MarkerObject *>(o.get())->center);
            break;
        case Annotation::Type::Polyline: {
            const auto *x = static_cast<const PolylineObject *>(o.get());
            j[QStringLiteral("points")] = points(x->points);
            j[QStringLiteral("closed")] = x->closed;
            break;
        }
        }
        objects.append(j);
    }
    QJsonObject doc;
    doc[QStringLiteral("format")] = QStringLiteral("rolidan");
    doc[QStringLiteral("version")] = kFormatVersion;
    doc[QStringLiteral("width")] = s.base.width();
    doc[QStringLiteral("height")] = s.base.height();
    doc[QStringLiteral("objects")] = objects;
    if (!s.notes.isEmpty())
        files.append({QStringLiteral("notes.md"), s.notes.toUtf8()});
    files.prepend({QStringLiteral("base.png"), png(s.base, fast)});
    files.prepend({QStringLiteral("document.json"), QJsonDocument(doc).toJson(QJsonDocument::Indented)});
    return zip::write(files);
}

bool decode(const QByteArray &data, DocState *out, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    QList<zip::Entry> entries;
    QString zipError;
    if (!zip::read(data, &entries, &zipError))
        return fail(tr("Not a Rolidan project (%1).").arg(zipError));
    QHash<QString, QByteArray> files;
    for (const auto &e : entries)
        files.insert(e.name, e.data);
    QJsonParseError pe;
    const QJsonDocument jd = QJsonDocument::fromJson(files.value(QStringLiteral("document.json")), &pe);
    const QJsonObject doc = jd.object();
    if (pe.error != QJsonParseError::NoError || doc.value(QStringLiteral("format")).toString() != QLatin1String("rolidan"))
        return fail(tr("Not a Rolidan project (document.json missing or damaged)."));
    if (doc.value(QStringLiteral("version")).toInt() > kFormatVersion)
        return fail(tr("The project was saved by a newer version of Rolidan."));
    QImage base = QImage::fromData(files.value(QStringLiteral("base.png")), "png");
    if (base.isNull())
        return fail(tr("The project's image (base.png) is missing or damaged."));
    base = imageio::normalize(base);

    QHash<QString, Prop> propByName;
    for (Prop p : allProps())
        propByName.insert(QLatin1String(propName(p)), p);
    QHash<QString, ToolId> toolByName;
    for (int i = int(ToolId::Pointer); i <= int(ToolId::Bubble); ++i)
        toolByName.insert(QLatin1String(toolName(ToolId(i))), ToolId(i));

    ObjectList objects;
    for (const QJsonValue &v : doc.value(QStringLiteral("objects")).toArray()) {
        const QJsonObject j = v.toObject();
        const QString type = j.value(QStringLiteral("type")).toString();
        std::shared_ptr<Annotation> obj;
        if (type == QLatin1String("image")) {
            QImage img = QImage::fromData(files.value(j.value(QStringLiteral("image")).toString()), "png");
            if (img.isNull())
                continue;
            obj = std::make_shared<ImageObject>(imageio::normalize(img));
        } else if (type == QLatin1String("text")) {
            auto x = std::make_shared<TextObject>();
            x->text = j.value(QStringLiteral("text")).toString();
            x->fixedWidth = j.value(QStringLiteral("fixedWidth")).toDouble();
            x->bubble = j.value(QStringLiteral("bubble")).toBool();
            x->tail = toPoint(j.value(QStringLiteral("tail")));
            for (const QJsonValue &v : j.value(QStringLiteral("spans")).toArray()) {
                const QJsonObject js = v.toObject();
                TextSpan sp;
                sp.start = js.value(QStringLiteral("start")).toInt();
                sp.length = js.value(QStringLiteral("length")).toInt();
                const struct { const char *key; quint8 bit; } attrs[] = {
                    {"bold", TextSpan::Bold}, {"italic", TextSpan::Italic}, {"underline", TextSpan::Underline}};
                for (const auto &a : attrs) {
                    if (js.contains(QLatin1String(a.key))) {
                        sp.set |= a.bit;
                        if (js.value(QLatin1String(a.key)).toBool())
                            sp.on |= a.bit;
                    }
                }
                if (js.contains(QStringLiteral("color"))) {
                    sp.color = QColor::fromString(js.value(QStringLiteral("color")).toString());
                    if (sp.color.isValid())
                        sp.set |= TextSpan::Color;
                }
                if (sp.set && sp.length > 0)
                    x->spans.append(sp);
            }
            obj = x;
        } else if (type == QLatin1String("line")) {
            auto x = std::make_shared<LineObject>();
            x->p1 = toPoint(j.value(QStringLiteral("p1")));
            x->p2 = toPoint(j.value(QStringLiteral("p2")));
            obj = x;
        } else if (type == QLatin1String("shape")) {
            auto x = std::make_shared<ShapeObject>();
            const QJsonArray r = j.value(QStringLiteral("rect")).toArray();
            if (r.size() == 4)
                x->rect = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
            x->kind = j.value(QStringLiteral("kind")).toString() == QLatin1String("ellipse") ? ShapeObject::Kind::Ellipse
                                                                                             : ShapeObject::Kind::Rect;
            const QString style = j.value(QStringLiteral("style")).toString();
            x->style = style == QLatin1String("cover")       ? ShapeObject::Style::Cover
                       : style == QLatin1String("highlight") ? ShapeObject::Style::Highlight
                                                             : ShapeObject::Style::Outline;
            obj = x;
        } else if (type == QLatin1String("stroke")) {
            auto x = std::make_shared<StrokeObject>();
            x->points = toPoints(j.value(QStringLiteral("points")));
            x->highlighter = j.value(QStringLiteral("highlighter")).toBool();
            if (x->points.isEmpty())
                continue;
            obj = x;
        } else if (type == QLatin1String("marker")) {
            auto x = std::make_shared<MarkerObject>();
            x->center = toPoint(j.value(QStringLiteral("center")));
            obj = x;
        } else if (type == QLatin1String("polyline")) {
            auto x = std::make_shared<PolylineObject>();
            x->points = toPoints(j.value(QStringLiteral("points")));
            x->closed = j.value(QStringLiteral("closed")).toBool();
            if (x->points.size() < 2)
                continue;
            obj = x;
        } else {
            continue; // unknown type from a newer version: skip, keep the rest
        }
        const QJsonArray t = j.value(QStringLiteral("transform")).toArray();
        if (t.size() == 6)
            obj->transform = QTransform(t[0].toDouble(), t[1].toDouble(), t[2].toDouble(), t[3].toDouble(),
                                        t[4].toDouble(), t[5].toDouble());
        obj->origin = toolByName.value(j.value(QStringLiteral("origin")).toString(), ToolId::Pointer);
        const QJsonObject props = j.value(QStringLiteral("props")).toObject();
        for (auto it = props.begin(); it != props.end(); ++it) {
            const auto p = propByName.find(it.key());
            if (p == propByName.end())
                continue;
            if (isColorProp(*p))
                obj->setProp(*p, QColor::fromString(it.value().toString()));
            else
                obj->setProp(*p, it.value().toVariant());
        }
        obj->id = Annotation::nextId();
        objects.append(obj);
    }
    *out = DocState{base, objects, QString::fromUtf8(files.value(QStringLiteral("notes.md")))};
    return true;
}

bool save(const DocState &s, const QString &path, QString *error, bool fast)
{
    const QByteArray data = encode(s, fast);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = tr("Cannot write to %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

bool load(const QString &path, DocState *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    return decode(f.readAll(), out, error);
}

} // namespace project
