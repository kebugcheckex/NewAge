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

// "102 - Disable Tech" style label for an effect command Type. Unknown or
// version-inappropriate types are "%1 - Unknown".
QString effectTypeName(genie::GameVersion version, int type);

} // namespace newage
