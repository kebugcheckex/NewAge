#pragma once

#include <QString>

namespace newage {

class Session;

// Language name, else internal name, of unit `id` in `civ`. Empty when the slot
// is unused or unnamed, so a reference field keeps the plain ID.
QString unitName(const Session &session, int civ, int id);

} // namespace newage
