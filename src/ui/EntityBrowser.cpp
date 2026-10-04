#include "ui/EntityBrowser.h"

#include <QAbstractSlider>
#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHelpEvent>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPixmap>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include "core/Config.h"
#include "core/Session.h"
#include "core/SpriteLibrary.h"
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
    fieldView_->viewport()->installEventFilter(this);

    iconPopup_ = new QLabel(this);
    iconPopup_->setObjectName(QStringLiteral("techIconPopup"));
    iconPopup_->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    iconPopup_->setAttribute(Qt::WA_ShowWithoutActivating);
    iconPopup_->setAttribute(Qt::WA_TransparentForMouseEvents);
    iconPopup_->setMargin(4);
    iconPopup_->setAutoFillBackground(true);
    iconPopup_->setBackgroundRole(QPalette::Base);
    iconPopup_->setFrameShape(QFrame::Box);
    iconPopup_->setFrameShadow(QFrame::Plain);
    iconPopup_->setLineWidth(1);
    iconPopup_->hide();

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
        hideTechIcon();
        // Group headings span both columns so they read as section titles.
        for (int row = 0; row < fieldModel_->rowCount(); ++row)
            fieldView_->setFirstColumnSpanned(row, {}, true);
        fieldView_->expandAll();
    });
    connect(fieldView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current) {
                if (!isTechIconRow(current))
                {
                    hideTechIcon();
                    return;
                }
                showTechIcon(current, techIconAnchor(current));
            });
    const auto followScroll = [this] {
        const QModelIndex current = fieldView_->currentIndex();
        if (!isTechIconRow(current))
        {
            hideTechIcon();
            return;
        }
        showTechIcon(current, techIconAnchor(current));
    };
    connect(fieldView_->verticalScrollBar(), &QAbstractSlider::valueChanged, this, followScroll);
    connect(fieldView_->horizontalScrollBar(), &QAbstractSlider::valueChanged, this, followScroll);

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

bool EntityBrowser::isTechIconRow(const QModelIndex &index) const
{
    return index.isValid() && index.parent().isValid()
           && index.data(FieldTreeModel::SpriteRole).toInt() == static_cast<int>(SpriteKind::TechIcon);
}

QImage EntityBrowser::techIconImage(const QModelIndex &index) const
{
    if (!session_->isOpen() || !isTechIconRow(index))
        return {};
    const int iconId = index.siblingAtColumn(FieldTreeModel::ValueColumn).data(FieldTreeModel::ValueRole).toInt();
    if (iconId < 0)
        return {};

    int iconSet = 0;
    const int civ = listModel_->civ();
    const auto &civs = session_->dat()->Civs;
    if (civ >= 0 && civ < static_cast<int>(civs.size()))
        iconSet = civs[static_cast<size_t>(civ)].IconSet;

    const SpriteImage sprite = session_->sprites().frame(SpriteLibrary::techIconSlpId(session_->gameVersion(), iconSet), iconId);
    if (sprite.isNull())
        return {};
    // copy() detaches from the buffer SpriteImage is about to leave.
    return QImage(reinterpret_cast<const uchar *>(sprite.rgba.constData()), sprite.width, sprite.height,
                  sprite.width * 4, QImage::Format_RGBA8888)
        .copy();
}

QPoint EntityBrowser::techIconAnchor(const QModelIndex &index) const
{
    const QModelIndex value = index.siblingAtColumn(FieldTreeModel::ValueColumn);
    const QRect rect = fieldView_->visualRect(value);
    if (!rect.isValid() || !fieldView_->viewport()->rect().intersects(rect))
        return {};
    return fieldView_->viewport()->mapToGlobal(rect.topRight() + QPoint(8, 0));
}

void EntityBrowser::showTechIcon(const QModelIndex &index, const QPoint &globalPos)
{
    if (globalPos.isNull())
    {
        hideTechIcon();
        return;
    }
    QImage image = techIconImage(index);
    if (image.isNull())
    {
        hideTechIcon();
        return;
    }

    // Nearest-neighbour up to the screen scale, then mark that scale so the
    // icon stays its native logical size and doesn't get scaled twice.
    const int scale = qBound(1, qRound(fieldView_->devicePixelRatioF()), 3);
    if (scale != 1)
        image = image.scaled(image.width() * scale, image.height() * scale, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    image.setDevicePixelRatio(scale);
    iconPopup_->setPixmap(QPixmap::fromImage(image));
    iconPopup_->adjustSize();

    QPoint pos = globalPos;
    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = this->screen();
    if (screen)
    {
        const QRect avail = screen->availableGeometry();
        const QSize size = iconPopup_->size();
        if (pos.x() + size.width() > avail.right())
            pos.setX(qMax(avail.left(), globalPos.x() - size.width() - 8));
        if (pos.y() + size.height() > avail.bottom())
            pos.setY(qMax(avail.top(), globalPos.y() - size.height() - 8));
        pos.setX(qBound(avail.left(), pos.x(), qMax(avail.left(), avail.right() - size.width() + 1)));
        pos.setY(qBound(avail.top(), pos.y(), qMax(avail.top(), avail.bottom() - size.height() + 1)));
    }
    iconPopup_->move(pos);
    iconPopup_->show();
}

void EntityBrowser::hideTechIcon()
{
    if (iconPopup_)
        iconPopup_->hide();
}

bool EntityBrowser::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == fieldView_->viewport())
    {
        if (event->type() == QEvent::ToolTip)
        {
            const auto *help = static_cast<const QHelpEvent *>(event);
            const QModelIndex index = fieldView_->indexAt(help->pos());
            if (isTechIconRow(index))
            {
                showTechIcon(index, help->globalPos() + QPoint(16, 16));
                return true;
            }
            // Moving off the icon row: keep a keyboard preview beside the cell,
            // otherwise drop the hover preview.
            if (isTechIconRow(fieldView_->currentIndex()))
                showTechIcon(fieldView_->currentIndex(), techIconAnchor(fieldView_->currentIndex()));
            else
                hideTechIcon();
        }
        else if (event->type() == QEvent::Leave)
        {
            const QModelIndex current = fieldView_->currentIndex();
            if (isTechIconRow(current))
                showTechIcon(current, techIconAnchor(current));
            else
                hideTechIcon();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void EntityBrowser::hideEvent(QHideEvent *event)
{
    hideTechIcon();
    QWidget::hideEvent(event);
}

void EntityBrowser::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && !isActiveWindow())
        hideTechIcon();
    QWidget::changeEvent(event);
}

} // namespace newage
