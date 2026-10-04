#include "model/EffectListModel.h"

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/EffectFields.h"
#include "model/EffectNames.h"
#include "model/FieldTreeModel.h"
#include "model/ResourceNames.h"
#include "model/UnitNames.h"

namespace newage {

EffectListModel::EffectListModel(Session *session, QObject *parent)
    : EntityListModel(session, parent)
{
}

const genie::Effect *EffectListModel::effect(int row) const
{
    if (!hasCiv() || row < 0 || row >= static_cast<int>(session()->dat()->Effects.size()))
        return nullptr;
    return &session()->dat()->Effects.at(row);
}

int EffectListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !hasCiv())
        return 0;
    return static_cast<int>(session()->dat()->Effects.size());
}

QString EffectListModel::name(int row) const
{
    const genie::Effect *e = effect(row);
    if (!e)
        return {};
    if (!e->Name.empty())
        return QString::fromLatin1(e->Name);
    return tr("(unnamed)");
}

void EffectListModel::showFields(int row, FieldTreeModel &fields)
{
    const genie::Effect *e = effect(row);
    if (!e)
    {
        fields.clear();
        return;
    }

    const auto refNamer = [this](RefKind kind, int id) {
        switch (kind)
        {
        case RefKind::Tech:
        {
            const auto &techs = session()->dat()->Techs;
            if (id < 0 || id >= static_cast<int>(techs.size()))
                return QString();
            const genie::Tech &tech = techs[id];
            if (const QString text = session()->names().text(tech.LanguageDLLName); !text.isEmpty())
                return text;
            if (!tech.Name.empty())
                return QString::fromLatin1(tech.Name);
            return QString();
        }
        case RefKind::Resource: return resourceName(session()->gameVersion(), id);
        case RefKind::Unit: return unitName(*session(), civ(), id);
        case RefKind::UnitClass: return unitClassName(session()->gameVersion(), id);
        case RefKind::Attribute: return effectAttributeName(session()->gameVersion(), id);
        case RefKind::Effect:
        {
            const auto &effects = session()->dat()->Effects;
            if (id < 0 || id >= static_cast<int>(effects.size()) || effects[id].Name.empty())
                return QString();
            return QString::fromLatin1(effects[id].Name);
        }
        default: return QString();
        }
    };
    fields.setObject(effectFields(*e, session()->gameVersion()), EffectRef{row, session()->dat()->Effects.at(row)},
                     nullptr, {}, refNamer);
}

bool EffectListModel::isActive(int row) const
{
    return effect(row) != nullptr;
}

QString EffectListModel::internalName(int row) const
{
    const genie::Effect *e = effect(row);
    return e ? QString::fromLatin1(e->Name) : QString();
}

} // namespace newage
