#include "ui/MainWindow.h"

#include <exception>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>

#include "core/Config.h"
#include "core/GameInstall.h"
#include "core/Mods.h"
#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "model/EffectListModel.h"
#include "model/TechListModel.h"
#include "model/UnitListModel.h"
#include "ui/EntityBrowser.h"
#include "ui/ModsDialog.h"
#include "ui/ModsPanel.h"
#include "ui/OptionsDialog.h"

namespace newage {

namespace {

const auto kLastVersionKey = QStringLiteral("open/lastVersion");
const auto kLastDirKey = QStringLiteral("open/lastDir");
const auto kLastGameDirKey = QStringLiteral("open/lastGameDir");
// .dat file name of the data set last opened from a game folder.
const auto kLastDatasetKey = QStringLiteral("open/lastDataset");
// Mods folder last used for each game folder, as a map.
const auto kModsFoldersKey = QStringLiteral("mods/folders");
// Mod folder last edited for each game .dat, as a map; empty for the game's
// own data.
const auto kEditedModsKey = QStringLiteral("mods/edited");
// Whether the Mods panel is shown for game folders that have mods.
const auto kModsPanelKey = QStringLiteral("mods/panelVisible");
// Language folder used for HD / DE. Hard-coded until there's a setting.
const auto kLocale = QStringLiteral("en");
const auto kDatFilter = QStringLiteral("Genie data files (*.dat);;All files (*)");

// Stays up for the whole load. Closing it would drop modality while the read
// is still running, and the load can't be cancelled.
class LoadDialog : public QProgressDialog
{
public:
    LoadDialog(const QString &label, QWidget *parent)
        : QProgressDialog(label, QString(), 0, 0, parent)
    {
        setObjectName(QStringLiteral("loadProgress"));
        setWindowTitle(tr("Loading"));
        setCancelButton(nullptr);
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        setWindowModality(Qt::WindowModal);
        setMinimumDuration(0);
        setAutoClose(false);
        setAutoReset(false);
        setMinimumWidth(360);
        setRange(0, 0);
    }

protected:
    void closeEvent(QCloseEvent *event) override { event->ignore(); }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() != Qt::Key_Escape)
            QProgressDialog::keyPressEvent(event);
    }
};

} // namespace

MainWindow::MainWindow(Config *config, QWidget *parent)
    : QMainWindow(parent),
      config_(config),
      session_(new Session(this)),
      pages_(new QStackedWidget(this)),
      placeholder_(new QLabel(tr("No data open. Use File > Open Game Folder."), this)),
      browsers_(new QTabWidget(this)),
      fileInfo_(new QLabel(this)),
      modsPanel_(new ModsPanel(this)),
      modsDock_(new QDockWidget(tr("Mods"), this))
{
    placeholder_->setAlignment(Qt::AlignCenter);
    pages_->addWidget(placeholder_);
    browsers_->addTab(new EntityBrowser(session_, config_, new UnitListModel(session_), &Config::hideEmptyUnits,
                                        tr("Filter units")),
                      tr("&Units"));
    techBrowser_ = new EntityBrowser(session_, config_, new TechListModel(session_), &Config::hideUnavailableTechs,
                                     tr("Filter techs"), this);
    effectsBrowser_ = new EntityBrowser(session_, config_, new EffectListModel(session_), nullptr,
                                        tr("Filter effects"), this);
    browsers_->addTab(techBrowser_, tr("T&echs"));
    browsers_->addTab(effectsBrowser_, tr("Effects"));
    connect(techBrowser_, &EntityBrowser::effectActivated, this, [this](int id) {
        browsers_->setCurrentWidget(effectsBrowser_);
        effectsBrowser_->selectEntity(id);
    });
    pages_->addWidget(browsers_);
    setCentralWidget(pages_);
    statusBar()->addPermanentWidget(fileInfo_);

    modsDock_->setObjectName(QStringLiteral("modsDock"));
    modsDock_->setWidget(modsPanel_);
    modsDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, modsDock_);
    modsDock_->hide();
    connect(modsPanel_, &ModsPanel::editRequested, this, &MainWindow::editMod);
    connect(modsPanel_, &ModsPanel::modCreated, this, &MainWindow::startNewMod);
    connect(modsPanel_, &ModsPanel::saveHereRequested, this, [this](const QString &modDir) {
        saveIntoMod(modDir, true);
    });
    connect(modsPanel_, &ModsPanel::modInfoChanged, this, [this](const QString &modDir, const ModInfo &info) {
        if (QDir::cleanPath(modDir) == modDir_)
        {
            modTitle_ = info.title;
            refresh();
        }
    });
    connect(modsPanel_, &ModsPanel::folderChanged, this, [this](const QString &folder) {
        QSettings settings;
        QVariantMap folders = settings.value(kModsFoldersKey).toMap();
        folders.insert(gameDataset_.gameDir, folder);
        settings.setValue(kModsFoldersKey, folders);
    });

    createActions();

    connect(session_, &Session::opened, this, &MainWindow::refresh);
    connect(session_, &Session::closed, this, &MainWindow::refresh);
    connect(session_, &Session::modifiedChanged, this, &MainWindow::refresh);

    resize(1024, 720);
    refresh();
}

