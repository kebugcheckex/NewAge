#pragma once

#include <QList>

#include "model/EntityListModel.h"

namespace genie {
class Tech;
}

namespace newage {

// All techs (DatFile::Techs is global, not per civ), one row per tech, so
// row == tech ID, marked by whether the current civ can research them. Techs
// it can't are inactive but stay selectable, since they still have data.
// Labels follow the unit list rule ("22 - Loom", else "22 - LOOM").
class TechListModel : public EntityListModel
{
    Q_OBJECT

public:
    enum class Availability
    {
        Available,
        // Tech::Civ names another civ (unique techs, civ bonuses).
        OtherCiv,
        // The civ's tech tree effect (Civ::TechTreeID) disables it with a
        // "disable tech" command (type 102).
        DisabledByTechTree,
    };

    explicit TechListModel(Session *session, QObject *parent = nullptr);

    // nullptr when `row` is out of range or no data is open.
    const genie::Tech *tech(int row) const;
    Availability availability(int row) const;

    QString name(int row) const override;
    void showFields(int row, FieldTreeModel &fields) override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

protected:
    void civChanged() override;
    bool isActive(int row) const override;
    QString internalName(int row) const override;

private:
    // Per tech, for civ(). None of the editable fields affect it, so it is
    // worked out once per civ switch.
    QList<Availability> availability_;
};

} // namespace newage
