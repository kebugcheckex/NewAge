#include "ui/ModsPanel.h"

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "ui/ModsDialog.h"

namespace newage {

namespace {

constexpr int kDirRole = Qt::UserRole;         // Mod folder; empty for the game data row.
constexpr int kHasDatRole = Qt::UserRole + 1;  // Whether the row has a .dat to load.
const auto kEditedMark = QStringLiteral("✎ ");

} // namespace

ModsPanel::ModsPanel(QWidget *parent)
    : QWidget(parent),
      folder_(new QComboBox(this)),
      mods_(new QListWidget(this)),
      details_(new QLabel(this)),
      editButton_(new QPushButton(tr("&Edit"), this)),
      newButton_(new QPushButton(tr("&New Mod..."), this)),
      moreButton_(new QToolButton(this)),
      editAction_(new QAction(tr("&Edit"), this)),
      saveHereAction_(new QAction(tr("&Save Current Data Here"), this)),
      editInfoAction_(new QAction(tr("Edit &Info..."), this)),
      showInFolderAction_(new QAction(tr("Show in &Folder"), this))
{
    folder_->setObjectName(QStringLiteral("modsFolder"));
    folder_->setEditable(true);
    folder_->setInsertPolicy(QComboBox::NoInsert);
    folder_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    folder_->setToolTip(tr("The folder the game reads local mods from. Each mod is a folder in it."));
    auto *browse = new QToolButton(this);
    browse->setText(QStringLiteral("..."));
    browse->setToolTip(tr("Choose another mods folder"));

    mods_->setObjectName(QStringLiteral("mods"));
    mods_->setContextMenuPolicy(Qt::CustomContextMenu);
    details_->setObjectName(QStringLiteral("modDetails"));
    details_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details_->setFrameShape(QFrame::StyledPanel);
    details_->setMargin(6);
    details_->setMinimumHeight(details_->fontMetrics().lineSpacing() * 6);

    editAction_->setToolTip(tr("Load this data for editing. Save then writes to it."));
    saveHereAction_->setToolTip(tr("Save the open data as this mod's data file."));
    editInfoAction_->setToolTip(tr("Change the mod's name, author and description."));
    editButton_->setObjectName(QStringLiteral("editMod"));
    editButton_->setToolTip(editAction_->toolTip());
    newButton_->setObjectName(QStringLiteral("newMod"));
    newButton_->setToolTip(tr("Create a mod with a name, author and description, and start editing it."));
    moreButton_->setObjectName(QStringLiteral("modActions"));
    moreButton_->setText(QStringLiteral("⋯"));
    moreButton_->setToolTip(tr("More mod actions"));
    moreButton_->setPopupMode(QToolButton::InstantPopup);
    auto *more = new QMenu(moreButton_);
    more->addAction(saveHereAction_);
    more->addAction(editInfoAction_);
    more->addAction(showInFolderAction_);
    moreButton_->setMenu(more);

    connect(folder_, &QComboBox::activated, this, [this] {
        showFolder();
        emit folderChanged(modsFolder());
    });
    connect(folder_->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        showFolder();
        emit folderChanged(modsFolder());
    });
    connect(browse, &QToolButton::clicked, this, &ModsPanel::browseFolder);
    connect(mods_, &QListWidget::currentItemChanged, this, &ModsPanel::updateDetails);
    connect(mods_, &QListWidget::itemActivated, editAction_, &QAction::trigger);
    connect(mods_, &QListWidget::customContextMenuRequested, this, &ModsPanel::showContextMenu);
    connect(editButton_, &QPushButton::clicked, editAction_, &QAction::trigger);
    connect(newButton_, &QPushButton::clicked, this, &ModsPanel::newMod);
    connect(editAction_, &QAction::triggered, this, [this] {
        if (editAction_->isEnabled() && mods_->currentRow() >= 0)
            emit editRequested(selectedModDir());
    });
    connect(saveHereAction_, &QAction::triggered, this, [this] {
        if (!selectedModDir().isEmpty())
            emit saveHereRequested(selectedModDir());
    });
    connect(editInfoAction_, &QAction::triggered, this, &ModsPanel::editInfo);
    connect(showInFolderAction_, &QAction::triggered, this, &ModsPanel::showInFolder);

    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(editButton_);
    buttons->addWidget(newButton_);
    buttons->addStretch();
    buttons->addWidget(moreButton_);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(folderRow);
    layout->addWidget(mods_, 1);
    layout->addWidget(details_);
    layout->addLayout(buttons);

