#include "model/UnitListModel.h"

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/EntityKind.h"
#include "model/FieldTreeModel.h"
#include "model/RefNames.h"
#include "model/UnitFields.h"

namespace newage {

UnitListModel::UnitListModel(Session *session, QObject *parent)
    : EntityListModel(session, parent)
{
}

const genie::Unit *UnitListModel::unit(int row) const
{
    if (!isActive(row))
        return nullptr;
    return &session()->dat()->Civs.at(civ()).Units.at(row);
}

int UnitListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !hasCiv())
        return 0;
    return static_cast<int>(session()->dat()->Civs.at(civ()).Units.size());
}

QString UnitListModel::name(int row) const
{
    return unitKind().name(*session(), civ(), row);
}

void UnitListModel::showFields(int row, FieldTreeModel &fields)
{
    const genie::Unit *u = unit(row);
    if (!u)
    {
        fields.clear();
        return;
    }

    // Units are per civ: an edit changes this civ's copy only. The unit is
    // looked up again on every write, so the writer never holds a pointer
    // into the data.
    const int editCiv = civ();
    const auto writer = [this, editCiv, row](int field, const QVariant &value) -> QVariant {
        if (civ() != editCiv)
            return {};
        const SetResult result = unitKind().set(*session(), editCiv, row, unitFields().at(field).key, value);
        if (!result.ok)
            return {};
        if (result.changed)
            entityEdited(row);
        return result.newValue;
    };
    const auto refNamer = [this](RefKind kind, int id) { return refName(*session(), kind, id, civ()); };
    fields.setObject(unitFields(), *u, &session()->names(), writer, refNamer);
}

Qt::ItemFlags UnitListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || !isActive(index.row()))
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool UnitListModel::isActive(int row) const
{
    return unitKind().isActive(*session(), civ(), row);
}

QString UnitListModel::internalName(int row) const
{
    return unitKind().internalName(*session(), civ(), row);
}

} // namespace newage
