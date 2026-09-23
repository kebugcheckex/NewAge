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
      hideEmptyUnits_(new QCheckBox(tr("&Hide empty unit slots"), this))
{
    setWindowTitle(tr("Options"));

    hideEmptyUnits_->setToolTip(tr("Leave out unit IDs that have no unit in the selected civilization."));
    hideEmptyUnits_->setChecked(config_->hideEmptyUnits());

    auto *unitList = new QGroupBox(tr("Unit list"), this);
    auto *unitListLayout = new QVBoxLayout(unitList);
    unitListLayout->addWidget(hideEmptyUnits_);

    auto *location = new QLabel(tr("Stored in %1").arg(QDir::toNativeSeparators(config_->path())), this);
    location->setTextInteractionFlags(Qt::TextSelectableByMouse);
    location->setWordWrap(true);
    location->setEnabled(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(unitList);
    layout->addStretch();
    layout->addWidget(location);
    layout->addWidget(buttons);

    resize(420, sizeHint().height());
}

void OptionsDialog::accept()
{
    config_->setHideEmptyUnits(hideEmptyUnits_->isChecked());
    QDialog::accept();
}

} // namespace newage
