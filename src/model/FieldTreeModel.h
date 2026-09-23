#pragma once

#include <QAbstractItemModel>
#include <QList>
#include <QString>

#include "core/NameProvider.h"
#include "model/FieldDesc.h"

namespace newage {

// Two-level tree of name/value rows grouped under headings, for showing the
// fields of one selected object. It doesn't know the entity type: callers
// bind a descriptor table to an object with setObject().
class FieldTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column { NameColumn, ValueColumn, ColumnCount };

    // One field with its value already read from the object.
    struct Row
    {
        QString name;
        QString group;
        QVariant value;
        // Shown after the value, e.g. the language string of a string ID.
        QString note = {};
    };

    explicit FieldTreeModel(QObject *parent = nullptr);

    // Reads every field of `fields` that applies to `object`. The model keeps
    // the values, not the object, so it can't be left pointing at freed data.
    // String ID fields are looked up in `names` (if given).
    template <typename T>
    void setObject(const QList<FieldDesc<T>> &fields, const T &object, const NameProvider *names = nullptr)
    {
        QList<Row> rows;
        for (const FieldDesc<T> &field : fields)
        {
            if (field.applies && !field.applies(object))
                continue;
            Row row{field.name, field.group, field.get(object)};
            if (field.isStringId && names)
                row.note = names->text(row.value.toInt());
            rows.append(row);
        }
        setRows(rows);
    }

    void setRows(const QList<Row> &rows);
    void clear() { setRows({}); }

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Formats a field value for display. Floats use the shortest text that
    // reads back as the same float (0.2, not 0.200000003).
    static QString displayText(const QVariant &value);

private:
    struct Group
    {
        QString name;
        QList<Row> rows;
    };

    QList<Group> groups_;
};

} // namespace newage
