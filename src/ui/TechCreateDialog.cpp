#include "ui/TechCreateDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/Session.h"
#include "genie/dat/DatFile.h"

namespace newage {

TechCreateDialog::TechCreateDialog(const Session &session, int civ, QWidget *parent)
    : QDialog(parent), civ_(new QComboBox(this)), name_(new QLineEdit(this)), effectName_(new QLineEdit(this)),
      prerequisites_(new QLineEdit(this)), commands_(new QTableWidget(this))
{
    setWindowTitle(tr("New automatic tech"));
    resize(840, 420);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Create an automatic civilization bonus and its effect in the open mod."), this));
    auto *form = new QFormLayout;
    for (int id = 0; id < static_cast<int>(session.dat()->Civs.size()); ++id)
        civ_->addItem(QStringLiteral("%1 - %2").arg(id).arg(QString::fromLatin1(session.dat()->Civs[id].Name)), id);
    civ_->setCurrentIndex(qMax(0, civ));
    name_->setObjectName(QStringLiteral("techInternalName"));
    effectName_->setObjectName(QStringLiteral("effectInternalName"));
    prerequisites_->setPlaceholderText(tr("Tech IDs separated by commas; leave blank for none"));
    form->addRow(tr("Civilization"), civ_);
    form->addRow(tr("Tech internal name"), name_);
    form->addRow(tr("Effect internal name"), effectName_);
    form->addRow(tr("Prerequisites"), prerequisites_);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Attribute commands: specify a unit ID, class ID, or both. Use -1 for no target."), this));
    commands_->setObjectName(QStringLiteral("techCommands"));
    commands_->setColumnCount(5);
    commands_->setHorizontalHeaderLabels({tr("Operation"), tr("Unit ID"), tr("Class ID"), tr("Attribute ID"), tr("Amount")});
    commands_->verticalHeader()->hide();
    commands_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(commands_, 1);
    auto *rowButtons = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add command"), this);
    auto *remove = new QPushButton(tr("Remove selected"), this);
    rowButtons->addWidget(add);
    rowButtons->addWidget(remove);
    rowButtons->addStretch();
    layout->addLayout(rowButtons);
    connect(add, &QPushButton::clicked, this, &TechCreateDialog::addCommand);
    connect(remove, &QPushButton::clicked, this, [this] {
        if (commands_->currentRow() >= 0 && commands_->rowCount() > 1)
            commands_->removeRow(commands_->currentRow());
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Preview..."));
    connect(buttons, &QDialogButtonBox::accepted, this, &TechCreateDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &TechCreateDialog::reject);
    layout->addWidget(buttons);
    addCommand();
}

void TechCreateDialog::addCommand()
{
    const int row = commands_->rowCount();
    commands_->insertRow(row);
    auto *type = new QComboBox(commands_);
    type->addItem(tr("Set"), 0);
    type->addItem(tr("Add"), 4);
    type->addItem(tr("Multiply"), 5);
    type->setCurrentIndex(1);
    commands_->setCellWidget(row, 0, type);
    for (int column = 1; column <= 3; ++column)
    {
        auto *id = new QSpinBox(commands_);
        id->setRange(-1, 32767);
        id->setValue(-1);
        commands_->setCellWidget(row, column, id);
    }
    auto *amount = new QDoubleSpinBox(commands_);
    amount->setRange(-1e9, 1e9);
    amount->setDecimals(6);
    commands_->setCellWidget(row, 4, amount);
}

CreateTechSpec TechCreateDialog::spec() const
{
    CreateTechSpec result;
    result.civ = civ_->currentData().toInt();
    result.internalName = name_->text();
    result.effectInternalName = effectName_->text();
    for (const QString &part : prerequisites_->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
        result.prerequisites.append(part.trimmed().toInt());
    for (int row = 0; row < commands_->rowCount(); ++row)
    {
        AttributeCommand command;
        command.type = qobject_cast<QComboBox *>(commands_->cellWidget(row, 0))->currentData().toInt();
        command.unit = qobject_cast<QSpinBox *>(commands_->cellWidget(row, 1))->value();
        command.unitClass = qobject_cast<QSpinBox *>(commands_->cellWidget(row, 2))->value();
        command.attribute = qobject_cast<QSpinBox *>(commands_->cellWidget(row, 3))->value();
        command.amount = qobject_cast<QDoubleSpinBox *>(commands_->cellWidget(row, 4))->value();
        result.commands.append(command);
    }
    return result;
}

void TechCreateDialog::accept()
{
    if (name_->text().isEmpty() || effectName_->text().isEmpty())
    {
        QMessageBox::warning(this, tr("Missing name"), tr("Enter both internal names."));
        return;
    }
    for (const QString &part : prerequisites_->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        bool ok = false;
        part.trimmed().toInt(&ok);
        if (!ok)
        {
            QMessageBox::warning(this, tr("Invalid prerequisite"), tr("Prerequisites must be comma-separated tech IDs."));
            return;
        }
    }
    QDialog::accept();
}

} // namespace newage
