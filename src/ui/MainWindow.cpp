#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>

#include "core/Config.h"
#include "core/GameInstall.h"
#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "ui/OptionsDialog.h"
#include "ui/UnitBrowser.h"

namespace newage {

namespace {

const auto kLastVersionKey = QStringLiteral("open/lastVersion");
const auto kLastDirKey = QStringLiteral("open/lastDir");
const auto kLastGameDirKey = QStringLiteral("open/lastGameDir");
// .dat file name of the data set last opened from a game folder.
const auto kLastDatasetKey = QStringLiteral("open/lastDataset");
// Language folder used for HD / DE. Hard-coded until there's a setting.
const auto kLocale = QStringLiteral("en");
const auto kDatFilter = QStringLiteral("Genie data files (*.dat);;All files (*)");

} // namespace

MainWindow::MainWindow(Config *config, QWidget *parent)
    : QMainWindow(parent),
      config_(config),
      session_(new Session(this)),
      pages_(new QStackedWidget(this)),
      placeholder_(new QLabel(tr("No data open. Use File > Open Game Folder."), this)),
      unitBrowser_(new UnitBrowser(session_, config_, this)),
      fileInfo_(new QLabel(this))
{
    placeholder_->setAlignment(Qt::AlignCenter);
    pages_->addWidget(placeholder_);
    pages_->addWidget(unitBrowser_);
    setCentralWidget(pages_);
    statusBar()->addPermanentWidget(fileInfo_);

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

    saveAsAction_ = fileMenu->addAction(tr("Save &As..."), this, &MainWindow::saveFileAs);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);

    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

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

    statusBar()->showMessage(tr("Loading %1...").arg(QDir::toNativeSeparators(dataset.datPath)));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    QStringList warnings;
    const bool loaded = session_->open(dataset, &error, &warnings);
    QApplication::restoreOverrideCursor();

    if (!loaded)
    {
        statusBar()->clearMessage();
        QMessageBox::critical(this, tr("Open failed"), error);
        return;
    }
    statusBar()->showMessage(tr("Loaded %1").arg(dataset.title), 5000);
    if (dataset.languageFiles.isEmpty())
        warnings << tr("No language files were found, so units show their internal names.");
    if (!warnings.isEmpty())
        QMessageBox::warning(this, tr("Language files"), warnings.join(QLatin1Char('\n')));
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
    // strings, so units show their internal names.
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
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool loaded = session_->open(path, profile, &error);
    QApplication::restoreOverrideCursor();

    if (!loaded)
    {
        statusBar()->clearMessage();
        QMessageBox::critical(this, tr("Open failed"), error);
        return;
    }
    statusBar()->showMessage(tr("Loaded %1").arg(path), 5000);
}

void MainWindow::saveFileAs()
{
    if (!session_->isOpen())
        return;

    const QString path = QFileDialog::getSaveFileName(this, tr("Save data file"), session_->datPath(), kDatFilter);
    if (path.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool saved = session_->saveAs(path, &error);
    QApplication::restoreOverrideCursor();

    if (!saved)
    {
        QMessageBox::critical(this, tr("Save failed"), error);
        return;
    }
    statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    refresh();
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
        this, tr("Unsaved changes"), tr("Discard unsaved changes?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
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
    saveAsAction_->setEnabled(session_->isOpen());

    if (!session_->isOpen())
    {
        setWindowTitle(QStringLiteral("NewAge"));
        pages_->setCurrentWidget(placeholder_);
        fileInfo_->clear();
        return;
    }

    setWindowTitle(QStringLiteral("%1%2 - NewAge")
                       .arg(QFileInfo(session_->datPath()).fileName(),
                            session_->isModified() ? QStringLiteral("*") : QString()));
    pages_->setCurrentWidget(unitBrowser_);

    const genie::DatFile &dat = *session_->dat();
    const int languageFiles = static_cast<int>(session_->names().files().size());
    fileInfo_->setText(tr("%1 | %2 civs | %3 units | %4")
                           .arg(QString::fromLatin1(dat.FileVersion.c_str()))
                           .arg(dat.Civs.size())
                           .arg(dat.Civs.front().Units.size())
                           .arg(languageFiles ? tr("%n language file(s)", nullptr, languageFiles)
                                              : tr("no language files")));
}

} // namespace newage
