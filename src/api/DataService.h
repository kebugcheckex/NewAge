#pragma once

#include <optional>

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "core/GameInstall.h"
#include "core/Mods.h"
#include "core/Session.h"
#include "model/EntityKind.h"

namespace newage {

// Where data is read from, and where a write would go. Empty strings are
// unset. A loose `datPath` is read-only and ignores `gameDir`. Otherwise
// `gameDir` is detected with detectInstall; `dataset` picks a file name when
// the folder has several (default: the newest). `mod` is a mod title or an
// existing directory.
struct DataSource
{
    QString gameDir;
    QString dataset;
    QString mod;
    QString modsFolder;
    QString datPath;
    QString versionKey;
    QString locale = QStringLiteral("en");
};

// Fills empty game, dataset, mod and mods-folder fields from NEWAGE_GAME,
// NEWAGE_DATASET, NEWAGE_MOD and NEWAGE_MODS_FOLDER. open() does not do this,
// so a caller that wants the process environment asks for it.
DataSource resolveSource(DataSource source);

// One assignment. `civ` is required for a per-civ kind and ignored otherwise.
// `expect`, when set, is compare-and-set against the value the edits before
// this one left.
struct FieldEdit
{
    QString kind;
    int id = -1;
    int civ = -1;
    QString key;
    QVariant value;
    std::optional<QVariant> expect;
};

// One field an apply would change, or left alone because it already held
// `newValue`. `civ` is -1 for a global kind.
struct FieldChange
{
    QString kind;
    int id = -1;
    int civ = -1;
    QString key;
    QVariant oldValue;
    QVariant newValue;
};

// A failed open or apply. `kind`, `id`, `civ`, `key` and `value` name the edit
// that failed, when there is one.
struct ServiceError
{
    QString code;
    QString message;
    QString kind;
    int id = -1;
    int civ = -1;
    QString key;
    QVariant value;
};

// One `lookup` row. `internalName` is set for tables of entities in the data,
// `ownerCiv` for techs (Tech::Civ, -1 for any civ). `match` is "exact",
// "prefix" or "substring", and empty when the lookup had no text.
struct LookupMatch
{
    int id = -1;
    QString name;
    std::optional<QString> internalName;
    std::optional<int> ownerCiv;
    QString match;
};

struct LookupResult
{
    bool ok = false;
    ServiceError error;
    QList<LookupMatch> matches;
};

// Tables `lookup` accepts: the entity kinds in registry order, then the fixed
// lists (resource, unit-class, attribute, effect-type, unit-type, tech-type,
// resource-mode, tech-modifier-mode, enable-mode, upgrade-mode).
QStringList lookupTables();

// What `list` returns. `civ` selects the unit copy (required for units) and,
// for techs, which civ's availability decides whether a tech is active; other
// kinds take no civ. `ownerCiv` keeps techs whose Tech::Civ equals it. `all`
// keeps inactive rows. `limit` -1 means no limit.
struct ListQuery
{
    QString kind;
    int civ = -1;
    std::optional<int> ownerCiv;
    bool all = false;
    int offset = 0;
    int limit = -1;
};

// One `list` row. `ownerCiv` is set for techs. `active` is set when the
// entity's activity is known: always for units, for techs when a civ is given.
struct ListRow
{
    int id = -1;
    QString name;
    QString internalName;
    std::optional<int> ownerCiv;
    std::optional<bool> active;
};

// `total` counts the rows that pass the filters, before offset and limit.
struct ListResult
{
    bool ok = false;
    ServiceError error;
    int total = 0;
    QList<ListRow> rows;
};

// What `get` reads. `civ` selects the unit copy (required for units); for
// techs it is optional and decides `active`. Other kinds take no civ.
// `fields` are keys or `*` wildcard patterns; empty means every field.
struct GetQuery
{
    QString kind;
    QList<int> ids;
    int civ = -1;
    QStringList fields;
};

// One entity `get` read. `active` is set for techs when a civ is given.
// `fields` are the applicable fields that match the query, in descriptor order.
struct GetItem
{
    int id = -1;
    QString name;
    QString internalName;
    std::optional<bool> active;
    QList<FieldValue> fields;
};

// `items` follow the order of `GetQuery::ids`.
struct GetResult
{
    bool ok = false;
    ServiceError error;
    QList<GetItem> items;
};

// A mod `listMods` found. `hasDat` is whether it has its own copy of the
// data set's .dat; a write to a mod without one starts from the game's data.
struct ModEntry
{
    Mod mod;
    bool hasDat = false;
};

// `modsFolder` is where mod names are matched: `DataSource::modsFolder` when
// set, else the first of modsFolders(). It is empty when DE has no profile
// with a mods folder. `otherModsFolders` are the other detected folders.
// `mods` are sorted by title.
struct ModsResult
{
    bool ok = false;
    ServiceError error;
    GameDataset dataset;
    QString modsFolder;
    QStringList otherModsFolders;
    QList<ModEntry> mods;
};

// The mods of the data set `source` names, without loading any data.
// `source.mod` is ignored. A loose file, or a data set without mods, is
// mods_unsupported; a missing game folder or data set is no_dataset.
ModsResult listMods(const DataSource &source);

struct CreateModResult
{
    bool ok = false;
    ServiceError error;
    // The new mod folder, and where its .dat goes once it has one.
    QString dir;
    QString datPath;
    // Whether `from` copied a .dat into the new mod.
    bool copiedDat = false;
};

// Creates a mod for the data set `source` names, in `source.modsFolder` or the
// first of modsFolders(). `from` is empty or "game" for a mod that starts from
// the game's data (its .dat appears on the first write), or "mod:NAME" to copy
// that mod's .dat. A loose file or a data set without mods is mods_unsupported;
// a missing game folder or data set is no_dataset; an unusable title is
// bad_value; a missing source mod is mod_not_found.
CreateModResult createModForSource(const DataSource &source, const ModInfo &info, const QString &from = {});

struct OpenResult
{
    bool ok = false;
    ServiceError error;
    QStringList warnings;
};

struct ApplyResult
{
    bool ok = false;
    ServiceError error;
    // Path written. Empty when nothing changed, on a dry run, or on failure.
    QString saved;
    QList<FieldChange> changes;
    QList<FieldChange> unchanged;
};

// The first creation mode is an automatic, hidden civ bonus. Each command is
// an attribute set, add, or multiply effect (types 0, 4, and 5). -1 means no
// unit or class target; at least one target is required.
struct AttributeCommand
{
    int type = -1;
    int unit = -1;
    int unitClass = -1;
    int attribute = -1;
    double amount = 0;
};

struct CreateTechSpec
{
    int civ = -1;
    QString internalName;
    QString effectInternalName;
    QList<int> prerequisites;
    QList<AttributeCommand> commands;
    std::optional<int> expectedTechCount;
    std::optional<int> expectedEffectCount;
    QString expectedFileVersion;
    QString expectedSourceFingerprint;
};

struct CreateTechResult
{
    bool ok = false;
    ServiceError error;
    int techId = -1;
    int effectId = -1;
    QString saved;
    QString sourceFingerprint;
    QList<FieldValue> techFields;
    QList<FieldValue> effectFields;
};

// What `diff` compares. An empty `kind` compares every kind. `civ` limits
// units to that civ; -1 compares every civ both files have. `fields` are key
// patterns, as in GetQuery, and need a kind; they limit the fields compared.
// `limit` -1 means no limit.
struct DiffQuery
{
    QString kind;
    int civ = -1;
    QStringList fields;
    int offset = 0;
    int limit = -1;
};

// One field whose stored value differs. A side the field doesn't apply to,
// such as an effect command the other file lacks, has an invalid value.
// Labels and texts are filled for the items diff() returns, as `get` fills
// them.
struct DiffField
{
    QString key;
    RefKind labelKind = RefKind::None;
    bool stringId = false;
    QVariant modValue;
    QVariant gameValue;
    QString modLabel;
    QString gameLabel;
    QString modText;
    QString gameText;
};

enum class DiffStatus
{
    Changed,
    // The game has the entity and the mod doesn't: a civ or unit the mod
    // predates, or a unit slot the mod emptied.
    OnlyInGame,
    OnlyInMod,
};

// One entity that differs. `civs` are the unit copies with this same
// difference, ascending, and empty for global kinds. `name` and
// `internalName` come from the mod, or from the game when the mod lacks the
// entity or its internal name is empty (a placeholder). `unlisted` is set when the entity also differs outside the fields
// the kind lists (attacks, armors, ...); it is not checked when the query
// names fields.
struct DiffItem
{
    DiffStatus status = DiffStatus::Changed;
    QString kind;
    int id = -1;
    QList<int> civs;
    QString name;
    QString internalName;
    QList<DiffField> fields;
    bool unlisted = false;
};

// Entity counts of one kind; units count civ 0's slots, as `info` does.
struct DiffCount
{
    QString kind;
    int mod = 0;
    int game = 0;
};

// `hasDat` false means the mod has no .dat of its own, so it uses the game's
// data unchanged and nothing is compared. `changed`, `onlyInGame` and
// `onlyInMod` count every item, before offset and limit; `total` is their sum.
struct DiffResult
{
    bool ok = false;
    ServiceError error;
    QStringList warnings;
    bool hasDat = false;
    QString modDat;
    QString gameDat;
    QString modFileVersion;
    QString gameFileVersion;
    QList<DiffCount> counts;
    int changed = 0;
    int onlyInGame = 0;
    int onlyInMod = 0;
    int total = 0;
    QList<DiffItem> items;
};

// Owns one Session and is the only way a caller outside the GUI opens data
// and writes it. Writes go to the open mod's .dat, never to a game file.
class DataService
{
public:
    DataService() = default;

