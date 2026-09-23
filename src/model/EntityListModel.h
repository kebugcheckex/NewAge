#pragma once

#include <QAbstractListModel>

namespace newage {

class FieldTreeModel;
class Session;

// Base for the list of one entity type (units, techs, ...) as seen by one
// civilization, one row per entity ID, so row == ID. Rows the civ can't use
// (an empty unit slot, a tech of another civ) are "inactive": still listed,
// and hidden by ListFilterModel on request.
//
// Rows are labelled "<ID> - <name()>", with the internal name as tooltip.
class EntityListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        // bool: the row is usable by the current civ.
        ActiveRole = Qt::UserRole,
        // QString: the label plus the internal name, for text filtering, so
        // both "castle" and "cstl" find unit 82.
        SearchTextRole,
    };

    int civ() const { return civ_; }
    // Switches to civ `civ`, or shows nothing if it is out of range.
    void setCiv(int civ);

    // The label rule of AGE's GetUnitName: language name, else internal
    // name, else "(unnamed)".
    virtual QString name(int row) const = 0;
    // Fills `fields` with the fields of the entity in `row`, or clears it.
    virtual void showFields(int row, FieldTreeModel &fields) const = 0;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

protected:
    explicit EntityListModel(Session *session, QObject *parent = nullptr);

    Session *session() const { return session_; }
    // Whether civ() is a valid civ of the open data.
    bool hasCiv() const;

    // Called inside the model reset of setCiv(), after civ() has changed.
    virtual void civChanged() {}
    virtual bool isActive(int row) const = 0;
    // Empty when the row has no entity.
    virtual QString internalName(int row) const = 0;

private:
    Session *session_;
    int civ_ = -1;
};

} // namespace newage
