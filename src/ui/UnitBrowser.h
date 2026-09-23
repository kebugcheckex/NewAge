#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QListView;
class QTreeView;

namespace newage {

class Config;
class FieldTreeModel;
class Session;
class UnitFilterModel;
class UnitListModel;

// Civ picker + filterable unit list on the left, the selected unit's fields on
// the right. Read-only for now. Follows the unit list entries of `config`.
class UnitBrowser : public QWidget
{
    Q_OBJECT

public:
    UnitBrowser(Session *session, Config *config, QWidget *parent = nullptr);

private:
    void reloadCivs();
    void showCiv(int civ);
    void showSelectedUnit();
    // Unit index (row in UnitListModel) of the selection, or -1.
    int selectedUnit() const;
    void selectUnit(int unit);
    void applyConfig();

    Session *session_;
    Config *config_;
    UnitListModel *unitModel_;
    UnitFilterModel *unitFilter_;
    FieldTreeModel *fieldModel_;

    QComboBox *civCombo_;
    QLineEdit *filterEdit_;
    QListView *unitView_;
    QTreeView *fieldView_;
};

} // namespace newage
