#pragma once

#include "model/EntityListModel.h"

namespace genie {
class Unit;
}

namespace newage {

// The units of one civilization, one row per unit slot, so row == unit index.
// Slots without a unit (Civ::UnitPointers[i] == 0) are listed but inactive
// and disabled. Units are labelled with their language name when the session
// has strings ("82 - Castle"), else with their internal name ("82 - CSTL").
class UnitListModel : public EntityListModel
{
    Q_OBJECT

public:
    explicit UnitListModel(Session *session, QObject *parent = nullptr);

    // nullptr for an empty slot or when no data is open.
    const genie::Unit *unit(int row) const;

    // "(empty)" for an empty slot, else the label rule.
    QString name(int row) const override;
    void showFields(int row, FieldTreeModel &fields) override;

    int rowCount(const QModelIndex &parent = {}) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

protected:
    bool isActive(int row) const override;
    QString internalName(int row) const override;
};

} // namespace newage
