#pragma once

#include <functional>

#include <QList>
#include <QString>
#include <QVariant>

namespace newage {

// Describes one field of an entity type T (a unit, a tech, ...), so views can
// show any entity without per-field widget code.
// Read-only for now; `set`, a value type and a minimum game version come with
// editing.
template <typename T>
struct FieldDesc
{
    QString name;
    QString group;
    // Whether the field exists on this particular object, e.g. unit Speed only
    // for Type >= 20. Empty means always.
    std::function<bool(const T &)> applies;
    std::function<QVariant(const T &)> get;
};

} // namespace newage
