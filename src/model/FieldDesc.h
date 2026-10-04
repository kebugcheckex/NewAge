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
    // Unit::Class, and an effect command's class slot. -1 is not a class.
    UnitClass,
    // An effect-command attribute index (hit points, line of sight, ...).
    // -1 is not an attribute.
    Attribute,
    // Unit::Type (70 - Combatant, ...). A code, see isCodeKind().
    UnitType,
    // Tech::Type (0 - Regular, 2 - Age). A code, see isCodeKind().
    TechType,
};

// Whether values of `kind` are codes from a fixed list (unit types) rather
// than IDs of entities in the data. Views show codes as "70 - Combatant", as
// AGE does, and references as "Archer (4)".
inline bool isCodeKind(RefKind kind)
{
    return kind == RefKind::UnitType || kind == RefKind::TechType;
}

// An int field that is a frame index in a game sprite. The view may preview
// it; the number in the field tree stays the value. Tech and unit icons are
// different sprites.
enum class SpriteKind
{
    None,
    TechIcon,
    UnitIcon,
};

// Describes one field of an entity type T (a unit, a tech, ...), so views can
// show any entity without per-field widget code.
// Only number fields can be editable; build those with numberField().
template <typename T>
struct FieldDesc
{
    // Stable identifier for scripts and the CLI, unique within an entity kind:
    // snake_case, with a dotted prefix for numbered slots ("hit_points",
    // "cost1.amount"). Unlike `name`, it doesn't change when a label is
    // reworded.
    QString key;
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
    // Schema type, independent of whether a particular object has this field.
    int typeId = QMetaType::Int;
};

// An editable field for a number member of T. `access` returns a reference to
// the member for a const or non-const object, typically
// `[](auto &u) -> auto & { return u.HitPoints; }`. Integer members read as int
// and accept their type's range; float members read as float.
template <typename T, typename Access>
FieldDesc<T> numberField(const QString &key, const QString &name, const QString &group,
                         std::function<bool(const T &)> applies, Access access)
{
    using Value = std::remove_cvref_t<decltype(access(std::declval<T &>()))>;
    static_assert(std::is_same_v<Value, float>
                      || (std::is_integral_v<Value> && sizeof(Value) <= sizeof(int)
                          && (std::is_signed_v<Value> || sizeof(Value) < sizeof(int))),
                  "numberField needs a float member or an integer member that fits in int");

    FieldDesc<T> field{key, name, group, std::move(applies), [access](const T &object) {
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
    if constexpr (std::is_same_v<Value, float>)
        field.typeId = QMetaType::Float;
    if constexpr (std::is_integral_v<Value>)
    {
        field.minimum = std::numeric_limits<Value>::min();
        field.maximum = std::numeric_limits<Value>::max();
    }
    return field;
}

// The part of a field descriptor that parsing needs: the stored type and, for
// an int, the inclusive range. `typeId` is QMetaType::Float or an integer type.
struct FieldValueDesc
{
    int typeId = QMetaType::Int;
    int minimum = 0;
    int maximum = 0;
};

// A parsed edit. `value` is the int or float to store exactly when `code` is
// empty. Otherwise `code` is "bad_value" or "out_of_range" and `value` is
// invalid. An out-of-range `message` is "accepts <min>..<max>", so a caller
// can prefix the field key.
struct ParsedField
{
    QVariant value;
    QString code;
    QString message;
};

ParsedField parseFieldValue(const FieldValueDesc &field, const QVariant &input);

} // namespace newage
