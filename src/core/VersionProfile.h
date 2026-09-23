#pragma once

#include <QList>
#include <QString>

#include "genie/Types.h"

namespace newage {

// A game version the user can pick when opening a .dat file.
// Mirrors AGE's EditableVersion list, minus the internal test builds.
struct VersionProfile
{
    QString key;                   // Stable id stored in settings, e.g. "aoe2de".
    QString displayName;           // Shown in the UI.
    genie::GameVersion gameVersion; // Version handed to genieutils before loading.
};

// All selectable versions, in display order.
const QList<VersionProfile> &versionProfiles();

// Returns nullptr when the key is unknown.
const VersionProfile *findVersionProfile(const QString &key);

} // namespace newage
