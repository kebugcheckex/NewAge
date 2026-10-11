#pragma once

#include <QDialog>

class QCheckBox;

namespace newage {

class Config;

// Edits the entries of a Config, grouped by related settings. Changes
// are written to the Config only on OK; saving it to disk is up to the caller.
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(Config *config, QWidget *parent = nullptr);

    void accept() override;

private:
    Config *config_;
    QCheckBox *hideEmptyUnits_;
    QCheckBox *hideUnavailableTechs_;
    QCheckBox *showUnusedTechFields_;
    QCheckBox *showUnusedEffectFields_;
};

} // namespace newage
