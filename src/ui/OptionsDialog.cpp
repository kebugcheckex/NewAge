#include "ui/OptionsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include "core/Config.h"

namespace newage {

OptionsDialog::OptionsDialog(Config *config, QWidget *parent)
    : QDialog(parent),
      config_(config),
      hideEmptyUnits_(new QCheckBox(tr("&Hide empty unit slots"), this)),
      hideUnavailableTechs_(new QCheckBox(tr("Hide &techs the civilization can't research"), this))
{
    setWindowTitle(tr("Options"));

    hideEmptyUnits_->setObjectName(QStringLiteral("hideEmptyUnits"));
    hideEmptyUnits_->setToolTip(tr("Leave out unit IDs that have no unit in the selected civilization."));
    hideEmptyUnits_->setChecked(config_->hideEmptyUnits());

    hideUnavailableTechs_->setObjectName(QStringLiteral("hideUnavailableTechs"));
    hideUnavailableTechs_->setToolTip(tr("Leave out techs of other civilizations and techs disabled by the "
                                         "selected civilization's tech tree."));
    hideUnavailableTechs_->setChecked(config_->hideUnavailableTechs());

    auto *unitList = new QGroupBox(tr("Unit list"), this);
    auto *unitListLayout = new QVBoxLayout(unitList);
    unitListLayout->addWidget(hideEmptyUnits_);

    auto *techList = new QGroupBox(tr("Tech list"), this);
    auto *techListLayout = new QVBoxLayout(techList);
    techListLayout->addWidget(hideUnavailableTechs_);

    auto *location = new QLabel(tr("Stored in %1").arg(QDir::toNativeSeparators(config_->path())), this);
    location->setTextInteractionFlags(Qt::TextSelectableByMouse);
    location->setWordWrap(true);
    location->setEnabled(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(unitList);
    layout->addWidget(techList);
    layout->addStretch();
    layout->addWidget(location);
    layout->addWidget(buttons);

    resize(420, sizeHint().height());
}

void OptionsDialog::accept()
{
    config_->setHideEmptyUnits(hideEmptyUnits_->isChecked());
    config_->setHideUnavailableTechs(hideUnavailableTechs_->isChecked());
    QDialog::accept();
}

} // namespace newage
