#include "model/ListFilterModel.h"

#include "model/EntityListModel.h"

namespace newage {

ListFilterModel::ListFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterRole(EntityListModel::SearchTextRole);
}

void ListFilterModel::setHideInactive(bool hide)
{
    if (hide == hideInactive_)
        return;
    hideInactive_ = hide;
    invalidateFilter();
}

bool ListFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (hideInactive_
        && !sourceModel()->index(sourceRow, 0, sourceParent).data(EntityListModel::ActiveRole).toBool())
        return false;
    return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

} // namespace newage
