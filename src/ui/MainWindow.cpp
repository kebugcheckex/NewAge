#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>

#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "ui/UnitBrowser.h"

namespace newage {

namespace {

const auto kLastVersionKey = QStringLiteral("open/lastVersion");
const auto kLastDirKey = QStringLiteral("open/lastDir");
const auto kDatFilter = QStringLiteral("Genie data files (*.dat);;All files (*)");

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      session_(new Session(this)),
      pages_(new QStackedWidget(this)),
      placeholder_(new QLabel(tr("No data open. Use File > Open."), this)),
      unitBrowser_(new UnitBrowser(session_, this)),
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

    QAction *openAction = fileMenu->addAction(tr("&Open..."), this, &MainWindow::openFile);
    openAction->setShortcut(QKeySequence::Open);

    saveAsAction_ = fileMenu->addAction(tr("Save &As..."), this, &MainWindow::saveFileAs);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);

    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);
}

void MainWindow::openFile()
{
    if (!confirmDiscardChanges())
        return;

    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open data file"), settings.value(kLastDirKey).toString(), kDatFilter);
    if (path.isEmpty())
        return;

    // Temporary version picker; replaced by a proper open dialog / profiles later.
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
    fileInfo_->setText(tr("%1 | %2 civs | %3 units")
                           .arg(QString::fromLatin1(dat.FileVersion.c_str()))
                           .arg(dat.Civs.size())
                           .arg(dat.Civs.front().Units.size()));
}

} // namespace newage