    updateDetails();
}

void ModsPanel::setGame(const GameDataset &dataset, const QString &folder)
{
    dataset_ = dataset;
    edited_ = Edited::Nothing;
    editedModDir_.clear();

    folder_->clear();
    QStringList folders = modsFolders(dataset_);
    if (!folder.isEmpty())
    {
        folders.removeIf([&](const QString &f) { return QDir::cleanPath(f) == QDir::cleanPath(folder); });
        folders.prepend(folder);
    }
    for (const QString &f : folders)
        folder_->addItem(QDir::toNativeSeparators(f));
    mods_->clear();
    showFolder();
}

void ModsPanel::reload()
{
    showFolder();
}

void ModsPanel::setEdited(Edited edited, const QString &modDir)
{
    const QString dir = edited == Edited::Mod ? QDir::cleanPath(modDir) : QString();
    if (edited == edited_ && dir == editedModDir_)
        return;
    edited_ = edited;
    editedModDir_ = dir;
    if (edited_ != Edited::Nothing)
        select(editedModDir_);
    updateItems();
    updateDetails();
}

void ModsPanel::setDataOpen(bool open)
{
    dataOpen_ = open;
    updateDetails();
}

QString ModsPanel::modsFolder() const
{
    const QString text = folder_->currentText().trimmed();
    return text.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(text));
}

bool ModsPanel::gameDataSelected() const
{
    return mods_->currentRow() == 0;
}

QString ModsPanel::selectedModDir() const
{
    const Mod *mod = modAt(mods_->currentRow());
    return mod ? mod->dir : QString();
}

void ModsPanel::select(const QString &modDir)
{
    const QString dir = QDir::cleanPath(modDir);
    for (int row = 0; row < mods_->count(); ++row)
    {
        const Mod *mod = modAt(row);
        if (mod ? mod->dir == dir : modDir.isEmpty())
        {
            mods_->setCurrentRow(row);
            return;
        }
    }
}

void ModsPanel::showFolder()
{
    const bool hadSelection = mods_->currentRow() >= 0;
    const bool gameData = gameDataSelected();
    const QString previous = selectedModDir();

    mods_->clear();
    modList_ = findMods(modsFolder());
    auto *game = new QListWidgetItem(tr("Game data (unmodded)"), mods_);
    game->setData(kHasDatRole, true);
    for (const Mod &mod : modList_)
    {
        auto *item = new QListWidgetItem(mod.info.title, mods_);
        item->setData(kDirRole, mod.dir);
        item->setData(kHasDatRole, QFileInfo::exists(modDatPath(dataset_, mod.dir)));
    }
    updateItems();

    if (hadSelection)
        select(gameData ? QString() : previous);
    if (mods_->currentRow() < 0 && edited_ != Edited::Nothing)
        select(editedModDir_);
    if (mods_->currentRow() < 0)
        mods_->setCurrentRow(0);
    updateDetails();
}

void ModsPanel::browseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Mods folder"), modsFolder());
    if (dir.isEmpty())
        return;
    folder_->setCurrentText(QDir::toNativeSeparators(dir));
    showFolder();
    emit folderChanged(modsFolder());
}