void MainWindow::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *openAction = fileMenu->addAction(tr("&Open Game Folder..."), this, &MainWindow::openGameFolder);
    openAction->setShortcut(QKeySequence::Open);
    fileMenu->addAction(tr("Open &Data File..."), this, &MainWindow::openDataFile);

    saveAction_ = fileMenu->addAction(tr("&Save"), this, &MainWindow::saveFile);
    saveAction_->setShortcut(QKeySequence::Save);
    saveAsAction_ = fileMenu->addAction(tr("Save &As..."), this, &MainWindow::saveFileAs);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    saveAsModAction_ = fileMenu->addAction(tr("Save As &Mod..."), this, &MainWindow::saveAsMod);
    saveAsModAction_->setStatusTip(tr("Save the data into a mod folder, leaving the game's own file as it is."));

    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *modsAction = modsDock_->toggleViewAction();
    modsAction->setText(tr("&Mods"));
    modsAction->setStatusTip(tr("Show the mods of the open game folder."));
    // Also follows the dock's close button. Hiding the panel for data without
    // mods isn't the user's choice, so it isn't remembered.
    connect(modsAction, &QAction::toggled, this, [this](bool checked) {
        if (supportsMods(gameDataset_))
            QSettings().setValue(kModsPanelKey, checked);
    });
    viewMenu->addAction(modsAction);

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    QAction *optionsAction = toolsMenu->addAction(tr("&Options..."), this, &MainWindow::showOptions);
    optionsAction->setShortcut(QKeySequence::Preferences);
    optionsAction->setMenuRole(QAction::PreferencesRole);
}

void MainWindow::openGameFolder()
{
    if (!confirmDiscardChanges())
        return;

    QSettings settings;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open game folder"),
                                                          settings.value(kLastGameDirKey).toString());
    if (dir.isEmpty())
        return;

    const QList<GameDataset> datasets = detectInstall(dir, kLocale);
    if (datasets.isEmpty())
    {
        QMessageBox::critical(this, tr("No game data found"),
                              tr("%1 doesn't look like a game folder. None of these files are in it:\n\n%2\n\n"
                                 "To open a .dat file from somewhere else, use File > Open Data File.")
                                  .arg(QDir::toNativeSeparators(dir), knownDatPaths().join(QLatin1Char('\n'))));
        return;
    }
    settings.setValue(kLastGameDirKey, dir);

    int chosen = 0;
    if (datasets.size() > 1)
    {
        QStringList titles;
        const QString lastDat = settings.value(kLastDatasetKey).toString();
        for (const GameDataset &dataset : datasets)
        {
            if (QFileInfo(dataset.datPath).fileName() == lastDat)
                chosen = titles.size();
            titles << dataset.title;
        }
        bool ok = false;
        const QString title = QInputDialog::getItem(this, tr("Open game folder"),
                                                    tr("This folder holds several data files. Open:"), titles,
                                                    chosen, false, &ok);
        if (!ok)
            return;
        chosen = titles.indexOf(title);
    }
    const GameDataset &dataset = datasets.at(chosen);
    settings.setValue(kLastDatasetKey, QFileInfo(dataset.datPath).fileName());
    gameDataset_ = dataset;
    modDir_.clear();
    modTitle_.clear();
    if (!supportsMods(dataset))
    {
        openDataset(dataset);
        return;
    }

    modsPanel_->setGame(dataset, settings.value(kModsFoldersKey).toMap().value(dataset.gameDir).toString());
    modsDock_->setVisible(settings.value(kModsPanelKey, true).toBool());
    // Back to the mod edited last time, if it's still there.
    const QString lastMod = settings.value(kEditedModsKey).toMap().value(dataset.datPath).toString();
    if (!lastMod.isEmpty() && QFileInfo(lastMod).isDir() && loadMod(lastMod))
        return;
    loadMod(QString());
}

