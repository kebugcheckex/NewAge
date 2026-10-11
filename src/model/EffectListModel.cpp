#include "model/EffectListModel.h"

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/EffectFields.h"
#include "model/EntityKind.h"
#include "model/FieldTreeModel.h"
#include "model/RefNames.h"

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
    if (!hasCiv())
        return {};
    return effectKind().name(*session(), civ(), row);
}

void EffectListModel::showFields(int row, FieldTreeModel &fields)
{
    const genie::Effect *e = effect(row);
    if (!e)
    {
        fields.clear();
        return;
    }

    const auto descriptors = effectFields(*e, session()->gameVersion());
    QStringList keys;
    for (const auto &descriptor : descriptors)
        keys.append(descriptor.key);
    const auto writer = [this, row, keys](int field, const QVariant &value) -> QVariant {
        if (field < 0 || field >= keys.size())
            return {};
        const SetResult result = effectKind().set(*session(), civ(), row, keys.at(field), value);
        if (!result.ok)
            return {};
        if (result.changed)
            entityEdited(row);
        return result.newValue;
    };
    const auto refNamer = [this](RefKind kind, int id) { return refName(*session(), kind, id, civ()); };
    fields.setObject(descriptors, EffectRef{row, session()->dat()->Effects.at(row)},
                     nullptr, writer, refNamer, fields.includeUnused());
}

bool EffectListModel::isActive(int row) const
{
    return hasCiv() && effectKind().isActive(*session(), civ(), row);
}

QString EffectListModel::internalName(int row) const
{
    if (!hasCiv())
        return {};
    return effectKind().internalName(*session(), civ(), row);
}

} // namespace newage
