#pragma once

#include <QList>
#include <QString>

#include "genie/Types.h"
#include "model/FieldDesc.h"

namespace genie {
class Effect;
}

namespace newage {

// An effect together with its ID, which genie::Effect doesn't store: the ID is
// the effect's index in DatFile::Effects.
struct EffectRef
{
    int id;
    genie::Effect &effect;
};

// Descriptors for one effect. Command count varies, so this is built per
// effect rather than returned from a static list. Groups: General, then
// "Command 1", "Command 2", ... Parameters a command type doesn't use are
// left out, as are slots still stored as the unused value -1 (usually Unit,
// when the command targets a class, or Class, when it targets a unit).
// Amount, Mode and Modify Tech's Action keep -1: it is a real value. An
// unknown type shows A, B, C and D, omitting any of A/B/C that are -1.
QList<FieldDesc<EffectRef>> effectFields(const genie::Effect &effect, genie::GameVersion version);

// Label for an effect command Type, without the number: "Disable Tech".
// Unknown or version-inappropriate types are "Unknown".
QString effectTypeName(genie::GameVersion version, int type);

// The effect command Types `version` knows, ascending: those effectTypeName()
// doesn't call unknown.
QList<int> effectTypeIds(genie::GameVersion version);

// Label for an effect command Mode of `kind` (RefKind::ResourceMode,
// TechModifierMode, EnableMode or UpgradeMode), without the number: "Set",
// "+/-", "Multiply", "Disable", "Enable", "All", "On map". Every value has a
// label: besides the special values (0; 2 for DE tech modifiers; -1 for
// UpgradeMode), the game treats any value alike, so -1 adds or enables.
QString effectModeName(genie::GameVersion version, RefKind kind, int mode);

// The values of a Mode `kind` that `version`'s game data uses, ascending:
// the special ones and the usual other one (1).
QList<int> effectModeIds(genie::GameVersion version, RefKind kind);

} // namespace newage
