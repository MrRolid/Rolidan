#include "propertypanel.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSpinBox>
#include <QToolButton>

namespace {
// Quick colors (spec 5: fast annotation of screenshots).
const QList<QColor> kPresets = {QColor(0xe5, 0x1c, 0x23), QColor(0xff, 0x8f, 0x00), QColor(0xff, 0xeb, 0x3b),
                                QColor(0x43, 0xa0, 0x47), QColor(0x1e, 0x88, 0xe5), QColor(0x8e, 0x24, 0xaa),
                                QColor(Qt::black),        QColor(Qt::white)};

QIcon swatch(const QColor &c, int w = 18, int h = 18)
{
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    if (c.alpha() == 0) { // "none": white with a red diagonal
        p.fillRect(pm.rect(), Qt::white);
        p.setPen(QPen(Qt::red, 2));
        p.drawLine(1, h - 2, w - 2, 1);
    } else {
        p.fillRect(pm.rect(), c);
    }
    p.setPen(QColor(0, 0, 0, 120));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return QIcon(pm);
}

QLabel *label(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setContentsMargins(6, 0, 0, 0);
    return l;
}
} // namespace

PropertyPanel::PropertyPanel(const QList<Prop> &props, QWidget *parent)
    : QWidget(parent)
    , m_props(props)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 0, 4, 0);
    lay->setSpacing(4);

    for (Prop p : props) {
        switch (p) {
        case Prop::Color: {
            lay->addWidget(colorButton(p, false));
            // One-click preset swatches.
            for (const QColor &c : kPresets) {
                auto *b = new QToolButton(this);
                b->setIcon(swatch(c, 14, 14));
                b->setAutoRaise(true);
                b->setToolTip(c.name());
                connect(b, &QToolButton::clicked, this, [this, c] { emitChanged(Prop::Color, c); });
                lay->addWidget(b);
            }
            break;
        }
        case Prop::Fill:
            lay->addWidget(label(tr("Fill:"), this));
            lay->addWidget(colorButton(p, true));
            break;
        case Prop::Background:
            lay->addWidget(label(tr("Background:"), this));
            lay->addWidget(colorButton(p, true));
            break;
        case Prop::Outline:
            lay->addWidget(label(tr("Outline:"), this));
            lay->addWidget(colorButton(p, true));
            break;
        case Prop::Width:
        case Prop::HeadSize:
        case Prop::Radius:
        case Prop::Size: {
            const QString text = p == Prop::Width      ? tr("Width:")
                                 : p == Prop::HeadSize ? tr("Head:")
                                 : p == Prop::Size     ? tr("Size:")
                                                       : tr("Radius:");
            lay->addWidget(label(text, this));
            auto *sb = new QSpinBox(this);
            sb->setRange(p == Prop::Width || p == Prop::Size ? 1 : 0,
                         p == Prop::Width ? 200 : p == Prop::Size ? 1000 : 500);
            if (p == Prop::Size)
                sb->setToolTip(tr("Brush diameter ([ and ] change it)"));
            sb->setSuffix(QStringLiteral(" px"));
            if (p == Prop::HeadSize)
                sb->setSpecialValueText(tr("Auto"));
            connect(sb, &QSpinBox::valueChanged, this, [this, p](int v) { emitChanged(p, double(v)); });
            m_widgets[p] = sb;
            lay->addWidget(sb);
            break;
        }
        case Prop::Opacity: {
            lay->addWidget(label(tr("Opacity:"), this));
            auto *sb = new QSpinBox(this);
            sb->setRange(5, 100);
            sb->setSingleStep(5);
            sb->setSuffix(QStringLiteral(" %"));
            connect(sb, &QSpinBox::valueChanged, this, [this](int v) { emitChanged(Prop::Opacity, v / 100.0); });
            m_widgets[p] = sb;
            lay->addWidget(sb);
            break;
        }
        case Prop::Number: {
            lay->addWidget(label(tr("Number:"), this));
            auto *sb = new QSpinBox(this);
            sb->setRange(0, 9999);
            connect(sb, &QSpinBox::valueChanged, this, [this](int v) { emitChanged(Prop::Number, v); });
            m_widgets[p] = sb;
            lay->addWidget(sb);
            break;
        }
        case Prop::Hardness: {
            lay->addWidget(label(tr("Hardness:"), this));
            auto *sb = new QSpinBox(this);
            sb->setRange(0, 100);
            sb->setSingleStep(10);
            sb->setSuffix(QStringLiteral(" %"));
            sb->setToolTip(tr("0 % = soft edge, 100 % = sharp edge"));
            connect(sb, &QSpinBox::valueChanged, this, [this](int v) { emitChanged(Prop::Hardness, v / 100.0); });
            m_widgets[p] = sb;
            lay->addWidget(sb);
            break;
        }
        case Prop::Strength: {
            lay->addWidget(label(tr("Blur:"), this));
            auto *sb = new QSpinBox(this);
            sb->setRange(1, 100);
            sb->setSuffix(QStringLiteral(" px"));
            connect(sb, &QSpinBox::valueChanged, this, [this](int v) { emitChanged(Prop::Strength, v); });
            m_widgets[p] = sb;
            lay->addWidget(sb);
            break;
        }
        case Prop::Cell: {
            lay->addWidget(label(tr("Cell:"), this));
            auto *sb = new QSpinBox(this);
            sb->setRange(2, 200);
            sb->setSuffix(QStringLiteral(" px"));
            sb->setToolTip(tr("Size of the mosaic squares"));
            connect(sb, &QSpinBox::valueChanged, this, [this](int v) { emitChanged(Prop::Cell, v); });
            m_widgets[p] = sb;
            lay->addWidget(sb);
            break;
        }
        case Prop::Tolerance: {
            lay->addWidget(label(tr("Tolerance:"), this));
            auto *sb = new QSpinBox(this);
            sb->setRange(0, 100);
            sb->setSuffix(QStringLiteral(" %"));
            sb->setToolTip(tr("How similar a color must be to the clicked one to be filled"));
            connect(sb, &QSpinBox::valueChanged, this, [this](int v) { emitChanged(Prop::Tolerance, v); });
            m_widgets[p] = sb;
            lay->addWidget(sb);
            break;
        }
        case Prop::Contiguous: {
            auto *b = new QToolButton(this);
            b->setCheckable(true);
            b->setText(tr("Contiguous"));
            b->setToolTip(tr("On: fills only the connected area around the click.\n"
                             "Off: fills every pixel of a similar color."));
            connect(b, &QToolButton::toggled, this, [this](bool on) { emitChanged(Prop::Contiguous, on); });
            m_widgets[p] = b;
            lay->addWidget(b);
            break;
        }
        case Prop::Aligned: {
            auto *b = new QToolButton(this);
            b->setCheckable(true);
            b->setText(tr("Aligned"));
            b->setToolTip(tr("Aligned: the source follows the brush across strokes.\n"
                             "Off: every stroke starts at the original source."));
            connect(b, &QToolButton::toggled, this, [this](bool on) { emitChanged(Prop::Aligned, on); });
            m_widgets[p] = b;
            lay->addWidget(b);
            break;
        }
        case Prop::FontSize: {
            auto *sb = new QSpinBox(this);
            sb->setRange(4, 1000);
            sb->setSuffix(QStringLiteral(" px"));
            sb->setToolTip(tr("Font size in image pixels"));
            connect(sb, &QSpinBox::valueChanged, this, [this](int v) { emitChanged(Prop::FontSize, v); });
            m_widgets[p] = sb;
            lay->addWidget(sb);
            break;
        }
        case Prop::FontFamily: {
            auto *fc = new QFontComboBox(this);
            fc->setMaximumWidth(180);
            connect(fc, &QFontComboBox::currentFontChanged, this,
                    [this](const QFont &f) { emitChanged(Prop::FontFamily, f.family()); });
            m_widgets[p] = fc;
            lay->addWidget(fc);
            break;
        }
        case Prop::Bold:
        case Prop::Italic:
        case Prop::Underline:
        case Prop::HeadStart:
        case Prop::HeadEnd: {
            auto *b = new QToolButton(this);
            b->setCheckable(true);
            b->setAutoRaise(true);
            QFont f = b->font();
            switch (p) {
            case Prop::Bold: b->setText(tr("B", "bold")); f.setBold(true); b->setToolTip(tr("Bold (Ctrl+B)") + QLatin1Char('\n') + tr("Applies only to the selected characters while text is being edited and some is selected.")); break;
            case Prop::Italic: b->setText(tr("I", "italic")); f.setItalic(true); b->setToolTip(tr("Italic (Ctrl+I)") + QLatin1Char('\n') + tr("Applies only to the selected characters while text is being edited and some is selected.")); break;
            case Prop::Underline:
                b->setText(tr("U", "underline"));
                f.setUnderline(true);
                b->setToolTip(tr("Underline (Ctrl+U)") + QLatin1Char('\n') + tr("Applies only to the selected characters while text is being edited and some is selected."));
                break;
            case Prop::HeadStart: b->setText(QStringLiteral("◀")); b->setToolTip(tr("Arrow head at start")); break;
            default: b->setText(QStringLiteral("▶")); b->setToolTip(tr("Arrow head at end")); break;
            }
            b->setFont(f);
            connect(b, &QToolButton::toggled, this, [this, p](bool on) { emitChanged(p, on); });
            m_widgets[p] = b;
            lay->addWidget(b);
            break;
        }
        case Prop::Align: {
            auto *group = new QButtonGroup(this);
            const QList<QPair<int, QString>> items = {{int(Qt::AlignLeft), tr("Left")},
                                                      {int(Qt::AlignHCenter), tr("Center")},
                                                      {int(Qt::AlignRight), tr("Right")}};
            const QStringList glyphs = {QStringLiteral("⇤"), QStringLiteral("↔"), QStringLiteral("⇥")};
            for (int i = 0; i < items.size(); ++i) {
                auto *b = new QToolButton(this);
                b->setCheckable(true);
                b->setAutoRaise(true);
                b->setText(glyphs[i]);
                b->setToolTip(items[i].second);
                group->addButton(b, items[i].first);
                lay->addWidget(b);
            }
            group->setExclusive(true);
            connect(group, &QButtonGroup::idClicked, this, [this](int id) { emitChanged(Prop::Align, id); });
            m_widgets[p] = group->buttons().first(); // marker; buttons found via group
            setProperty("alignGroup", QVariant::fromValue(static_cast<QObject *>(group)));
            break;
        }
        case Prop::HighlightMode: {
            auto *cb = new QComboBox(this);
            cb->addItem(tr("Freehand"), 0);
            cb->addItem(tr("Rectangle"), 1);
            connect(cb, &QComboBox::activated, this,
                    [this, cb](int i) { emitChanged(Prop::HighlightMode, cb->itemData(i)); });
            m_widgets[p] = cb;
            lay->addWidget(cb);
            break;
        }
        }
        // Stable names for tests and style sheets.
        if (QWidget *w = m_widgets.value(p))
            w->setObjectName(QLatin1String(propName(p)));
        if (QToolButton *b = m_colorButtons.value(p))
            b->setObjectName(QLatin1String(propName(p)));
    }
    lay->addStretch();
}

