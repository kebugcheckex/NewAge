#pragma once

#include <QWidget>

#include "core/GameInstall.h"
#include "core/Mods.h"

class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;

namespace newage {

// The Mods side panel, shown while a game folder that supports mods is open.
// It lists the game's own data and the mods in a mods folder, marks the one
// being edited, and creates mods and edits their info.json. Loading and
// saving data are up to MainWindow, through the signals.
class ModsPanel : public QWidget
{
    Q_OBJECT

public:
    enum class Edited
    {
        Nothing,  // No data open, or a file that is neither.
        GameData, // The game's own data.
        Mod,      // A mod's data, or data that saves into the mod.
    };

    explicit ModsPanel(QWidget *parent = nullptr);

    // Shows the mods of `dataset` (the game's data set, not a mod's), in
    // `folder`, or in the most likely mods folder if it's empty.
    void setGame(const GameDataset &dataset, const QString &folder);
    // Lists the mods folder again, keeping the selection.
    void reload();
    // Marks what the main window is editing; `modDir` for Edited::Mod.
    void setEdited(Edited edited, const QString &modDir = QString());
    // Whether data is open, which Save Current Data Here and starting a new
    // mod from the open data need.
    void setDataOpen(bool open);

    QString modsFolder() const;
    // Whether the selected row is the game's data, and the selected mod's
    // folder (empty for the game data or no selection).
    bool gameDataSelected() const;
    QString selectedModDir() const;
    // Makes the game data (empty `modDir`) or a listed mod the selection.
    void select(const QString &modDir);

signals:
    // Load the game data (empty `modDir`) or the mod's data for editing.
    void editRequested(const QString &modDir);
    // A mod was created. It should be edited starting from `startDat`, or
    // from the open data if that's empty.
    void modCreated(const QString &modDir, const QString &startDat);
    // Save the open data as the mod's .dat.
    void saveHereRequested(const QString &modDir);
    // A mod's info.json changed.
    void modInfoChanged(const QString &modDir, const ModInfo &info);
    // The user picked another mods folder.
    void folderChanged(const QString &folder);

private:
    void showFolder();
    void browseFolder();
    void newMod();
    void editInfo();
    void showInFolder();
    void showContextMenu(const QPoint &pos);
    void updateItems();
    void updateDetails();
    // The listed mod in `row`, or nullptr for the game data row.
    const Mod *modAt(int row) const;
    bool isEdited(int row) const;

    GameDataset dataset_;
    Edited edited_ = Edited::Nothing;
    QString editedModDir_;
    bool dataOpen_ = false;
    QList<Mod> modList_; // The mods in the list after the game data row, in list order.
    QComboBox *folder_;
    QListWidget *mods_;
    QLabel *details_;
    QPushButton *editButton_;
    QPushButton *newButton_;
    QToolButton *moreButton_;
    QAction *editAction_;
    QAction *saveHereAction_;
    QAction *editInfoAction_;
    QAction *showInFolderAction_;
};

} // namespace newage
