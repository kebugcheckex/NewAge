#include "model/UnitNames.h"

#include "core/Session.h"
#include "genie/dat/DatFile.h"

namespace newage {

QString unitName(const Session &session, int civ, int id)
{
    if (!session.isOpen() || civ < 0 || id < 0)
        return {};
    const auto &civs = session.dat()->Civs;
    if (civ >= static_cast<int>(civs.size()))
        return {};
    const genie::Civ &selected = civs[civ];
    if (id >= static_cast<int>(selected.Units.size()) || id >= static_cast<int>(selected.UnitPointers.size())
        || selected.UnitPointers[id] == 0)
        return {};
    const genie::Unit &unit = selected.Units[id];
    if (const QString name = session.names().text(unit.LanguageDLLName); !name.isEmpty())
        return name;
    if (!unit.Name.empty())
        return QString::fromLatin1(unit.Name);
    return {};
}

} // namespace newage
