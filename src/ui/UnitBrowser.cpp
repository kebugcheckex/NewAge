#include "ui/UnitBrowser.h"

#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QListView>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/FieldTreeModel.h"
#include "model/UnitFields.h"
#include "model/UnitListModel.h"

namespace newage {

UnitBrowser::UnitBrowser(Session *session, QWidget *parent)
    : QWidget(parent),
      session_(session),
      unitModel_(new UnitListModel(session, this)),
      unitFilter_(new QSortFilterProxyModel(this)),
      fieldModel_(new FieldTreeModel(this)),
      civCombo_(new QComboBox(this)),
      filterEdit_(new QLineEdit(this)),
      unitView_(new QListView(this)),
      fieldView_(new QTreeView(this))
{
    unitFilter_->setSourceModel(unitModel_);
    unitFilter_->setFilterCaseSensitivity(Qt::CaseInsensitive);

    filterEdit_->setPlaceholderText(tr("Filter units"));
    filterEdit_->setClearButtonEnabled(true);

    unitView_->setModel(unitFilter_);
    unitView_->setSelectionMode(QAbstractItemView::SingleSelection);
    unitView_->setUniformItemSizes(true);

    fieldView_->setModel(fieldModel_);
    fieldView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fieldView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fieldView_->setAlternatingRowColors(true);
    fieldView_->header()->setSectionResizeMode(FieldTreeModel::NameColumn, QHeaderView::ResizeToContents);

    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(civCombo_);
    leftLayout->addWidget(filterEdit_);
    leftLayout->addWidget(unitView_);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(left);
    splitter->addWidget(fieldView_);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 700});

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    connect(session_, &Session::opened, this, &UnitBrowser::reloadCivs);
    connect(session_, &Session::closed, this, &UnitBrowser::reloadCivs);
    connect(civCombo_, &QComboBox::currentIndexChanged, this, &UnitBrowser::showCiv);
    connect(filterEdit_, &QLineEdit::textChanged, unitFilter_, &QSortFilterProxyModel::setFilterFixedString);
    connect(unitView_->selectionModel(), &QItemSelectionModel::currentChanged, this, &UnitBrowser::showSelectedUnit);
    connect(fieldModel_, &QAbstractItemModel::modelReset, this, [this] {
        // Group headings span both columns so they read as section titles.
        for (int row = 0; row < fieldModel_->rowCount(); ++row)
            fieldView_->setFirstColumnSpanned(row, {}, true);
        fieldView_->expandAll();
    });

    reloadCivs();
}

void UnitBrowser::reloadCivs()
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

void UnitBrowser::showCiv(int civ)
{
    // Keep the same unit selected so it can be compared across civs.
    const int unit = selectedUnit();
    unitModel_->setCiv(civ);
    selectUnit(unit);
    showSelectedUnit();
}

void UnitBrowser::showSelectedUnit()
{
    const genie::Unit *unit = unitModel_->unit(selectedUnit());
    if (unit)
        fieldModel_->setObject(unitFields(), *unit);
    else
        fieldModel_->clear();
}

int UnitBrowser::selectedUnit() const
{
    const QModelIndex current = unitView_->currentIndex();
    return current.isValid() ? unitFilter_->mapToSource(current).row() : -1;
}

void UnitBrowser::selectUnit(int unit)
{
    const QModelIndex index = unitFilter_->mapFromSource(unitModel_->index(unit));
    if (!index.isValid() || !(index.flags() & Qt::ItemIsSelectable))
        return;
    unitView_->setCurrentIndex(index);
    unitView_->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

} // namespace newage
