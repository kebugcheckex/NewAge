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

// "70 - Combatant" style label for a unit Type value (AGE's names).
QString unitTypeName(int type);

} // namespace newage
