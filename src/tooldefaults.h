#pragma once

#include "annotation.h"

#include <QList>
#include <QVariant>

// Default property values per drawing tool, remembered across sessions (spec D1, 13).
namespace tooldefaults {

// Properties offered in the options bar for a drawing tool.
QList<Prop> props(ToolId tool);
QVariant value(ToolId tool, Prop prop);
void setValue(ToolId tool, Prop prop, const QVariant &v);
// Apply all defaults of `tool` to a freshly created object.
void applyTo(ToolId tool, Annotation &obj);
// Forget stored values (tests).
void resetAll();

} // namespace tooldefaults