    DataService(const DataService &) = delete;
    DataService &operator=(const DataService &) = delete;

    OpenResult open(const DataSource &source);
    void close();

    bool isOpen() const { return session_.isOpen(); }
    Session &session() { return session_; }
    const Session &session() const { return session_; }

    // The game's data set. Empty after a loose-file open.
    const GameDataset &dataset() const { return gameDataset_; }
    const QString &modDir() const { return modDir_; }
    // Where apply() would write. Empty when the source is read-only.
    const QString &savePath() const { return savePath_; }
    const QStringList &warnings() const { return warnings_; }

    QList<EntityKind *> kinds() const { return entityKinds(); }
    EntityKind *kind(const QString &key) const { return findEntityKind(key); }

    // Rows of `table` whose name or internal name contains `text`, ignoring
    // case: exact matches first, then prefixes, then substrings, each by ID.
    // Empty text returns the whole table, by ID. `civ` selects the unit copy;
    // empty unit slots are left out. A civ out of range is unknown_entity, an
    // unknown table unknown_kind.
    LookupResult lookup(const QString &table, const QString &text, int civ = -1) const;

    // Entities of one kind in ID order, filtered as `query` says, then paged.
    // Inactive rows (empty unit slots, techs the civ can't research) are left
    // out unless `query.all`. A civ or owner civ out of range is
    // unknown_entity; an unknown kind is unknown_kind.
    ListResult list(const ListQuery &query) const;

