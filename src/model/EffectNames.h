#pragma once

#include <QString>

#include "genie/Types.h"

namespace newage {

// Unit class names (Unit::Class, and an effect command's Class slot). The game
// files don't store them, so these are AGE's default names. Empty if `id` is
// not a class in `version`.
QString unitClassName(genie::GameVersion version, int id);

// Effect-command attribute names (hit points, line of sight, ...). AGE's
// default names; the "(types ...)" notes from its combos are left off. Empty
// if `version` has no attribute `id`.
QString effectAttributeName(genie::GameVersion version, int id);

} // namespace newage
