#include "model/FieldTreeModel.h"

#include <algorithm>
#include <charconv>

namespace newage {

namespace {

// internalId() of a field index is its group row + 1; group indexes use 0.
constexpr quintptr kGroupId = 0;

} // namespace

FieldTreeModel::FieldTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

void FieldTreeModel::setRows(const QList<Row> &rows)
{
    beginResetModel();
    groups_.clear();
    for (const Row &row : rows)
    {
        auto it = std::find_if(groups_.begin(), groups_.end(),
                               [&](const Group &group) { return group.name == row.group; });
        if (it == groups_.end())
        {
            groups_.append({row.group, {}});
            it = groups_.end() - 1;
        }
        it->rows.append(row);
    }
    endResetModel();
}

QModelIndex FieldTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};
    if (!parent.isValid())
        return createIndex(row, column, kGroupId);
    return createIndex(row, column, static_cast<quintptr>(parent.row()) + 1);
}

QModelIndex FieldTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() == kGroupId)
        return {};
    return createIndex(static_cast<int>(child.internalId() - 1), 0, kGroupId);
}

int FieldTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return static_cast<int>(groups_.size());
    if (parent.internalId() == kGroupId && parent.column() == 0)
        return static_cast<int>(groups_.at(parent.row()).rows.size());
    return 0;
}

int FieldTreeModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant FieldTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    if (index.internalId() == kGroupId)
    {
        if (index.column() == NameColumn && role == Qt::DisplayRole)
            return groups_.at(index.row()).name;
        return {};
    }

    const Row &row = groups_.at(static_cast<int>(index.internalId() - 1)).rows.at(index.row());
    if (role == Qt::DisplayRole)
    {
        if (index.column() == NameColumn)
            return row.name;
        if (row.note.isEmpty())
            return displayText(row.value);
        // Keep multi-line strings on one line in the tree.
        QString note = row.note;
        note.replace(QLatin1Char('\n'), QLatin1Char(' '));
        return QStringLiteral("%1 \"%2\"").arg(displayText(row.value), note);
    }
    if (role == Qt::ToolTipRole && index.column() == ValueColumn && !row.note.isEmpty())
        return row.note;
    if (role == Qt::UserRole && index.column() == ValueColumn)
        return row.value;
    return {};
}

QVariant FieldTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    return section == NameColumn ? tr("Field") : tr("Value");
}

QString FieldTreeModel::displayText(const QVariant &value)
{
    if (value.typeId() == QMetaType::Float)
    {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.toFloat());
        return QString::fromLatin1(buffer, result.ptr - buffer);
    }
    return value.toString();
}

} // namespace newage
