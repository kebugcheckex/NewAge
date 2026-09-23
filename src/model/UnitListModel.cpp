#include "model/UnitListModel.h"

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/FieldTreeModel.h"
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
    const genie::Unit *u = unit(row);
    if (!u)
        return tr("(empty)");
    if (const QString name = session()->names().text(u->LanguageDLLName); !name.isEmpty())
        return name;
    if (!u->Name.empty())
        return QString::fromLatin1(u->Name);
    return tr("(unnamed)");
}

void UnitListModel::showFields(int row, FieldTreeModel &fields) const
{
    if (const genie::Unit *u = unit(row))
        fields.setObject(unitFields(), *u, &session()->names());
    else
        fields.clear();
}

Qt::ItemFlags UnitListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || !isActive(index.row()))
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool UnitListModel::isActive(int row) const
{
    if (!hasCiv())
        return false;
    const genie::Civ &civ = session()->dat()->Civs.at(this->civ());
    return row >= 0 && row < static_cast<int>(civ.Units.size()) && row < static_cast<int>(civ.UnitPointers.size())
           && civ.UnitPointers.at(row) != 0;
}

QString UnitListModel::internalName(int row) const
{
    const genie::Unit *u = unit(row);
    return u ? QString::fromLatin1(u->Name) : QString();
}

} // namespace newage
