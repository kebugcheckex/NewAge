#pragma once

#include <QSortFilterProxyModel>

namespace newage {

// Filter for a UnitListModel: the usual text filter, plus optionally hiding
// slots that hold no unit (UnitListModel::HasUnitRole is false).
class UnitFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit UnitFilterModel(QObject *parent = nullptr);

    bool hidesEmpty() const { return hideEmpty_; }
    void setHideEmpty(bool hide);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool hideEmpty_ = false;
};

} // namespace newage
