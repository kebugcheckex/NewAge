#pragma once

#include <QAbstractListModel>

namespace genie {
class Unit;
}

namespace newage {

class Session;

// The units of one civilization, one row per unit slot, so row == unit index.
// Slots without a unit (Civ::UnitPointers[i] == 0) are listed but disabled.
// Units are labelled with their language name when the session has strings
// ("82 - Castle"), else with their internal name ("82 - CSTL").
class UnitListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        // bool: the slot holds a unit.
        HasUnitRole = Qt::UserRole,
        // QString: the label plus the internal name, for text filtering, so
        // both "castle" and "cstl" find unit 82.
        SearchTextRole,
    };

    explicit UnitListModel(Session *session, QObject *parent = nullptr);

    int civ() const { return civ_; }
    // Switches to the units of civ `civ`, or shows nothing if it is out of range.
    void setCiv(int civ);

    // nullptr for an empty slot or when no data is open.
    const genie::Unit *unit(int row) const;

    // The label rule of AGE's GetUnitName: language name, else internal
    // name, else "(unnamed)"; "(empty)" for an empty slot.
    QString unitName(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    bool hasUnit(int row) const;

    Session *session_;
    int civ_ = -1;
};

} // namespace newage
