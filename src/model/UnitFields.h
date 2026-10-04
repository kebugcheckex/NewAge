#pragma once

#include <QList>
#include <QString>

#include "model/FieldDesc.h"

namespace genie {
class Unit;
}

namespace newage {

// Descriptors for the unit fields shown in the property view, in display
// order. Groups appear in the order of their first field.
const QList<FieldDesc<genie::Unit>> &unitFields();

// AGE's name for a unit Type value ("Combatant" for 70), or "Unknown". Views
// show it as "70 - Combatant".
QString unitTypeName(int type);

} // namespace newage