bool MainWindow::loadShowingProgress(const QString &label,
                                    const std::function<LoadResult(const Session::LoadProgress &)> &read,
                                    QString *error, QStringList *warnings)
{
    LoadDialog dialog(label, this);
    dialog.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    LoadResult result;
    QEventLoop loop;
    auto *thread = QThread::create([this, &dialog, &result, &read] {
        try
        {
            result = read([this, &dialog](int index, int count) {
                if (index <= 0 || count <= 1)
                    return;
                QMetaObject::invokeMethod(
                    &dialog,
                    [this, &dialog, index, count] {
                        dialog.setLabelText(tr("Loading language file %1 of %2...").arg(index).arg(count - 1));
                    },
                    Qt::QueuedConnection);
            });
        }
        catch (const std::exception &e)
        {
            result.ok = false;
            result.error = QString::fromLocal8Bit(e.what());
        }
        catch (...)
        {
            result.ok = false;
            result.error = QStringLiteral("Failed to load.");
        }
    });
    connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec();
    thread->wait();
    delete thread;

    if (!result.ok)
    {
        if (error)
            *error = result.error;
        session_->close();
        return false;
    }
    if (warnings)
        *warnings = result.warnings;
    dialog.setLabelText(tr("Opening..."));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    session_->adopt(std::move(result));
    return true;
}

bool MainWindow::openDataset(const GameDataset &dataset)
{
    statusBar()->showMessage(tr("Loading %1...").arg(QDir::toNativeSeparators(dataset.datPath)));
    QString error;
    QStringList warnings;
    const bool loaded = loadShowingProgress(tr("Loading %1...").arg(dataset.title),
                                            [dataset](const Session::LoadProgress &progress) {
                                                return Session::read(dataset, progress);
                                            },
                                            &error, &warnings);

    if (!loaded)
    {
        statusBar()->clearMessage();
        QMessageBox::critical(this, tr("Open failed"), error);
        return false;
    }
    statusBar()->showMessage(tr("Loaded %1").arg(dataset.title), 5000);
    if (dataset.languageFiles.isEmpty())
        warnings << tr("No language files were found, so units and techs show their internal names.");
    if (!warnings.isEmpty())
        QMessageBox::warning(this, tr("Language files"), warnings.join(QLatin1Char('\n')));
    return true;
}

