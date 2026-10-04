#pragma once

#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

#include <QList>
#include <QString>
#include <QVariant>

namespace newage {

// What an int field's value is the ID of. Views show the referenced entity's
// name with the ID.
enum class RefKind
{
    None,
    Tech,
    // A civ resource (Food Storage, ...), as in resource costs.
    Resource,
    // An index into DatFile::Civs. -1 is not a civ.
    Civ,
    // An index into DatFile::Effects. -1 is not an effect.
    Effect,
    // An index into the current civ's units. -1 is not a unit.
    Unit,
};

// An int field that is a frame index in a game sprite. The view may preview
// it; the number in the field tree stays the value. Unit icons are a different
// sprite and are not marked here.
enum class SpriteKind
{
    None,
    TechIcon,
};

// Describes one field of an entity type T (a unit, a tech, ...), so views can
// show any entity without per-field widget code.
// Only number fields can be editable; build those with numberField().
template <typename T>
struct FieldDesc
{
    QString name;
    QString group;
    // Whether the field exists on this particular object, e.g. unit Speed only
    // for Type >= 20. Empty means always.
    std::function<bool(const T &)> applies;
    std::function<QVariant(const T &)> get;
    // The value is a language string ID; views show the string next to it.
    bool isStringId = false;
    // Stores a value of the type get() returns (int or float), already
    // checked against minimum/maximum. Empty for read-only fields.
    std::function<void(T &, const QVariant &)> set = {};
    // Range an int field accepts, inclusive. Unused for floats.
    int minimum = 0;
    int maximum = 0;
    RefKind ref = RefKind::None;
    SpriteKind sprite = SpriteKind::None;
};

// An editable field for a number member of T. `access` returns a reference to
// the member for a const or non-const object, typically
// `[](auto &u) -> auto & { return u.HitPoints; }`. Integer members read as int
// and accept their type's range; float members read as float.
template <typename T, typename Access>
FieldDesc<T> numberField(const QString &name, const QString &group, std::function<bool(const T &)> applies,
                         Access access)
{
    using Value = std::remove_cvref_t<decltype(access(std::declval<T &>()))>;
    static_assert(std::is_same_v<Value, float>
                      || (std::is_integral_v<Value> && sizeof(Value) <= sizeof(int)
                          && (std::is_signed_v<Value> || sizeof(Value) < sizeof(int))),
                  "numberField needs a float member or an integer member that fits in int");

    FieldDesc<T> field{name, group, std::move(applies), [access](const T &object) {
                           if constexpr (std::is_same_v<Value, float>)
                               return QVariant(access(object));
                           else
                               return QVariant(static_cast<int>(access(object)));
                       }};
    field.set = [access](T &object, const QVariant &value) {
        if constexpr (std::is_same_v<Value, float>)
            access(object) = value.toFloat();
        else
            access(object) = static_cast<Value>(value.toInt());
    };
    if constexpr (std::is_integral_v<Value>)
    {
        field.minimum = std::numeric_limits<Value>::min();
        field.maximum = std::numeric_limits<Value>::max();
    }
    return field;
}

} // namespace newage