void ModsPanel::newMod()
{
    // Where the new mod's data comes from, in the order of the choices.
    QStringList choices{tr("Game data (unmodded)")};
    QStringList startDats{dataset_.datPath};
    if (dataOpen_)
    {
        choices << tr("The open data");
        startDats << QString();
    }
    for (const Mod &mod : modList_)
    {
        const QString dat = modDatPath(dataset_, mod.dir);
        if (QFileInfo::exists(dat))
        {
            choices << tr("Mod: %1").arg(mod.info.title);
            startDats << dat;
        }
    }

    ModInfoDialog dialog(modsFolder(), choices, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    const QString dir = createMod(modsFolder(), dialog.info(), &error);
    if (dir.isEmpty())
    {
        QMessageBox::critical(this, tr("New mod"), error);
        return;
    }
    showFolder();
    select(dir);
    emit modCreated(dir, startDats.value(dialog.startIndex()));
}

void ModsPanel::editInfo()
{
    const QString dir = selectedModDir();
    if (dir.isEmpty())
        return;
    ModInfoDialog dialog(readModInfo(dir), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!writeModInfo(dir, dialog.info(), &error))
    {
        QMessageBox::critical(this, tr("Mod info"), error);
        return;
    }
    showFolder();
    emit modInfoChanged(dir, dialog.info());
}

void ModsPanel::showInFolder()
{
    QString dir;
    if (mods_->currentRow() < 0)
        dir = modsFolder();
    else if (gameDataSelected())
        dir = QFileInfo(dataset_.datPath).absolutePath();
    else
        dir = selectedModDir();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void ModsPanel::showContextMenu(const QPoint &pos)
{
    if (!mods_->itemAt(pos))
        return;
    QMenu menu(this);
    menu.addAction(editAction_);
    menu.addSeparator();
    menu.addAction(saveHereAction_);
    menu.addAction(editInfoAction_);
    menu.addAction(showInFolderAction_);
    menu.exec(mods_->viewport()->mapToGlobal(pos));
}

void ModsPanel::updateItems()
{
    const QColor dimmed = palette().color(QPalette::Disabled, QPalette::Text);
    const QString datName = QFileInfo(dataset_.datPath).fileName();
    for (int row = 0; row < mods_->count(); ++row)
    {
        QListWidgetItem *item = mods_->item(row);
        const Mod *mod = modAt(row);
        const QString name = mod ? mod->info.title : tr("Game data (unmodded)");
        const bool edited = isEdited(row);
        item->setText(edited ? kEditedMark + name : name);
        QFont font = item->font();
        font.setBold(edited);
        item->setFont(font);
        const bool hasDat = item->data(kHasDatRole).toBool();
        item->setForeground(hasDat ? QBrush() : QBrush(dimmed));
        item->setToolTip(hasDat ? QString() : tr("No %1 of its own").arg(datName));
    }
}

void ModsPanel::updateDetails()
{
    const int row = mods_->currentRow();
    const Mod *mod = modAt(row);
    const bool edited = row >= 0 && isEdited(row);
    editAction_->setEnabled(row >= 0 && !edited);
    editButton_->setEnabled(editAction_->isEnabled());
    saveHereAction_->setEnabled(dataOpen_ && mod && !edited);
    editInfoAction_->setEnabled(mod != nullptr);
    showInFolderAction_->setEnabled(row >= 0 || QFileInfo(modsFolder()).isDir());
    newButton_->setEnabled(!modsFolder().isEmpty() && !dataset_.datPath.isEmpty());

    QString html;
    if (row < 0)
        html = tr("Choose the folder the game reads local mods from.");
    else if (!mod)
        html = tr("<p><b>Game data</b></p><p>The game's own data. Saving it asks whether to save to a mod "
                  "instead.</p><p><small>%1</small></p>")
                   .arg(QDir::toNativeSeparators(dataset_.datPath).toHtmlEscaped());
    else
    {
        html = modDetailsHtml(dataset_, *mod);
        if (!mods_->item(row)->data(kHasDatRole).toBool())
            html += tr("<p>Edit starts from the game data; saving adds the mod's own data file.</p>");
    }
    if (edited)
        html += tr("<p><i>Being edited.</i></p>");
    else if (row == 0 && modList_.isEmpty())
        html += QFileInfo(modsFolder()).isDir() ? tr("<p>No mods in this folder yet. Use New Mod to make one.</p>")
                                                : tr("<p>The mods folder doesn't exist yet. New Mod creates it.</p>");
    details_->setText(html);
}

const Mod *ModsPanel::modAt(int row) const
{
    return row >= 1 && row <= modList_.size() ? &modList_.at(row - 1) : nullptr;
}

bool ModsPanel::isEdited(int row) const
{
    if (row < 0 || row >= mods_->count())
        return false;
    const Mod *mod = modAt(row);
    return mod ? edited_ == Edited::Mod && mod->dir == editedModDir_ : edited_ == Edited::GameData;
}

} // namespace newage
