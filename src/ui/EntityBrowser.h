#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QListView;
class QTreeView;

namespace newage {

class Config;
class EntityListModel;
class FieldTreeModel;
class ListFilterModel;
class Session;

// Civ picker + filterable entity list on the left, the selected entity's
// fields on the right. Read-only for now. Works for any EntityListModel; the
// per-type parts are the model and the Config entry that hides inactive rows.
class EntityBrowser : public QWidget
{
    Q_OBJECT

public:
    using HideInactiveEntry = bool (Config::*)() const;

    // Takes ownership of `model`. `hideInactive` is re-read whenever `config`
    // changes. `filterHint` is the filter box placeholder ("Filter units").
    EntityBrowser(Session *session, Config *config, EntityListModel *model, HideInactiveEntry hideInactive,
                  const QString &filterHint, QWidget *parent = nullptr);

    EntityListModel *model() const { return listModel_; }

private:
    void reloadCivs();
    void showCiv(int civ);
    void showSelected();
    // Entity ID (row in the list model) of the selection, or -1.
    int selectedRow() const;
    void selectRow(int row);
    void applyConfig();

    Session *session_;
    Config *config_;
    HideInactiveEntry hideInactive_;
    EntityListModel *listModel_;
    ListFilterModel *listFilter_;
    FieldTreeModel *fieldModel_;

    QComboBox *civCombo_;
    QLineEdit *filterEdit_;
    QListView *listView_;
    QTreeView *fieldView_;
};

} // namespace newage