void MainWindow::openDataFile()
{
    if (!confirmDiscardChanges())
        return;

    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open data file"), settings.value(kLastDirKey).toString(), kDatFilter);
    if (path.isEmpty())
        return;

    // A loose .dat file: the user picks the version, and there are no language
    // strings, so units and techs show their internal names.
    QStringList names;
    int current = 0;
    const QString lastKey = settings.value(kLastVersionKey).toString();
    for (const VersionProfile &profile : versionProfiles())
    {
        if (profile.key == lastKey)
            current = names.size();
        names << profile.displayName;
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Game version"), tr("Game version of this file:"), names, current, false, &ok);
    if (!ok)
        return;
    const VersionProfile &profile = versionProfiles().at(names.indexOf(chosen));

    settings.setValue(kLastDirKey, QFileInfo(path).absolutePath());
    settings.setValue(kLastVersionKey, profile.key);

    statusBar()->showMessage(tr("Loading %1...").arg(path));
    QString error;
    gameDataset_ = {};
    modDir_.clear();
    modTitle_.clear();
    const bool loaded = loadShowingProgress(tr("Loading %1...").arg(QFileInfo(path).fileName()),
                                            [path, profile](const Session::LoadProgress &progress) {
                                                return Session::read(path, profile, progress);
                                            },
                                            &error, nullptr);

    if (!loaded)
    {
        statusBar()->clearMessage();
        QMessageBox::critical(this, tr("Open failed"), error);
        return;
    }
    statusBar()->showMessage(tr("Loaded %1").arg(path), 5000);
}

bool MainWindow::saveFile()
{
    if (!session_->isOpen())
        return false;
    if (!modDir_.isEmpty())
        return saveIntoMod(modDir_, false);

    if (canUseMods() && isGameDataFile(gameDataset_, session_->datPath()))
    {
        QMessageBox box(QMessageBox::Question, tr("Save"),
                        tr("%1 is the game's own data file. Save your changes to a mod instead?")
                            .arg(QFileInfo(session_->datPath()).fileName()),
                        QMessageBox::NoButton, this);
        box.setInformativeText(tr("A mod leaves the original as it is. Overwriting it changes the unmodded game, "
                                  "and only verifying or reinstalling the game files brings it back."));
        QPushButton *toMod = box.addButton(tr("Save As &Mod..."), QMessageBox::AcceptRole);
        QPushButton *overwrite = box.addButton(tr("&Overwrite"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(toMod);
        box.exec();
        if (box.clickedButton() == toMod)
            return saveAsMod();
        if (box.clickedButton() != overwrite)
            return false;
    }
    return saveTo(session_->datPath());
}

bool MainWindow::saveFileAs()
{
    if (!session_->isOpen())
        return false;

    const QString path = QFileDialog::getSaveFileName(this, tr("Save data file"), session_->datPath(), kDatFilter);
    if (path.isEmpty())
        return false;
    if (!saveTo(path))
        return false;
    // A copy somewhere else is no longer the mod's data.
    if (QDir::cleanPath(path) != modDat())
    {
        modDir_.clear();
        modTitle_.clear();
        refresh();
    }
    return true;
}

bool MainWindow::saveTo(const QString &path)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool saved = session_->saveAs(path, &error);
    QApplication::restoreOverrideCursor();

    if (!saved)
    {
        QMessageBox::critical(this, tr("Save failed"), error);
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)), 5000);
    refresh();
    return true;
}

bool MainWindow::saveAsMod()
{
    if (!canUseMods())
        return false;
    if (modsPanel_->modsFolder().isEmpty())
    {
        modsDock_->show();
        QMessageBox::information(this, tr("Save As Mod"),
                                 tr("Choose the folder the game reads local mods from in the Mods panel first."));
        return false;
    }

    SaveAsModDialog dialog(gameDataset_, modsPanel_->modsFolder(), modDir_, this);
    const bool accepted = dialog.exec() == QDialog::Accepted && !dialog.modDir().isEmpty();
    // The dialog may have made new mods.
    modsPanel_->reload();
    if (!accepted)
        return false;
    return saveIntoMod(dialog.modDir(), true);
}

