#pragma once

#include <QList>
#include <QString>

#include "genie/Types.h"

#include "model/FieldDesc.h"

namespace newage {

class Session;

// Name of reference `id` of `kind`, for a field label. `civ` selects the unit
// copy for RefKind::Unit and is ignored otherwise. Language name, else internal
// name, else a fixed-list name. Empty when there is none, so the field keeps
// the plain ID; "(unnamed)" is a list label, not a reference name.
QString refName(const Session &session, RefKind kind, int id, int civ);

// IDs named by the fixed list behind `kind` (resources, unit classes,
// attributes, unit types, tech types), ascending. Empty for kinds whose values
// are entities in the data.
QList<int> fixedRefIds(genie::GameVersion version, RefKind kind);

} // namespace newage
