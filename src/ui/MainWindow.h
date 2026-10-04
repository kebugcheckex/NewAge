#pragma once

#include <functional>

#include <QMainWindow>

#include "core/GameInstall.h"
#include "core/Session.h"

class QDockWidget;
class QLabel;
class QStackedWidget;
class QTabWidget;

namespace newage {

class Config;
class EntityBrowser;
class ModsPanel;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Config *config, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void openGameFolder();
    void openDataFile();
    // Loads a data set and its language files, reporting problems.
    bool openDataset(const GameDataset &dataset);
    // Shows a progress dialog and runs `read` off the UI thread, so the dialog
    // can paint while a large data set (DE) loads. Installs the result here.
    bool loadShowingProgress(const QString &label,
                             const std::function<LoadResult(const Session::LoadProgress &)> &read, QString *error,
                             QStringList *warnings);
    // All return false if the file wasn't saved.
    bool saveFile();
    bool saveFileAs();
    bool saveTo(const QString &path);
    // Asks for a mod to save the open data into, then saves it there.
    bool saveAsMod();
    // Saves the open data as the mod's .dat, which then becomes the file
    // being edited. `confirmReplace` asks first if the mod has a .dat that
    // isn't the one being edited.
    bool saveIntoMod(const QString &modDir, bool confirmReplace);
    // Loads the game data (empty `modDir`) or a mod for editing, after
    // offering to save changes.
    void editMod(const QString &modDir);
    // editMod() without the offer. A mod without a .dat of its own starts
    // from `startDat` (the game's .dat if empty) and saves into the mod.
    bool loadMod(const QString &modDir, const QString &startDat = QString());
    // A new mod made in the panel; see ModsPanel::modCreated.
    void startNewMod(const QString &modDir, const QString &startDat);
    // Whether mods can be opened and saved for the open data.
    bool canUseMods() const;
    // Where Save writes while a mod is being edited, or empty.
    QString modDat() const;
    // Remembers the mod (or the game data) being edited, for the next time
    // this game folder is opened.
    void rememberEditedMod();
    void showOptions();
    // Offers to save unsaved changes. Returns false if the user cancelled or
    // the save failed.
    bool confirmDiscardChanges();
    void refresh();

    Config *config_;
    Session *session_;
    QStackedWidget *pages_;
    QLabel *placeholder_;
    // Units, Techs and Effects tabs, shown while data is open.
    QTabWidget *browsers_;
    EntityBrowser *techBrowser_ = nullptr;
    EntityBrowser *effectsBrowser_ = nullptr;
    QLabel *fileInfo_;
    ModsPanel *modsPanel_;
    QDockWidget *modsDock_;
    QAction *saveAction_ = nullptr;
    QAction *saveAsAction_ = nullptr;
    QAction *saveAsModAction_ = nullptr;
    // The game data set last opened from a game folder (not a mod's), the
    // base for its mods. Empty after opening a loose .dat file.
    GameDataset gameDataset_;
    // The mod being edited: Save writes the open data to its .dat, which
    // need not exist yet. Empty while editing anything else.
    QString modDir_;
    QString modTitle_;
};

} // namespace newage