bool MainWindow::saveIntoMod(const QString &modDir, bool confirmReplace)
{
    if (!canUseMods())
        return false;

    const QString dir = QDir::cleanPath(modDir);
    const QString title = readModInfo(dir).title;
    const QString path = modDatPath(gameDataset_, dir);
    if (confirmReplace && dir != modDir_ && QFileInfo::exists(path))
    {
        const auto answer = QMessageBox::question(
            this, tr("Save to mod"),
            tr("%1 already has a %2. Replace it with the open data?").arg(title, QFileInfo(path).fileName()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return false;
    }
    const QString datDir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(datDir))
    {
        QMessageBox::critical(this, tr("Save failed"), tr("Couldn't create %1.").arg(QDir::toNativeSeparators(datDir)));
        return false;
    }
    if (!saveTo(path))
        return false;
    modDir_ = dir;
    modTitle_ = title;
    rememberEditedMod();
    // The mod may have just got its .dat.
    modsPanel_->reload();
    refresh();
    return true;
}

void MainWindow::editMod(const QString &modDir)
{
    if (confirmDiscardChanges())
        loadMod(modDir);
}

bool MainWindow::loadMod(const QString &modDir, const QString &startDat)
{
    if (!supportsMods(gameDataset_))
        return false;

    modDir_.clear();
    modTitle_.clear();
    if (modDir.isEmpty())
    {
        if (!openDataset(gameDataset_))
            return false;
        rememberEditedMod();
        return true;
    }

    GameDataset dataset = modDataset(gameDataset_, modDir, kLocale);
    if (!startDat.isEmpty())
        dataset.datPath = startDat;
    else if (!QFileInfo::exists(dataset.datPath))
        dataset.datPath = gameDataset_.datPath;
    // Set first, so the window title is right as soon as the data shows.
    modDir_ = QDir::cleanPath(modDir);
    modTitle_ = readModInfo(modDir_).title;
    if (!openDataset(dataset))
    {
        modDir_.clear();
        modTitle_.clear();
        refresh();
        return false;
    }
    rememberEditedMod();
    return true;
}

void MainWindow::startNewMod(const QString &modDir, const QString &startDat)
{
    if (startDat.isEmpty())
    {
        // Keep the open data; the next save writes it into the new mod.
        if (!session_->isOpen())
            return;
        modDir_ = QDir::cleanPath(modDir);
        modTitle_ = readModInfo(modDir_).title;
        rememberEditedMod();
        refresh();
        return;
    }
    if (confirmDiscardChanges())
        loadMod(modDir, startDat);
}

bool MainWindow::canUseMods() const
{
    return session_->isOpen() && supportsMods(gameDataset_);
}

QString MainWindow::modDat() const
{
    return modDir_.isEmpty() ? QString() : modDatPath(gameDataset_, modDir_);
}

void MainWindow::rememberEditedMod()
{
    if (!supportsMods(gameDataset_))
        return;
    QSettings settings;
    QVariantMap edited = settings.value(kEditedModsKey).toMap();
    edited.insert(gameDataset_.datPath, modDir_);
    settings.setValue(kEditedModsKey, edited);
}

void MainWindow::showOptions()
{
    OptionsDialog dialog(config_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!config_->save(&error))
        QMessageBox::warning(this, tr("Options not saved"),
                             tr("%1\n\nThe new options apply until NewAge is closed.").arg(error));
}

bool MainWindow::confirmDiscardChanges()
{
    if (!session_->isModified())
        return true;

    const auto answer = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("Save changes to %1?").arg(modDir_.isEmpty() ? QFileInfo(session_->datPath()).fileName() : modTitle_),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return saveFile();
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (confirmDiscardChanges())
        event->accept();
    else
        event->ignore();
}

void MainWindow::refresh()
{
    // A mod that doesn't have its own .dat yet gets one on the first save.
    const bool modDatMissing = !modDir_.isEmpty() && !QFileInfo::exists(modDat());
    saveAction_->setEnabled(session_->isOpen() && (session_->isModified() || modDatMissing));
    saveAsAction_->setEnabled(session_->isOpen());
    saveAsModAction_->setEnabled(canUseMods());

    const bool hasMods = supportsMods(gameDataset_);
    modsDock_->toggleViewAction()->setEnabled(hasMods);
    if (!hasMods)
        modsDock_->hide();
    modsPanel_->setDataOpen(session_->isOpen());
    ModsPanel::Edited edited = ModsPanel::Edited::Nothing;
    if (session_->isOpen() && !modDir_.isEmpty())
        edited = ModsPanel::Edited::Mod;
    else if (session_->isOpen() && session_->datPath() == gameDataset_.datPath)
        edited = ModsPanel::Edited::GameData;
    modsPanel_->setEdited(edited, modDir_);

    if (!session_->isOpen())
    {
        setWindowTitle(QStringLiteral("NewAge"));
        pages_->setCurrentWidget(placeholder_);
        fileInfo_->clear();
        return;
    }

    setWindowTitle(QStringLiteral("%1%2%3 - NewAge")
                       .arg(modDir_.isEmpty() ? QString() : modTitle_ + QStringLiteral(" - "),
                            QFileInfo(session_->datPath()).fileName(),
                            session_->isModified() ? QStringLiteral("*") : QString()));
    pages_->setCurrentWidget(browsers_);

    const genie::DatFile &dat = *session_->dat();
    const int languageFiles = static_cast<int>(session_->names().files().size());
    fileInfo_->setText(tr("%1 | %2 civs | %3 units | %4 techs | %5 effects | %6")
                           .arg(QString::fromLatin1(dat.FileVersion.c_str()))
                           .arg(dat.Civs.size())
                           .arg(dat.Civs.front().Units.size())
                           .arg(dat.Techs.size())
                           .arg(dat.Effects.size())
                           .arg(languageFiles ? tr("%n language file(s)", nullptr, languageFiles)
                                              : tr("no language files")));
}

} // namespace newage
