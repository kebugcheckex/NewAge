#pragma once

#include <QString>
#include <QStringList>

#include "genie/Types.h"

namespace newage {

// Names of the civ resources (Food Storage, Wood Storage, ...) of a game
// version, indexed by resource ID. The game files don't name them, so these
// are AGE's default names.
const QStringList &resourceNames(genie::GameVersion version);

// Name of resource `id`, or an empty string if `version` has no such resource.
QString resourceName(genie::GameVersion version, int id);

} // namespace newage
