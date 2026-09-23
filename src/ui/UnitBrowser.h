#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;
class QTreeView;

namespace newage {

class FieldTreeModel;
class Session;
class UnitListModel;

// Civ picker + filterable unit list on the left, the selected unit's fields on
// the right. Read-only for now.
class UnitBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit UnitBrowser(Session *session, QWidget *parent = nullptr);

private:
    void reloadCivs();
    void showCiv(int civ);
    void showSelectedUnit();
    // Unit index (row in UnitListModel) of the selection, or -1.
    int selectedUnit() const;
    void selectUnit(int unit);

    Session *session_;
    UnitListModel *unitModel_;
    QSortFilterProxyModel *unitFilter_;
    FieldTreeModel *fieldModel_;

    QComboBox *civCombo_;
    QLineEdit *filterEdit_;
    QListView *unitView_;
    QTreeView *fieldView_;
};

} // namespace newage
