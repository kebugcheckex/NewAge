#include "api/DataService.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLockFile>
#include <QRegularExpression>
#include <QSet>

#include "core/Mods.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "model/EffectFields.h"
#include "model/RefNames.h"

namespace newage {

namespace {

ServiceError makeError(const QString &code, const QString &message)
{
    ServiceError error;
    error.code = code;
    error.message = message;
    return error;
}

QByteArray fileFingerprint(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return hash.result();
}

ServiceError editError(const FieldEdit &edit, const QString &code, const QString &message)
{
    ServiceError error = makeError(code, message);
    error.kind = edit.kind;
    error.id = edit.id;
    error.civ = edit.civ;
    error.key = edit.key;
    error.value = edit.value;
    return error;
}

bool sameStored(const QVariant &current, const QVariant &expected)
{
    if (current == expected)
        return true;
    if (current.typeId() == QMetaType::Float || current.typeId() == QMetaType::Double)
    {
        bool ok = false;
        const double number = expected.toDouble(&ok);
        return ok && static_cast<float>(number) == current.toFloat();
    }
    bool ok = false;
    const qlonglong number = expected.toLongLong(&ok);
    return ok && number == current.toLongLong();
}

QString resolveModDir(const GameDataset &dataset, const QString &mod, const QString &modsFolder)
{
    if (QFileInfo(mod).isDir())
        return QDir::cleanPath(QFileInfo(mod).absoluteFilePath());
    if (mod.contains(QLatin1Char('/')) || mod.contains(QLatin1Char('\\')) || QDir::isAbsolutePath(mod))
        return {};

    QString folder = modsFolder;
    if (folder.isEmpty())
    {
        const QStringList folders = modsFolders(dataset);
        if (folders.isEmpty())
            return {};
        folder = folders.first();
    }
    for (const Mod &entry : findMods(folder))
    {
        if (entry.info.title.compare(mod, Qt::CaseInsensitive) == 0)
            return entry.dir;
    }
    return {};
}

const GameDataset *selectDataset(const QList<GameDataset> &datasets, const QString &fileName)
{
    if (fileName.isEmpty())
        return datasets.isEmpty() ? nullptr : &datasets.first();
    const QString wanted = QFileInfo(fileName).fileName();
    for (const GameDataset &dataset : datasets)
    {
        if (QFileInfo(dataset.datPath).fileName().compare(wanted, Qt::CaseInsensitive) == 0)
            return &dataset;
    }
    return nullptr;
}

// The data set `source` names in its game folder, or the no_dataset error.
std::optional<ServiceError> chooseDataset(const DataSource &source, GameDataset &out)
{
    if (source.gameDir.isEmpty())
        return makeError(QStringLiteral("no_dataset"), QStringLiteral("No game folder or data file."));
    const QList<GameDataset> datasets = detectInstall(source.gameDir, source.locale);
    if (datasets.isEmpty())
        return makeError(QStringLiteral("no_dataset"),
                         QStringLiteral("No data set in %1.").arg(QDir::toNativeSeparators(source.gameDir)));
    const GameDataset *chosen = selectDataset(datasets, source.dataset);
    if (!chosen)
        return makeError(QStringLiteral("no_dataset"),
                         QStringLiteral("No data set named \"%1\".").arg(QFileInfo(source.dataset).fileName()));
    out = *chosen;
    return std::nullopt;
}

struct FixedTable
{
    QLatin1StringView key;
    RefKind kind;
};

// Fixed lists that lookup serves.
constexpr FixedTable kFixedTables[] = {
    {QLatin1StringView("resource"), RefKind::Resource},
    {QLatin1StringView("unit-class"), RefKind::UnitClass},
    {QLatin1StringView("attribute"), RefKind::Attribute},
    {QLatin1StringView("effect-type"), RefKind::EffectType},
    {QLatin1StringView("unit-type"), RefKind::UnitType},
    {QLatin1StringView("tech-type"), RefKind::TechType},
    {QLatin1StringView("resource-mode"), RefKind::ResourceMode},
    {QLatin1StringView("tech-modifier-mode"), RefKind::TechModifierMode},
    {QLatin1StringView("enable-mode"), RefKind::EnableMode},
    {QLatin1StringView("upgrade-mode"), RefKind::UpgradeMode},
};

const FixedTable *findFixedTable(const QString &key)
{
    for (const FixedTable &table : kFixedTables)
    {
        if (table.key == key)
            return &table;
    }
    return nullptr;
}

// "exact", "prefix" or "substring" for the best of `names`, or empty.
QString matchQuality(const QString &text, const QStringList &names)
{
    QString best;
    for (const QString &name : names)
    {
        if (name.compare(text, Qt::CaseInsensitive) == 0)
            return QStringLiteral("exact");
        if (name.startsWith(text, Qt::CaseInsensitive))
            best = QStringLiteral("prefix");
        else if (best.isEmpty() && name.contains(text, Qt::CaseInsensitive))
            best = QStringLiteral("substring");
    }
    return best;
}

int matchRank(const QString &match)
{
    if (match == QLatin1String("exact"))
        return 0;
    if (match == QLatin1String("prefix"))
        return 1;
    return 2;
}

ServiceError unknownKind(const QString &kind, const QString &message)
{
    ServiceError error = makeError(QStringLiteral("unknown_kind"), message);
    error.kind = kind;
    return error;
}

ServiceError unknownCiv(int civ)
{
    ServiceError error = makeError(QStringLiteral("unknown_entity"), QStringLiteral("No civ %1.").arg(civ));
    error.kind = civKind().key();
    error.id = civ;
    return error;
}

// Whether a read's civ is out of range. Per-civ kinds need one; a tech read
// may name one for availability; other kinds take none.
bool civOutOfRange(const EntityKind &kind, int civ, int civs)
{
    const bool used = kind.perCiv() || (&kind == &techKind() && civ != -1);
    return used && (civ < 0 || civ >= civs);
}

// `get` field patterns: a key, or a key with `*` wildcards.
QList<QRegularExpression> fieldPatterns(const QStringList &fields)
{
    QList<QRegularExpression> patterns;
    for (const QString &field : fields)
        patterns.append(QRegularExpression(
            QRegularExpression::wildcardToRegularExpression(field, QRegularExpression::NonPathWildcardConversion)));
    return patterns;
}

bool matchesAny(const QList<QRegularExpression> &patterns, const QString &key)
{
    return std::any_of(patterns.cbegin(), patterns.cend(),
                       [&key](const QRegularExpression &pattern) { return pattern.match(key).hasMatch(); });
}

// The first pattern that matches no key of `schema`, or an empty string.
// Effect schemas list command fields as `commandN.*` templates, so a pattern
// naming a numbered command is also tried with the number as `N`.
QString unmatchedPattern(const QStringList &fields, const QList<FieldSchema> &schema)
{
    static const QRegularExpression commandNumber(QStringLiteral("command\\d+"));
    for (const QString &field : fields)
    {
        QString templated = field;
        templated.replace(commandNumber, QStringLiteral("commandN"));
        const QList<QRegularExpression> patterns = fieldPatterns({field, templated});
        const bool known = std::any_of(schema.cbegin(), schema.cend(), [&patterns](const FieldSchema &row) {
            return matchesAny(patterns, row.key);
        });
        if (!known)
            return field;
    }
    return {};
}

// Whether `session` has entity `id`: a filled unit slot, or any other ID in
// range. A tech the civ can't research is still there.
bool present(const EntityKind &kind, const Session &session, int civ, int id)
{
    if (kind.perCiv())
        return kind.isActive(session, civ, id);
    return id >= 0 && id < kind.count(session, civ);
}

// Stored values compare exactly, except that two NaNs are the same value.
bool sameValue(const QVariant &a, const QVariant &b)
{
    if (a.typeId() == QMetaType::Float && b.typeId() == QMetaType::Float && std::isnan(a.toFloat())
        && std::isnan(b.toFloat()))
        return true;
    return a == b;
}

// One entity's difference, before names and labels.
struct EntityDiff
{
    DiffStatus status = DiffStatus::Changed;
    QList<DiffField> fields;
    bool unlisted = false;
};

bool sameDiff(const EntityDiff &a, const EntityDiff &b)
{
    if (a.status != b.status || a.unlisted != b.unlisted || a.fields.size() != b.fields.size())
        return false;
    for (qsizetype i = 0; i < a.fields.size(); ++i)
    {
        const DiffField &x = a.fields.at(i);
        const DiffField &y = b.fields.at(i);
        if (x.key != y.key || !sameValue(x.modValue, y.modValue) || !sameValue(x.gameValue, y.gameValue))
            return false;
    }
    return true;
}

// The fields whose values differ, in the game's field order, then the fields
// only the mod has. Empty `patterns` keep every key.
QList<DiffField> fieldDifferences(const QList<FieldValue> &mod, const QList<FieldValue> &game,
                                  const QList<QRegularExpression> &patterns)
{
    QHash<QString, qsizetype> modIndex;
    for (qsizetype i = 0; i < mod.size(); ++i)
        modIndex.insert(mod.at(i).key, i);

    QList<DiffField> fields;
    const auto add = [&](const FieldValue &field, const QVariant &modValue, const QVariant &gameValue) {
        if (sameValue(modValue, gameValue) || (!patterns.isEmpty() && !matchesAny(patterns, field.key)))
            return;
        DiffField row;
        row.key = field.key;
        row.labelKind = field.labelKind;
        row.stringId = field.stringId;
        row.modValue = modValue;
        row.gameValue = gameValue;
        fields.append(row);
    };
    QSet<QString> gameKeys;
    for (const FieldValue &field : game)
    {
        gameKeys.insert(field.key);
        const auto found = modIndex.constFind(field.key);
        add(field, found == modIndex.cend() ? QVariant() : mod.at(*found).value, field.value);
    }
    for (const FieldValue &field : mod)
    {
        if (!gameKeys.contains(field.key))
            add(field, field.value, QVariant());
    }
    return fields;
}

// How entity `id` differs between the two files, or nothing when it doesn't.
// `version` is the genie version both sides are written as before their bytes
// are compared; equal bytes skip the field comparison.
std::optional<EntityDiff> compareEntity(const EntityKind &kind, const Session &mod, const Session &game, int civ,
                                        int id, int version, const QList<QRegularExpression> &patterns)
{
    const bool inMod = present(kind, mod, civ, id);
    const bool inGame = present(kind, game, civ, id);
    if (!inMod && !inGame)
        return std::nullopt;
    EntityDiff diff;
    if (!inMod || !inGame)
    {
        diff.status = inMod ? DiffStatus::OnlyInMod : DiffStatus::OnlyInGame;
        return diff;
    }
    if (kind.rawData(mod, civ, id, version, false) == kind.rawData(game, civ, id, version, false))
        return std::nullopt;
    diff.fields = fieldDifferences(kind.values(mod, civ, id), kind.values(game, civ, id), patterns);
    if (patterns.isEmpty())
        diff.unlisted = kind.rawData(mod, civ, id, version, true) != kind.rawData(game, civ, id, version, true);
    // Bytes no field reads, such as effect parameters a command type ignores.
    if (diff.fields.isEmpty() && !diff.unlisted)
        return std::nullopt;
    return diff;
}

} // namespace

QStringList lookupTables()
{
    QStringList tables;
    for (const EntityKind *kind : entityKinds())
        tables.append(kind->key());
    for (const FixedTable &table : kFixedTables)
        tables.append(QString(table.key));
    return tables;
}

DataSource resolveSource(DataSource source)
{
    if (source.gameDir.isEmpty())
        source.gameDir = qEnvironmentVariable("NEWAGE_GAME");
    if (source.dataset.isEmpty())
        source.dataset = qEnvironmentVariable("NEWAGE_DATASET");
    if (source.mod.isEmpty())
        source.mod = qEnvironmentVariable("NEWAGE_MOD");
    if (source.modsFolder.isEmpty())
        source.modsFolder = qEnvironmentVariable("NEWAGE_MODS_FOLDER");
    if (source.locale.isEmpty())
        source.locale = QStringLiteral("en");
    return source;
}

ModsResult listMods(const DataSource &source)
{
    ModsResult result;
    if (!source.datPath.isEmpty())
    {
        result.error = makeError(QStringLiteral("mods_unsupported"), QStringLiteral("A loose data file has no mods."));
        return result;
    }
    DataSource in = source;
    if (in.locale.isEmpty())
        in.locale = QStringLiteral("en");
    if (const std::optional<ServiceError> error = chooseDataset(in, result.dataset))
    {
        result.error = *error;
        return result;
    }
    if (!supportsMods(result.dataset))
    {
        result.error = makeError(QStringLiteral("mods_unsupported"), QStringLiteral("This data set has no mods."));
        return result;
    }

    QStringList folders;
    for (const QString &folder : modsFolders(result.dataset))
        folders.append(QDir::cleanPath(folder));
    if (!in.modsFolder.isEmpty())
    {
        const QString chosen = QDir::cleanPath(QFileInfo(in.modsFolder).absoluteFilePath());
        folders.removeAll(chosen);
        folders.prepend(chosen);
    }
    if (!folders.isEmpty())
        result.modsFolder = folders.takeFirst();
    result.otherModsFolders = folders;
    for (const Mod &mod : findMods(result.modsFolder))
        result.mods.append({mod, QFileInfo::exists(modDatPath(result.dataset, mod.dir))});
    result.ok = true;
    return result;
}

CreateModResult createModForSource(const DataSource &source, const ModInfo &info, const QString &from)
{
    CreateModResult result;
    if (!source.datPath.isEmpty())
    {
        result.error = makeError(QStringLiteral("mods_unsupported"), QStringLiteral("A loose data file has no mods."));
        return result;
    }
    DataSource in = source;
    if (in.locale.isEmpty())
        in.locale = QStringLiteral("en");
    GameDataset dataset;
    if (const std::optional<ServiceError> error = chooseDataset(in, dataset))
    {
        result.error = *error;
        return result;
    }
    if (!supportsMods(dataset))
    {
        result.error = makeError(QStringLiteral("mods_unsupported"), QStringLiteral("This data set has no mods."));
        return result;
    }

    QString folder = in.modsFolder;
    if (folder.isEmpty())
    {
        const QStringList folders = modsFolders(dataset);
        if (!folders.isEmpty())
            folder = folders.first();
    }
    if (folder.isEmpty())
    {
        result.error = makeError(QStringLiteral("usage"), QStringLiteral("No mods folder found; pass --mods-folder."));
        return result;
    }

    // Resolve the source mod first, so a bad `from` creates nothing.
    QString sourceDat;
    if (!from.isEmpty() && from != QLatin1String("game"))
    {
        if (!from.startsWith(QLatin1String("mod:")))
        {
            result.error = makeError(QStringLiteral("usage"), QStringLiteral("--from is \"game\" or \"mod:NAME\"."));
            return result;
        }
        const QString name = from.mid(4);
        const QString dir = resolveModDir(dataset, name, folder);
        if (dir.isEmpty())
        {
            result.error = makeError(QStringLiteral("mod_not_found"), QStringLiteral("No mod named \"%1\".").arg(name));
            return result;
        }
        const QString path = modDatPath(dataset, dir);
        if (QFileInfo::exists(path))
            sourceDat = path;
    }

    if (const QString problem = checkModTitle(folder, info.title); !problem.isEmpty())
    {
        result.error = makeError(QStringLiteral("bad_value"), problem);
        result.error.key = QStringLiteral("title");
        result.error.value = info.title;
        return result;
    }
    QString reason;
    const QString dir = createMod(folder, info, &reason);
    if (dir.isEmpty())
    {
        result.error = makeError(QStringLiteral("save_failed"), reason);
        return result;
    }
    result.datPath = modDatPath(dataset, dir);
    if (!sourceDat.isEmpty())
    {
        if (isGameDataFile(dataset, result.datPath)
            || !QDir().mkpath(QFileInfo(result.datPath).absolutePath()) || !QFile::copy(sourceDat, result.datPath))
        {
            QDir(dir).removeRecursively();
            result.error = makeError(QStringLiteral("save_failed"),
                                     QStringLiteral("Couldn't copy the data from \"%1\".").arg(from.mid(4)));
            return result;
        }
        result.copiedDat = true;
    }
    result.dir = dir;
    result.ok = true;
    return result;
}

void DataService::close()
{
    session_.close();
    gameDataset_ = {};
    modDir_.clear();
    savePath_.clear();
    warnings_.clear();
    loose_ = false;
    sourceFingerprint_.clear();
    saveExistedAtOpen_ = false;
}

OpenResult DataService::open(const DataSource &source)
{
    close();
    DataSource in = source;
    if (in.locale.isEmpty())
        in.locale = QStringLiteral("en");

    const auto fail = [&](const QString &code, const QString &message) {
        close();
        OpenResult result;
        result.error = makeError(code, message);
        return result;
    };

    if (!in.datPath.isEmpty())
    {
        if (!in.mod.isEmpty())
            return fail(QStringLiteral("mods_unsupported"), QStringLiteral("A loose data file is read-only."));
        const VersionProfile *profile = findVersionProfile(in.versionKey);
        if (!profile)
        {
            const QString message = in.versionKey.isEmpty()
                                        ? QStringLiteral("A loose data file needs a version.")
                                        : QStringLiteral("Unknown game version \"%1\".").arg(in.versionKey);
            return fail(QStringLiteral("load_failed"), message);
        }
        QString error;
        if (!session_.open(in.datPath, *profile, &error))
            return fail(QStringLiteral("load_failed"), error);
        loose_ = true;
        OpenResult result;
        result.ok = true;
        return result;
    }

    if (const std::optional<ServiceError> error = chooseDataset(in, gameDataset_))
        return fail(error->code, error->message);

    GameDataset opened = gameDataset_;
    bool selectedModExists = false;
    if (!in.mod.isEmpty())
    {
        if (!supportsMods(gameDataset_))
            return fail(QStringLiteral("mods_unsupported"), QStringLiteral("This data set has no mods."));
        const QString dir = resolveModDir(gameDataset_, in.mod, in.modsFolder);
        if (dir.isEmpty())
            return fail(QStringLiteral("mod_not_found"), QStringLiteral("No mod named \"%1\".").arg(in.mod));
        const QString path = modDatPath(gameDataset_, dir);
        if (isGameDataFile(gameDataset_, path))
            return fail(QStringLiteral("game_data_protected"), QStringLiteral("Refusing to write the game's data file."));
        modDir_ = dir;
        savePath_ = path;
        opened = modDataset(gameDataset_, modDir_, in.locale);
        selectedModExists = QFileInfo::exists(opened.datPath);
        if (!selectedModExists)
            opened.datPath = gameDataset_.datPath;
    }

    const bool beforeSaveExists = !savePath_.isEmpty() && QFileInfo::exists(savePath_);
    if (!modDir_.isEmpty() && beforeSaveExists != selectedModExists)
        return fail(QStringLiteral("conflict"), QStringLiteral("The mod data changed while it was opening."));
    const QByteArray beforeOpen = modDir_.isEmpty() ? QByteArray() : fileFingerprint(opened.datPath);
    if (!modDir_.isEmpty() && beforeOpen.isEmpty())
        return fail(QStringLiteral("load_failed"), QStringLiteral("Couldn't read the source data file."));
    QString error;
    if (!session_.open(opened, &error, &warnings_))
        return fail(QStringLiteral("load_failed"), error);
    if (!modDir_.isEmpty())
    {
        sourceFingerprint_ = fileFingerprint(session_.datPath());
        if (sourceFingerprint_ != beforeOpen)
            return fail(QStringLiteral("conflict"), QStringLiteral("The source data changed while it was loading."));
        if (QFileInfo::exists(savePath_) != beforeSaveExists)
            return fail(QStringLiteral("conflict"), QStringLiteral("The mod data changed while it was loading."));
    }
    saveExistedAtOpen_ = beforeSaveExists;

    OpenResult result;
    result.ok = true;
    result.warnings = warnings_;
    return result;
}

LookupResult DataService::lookup(const QString &table, const QString &text, int civ) const
{
    LookupResult result;
    const EntityKind *kind = findEntityKind(table);
    const FixedTable *fixed = findFixedTable(table);
    if (!kind && !fixed)
    {
        result.error = unknownKind(table, QStringLiteral("Unknown lookup table \"%1\".").arg(table));
        return result;
    }
    if (kind && kind->perCiv() && (civ < 0 || civ >= civKind().count(session_, 0)))
    {
        result.error = unknownCiv(civ);
        return result;
    }

    QList<LookupMatch> rows;
    if (kind)
    {
        const int count = kind->count(session_, civ);
        for (int id = 0; id < count; ++id)
        {
            if (kind->perCiv() && !kind->isActive(session_, civ, id))
                continue;
            LookupMatch row;
            row.id = id;
            row.name = kind->name(session_, civ, id);
            row.internalName = kind->internalName(session_, civ, id);
            if (kind == &techKind())
                row.ownerCiv = session_.dat()->Techs[id].Civ;
            rows.append(row);
        }
    }
    else
    {
        for (int id : fixedRefIds(session_.gameVersion(), fixed->kind))
            rows.append({id, refName(session_, fixed->kind, id, -1), {}, {}, {}});
    }

    const QString query = text.trimmed();
    if (query.isEmpty())
    {
        result.ok = true;
        result.matches = rows;
        return result;
    }
    for (LookupMatch &row : rows)
    {
        // "(unnamed)" is the label of an entity with neither name, not a name.
        QStringList candidates;
        const QString internal = row.internalName.value_or(QString());
        if (!internal.isEmpty())
            candidates.append(internal);
        if (!internal.isEmpty() || row.name != QLatin1String("(unnamed)"))
            candidates.append(row.name);
        row.match = matchQuality(query, candidates);
        if (!row.match.isEmpty())
            result.matches.append(row);
    }
    std::stable_sort(result.matches.begin(), result.matches.end(), [](const LookupMatch &a, const LookupMatch &b) {
        return matchRank(a.match) < matchRank(b.match);
    });
    result.ok = true;
    return result;
}

ListResult DataService::list(const ListQuery &query) const
{
    ListResult result;
    const EntityKind *kind = findEntityKind(query.kind);
    if (!kind)
    {
        result.error = unknownKind(query.kind, QStringLiteral("Unknown kind \"%1\".").arg(query.kind));
        return result;
    }
    const int civs = civKind().count(session_, 0);
    const bool isTech = kind == &techKind();
    if (civOutOfRange(*kind, query.civ, civs))
    {
        result.error = unknownCiv(query.civ);
        return result;
    }
    if (query.ownerCiv && (*query.ownerCiv < -1 || *query.ownerCiv >= civs))
    {
        result.error = unknownCiv(*query.ownerCiv);
        return result;
    }

    // Tech availability is computed once; isActive would recompute the civ's
    // tech tree for every tech.
    QList<TechAvailability> availability;
    if (isTech && query.civ >= 0)
        availability = techAvailability(session_, query.civ);

    const int count = kind->count(session_, query.civ);
    for (int id = 0; id < count; ++id)
    {
        ListRow row;
        row.id = id;
        if (isTech)
        {
            row.ownerCiv = session_.dat()->Techs[id].Civ;
            if (query.ownerCiv && *row.ownerCiv != *query.ownerCiv)
                continue;
            if (!availability.isEmpty())
                row.active = availability.at(id) == TechAvailability::Available;
        }
        else if (kind->perCiv())
        {
            row.active = kind->isActive(session_, query.civ, id);
        }
        if (row.active && !*row.active && !query.all)
            continue;

        ++result.total;
        if (result.total <= query.offset || (query.limit >= 0 && result.rows.size() >= query.limit))
            continue;
        row.name = kind->name(session_, query.civ, id);
        row.internalName = kind->internalName(session_, query.civ, id);
        result.rows.append(row);
    }
    result.ok = true;
    return result;
}

GetResult DataService::get(const GetQuery &query) const
{
    GetResult result;
    const EntityKind *kind = findEntityKind(query.kind);
    if (!kind)
    {
        result.error = unknownKind(query.kind, QStringLiteral("Unknown kind \"%1\".").arg(query.kind));
        return result;
    }
    if (civOutOfRange(*kind, query.civ, civKind().count(session_, 0)))
    {
        result.error = unknownCiv(query.civ);
        return result;
    }
    if (const QString field = unmatchedPattern(query.fields, kind->schema(session_)); !field.isEmpty())
    {
        result.error = makeError(QStringLiteral("unknown_field"),
                                 QStringLiteral("No %1 field matches \"%2\".").arg(query.kind, field));
        result.error.kind = query.kind;
        result.error.key = field;
        return result;
    }

    const bool isTech = kind == &techKind();
    const int labelCiv = query.civ >= 0 ? query.civ : 0;
    const int count = kind->count(session_, query.civ);
    const QList<QRegularExpression> patterns = fieldPatterns(query.fields);
    for (const int id : query.ids)
    {
        const auto entityError = [&](const QString &code, const QString &message) {
            result.error = makeError(code, message);
            result.error.kind = query.kind;
            result.error.id = id;
            result.error.civ = kind->perCiv() ? query.civ : -1;
            result.items.clear();
            return result;
        };
        if (id < 0 || id >= count)
            return entityError(QStringLiteral("unknown_entity"), QStringLiteral("No %1 %2.").arg(query.kind).arg(id));
        if (kind->perCiv() && !kind->isActive(session_, query.civ, id))
            return entityError(QStringLiteral("inactive_entity"),
                               QStringLiteral("%1 %2 is an empty slot in civ %3.").arg(query.kind).arg(id).arg(query.civ));

        GetItem item;
        item.id = id;
        item.name = kind->name(session_, query.civ, id);
        item.internalName = kind->internalName(session_, query.civ, id);
        if (isTech && query.civ >= 0)
            item.active = kind->isActive(session_, query.civ, id);
        for (const FieldValue &field : kind->fields(session_, labelCiv, id))
        {
            if (patterns.isEmpty() || matchesAny(patterns, field.key))
                item.fields.append(field);
        }
        result.items.append(item);
    }
    result.ok = true;
    return result;
}

ServiceError DataService::writeGuard() const
{
    if (!session_.isOpen())
        return makeError(QStringLiteral("load_failed"), QStringLiteral("No data is open."));
    if (loose_ || !supportsMods(gameDataset_))
        return makeError(QStringLiteral("mods_unsupported"), QStringLiteral("This data set has no mods."));
    if (modDir_.isEmpty())
        return makeError(QStringLiteral("mod_required"), QStringLiteral("Writing needs a mod."));
    if (savePath_.isEmpty() || isGameDataFile(gameDataset_, savePath_))
        return makeError(QStringLiteral("game_data_protected"), QStringLiteral("Refusing to write the game's data file."));
    return {};
}

ApplyResult DataService::apply(const QList<FieldEdit> &edits, bool dryRun)
{
    ApplyResult result;
    if (const ServiceError blocked = writeGuard(); !blocked.code.isEmpty())
    {
        result.error = blocked;
        return result;
    }

    // Edits are checked and stored in order, so one can depend on an earlier
    // one (a new command's slots on its type). Each entity is copied before
    // its first change; a failure or a dry run puts every copy back.
    const bool wasModified = session_.isModified();
    QList<std::function<void()>> restores;
    QSet<QString> backedUp;
    const auto restore = [&] {
        for (auto it = restores.rbegin(); it != restores.rend(); ++it)
            (*it)();
        session_.setModified(wasModified);
    };
    const auto fail = [&](const ServiceError &error) {
        restore();
        result.changes.clear();
        result.unchanged.clear();
        result.error = error;
        return result;
    };

    for (const FieldEdit &edit : edits)
    {
        EntityKind *kind = findEntityKind(edit.kind);
        if (!kind)
            return fail(editError(edit, QStringLiteral("unknown_kind"),
                                  QStringLiteral("Unknown kind \"%1\".").arg(edit.kind)));
        const int civ = kind->perCiv() ? edit.civ : -1;
        const SetResult checked = kind->set(session_, edit.civ, edit.id, edit.key, edit.value, false);
        if (!checked.ok)
            return fail(editError(edit, checked.code, checked.message));
        if (edit.expect && !sameStored(checked.oldValue, *edit.expect))
        {
            return fail(editError(edit, QStringLiteral("conflict"),
                                  QStringLiteral("%1 is %2, not %3.")
                                      .arg(edit.key, checked.oldValue.toString(), edit.expect->toString())));
        }

        FieldChange change{edit.kind, edit.id, civ, edit.key, checked.oldValue, checked.newValue};
        if (!checked.changed)
        {
            result.unchanged.append(change);
            continue;
        }
        const QString entity = QStringLiteral("%1/%2/%3").arg(edit.kind).arg(civ).arg(edit.id);
        if (!backedUp.contains(entity))
        {
            backedUp.insert(entity);
            if (std::function<void()> undo = kind->backup(session_, edit.civ, edit.id))
                restores.append(std::move(undo));
        }
        const SetResult stored = kind->set(session_, edit.civ, edit.id, edit.key, edit.value, true);
        if (!stored.ok)
            return fail(editError(edit, stored.code, stored.message));
        change.newValue = stored.newValue;
        result.changes.append(change);
    }

    if (dryRun || result.changes.isEmpty())
    {
        restore();
        result.ok = true;
        return result;
    }

    const QString directory = QFileInfo(savePath_).absolutePath();
    if (!QDir().mkpath(directory))
    {
        return fail(makeError(QStringLiteral("save_failed"),
                              QStringLiteral("Couldn't create %1.").arg(QDir::toNativeSeparators(directory))));
    }
    if (isGameDataFile(gameDataset_, savePath_))
        return fail(makeError(QStringLiteral("game_data_protected"), QStringLiteral("Refusing to write the game's data file.")));

    QString saveError;
    if (!session_.saveAs(savePath_, &saveError))
        return fail(makeError(QStringLiteral("save_failed"), saveError));

    sourceFingerprint_ = fileFingerprint(savePath_);
    saveExistedAtOpen_ = true;
    result.ok = true;
    result.saved = QDir::fromNativeSeparators(QDir::cleanPath(savePath_));
    return result;
}

CreateTechResult DataService::createTech(const CreateTechSpec &spec, bool dryRun)
{
    CreateTechResult result;
    const auto fail = [&](const QString &code, const QString &message) {
        result.error = makeError(code, message);
        return result;
    };
    if (const ServiceError blocked = writeGuard(); !blocked.code.isEmpty())
    {
        result.error = blocked;
        return result;
    }
    genie::DatFile &dat = *session_.dat();
    const int techCount = static_cast<int>(dat.Techs.size());
    const int effectCount = static_cast<int>(dat.Effects.size());
    if (spec.expectedTechCount && *spec.expectedTechCount != techCount)
        return fail(QStringLiteral("conflict"), QStringLiteral("Tech count is %1, not %2.").arg(techCount).arg(*spec.expectedTechCount));
    if (spec.expectedEffectCount && *spec.expectedEffectCount != effectCount)
        return fail(QStringLiteral("conflict"), QStringLiteral("Effect count is %1, not %2.").arg(effectCount).arg(*spec.expectedEffectCount));
    const QString fileVersion = QString::fromLatin1(dat.FileVersion.c_str());
    if (!spec.expectedFileVersion.isEmpty() && spec.expectedFileVersion != fileVersion)
        return fail(QStringLiteral("conflict"), QStringLiteral("File version is %1, not %2.").arg(fileVersion, spec.expectedFileVersion));
    result.sourceFingerprint = QString::fromLatin1(sourceFingerprint_.toHex());
    if (!spec.expectedSourceFingerprint.isEmpty() && spec.expectedSourceFingerprint != result.sourceFingerprint)
        return fail(QStringLiteral("conflict"), QStringLiteral("The source fingerprint does not match."));
    if (techCount >= std::numeric_limits<int16_t>::max())
        return fail(QStringLiteral("out_of_range"), QStringLiteral("The serialized 16-bit tech count is full."));
    if (effectCount > std::numeric_limits<int16_t>::max())
        return fail(QStringLiteral("out_of_range"), QStringLiteral("The new effect ID cannot fit in Tech::EffectID."));
    if (spec.civ < 0 || spec.civ >= static_cast<int>(dat.Civs.size()) || spec.civ > std::numeric_limits<int16_t>::max())
        return fail(QStringLiteral("unknown_entity"), QStringLiteral("No civ %1.").arg(spec.civ));

    const auto validName = [](const QString &name, int maxBytes) {
        const QByteArray latin1 = name.toLatin1();
        return !name.isEmpty() && name == name.trimmed() && QString::fromLatin1(latin1) == name
               && latin1.size() <= maxBytes;
    };
    if (!validName(spec.internalName, 255) || !validName(spec.effectInternalName, 30))
        return fail(QStringLiteral("bad_value"), QStringLiteral("Internal names must be non-empty Latin-1 text without surrounding spaces (tech: 255 bytes; effect: 30 bytes)."));
    for (const genie::Tech &existing : dat.Techs)
    {
        if (QString::fromLatin1(existing.Name).compare(spec.internalName, Qt::CaseInsensitive) == 0)
            return fail(QStringLiteral("bad_value"), QStringLiteral("A tech already has this internal name."));
    }
    for (const genie::Effect &existing : dat.Effects)
    {
        if (QString::fromLatin1(existing.Name).compare(spec.effectInternalName, Qt::CaseInsensitive) == 0)
            return fail(QStringLiteral("bad_value"), QStringLiteral("An effect already has this internal name."));
    }
    genie::Tech newTechTemplate;
    newTechTemplate.setGameVersion(session_.gameVersion());
    if (static_cast<std::size_t>(spec.prerequisites.size()) > newTechTemplate.RequiredTechs.size())
        return fail(QStringLiteral("out_of_range"), QStringLiteral("Too many prerequisites for this game version."));
    QSet<int> prerequisites;
    for (int id : spec.prerequisites)
    {
        if (id < 0 || id >= techCount)
            return fail(QStringLiteral("unknown_entity"), QStringLiteral("No prerequisite tech %1.").arg(id));
        if (prerequisites.contains(id))
            return fail(QStringLiteral("bad_value"), QStringLiteral("Prerequisite tech %1 is repeated.").arg(id));
        prerequisites.insert(id);
    }
    if (spec.commands.isEmpty() || spec.commands.size() > std::numeric_limits<int16_t>::max())
        return fail(QStringLiteral("out_of_range"), QStringLiteral("An effect needs 1 to 32767 commands."));
    const QList<int> classes = fixedRefIds(session_.gameVersion(), RefKind::UnitClass);
    const QList<int> attributes = fixedRefIds(session_.gameVersion(), RefKind::Attribute);
    for (int index = 0; index < spec.commands.size(); ++index)
    {
        const AttributeCommand &command = spec.commands[index];
        const QString prefix = QStringLiteral("Command %1: ").arg(index + 1);
        if (command.type != 0 && command.type != 4 && command.type != 5)
            return fail(QStringLiteral("bad_value"), prefix + QStringLiteral("only attribute effect types 0, 4, and 5 are supported."));
        if (command.unit < -1 || command.unitClass < -1 || command.unit > std::numeric_limits<int16_t>::max()
            || command.unitClass > std::numeric_limits<int16_t>::max())
            return fail(QStringLiteral("bad_value"), prefix + QStringLiteral("unit and class must be -1 or valid IDs."));
        if (command.unit < 0 && command.unitClass < 0)
            return fail(QStringLiteral("bad_value"), prefix + QStringLiteral("a unit or class is required."));
        if (command.unit >= 0 && !unitKind().isActive(session_, spec.civ, command.unit))
            return fail(QStringLiteral("unknown_entity"), prefix + QStringLiteral("unit %1 does not exist for civ %2.").arg(command.unit).arg(spec.civ));
        if (command.unitClass >= 0 && !classes.contains(command.unitClass))
            return fail(QStringLiteral("unknown_entity"), prefix + QStringLiteral("class %1 does not exist.").arg(command.unitClass));
        if (!attributes.contains(command.attribute))
            return fail(QStringLiteral("unknown_entity"), prefix + QStringLiteral("attribute %1 does not exist.").arg(command.attribute));
        if (!std::isfinite(command.amount) || !std::isfinite(static_cast<float>(command.amount)))
            return fail(QStringLiteral("out_of_range"), prefix + QStringLiteral("amount is not a finite float."));
    }
    const int treeId = dat.Civs[spec.civ].TechTreeID;
    if (treeId >= 0 && treeId < effectCount)
    {
        for (const genie::EffectCommand &command : dat.Effects[treeId].EffectCommands)
        {
            if (command.Type == 102 && command.D == static_cast<float>(techCount))
                return fail(QStringLiteral("bad_value"), QStringLiteral("The civ tech tree disables the new tech ID %1.").arg(techCount));
        }
    }

    const bool wasModified = session_.isModified();
    const auto restore = [&] {
        dat.Techs.resize(static_cast<std::size_t>(techCount));
        dat.Effects.resize(static_cast<std::size_t>(effectCount));
        session_.setModified(wasModified);
    };
    dat.Effects.emplace_back();
    genie::Effect &effect = dat.Effects.back();
    effect.Name = spec.effectInternalName.toLatin1().toStdString();
    effect.EffectCommands.reserve(static_cast<std::size_t>(spec.commands.size()));
    for (const AttributeCommand &input : spec.commands)
    {
        genie::EffectCommand command;
        command.Type = static_cast<uint8_t>(input.type);
        command.A = static_cast<int16_t>(input.unit);
        command.B = static_cast<int16_t>(input.unitClass);
        command.C = static_cast<int16_t>(input.attribute);
        command.D = static_cast<float>(input.amount);
        effect.EffectCommands.push_back(command);
    }
    effect.setGameVersion(session_.gameVersion());

    dat.Techs.emplace_back();
    genie::Tech &tech = dat.Techs.back();
    tech.setGameVersion(session_.gameVersion());
    tech.Name = spec.internalName.toLatin1().toStdString();
    tech.Civ = static_cast<int16_t>(spec.civ);
    tech.EffectID = static_cast<int16_t>(effectCount);
    tech.LanguageDLLName = -1;
    tech.LanguageDLLDescription = -1;
    tech.LanguageDLLHelp = -1;
    tech.LanguageDLLTechTree = -1;
    tech.RequiredTechCount = static_cast<int16_t>(spec.prerequisites.size());
    for (int index = 0; index < spec.prerequisites.size(); ++index)
        tech.RequiredTechs[static_cast<std::size_t>(index)] = static_cast<int16_t>(spec.prerequisites[index]);
    session_.setModified(true);
    result.techId = techCount;
    result.effectId = effectCount;
    result.techFields = techKind().fields(session_, spec.civ, techCount);
    result.effectFields = effectKind().fields(session_, spec.civ, effectCount);
    if (dryRun)
    {
        restore();
        result.ok = true;
        return result;
    }

    const QString directory = QFileInfo(savePath_).absolutePath();
    if (!QDir().mkpath(directory))
    {
        restore();
        return fail(QStringLiteral("save_failed"), QStringLiteral("Couldn't create %1.").arg(QDir::toNativeSeparators(directory)));
    }
    QLockFile lock(savePath_ + QStringLiteral(".lock"));
    if (!lock.tryLock())
    {
        restore();
        return fail(QStringLiteral("conflict"), QStringLiteral("Another process holds the mod data lock."));
    }
    if (sourceFingerprint_.isEmpty() || fileFingerprint(session_.datPath()) != sourceFingerprint_
        || QFileInfo::exists(savePath_) != saveExistedAtOpen_)
    {
        restore();
        return fail(QStringLiteral("conflict"), QStringLiteral("The source data changed since it was opened."));
    }
    QString saveError;
    if (!session_.saveAs(savePath_, &saveError))
    {
        restore();
        return fail(QStringLiteral("save_failed"), saveError);
    }
    saveExistedAtOpen_ = true;
    sourceFingerprint_ = fileFingerprint(savePath_);
    result.saved = QDir::fromNativeSeparators(QDir::cleanPath(savePath_));
    result.ok = true;
    return result;
}

DiffResult DataService::diff(const DiffQuery &query) const
{
    DiffResult result;
    if (!session_.isOpen())
    {
        result.error = makeError(QStringLiteral("load_failed"), QStringLiteral("No data is open."));
        return result;
    }
    if (loose_ || !supportsMods(gameDataset_))
    {
        result.error = makeError(QStringLiteral("mods_unsupported"), QStringLiteral("This data set has no mods."));
        return result;
    }
    if (modDir_.isEmpty())
    {
        result.error = makeError(QStringLiteral("mod_required"), QStringLiteral("diff compares a mod with the game."));
        return result;
    }
    const EntityKind *only = nullptr;
    if (!query.kind.isEmpty())
    {
        only = findEntityKind(query.kind);
        if (!only)
        {
            result.error = unknownKind(query.kind, QStringLiteral("Unknown kind \"%1\".").arg(query.kind));
            return result;
        }
    }
    if (!query.fields.isEmpty())
    {
        if (!only)
        {
            result.error = makeError(QStringLiteral("usage"), QStringLiteral("Field patterns need a kind."));
            return result;
        }
        if (const QString field = unmatchedPattern(query.fields, only->schema(session_)); !field.isEmpty())
        {
            result.error = makeError(QStringLiteral("unknown_field"),
                                     QStringLiteral("No %1 field matches \"%2\".").arg(query.kind, field));
            result.error.kind = query.kind;
            result.error.key = field;
            return result;
        }
    }
    const bool civUsed = query.civ >= 0 && (!only || only->perCiv());
    if (civUsed && query.civ >= civKind().count(session_, 0))
    {
        result.error = unknownCiv(query.civ);
        return result;
    }

    result.gameDat = gameDataset_.datPath;
    result.modDat = savePath_;
    result.modFileVersion = QString::fromLatin1(session_.dat()->FileVersion.c_str());
    result.hasDat = QFileInfo::exists(savePath_);
    if (!result.hasDat)
    {
        // The mod reads the game's data, so there is nothing to compare.
        result.gameFileVersion = result.modFileVersion;
        for (const EntityKind *kind : entityKinds())
            result.counts.append({kind->key(), kind->count(session_, 0), kind->count(session_, 0)});
        result.ok = true;
        return result;
    }

    Session game;
    QString error;
    QStringList gameWarnings;
    if (!game.open(gameDataset_, &error, &gameWarnings))
    {
        result.error = makeError(QStringLiteral("load_failed"), error);
        return result;
    }
    for (const QString &warning : gameWarnings)
    {
        if (!warnings_.contains(warning))
            result.warnings.append(warning);
    }
    result.gameFileVersion = QString::fromLatin1(game.dat()->FileVersion.c_str());
    for (const EntityKind *kind : entityKinds())
        result.counts.append({kind->key(), kind->count(session_, 0), kind->count(game, 0)});

    const int civs = std::min(civKind().count(session_, 0), civKind().count(game, 0));
    if (civUsed && query.civ >= civs)
    {
        result.error = unknownCiv(query.civ);
        return result;
    }
    QList<int> unitCivs;
    for (int civ = 0; civ < civs; ++civ)
    {
        if (!civUsed || civ == query.civ)
            unitCivs.append(civ);
    }

    // Both sides are written as the older version stores them, so a field
    // the newer version added doesn't make every entity differ.
    const int version = std::min(session_.gameVersion(), game.gameVersion());
    const QList<QRegularExpression> patterns = fieldPatterns(query.fields);
    QList<DiffItem> items;
    const auto addItem = [&items](const EntityKind &kind, int id, const QList<int> &itemCivs, const EntityDiff &diff) {
        DiffItem item;
        item.status = diff.status;
        item.kind = kind.key();
        item.id = id;
        item.civs = itemCivs;
        item.fields = diff.fields;
        item.unlisted = diff.unlisted;
        items.append(item);
    };
    for (const EntityKind *kind : entityKinds())
    {
        if (only && kind != only)
            continue;
        if (!kind->perCiv())
        {
            const int count = std::max(kind->count(session_, 0), kind->count(game, 0));
            for (int id = 0; id < count; ++id)
            {
                if (const std::optional<EntityDiff> diff = compareEntity(*kind, session_, game, 0, id, version, patterns))
                    addItem(*kind, id, {}, *diff);
            }
            continue;
        }

        int count = 0;
        for (const int civ : unitCivs)
            count = std::max({count, kind->count(session_, civ), kind->count(game, civ)});
        for (int id = 0; id < count; ++id)
        {
            // Civs with the same difference share one item.
            QList<std::pair<EntityDiff, QList<int>>> groups;
            for (const int civ : unitCivs)
            {
                const std::optional<EntityDiff> diff = compareEntity(*kind, session_, game, civ, id, version, patterns);
                if (!diff)
                    continue;
                const auto group = std::find_if(groups.begin(), groups.end(),
                                                [&diff](const auto &entry) { return sameDiff(entry.first, *diff); });
                if (group != groups.end())
                    group->second.append(civ);
                else
                    groups.append({*diff, {civ}});
            }
            for (const auto &[diff, groupCivs] : groups)
                addItem(*kind, id, groupCivs, diff);
        }
    }

    for (const DiffItem &item : items)
    {
        switch (item.status)
        {
        case DiffStatus::Changed: ++result.changed; break;
        case DiffStatus::OnlyInGame: ++result.onlyInGame; break;
        case DiffStatus::OnlyInMod: ++result.onlyInMod; break;
        }
    }
    result.total = static_cast<int>(items.size());
    result.items = items.mid(query.offset, query.limit);

    // Names and labels only for the page: they cost a lookup each.
    for (DiffItem &item : result.items)
    {
        const EntityKind &kind = *findEntityKind(item.kind);
        const int civ = item.civs.isEmpty() ? 0 : item.civs.first();
        // A mod's blank placeholder that the game has since filled is named
        // as the game names it.
        const bool gameNamed = item.status == DiffStatus::OnlyInGame
                               || (item.status == DiffStatus::Changed && kind.internalName(session_, civ, item.id).isEmpty());
        const Session &named = gameNamed ? game : session_;
        item.name = kind.name(named, civ, item.id);
        item.internalName = kind.internalName(named, civ, item.id);
        for (DiffField &field : item.fields)
        {
            if (field.labelKind != RefKind::None)
            {
                if (field.modValue.isValid())
                    field.modLabel = refName(session_, field.labelKind, field.modValue.toInt(), civ);
                if (field.gameValue.isValid())
                    field.gameLabel = refName(game, field.labelKind, field.gameValue.toInt(), civ);
            }
            if (field.stringId)
            {
                if (field.modValue.isValid())
                    field.modText = session_.names().text(field.modValue.toInt());
                if (field.gameValue.isValid())
                    field.gameText = game.names().text(field.gameValue.toInt());
            }
        }
    }
    result.ok = true;
    return result;
}

} // namespace newage
