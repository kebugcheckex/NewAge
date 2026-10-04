#include "ui/ModsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace newage {

namespace {

constexpr int kDirRole = Qt::UserRole;

} // namespace

QString modDetailsHtml(const GameDataset &dataset, const Mod &mod)
{
    const QString datName = QFileInfo(dataset.datPath).fileName();
    QString html = QStringLiteral("<p><b>%1</b>").arg(mod.info.title.toHtmlEscaped());
    if (!mod.info.author.isEmpty())
        html += QStringLiteral("<br>%1").arg(QDialog::tr("by %1").arg(mod.info.author.toHtmlEscaped()));
    html += QStringLiteral("</p>");
    if (!mod.info.description.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(mod.info.description.toHtmlEscaped().replace(
            QLatin1Char('\n'), QStringLiteral("<br>")));
    html += QStringLiteral("<p>%1</p>").arg(
        QFileInfo::exists(modDatPath(dataset, mod.dir))
            ? QDialog::tr("Has its own %1.").arg(datName)
            : QDialog::tr("No %1 yet, so the game uses its own data.").arg(datName));
    html += QStringLiteral("<p><small>%1</small></p>").arg(QDir::toNativeSeparators(mod.dir).toHtmlEscaped());
    return html;
}

SaveAsModDialog::SaveAsModDialog(const GameDataset &dataset, const QString &modsFolder,
                                 const QString &currentModDir, QWidget *parent)
    : QDialog(parent),
      dataset_(dataset),
      modsFolder_(modsFolder),
      mods_(new QListWidget(this)),
      details_(new QLabel(this)),
      saveButton_(new QPushButton(tr("&Save"), this))
{
    setWindowTitle(tr("Save As Mod"));

    auto *intro = new QLabel(tr("Save the open data as the data file of a mod in %1. The game's own file is left "
                                "as it is.")
                                 .arg(QDir::toNativeSeparators(modsFolder_)),
                             this);
    intro->setWordWrap(true);
    mods_->setObjectName(QStringLiteral("mods"));
    mods_->setMinimumWidth(200);
    details_->setObjectName(QStringLiteral("modDetails"));
    details_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details_->setFrameShape(QFrame::StyledPanel);
    details_->setMargin(8);
    details_->setMinimumWidth(260);

    auto *newButton = new QPushButton(tr("&New Mod..."), this);
    newButton->setObjectName(QStringLiteral("newMod"));
    newButton->setAutoDefault(false);
    saveButton_->setObjectName(QStringLiteral("saveToMod"));
    saveButton_->setDefault(true);
    auto *cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setAutoDefault(false);

    connect(mods_, &QListWidget::currentItemChanged, this, &SaveAsModDialog::updateDetails);
    connect(mods_, &QListWidget::itemActivated, this, &QDialog::accept);
    connect(newButton, &QPushButton::clicked, this, &SaveAsModDialog::newMod);
    connect(saveButton_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    auto *content = new QHBoxLayout;
    content->addWidget(mods_, 2);
    content->addWidget(details_, 3);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addStretch();
    buttons->addWidget(saveButton_);
    buttons->addWidget(cancelButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(content, 1);
    layout->addLayout(buttons);

    resize(600, 360);
    showMods(currentModDir);
}

QString SaveAsModDialog::modDir() const
{
    const QListWidgetItem *item = mods_->currentItem();
    return item ? item->data(kDirRole).toString() : QString();
}

void SaveAsModDialog::showMods(const QString &select)
{
    mods_->clear();
    modList_ = findMods(modsFolder_);
    for (const Mod &mod : modList_)
    {
        auto *item = new QListWidgetItem(mod.info.title, mods_);
        item->setData(kDirRole, mod.dir);
        if (mod.dir == QDir::cleanPath(select))
            mods_->setCurrentItem(item);
    }
    if (!mods_->currentItem() && mods_->count() > 0)
        mods_->setCurrentRow(0);
    updateDetails();
}

void SaveAsModDialog::newMod()
{
    ModInfoDialog dialog(modsFolder_, {}, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    const QString dir = createMod(modsFolder_, dialog.info(), &error);
    if (dir.isEmpty())
    {
        QMessageBox::critical(this, tr("New mod"), error);
        return;
    }
    showMods(dir);
    mods_->setFocus();
}

void SaveAsModDialog::updateDetails()
{
    const int row = mods_->currentRow();
    saveButton_->setEnabled(row >= 0);
    if (row < 0)
    {
        details_->setText(tr("No mods in this folder yet. Use New Mod to make one."));
        return;
    }
    details_->setText(modDetailsHtml(dataset_, modList_.at(row)));
}

ModInfoDialog::ModInfoDialog(const QString &modsFolder, const QStringList &startChoices, QWidget *parent)
    : QDialog(parent),
      modsFolder_(modsFolder),
      title_(new QLineEdit(this)),
      author_(new QLineEdit(this)),
      description_(new QPlainTextEdit(this)),
      problem_(new QLabel(this)),
      buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(tr("New Mod"));
    if (!startChoices.isEmpty())
    {
        start_ = new QComboBox(this);
        start_->setObjectName(QStringLiteral("modStart"));
        start_->addItems(startChoices);
        start_->setToolTip(tr("The data the new mod starts with. It gets its own data file when you save."));
    }
    build();
}

ModInfoDialog::ModInfoDialog(const ModInfo &info, QWidget *parent)
    : QDialog(parent),
      editing_(true),
      title_(new QLineEdit(info.title, this)),
      author_(new QLineEdit(info.author, this)),
      description_(new QPlainTextEdit(info.description, this)),
      problem_(new QLabel(this)),
      buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(tr("Mod Info"));
    build();
}

void ModInfoDialog::build()
{
    title_->setObjectName(QStringLiteral("modTitle"));
    title_->setPlaceholderText(tr("My Balance Mod"));
    title_->setToolTip(editing_ ? tr("The name the game shows. The folder keeps its name.")
                                : tr("Also the name of the mod's folder."));
    author_->setObjectName(QStringLiteral("modAuthor"));
    description_->setObjectName(QStringLiteral("modDescription"));
    description_->setTabChangesFocus(true);
    description_->setFixedHeight(description_->fontMetrics().lineSpacing() * 5);
    problem_->setObjectName(QStringLiteral("modProblem"));
    problem_->setWordWrap(true);
    problem_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    connect(title_, &QLineEdit::textChanged, this, &ModInfoDialog::validate);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow(tr("&Name:"), title_);
    form->addRow(tr("&Author:"), author_);
    form->addRow(tr("&Description:"), description_);
    if (start_)
        form->addRow(tr("&Start from:"), start_);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(problem_);
    layout->addStretch();
    layout->addWidget(buttons_);

    resize(440, sizeHint().height());
    validate();
}

QString ModInfoDialog::title() const
{
    return editing_ ? title_->text().trimmed() : title_->text();
}

QString ModInfoDialog::author() const
{
    return author_->text().trimmed();
}

QString ModInfoDialog::description() const
{
    return description_->toPlainText().trimmed();
}

int ModInfoDialog::startIndex() const
{
    return start_ ? start_->currentIndex() : -1;
}

void ModInfoDialog::validate()
{
    QString problem;
    if (editing_)
        problem = title().isEmpty() ? tr("Enter a name.") : QString();
    else
        problem = checkModTitle(modsFolder_, title());
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
    // An empty name is the starting state, not a mistake worth flagging.
    problem_->setEnabled(!problem.isEmpty() && !title_->text().isEmpty());
    if (editing_)
        problem_->setText(problem);
    else
        problem_->setText(problem.isEmpty()
                              ? tr("Creates %1").arg(QDir::toNativeSeparators(QDir(modsFolder_).filePath(title())))
                              : problem);
    problem_->setVisible(!problem_->text().isEmpty());
}

} // namespace newage
