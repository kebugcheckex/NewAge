#pragma once

#include <QSortFilterProxyModel>

namespace newage {

// Filter for an EntityListModel: the usual text filter, plus optionally hiding
// inactive rows (EntityListModel::ActiveRole is false), such as empty unit
// slots or techs the civ can't research.
class ListFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit ListFilterModel(QObject *parent = nullptr);

    bool hidesInactive() const { return hideInactive_; }
    void setHideInactive(bool hide);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool hideInactive_ = false;
};

} // namespace newage
