#include "model/EntityListModel.h"

#include "core/Session.h"
#include "genie/dat/DatFile.h"

namespace newage {

EntityListModel::EntityListModel(Session *session, QObject *parent)
    : QAbstractListModel(parent), session_(session)
{
    connect(session_, &Session::closed, this, [this] { setCiv(-1); });
}

void EntityListModel::setCiv(int civ)
{
    beginResetModel();
    const bool valid = session_->isOpen() && civ >= 0 && civ < static_cast<int>(session_->dat()->Civs.size());
    civ_ = valid ? civ : -1;
    civChanged();
    endResetModel();
}

bool EntityListModel::hasCiv() const
{
    return civ_ >= 0 && session_->isOpen();
}

void EntityListModel::entityEdited(int row)
{
    session_->setModified(true);
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed);
}

QVariant EntityListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const int row = index.row();
    switch (role)
    {
    case ActiveRole:
        return isActive(row);
    case Qt::DisplayRole:
        return QStringLiteral("%1 - %2").arg(row).arg(name(row));
    case Qt::ToolTipRole:
        if (const QString internal = internalName(row); !internal.isEmpty())
            return internal;
        return {};
    case SearchTextRole:
    {
        QString text = QStringLiteral("%1 - %2").arg(row).arg(name(row));
        if (const QString internal = internalName(row); !internal.isEmpty())
            text += QLatin1Char(' ') + internal;
        return text;
    }
    default:
        return {};
    }
}

} // namespace newage
