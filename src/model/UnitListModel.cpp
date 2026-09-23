#include "model/UnitListModel.h"

#include "core/Session.h"
#include "genie/dat/DatFile.h"

namespace newage {

UnitListModel::UnitListModel(Session *session, QObject *parent)
    : QAbstractListModel(parent), session_(session)
{
    connect(session_, &Session::closed, this, [this] { setCiv(-1); });
}

void UnitListModel::setCiv(int civ)
{
    beginResetModel();
    const bool valid = session_->isOpen() && civ >= 0 && civ < static_cast<int>(session_->dat()->Civs.size());
    civ_ = valid ? civ : -1;
    endResetModel();
}

const genie::Unit *UnitListModel::unit(int row) const
{
    if (!hasUnit(row))
        return nullptr;
    return &session_->dat()->Civs.at(civ_).Units.at(row);
}

int UnitListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || civ_ < 0 || !session_->isOpen())
        return 0;
    return static_cast<int>(session_->dat()->Civs.at(civ_).Units.size());
}

QVariant UnitListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || role != Qt::DisplayRole)
        return {};

    const int row = index.row();
    const genie::Unit *u = unit(row);
    if (!u)
        return tr("%1 - (empty)").arg(row);
    return QStringLiteral("%1 - %2").arg(row).arg(QString::fromLatin1(u->Name));
}

Qt::ItemFlags UnitListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || !hasUnit(index.row()))
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool UnitListModel::hasUnit(int row) const
{
    if (civ_ < 0 || !session_->isOpen())
        return false;
    const genie::Civ &civ = session_->dat()->Civs.at(civ_);
    return row >= 0 && row < static_cast<int>(civ.Units.size()) && row < static_cast<int>(civ.UnitPointers.size())
           && civ.UnitPointers.at(row) != 0;
}

} // namespace newage
