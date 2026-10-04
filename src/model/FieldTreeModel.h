#pragma once

#include <functional>

#include <QAbstractItemModel>
#include <QList>
#include <QString>

#include "core/NameProvider.h"
#include "model/FieldDesc.h"

namespace newage {

// Two-level tree of name/value rows grouped under headings, for showing the
// fields of one selected object. It doesn't know the entity type: callers
// bind a descriptor table to an object with setObject().
//
// Fields with a setter are editable in the value column when a Writer is
// given. setData() checks and converts the new value, then hands it to the
// writer, which stores it in the object.
class FieldTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column { NameColumn, ValueColumn, ColumnCount };

    enum Role
    {
        // The field value itself (int, float, string), on the value column.
        ValueRole = Qt::UserRole,
        // int: the range an editable int field accepts, inclusive.
        MinimumRole,
        MaximumRole,
        // int(SpriteKind). Set on both columns of a sprite field, so a hover
        // on the name or the value can preview it. None for everything else.
        SpriteRole,
        // int(RefKind). Set on both columns, so a double-click on the name or
        // the value can follow the reference. None for everything else.
        RefKindRole,
    };

    // One field with its value already read from the object.
    struct Row
    {
        QString name;
        QString group;
        QVariant value;
        // Shown after the value, e.g. the language string of a string ID.
        QString note = {};
        // Index of the field in its descriptor table, passed to the writer;
        // -1 for read-only fields.
        int field = -1;
        // Range of an editable int field, inclusive.
        int minimum = 0;
        int maximum = 0;
        // Shown instead of the value, which follows in parentheses, e.g. the
        // name of the tech a tech ID refers to. A code kind's label follows
        // the value instead: "70 - Combatant".
        QString label = {};
        // What the value is the ID of. The label is looked up again after an
        // edit.
        RefKind ref = RefKind::None;
        SpriteKind sprite = SpriteKind::None;
    };

    // Stores `value` (int or float, checked against the row's range) in field
    // `field` of the object. Returns the value now stored, or an invalid
    // QVariant if the object is gone.
    using Writer = std::function<QVariant(int field, const QVariant &value)>;

    // Name of the entity of kind `kind` with ID `id`, e.g. "Loom" for tech
    // 22, or an empty string if there is none.
    using RefNamer = std::function<QString(RefKind kind, int id)>;

    explicit FieldTreeModel(QObject *parent = nullptr);

    // Reads every field of `fields` that applies to `object`. The model keeps
    // the values, not the object, so it can't be left pointing at freed data.
    // String ID fields are looked up in `names` (if given). Fields with a
    // setter are editable through `writer` (if given), which is passed their
    // index in `fields`. Reference fields are labelled by `refNamer` (if given).
    template <typename T>
    void setObject(const QList<FieldDesc<T>> &fields, const T &object, const NameProvider *names = nullptr,
                   Writer writer = {}, const RefNamer &refNamer = {})
    {
        QList<Row> rows;
        for (qsizetype i = 0; i < fields.size(); ++i)
        {
            const FieldDesc<T> &field = fields.at(i);
            if (field.applies && !field.applies(object))
                continue;
            Row row{field.name, field.group, field.get(object)};
            if (field.isStringId && names)
                row.note = names->text(row.value.toInt());
            row.ref = field.ref;
            row.sprite = field.sprite;
            if (row.ref != RefKind::None && refNamer)
                row.label = refNamer(row.ref, row.value.toInt());
            if (field.set)
            {
                row.field = static_cast<int>(i);
                row.minimum = field.minimum;
                row.maximum = field.maximum;
            }
            rows.append(row);
        }
        setRows(rows, std::move(writer), refNamer);
    }

    // Rows with `field` >= 0 are editable when `writer` is set. `refNamer`
    // relabels reference rows after an edit.
    void setRows(const QList<Row> &rows, Writer writer = {}, RefNamer refNamer = {});
    void clear() { setRows({}); }

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    // Accepts an int or a number string for int fields, a float or a string
    // for float fields. Returns false for text that isn't a number, a value
    // out of range, or a failed write. Writing the current value is a no-op.
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
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

    // nullptr for a group heading.
    const Row *rowAt(const QModelIndex &index) const;
    bool isEditable(const Row &row) const { return row.field >= 0 && writer_; }

    QList<Group> groups_;
    Writer writer_;
    RefNamer refNamer_;
};

} // namespace newage