    // Fields of each entity in `query.ids`. An ID out of range is
    // unknown_entity and an empty unit slot inactive_entity, which fail the
    // whole read. A field pattern that matches no key in the kind's schema is
    // unknown_field; one that matches only fields this entity lacks is not an
    // error. Unit labels in global kinds name the civ's copy, else civ 0's.
    GetResult get(const GetQuery &query) const;

    // Checks and stores each edit in order, so an edit sees the ones before
    // it, then saves once. Any failure undoes them all and writes nothing. A
    // dry run reports the same change list, then undoes it.
    ApplyResult apply(const QList<FieldEdit> &edits, bool dryRun = false);

    // Appends an automatic civ tech and its effect. Validation, dry runs, and
    // failed saves restore both vectors and the modified state.
    CreateTechResult createTech(const CreateTechSpec &spec, bool dryRun = false);

    // Compares the open mod's .dat with the game's current data set, which it
    // loads alongside. Entities are matched by ID. Civs are compared by
    // presence only, and units over the civs both files have. Each side keeps
    // its own file version; entities are compared as the older of the two
    // writes them. Without an open mod it is mod_required; after a loose-file
    // open, mods_unsupported. Reads only; writes nothing.
    DiffResult diff(const DiffQuery &query) const;

private:
    ServiceError writeGuard() const;

    Session session_;
    GameDataset gameDataset_;
    QString modDir_;
    QString savePath_;
    QStringList warnings_;
    bool loose_ = false;
    QByteArray sourceFingerprint_;
    bool saveExistedAtOpen_ = false;
};

} // namespace newage
