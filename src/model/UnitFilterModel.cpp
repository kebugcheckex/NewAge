#include "model/UnitFilterModel.h"

#include "model/UnitListModel.h"

namespace newage {

UnitFilterModel::UnitFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterRole(UnitListModel::SearchTextRole);
}

void UnitFilterModel::setHideEmpty(bool hide)
{
    if (hide == hideEmpty_)
        return;
    hideEmpty_ = hide;
    invalidateFilter();
}

bool UnitFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (hideEmpty_
        && !sourceModel()->index(sourceRow, 0, sourceParent).data(UnitListModel::HasUnitRole).toBool())
        return false;
    return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

} // namespace newage
