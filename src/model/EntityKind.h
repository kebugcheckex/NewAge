#pragma once

#include <functional>
#include <optional>
#include <string>

#include <QList>
#include <QString>
#include <QVariant>

#include "model/FieldDesc.h"

namespace newage {

class Session;

// Why a tech is or isn't researchable by one civ. OtherCiv wins over a
// tech-tree disable: it is the more telling reason.
enum class TechAvailability
{
    Available,
    // Tech::Civ names another civ (unique techs, civ bonuses).
    OtherCiv,
    // The civ's tech tree effect (Civ::TechTreeID) disables it with a
    // "disable tech" command (type 102).
    DisabledByTechTree,
};

// One entry per tech. Empty when no data is open or `civ` is out of range.
QList<TechAvailability> techAvailability(const Session &session, int civ);

// One applicable field, as `get` will return it. `value` is the stored number
// or string. `label` is a reference name without the ID. `text` is the
// language string of a string-ID field. `minimum` and `maximum` are set only
// for an editable int.
struct FieldValue
{
    QString key;
    QString name;
    QString group;
    // "int", "float", or "string".
    QString type;
    QVariant value;
    bool editable = false;
    std::optional<int> minimum;
    std::optional<int> maximum;
    QString label;
    RefKind labelKind = RefKind::None;
    QString text;
    // Whether `value` is a language string ID, which `text` resolves.
    bool stringId = false;
};

// Descriptor metadata, including fields absent from the current data.
struct FieldSchema
{
    QString key;
    QString name;
    QString group;
    QString type;
    bool editable = false;
    bool conditional = false;
    std::optional<int> minimum;
    std::optional<int> maximum;
    RefKind labelKind = RefKind::None;
};

// Result of EntityKind::set. `ok` with `changed` false is a no-op: the value
// was already stored, and the session was not marked modified.
struct SetResult
{
    bool ok = false;
    // Empty on success. Otherwise unknown_entity, inactive_entity,
    // unknown_field, not_applicable, read_only, bad_value, or out_of_range.
    QString code;
    QString message;
    QVariant oldValue;
    QVariant newValue;
    bool changed = false;
};

// One entity type as the CLI sees it. IDs are indices, as in the list models.
// `civ` is ignored by global kinds, except that a tech's availability depends
// on the civ. The object itself holds no data.
class EntityKind
{
public:
    virtual ~EntityKind() = default;

    virtual QString key() const = 0; // "civ", "unit", "tech", "effect"
    virtual bool perCiv() const = 0;
    virtual int count(const Session &session, int civ) const = 0;
    virtual bool isActive(const Session &session, int civ, int id) const = 0;
    // Label rule: language name, else internal name, else "(unnamed)". An
    // empty unit slot is "(empty)". Missing entities are an empty string.
    virtual QString name(const Session &session, int civ, int id) const = 0;
    virtual QString internalName(const Session &session, int civ, int id) const = 0;
    // Applicable fields, in descriptor order. Empty when the entity is missing.
    virtual QList<FieldValue> fields(const Session &session, int civ, int id) const = 0;
    // fields() without `label` and `text`, which cost a name lookup each: the
    // form for comparing many entities.
    virtual QList<FieldValue> values(const Session &session, int civ, int id) const = 0;
    // The entity's bytes as genie writes them for `version` (a
    // genie::GameVersion). `masked` first zeroes what fields() reads, so two
    // entities whose masked bytes differ also differ outside the listed
    // fields. Effect fields list the whole effect, so a masked effect is
    // always the same. Empty for civs and missing or inactive entities.
    virtual std::string rawData(const Session &session, int civ, int id, int version, bool masked) const = 0;
    virtual QList<FieldSchema> schema(const Session &session) const = 0;
    // Parses, range-checks and, when `commit` is true, stores. Marks the
    // session modified when the stored value changes. `commit` false is the
    // check `DataService` runs on every edit before it changes anything.
    virtual SetResult set(Session &session, int civ, int id, const QString &key, const QVariant &value,
                          bool commit = true) = 0;
    // Copies the entity and returns what puts the copy back, for undoing a
    // series of set() calls. Empty when the entity is missing or the kind
    // has no fields.
    virtual std::function<void()> backup(Session &session, int civ, int id) const = 0;
    // When one request sets several keys of an entity, keys are written by
    // ascending rank, then by key: a key whose fields depend on another
    // (an effect command's slots on its type) ranks after it.
    virtual int writeRank(const QString &) const { return 0; }

protected:
    EntityKind() = default;

    EntityKind(const EntityKind &) = delete;
    EntityKind &operator=(const EntityKind &) = delete;
};

// Stable order: civ, unit, tech, effect.
QList<EntityKind *> entityKinds();
EntityKind *findEntityKind(const QString &key);

EntityKind &civKind();
EntityKind &unitKind();
EntityKind &techKind();
EntityKind &effectKind();

} // namespace newage
