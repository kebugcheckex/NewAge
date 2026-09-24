#include "ui/EntityBrowser.h"

#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QListView>
#include <QSpinBox>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include "core/Config.h"
#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/EntityListModel.h"
#include "model/FieldTreeModel.h"
#include "model/ListFilterModel.h"

namespace newage {

namespace {

// Draws inactive rows (EntityListModel::ActiveRole false) in the disabled text
// colour, including those that stay selectable, such as another civ's techs.
class InactiveRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (!index.data(EntityListModel::ActiveRole).toBool())
            option->palette.setColor(QPalette::Text, option->palette.color(QPalette::Disabled, QPalette::Text));
    }
};

// Editors for field values: the default ones (a spin box for ints, a line edit
// for floats, which edit as text), with int spin boxes limited to the field's
// range.
class FieldValueDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *spin = qobject_cast<QSpinBox *>(editor))
            spin->setRange(index.data(FieldTreeModel::MinimumRole).toInt(),
                           index.data(FieldTreeModel::MaximumRole).toInt());
        return editor;
    }
};

} // namespace

EntityBrowser::EntityBrowser(Session *session, Config *config, EntityListModel *model,
                             HideInactiveEntry hideInactive, const QString &filterHint, QWidget *parent)
    : QWidget(parent),
      session_(session),
      config_(config),
      hideInactive_(hideInactive),
      listModel_(model),
      listFilter_(new ListFilterModel(this)),
      fieldModel_(new FieldTreeModel(this)),
      civCombo_(new QComboBox(this)),
      filterEdit_(new QLineEdit(this)),
      listView_(new QListView(this)),
      fieldView_(new QTreeView(this))
{
    listModel_->setParent(this);
    listFilter_->setSourceModel(listModel_);

    filterEdit_->setPlaceholderText(filterHint);
    filterEdit_->setClearButtonEnabled(true);

    listView_->setModel(listFilter_);
    listView_->setSelectionMode(QAbstractItemView::SingleSelection);
    listView_->setUniformItemSizes(true);
    listView_->setItemDelegate(new InactiveRowDelegate(listView_));

    fieldView_->setModel(fieldModel_);
    fieldView_->setItemDelegateForColumn(FieldTreeModel::ValueColumn, new FieldValueDelegate(fieldView_));
    fieldView_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    fieldView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fieldView_->setAlternatingRowColors(true);
    fieldView_->header()->setSectionResizeMode(FieldTreeModel::NameColumn, QHeaderView::ResizeToContents);

    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(civCombo_);
    leftLayout->addWidget(filterEdit_);
    leftLayout->addWidget(listView_);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(left);
    splitter->addWidget(fieldView_);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 700});

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    connect(session_, &Session::opened, this, &EntityBrowser::reloadCivs);
    connect(session_, &Session::closed, this, &EntityBrowser::reloadCivs);
    connect(civCombo_, &QComboBox::currentIndexChanged, this, &EntityBrowser::showCiv);
    connect(filterEdit_, &QLineEdit::textChanged, listFilter_, &ListFilterModel::setFilterFixedString);
    connect(listView_->selectionModel(), &QItemSelectionModel::currentChanged, this, &EntityBrowser::showSelected);
    connect(config_, &Config::changed, this, &EntityBrowser::applyConfig);
    // Rows are selected whole, so the name cell edits the value too: F2 edits
    // the current cell, kept on the value column, and a double-click on the
    // name is passed on to the value.
    connect(fieldView_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) {
        if (current.column() == FieldTreeModel::NameColumn && current.parent().isValid())
            fieldView_->selectionModel()->setCurrentIndex(current.siblingAtColumn(FieldTreeModel::ValueColumn),
                                                          QItemSelectionModel::NoUpdate);
    });
    connect(fieldView_, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (index.column() == FieldTreeModel::NameColumn)
            fieldView_->edit(index.siblingAtColumn(FieldTreeModel::ValueColumn));
    });
    connect(fieldModel_, &QAbstractItemModel::modelReset, this, [this] {
        // Group headings span both columns so they read as section titles.
        for (int row = 0; row < fieldModel_->rowCount(); ++row)
            fieldView_->setFirstColumnSpanned(row, {}, true);
        fieldView_->expandAll();
    });

    applyConfig();
    reloadCivs();
}

void EntityBrowser::reloadCivs()
{
    const QSignalBlocker blocker(civCombo_);
    civCombo_->clear();
    if (session_->isOpen())
    {
        const auto &civs = session_->dat()->Civs;
        for (size_t i = 0; i < civs.size(); ++i)
            civCombo_->addItem(QStringLiteral("%1 - %2").arg(i).arg(QString::fromLatin1(civs[i].Name)));
        // Civ 0 is Gaia; start on the first playable civ when there is one.
        civCombo_->setCurrentIndex(civs.size() > 1 ? 1 : 0);
    }
    showCiv(civCombo_->currentIndex());
}

void EntityBrowser::showCiv(int civ)
{
    // Keep the same entity selected so it can be compared across civs.
    const int row = selectedRow();
    listModel_->setCiv(civ);
    selectRow(row);
    showSelected();
}

void EntityBrowser::showSelected()
{
    listModel_->showFields(selectedRow(), *fieldModel_);
}

int EntityBrowser::selectedRow() const
{
    const QModelIndex current = listView_->currentIndex();
    return current.isValid() ? listFilter_->mapToSource(current).row() : -1;
}

void EntityBrowser::selectRow(int row)
{
    const QModelIndex index = listFilter_->mapFromSource(listModel_->index(row));
    if (!index.isValid() || !(index.flags() & Qt::ItemIsSelectable))
        return;
    listView_->setCurrentIndex(index);
    listView_->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

void EntityBrowser::applyConfig()
{
    listFilter_->setHideInactive((config_->*hideInactive_)());
    if (listView_->currentIndex().isValid())
        listView_->scrollTo(listView_->currentIndex());
}

} // namespace newage
