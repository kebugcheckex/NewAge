#include "ui/DiffDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace newage {

namespace {

QString statusText(DiffStatus status)
{
    switch (status)
    {
    case DiffStatus::Changed: return QObject::tr("Changed");
    case DiffStatus::OnlyInGame: return QObject::tr("Only in game");
    case DiffStatus::OnlyInMod: return QObject::tr("Only in mod");
    }
    return {};
}

QString valueText(const QVariant &value, const QString &label, const QString &text)
{
    if (!value.isValid())
        return QObject::tr("(absent)");
    QString shown = value.toString();
    if (!label.isEmpty())
        shown += QStringLiteral(" (%1)").arg(label);
    if (!text.isEmpty())
        shown += QStringLiteral(" — %1").arg(text);
    return shown;
}

} // namespace

DiffDialog::DiffDialog(DiffResult result, QWidget *parent)
    : QDialog(parent), result_(std::move(result)), kind_(new QComboBox(this)), search_(new QLineEdit(this)),
      items_(new QTableWidget(this)), details_(new QTreeWidget(this)), open_(nullptr)
{
    setWindowTitle(tr("Compare mod with game"));
    resize(1100, 700);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("%1 changed · %2 only in game · %3 only in mod")
                                     .arg(result_.changed).arg(result_.onlyInGame).arg(result_.onlyInMod), this));
    if (!result_.hasDat)
        layout->addWidget(new QLabel(tr("This mod has no data file and currently uses the game's data."), this));
    if (result_.modFileVersion != result_.gameFileVersion)
        layout->addWidget(new QLabel(tr("File versions: mod %1, game %2")
                                         .arg(result_.modFileVersion, result_.gameFileVersion), this));
    kind_->addItem(tr("All kinds"), QString());
    for (const DiffCount &count : result_.counts)
        kind_->addItem(QStringLiteral("%1 (%2 / %3)").arg(count.kind).arg(count.mod).arg(count.game), count.kind);
    search_->setPlaceholderText(tr("Filter by ID or name"));
    auto *filters = new QWidget(this);
    auto *filterLayout = new QHBoxLayout(filters);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->addWidget(kind_);
    filterLayout->addWidget(search_, 1);
    layout->addWidget(filters);

    items_->setObjectName(QStringLiteral("diffItems"));
    items_->setColumnCount(4);
    items_->setHorizontalHeaderLabels({tr("Kind / ID"), tr("Name"), tr("Status"), tr("Civs")});
    items_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    items_->setSelectionBehavior(QAbstractItemView::SelectRows);
    items_->setSelectionMode(QAbstractItemView::SingleSelection);
    items_->verticalHeader()->hide();
    items_->horizontalHeader()->setStretchLastSection(true);
    details_->setObjectName(QStringLiteral("diffFields"));
    details_->setColumnCount(3);
    details_->setHeaderLabels({tr("Field"), tr("Mod"), tr("Game")});
    details_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    auto *splitter = new QSplitter(this);
    splitter->addWidget(items_);
    splitter->addWidget(details_);
    splitter->setSizes({450, 650});
    layout->addWidget(splitter, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    open_ = buttons->addButton(tr("Show in editor"), QDialogButtonBox::ActionRole);
    open_->setObjectName(QStringLiteral("showDiffEntity"));
    connect(open_, &QPushButton::clicked, this, &DiffDialog::openSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(kind_, &QComboBox::currentIndexChanged, this, &DiffDialog::filterItems);
    connect(search_, &QLineEdit::textChanged, this, &DiffDialog::filterItems);
    connect(items_, &QTableWidget::itemSelectionChanged, this, &DiffDialog::showDetails);
    connect(items_, &QTableWidget::cellDoubleClicked, this, &DiffDialog::openSelected);
    filterItems();
}

void DiffDialog::filterItems()
{
    const QString wanted = kind_->currentData().toString();
    const QString search = search_->text().trimmed();
    QList<int> visible;
    for (int index = 0; index < result_.items.size(); ++index)
    {
        const DiffItem &item = result_.items.at(index);
        if (!wanted.isEmpty() && item.kind != wanted)
            continue;
        if (!search.isEmpty() && !QStringLiteral("%1 %2 %3").arg(item.id).arg(item.name, item.internalName)
                                     .contains(search, Qt::CaseInsensitive))
            continue;
        visible.append(index);
    }
    items_->setUpdatesEnabled(false);
    items_->setRowCount(visible.size());
    for (int row = 0; row < visible.size(); ++row)
    {
        const int index = visible.at(row);
        const DiffItem &item = result_.items.at(index);
        auto *identity = new QTableWidgetItem(QStringLiteral("%1 %2").arg(item.kind).arg(item.id));
        identity->setData(Qt::UserRole, index);
        items_->setItem(row, 0, identity);
        items_->setItem(row, 1, new QTableWidgetItem(item.name));
        items_->setItem(row, 2, new QTableWidgetItem(statusText(item.status)));
        QStringList civs;
        for (int civ : item.civs)
            civs.append(QString::number(civ));
        items_->setItem(row, 3, new QTableWidgetItem(civs.join(QStringLiteral(", "))));
    }
    items_->setUpdatesEnabled(true);
    if (items_->rowCount() > 0)
        items_->selectRow(0);
    else
    {
        details_->clear();
        open_->setEnabled(false);
    }
}

void DiffDialog::showDetails()
{
    details_->clear();
    open_->setEnabled(false);
    const int row = items_->currentRow();
    if (row < 0 || !items_->item(row, 0))
        return;
    const DiffItem &item = result_.items.at(items_->item(row, 0)->data(Qt::UserRole).toInt());
    open_->setEnabled(item.status != DiffStatus::OnlyInGame && item.kind != QStringLiteral("civ"));
    for (const DiffField &field : item.fields)
        new QTreeWidgetItem(details_, {field.key, valueText(field.modValue, field.modLabel, field.modText),
                                       valueText(field.gameValue, field.gameLabel, field.gameText)});
    if (item.unlisted)
        new QTreeWidgetItem(details_, {tr("Other serialized data"), tr("Different"), tr("Different")});
}

void DiffDialog::openSelected()
{
    const int row = items_->currentRow();
    if (row < 0 || !items_->item(row, 0))
        return;
    const DiffItem &item = result_.items.at(items_->item(row, 0)->data(Qt::UserRole).toInt());
    if (item.status == DiffStatus::OnlyInGame || item.kind == QStringLiteral("civ"))
        return;
    emit entityActivated(item.kind, item.id, item.civs.isEmpty() ? -1 : item.civs.front());
}

} // namespace newage