QToolButton *PropertyPanel::colorButton(Prop prop, bool allowNone)
{
    auto *b = new QToolButton(this);
    b->setAutoRaise(true);
    b->setIconSize(QSize(22, 18));
    b->setToolTip(prop == Prop::Color ? tr("Color") : tr("Choose color"));
    if (allowNone) {
        auto *menu = new QMenu(b);
        menu->addAction(swatch(Qt::transparent), tr("None"), this,
                        [this, prop] { emitChanged(prop, QColor(Qt::transparent)); });
        for (const QColor &c : kPresets)
            menu->addAction(swatch(c), c.name(), this, [this, prop, c] { emitChanged(prop, c); });
        menu->addSeparator();
        menu->addAction(tr("Other…"), this, [this, prop] { pickColor(prop); });
        b->setMenu(menu);
        b->setPopupMode(QToolButton::InstantPopup);
    } else {
        connect(b, &QToolButton::clicked, this, [this, prop] { pickColor(prop); });
    }
    m_colorButtons[prop] = b;
    return b;
}

void PropertyPanel::setColorButton(QToolButton *b, const QColor &c)
{
    b->setIcon(swatch(c, 22, 18));
}

void PropertyPanel::pickColor(Prop prop)
{
    QColor current = m_values.value(prop).value<QColor>();
    if (!current.isValid() || current.alpha() == 0)
        current = Qt::red;
    const QColor c = QColorDialog::getColor(current, this, tr("Choose color"));
    if (c.isValid())
        emitChanged(prop, c);
}

