#include "model/RefNames.h"

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/EffectNames.h"
#include "model/ResourceNames.h"
#include "model/TechFields.h"
#include "model/UnitFields.h"
#include "model/UnitNames.h"

namespace newage {

namespace {

QString techName(const Session &session, int id)
{
    if (!session.isOpen() || id < 0)
        return {};
    const auto &techs = session.dat()->Techs;
    if (id >= static_cast<int>(techs.size()))
        return {};
    const genie::Tech &tech = techs[id];
    if (const QString text = session.names().text(tech.LanguageDLLName); !text.isEmpty())
        return text;
    if (!tech.Name.empty())
        return QString::fromLatin1(tech.Name);
    return {};
}

QString civName(const Session &session, int id)
{
    if (!session.isOpen() || id < 0)
        return {};
    const auto &civs = session.dat()->Civs;
    if (id >= static_cast<int>(civs.size()) || civs[id].Name.empty())
        return {};
    return QString::fromLatin1(civs[id].Name);
}

QString effectName(const Session &session, int id)
{
    if (!session.isOpen() || id < 0)
        return {};
    const auto &effects = session.dat()->Effects;
    if (id >= static_cast<int>(effects.size()) || effects[id].Name.empty())
        return {};
    return QString::fromLatin1(effects[id].Name);
}

} // namespace

QString refName(const Session &session, RefKind kind, int id, int civ)
{
    switch (kind)
    {
    case RefKind::None:
        return {};
    case RefKind::Tech:
        return techName(session, id);
    case RefKind::Resource:
        return resourceName(session.gameVersion(), id);
    case RefKind::Civ:
        return civName(session, id);
    case RefKind::Effect:
        return effectName(session, id);
    case RefKind::Unit:
        return unitName(session, civ, id);
    case RefKind::UnitClass:
        return unitClassName(session.gameVersion(), id);
    case RefKind::Attribute:
        return effectAttributeName(session.gameVersion(), id);
    case RefKind::UnitType:
        return unitTypeName(id);
    case RefKind::TechType:
        return techTypeName(id);
    }
    return {};
}

QList<int> fixedRefIds(genie::GameVersion version, RefKind kind)
{
    QList<int> ids;
    switch (kind)
    {
    case RefKind::Resource:
        for (int id = 0; id < resourceNames(version).size(); ++id)
            ids.append(id);
        break;
    case RefKind::UnitClass:
        for (int id = 0; id < unitClassCount(version); ++id)
            ids.append(id);
        break;
    case RefKind::Attribute:
        ids = effectAttributeIds(version);
        break;
    case RefKind::UnitType:
        ids = unitTypeIds();
        break;
    case RefKind::TechType:
        ids = techTypeIds();
        break;
    case RefKind::None:
    case RefKind::Tech:
    case RefKind::Civ:
    case RefKind::Effect:
    case RefKind::Unit:
        break;
    }
    return ids;
}

} // namespace newage
