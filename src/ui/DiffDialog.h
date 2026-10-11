#pragma once

#include <QDialog>

#include "api/DataService.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;

namespace newage {

// Shows the saved mod's differences from the current game data.
class DiffDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DiffDialog(DiffResult result, QWidget *parent = nullptr);

signals:
    void entityActivated(const QString &kind, int id, int civ);

private:
    void filterItems();
    void showDetails();
    void openSelected();

    DiffResult result_;
    QComboBox *kind_;
    QLineEdit *search_;
    QTableWidget *items_;
    QTreeWidget *details_;
    QPushButton *open_;
};

} // namespace newage
