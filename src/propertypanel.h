#pragma once

#include "toolid.h"

#include <QHash>
#include <QVariant>
#include <QWidget>

class QAbstractButton;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QSpinBox;
class QToolButton;

// Options bar content for drawing tools and selected objects (spec D1).
// Emits `changed` for every user edit; continuous edits (spin boxes) emit
// repeatedly and are merged into one undo step by the receiver.
class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    PropertyPanel(const QList<Prop> &props, QWidget *parent = nullptr);

    // Update displayed values without emitting `changed`.
    void setValues(const QHash<Prop, QVariant> &values);
    QList<Prop> props() const { return m_props; }

signals:
    void changed(Prop prop, const QVariant &value);

private:
    QToolButton *colorButton(Prop prop, bool allowNone);
    void setColorButton(QToolButton *b, const QColor &c);
    void pickColor(Prop prop);
    void emitChanged(Prop prop, const QVariant &v);

    QList<Prop> m_props;
    QHash<Prop, QVariant> m_values;
    QHash<Prop, QToolButton *> m_colorButtons;
    QHash<Prop, QWidget *> m_widgets;
    bool m_updating = false;
};
