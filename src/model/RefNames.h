#pragma once

#include <QString>

#include "model/FieldDesc.h"

namespace newage {

class Session;

// Name of reference `id` of `kind`, for a field label. `civ` selects the unit
// copy for RefKind::Unit and is ignored otherwise. Language name, else internal
// name, else a fixed-list name. Empty when there is none, so the field keeps
// the plain ID; "(unnamed)" is a list label, not a reference name.
QString refName(const Session &session, RefKind kind, int id, int civ);

} // namespace newage
