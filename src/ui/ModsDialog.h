#pragma once

#include <QDialog>

#include "core/GameInstall.h"
#include "core/Mods.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace newage {

// Rich text describing `mod` for the data set: title, author, description,
// whether it has its own .dat, and its folder.
QString modDetailsHtml(const GameDataset &dataset, const Mod &mod);

// File > Save As Mod: picks a mod in `modsFolder`, or creates one, to save the
// open data into. The dialog only touches the file system to create mods;
// saving is up to the caller, after exec() returns Accepted.
class SaveAsModDialog : public QDialog
{
    Q_OBJECT

public:
    // `dataset` is the game's data set (not a mod's). `currentModDir` is
    // selected first, if it is in the folder.
    SaveAsModDialog(const GameDataset &dataset, const QString &modsFolder, const QString &currentModDir = QString(),
                    QWidget *parent = nullptr);

    // The chosen mod's folder.
    QString modDir() const;

private:
    void showMods(const QString &select);
    void newMod();
    void updateDetails();

    GameDataset dataset_;
    QString modsFolder_;
    QList<Mod> modList_; // The mods in the list, in list order.
    QListWidget *mods_;
    QLabel *details_;
    QPushButton *saveButton_;
};

// Asks for a mod's name, author and description: for a new mod in a mods
// folder, or to edit an existing mod's info.json.
class ModInfoDialog : public QDialog
{
    Q_OBJECT

public:
    // A new mod in `modsFolder`. OK stays disabled until the title can name a
    // new folder there. `startChoices`, if any, fill a "Start from" combo,
    // the first selected.
    explicit ModInfoDialog(const QString &modsFolder, const QStringList &startChoices = {},
                           QWidget *parent = nullptr);
    // Edits `info`. The folder keeps its name, so any non-empty title will do.
    explicit ModInfoDialog(const ModInfo &info, QWidget *parent = nullptr);

    QString title() const;
    QString author() const;
    QString description() const;
    ModInfo info() const { return {title(), author(), description()}; }
    // The chosen start choice, or -1 if there were none.
    int startIndex() const;

private:
    void build();
    void validate();

    bool editing_ = false;
    QString modsFolder_;
    QLineEdit *title_;
    QLineEdit *author_;
    QPlainTextEdit *description_;
    QComboBox *start_ = nullptr;
    QLabel *problem_;
    QDialogButtonBox *buttons_;
};

} // namespace newage