void PropertyPanel::emitChanged(Prop prop, const QVariant &v)
{
    if (m_updating)
        return;
    m_values[prop] = v;
    if (auto *b = m_colorButtons.value(prop))
        setColorButton(b, v.value<QColor>());
    emit changed(prop, v);
}

void PropertyPanel::setValues(const QHash<Prop, QVariant> &values)
{
    m_updating = true;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const Prop p = it.key();
        const QVariant &v = it.value();
        m_values[p] = v;
        if (auto *b = m_colorButtons.value(p)) {
            setColorButton(b, v.value<QColor>());
            continue;
        }
        QWidget *w = m_widgets.value(p);
        if (!w)
            continue;
        if (auto *sb = qobject_cast<QSpinBox *>(w)) {
            const bool percent = p == Prop::Opacity || p == Prop::Hardness;
            sb->setValue(percent ? int(std::lround(v.toDouble() * 100)) : int(std::lround(v.toDouble())));
        } else if (auto *fc = qobject_cast<QFontComboBox *>(w)) {
            fc->setCurrentFont(QFont(v.toString()));
        } else if (auto *cb = qobject_cast<QComboBox *>(w)) {
            cb->setCurrentIndex(std::max(0, cb->findData(v)));
        } else if (p == Prop::Align) {
            if (auto *group = qobject_cast<QButtonGroup *>(property("alignGroup").value<QObject *>()))
                if (QAbstractButton *b = group->button(v.toInt()))
                    b->setChecked(true);
        } else if (auto *tb = qobject_cast<QToolButton *>(w)) {
            tb->setChecked(v.toBool());
        }
    }
    m_updating = false;
}
