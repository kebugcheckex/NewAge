#pragma once

#include <QAbstractListModel>

namespace genie {
class Unit;
}

namespace newage {

class Session;

// The units of one civilization, one row per unit slot, so row == unit index.
// Slots without a unit (Civ::UnitPointers[i] == 0) are listed but disabled.
class UnitListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit UnitListModel(Session *session, QObject *parent = nullptr);

    int civ() const { return civ_; }
    // Switches to the units of civ `civ`, or shows nothing if it is out of range.
    void setCiv(int civ);

    // nullptr for an empty slot or when no data is open.
    const genie::Unit *unit(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    bool hasUnit(int row) const;

    Session *session_;
    int civ_ = -1;
};

} // namespace newage
