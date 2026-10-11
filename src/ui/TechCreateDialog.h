#pragma once

#include <QDialog>

#include "api/DataService.h"

class QComboBox;
class QLineEdit;
class QTableWidget;

namespace newage {

class Session;

class TechCreateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TechCreateDialog(const Session &session, int civ, QWidget *parent = nullptr);
    CreateTechSpec spec() const;

protected:
    void accept() override;

private:
    void addCommand();

    QComboBox *civ_;
    QLineEdit *name_;
    QLineEdit *effectName_;
    QLineEdit *prerequisites_;
    QTableWidget *commands_;
};

} // namespace newage
