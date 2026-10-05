#include "api/DataService.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "core/Mods.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
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

void DataService::close()
{
    session_.close();
    gameDataset_ = {};
    modDir_.clear();
    savePath_.clear();
    warnings_.clear();
    loose_ = false;
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
        if (!QFileInfo::exists(opened.datPath))
            opened.datPath = gameDataset_.datPath;
    }

    QString error;
    if (!session_.open(opened, &error, &warnings_))
        return fail(QStringLiteral("load_failed"), error);

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

    struct Planned
    {
        FieldEdit edit;
        EntityKind *kind = nullptr;
        QVariant oldValue;
        QVariant newValue;
        bool changed = false;
    };

    const auto toChange = [](const Planned &item) {
        FieldChange change;
        change.kind = item.edit.kind;
        change.id = item.edit.id;
        change.civ = item.kind->perCiv() ? item.edit.civ : -1;
        change.key = item.edit.key;
        change.oldValue = item.oldValue;
        change.newValue = item.newValue;
        return change;
    };

    QList<Planned> planned;
    for (const FieldEdit &edit : edits)
    {
        EntityKind *kind = findEntityKind(edit.kind);
        if (!kind)
        {
            result.error = editError(edit, QStringLiteral("unknown_kind"),
                                     QStringLiteral("Unknown kind \"%1\".").arg(edit.kind));
            return result;
        }
        const SetResult checked = kind->set(session_, edit.civ, edit.id, edit.key, edit.value, false);
        if (!checked.ok)
        {
            result.error = editError(edit, checked.code, checked.message);
            return result;
        }
        if (edit.expect && !sameStored(checked.oldValue, *edit.expect))
        {
            result.error = editError(edit, QStringLiteral("conflict"),
                                     QStringLiteral("%1 is %2, not %3.")
                                         .arg(edit.key, checked.oldValue.toString(), edit.expect->toString()));
            return result;
        }
        planned.append({edit, kind, checked.oldValue, checked.newValue, checked.changed});
    }

    if (dryRun)
    {
        result.ok = true;
        for (const Planned &item : planned)
        {
            if (item.changed)
                result.changes.append(toChange(item));
            else
                result.unchanged.append(toChange(item));
        }
        return result;
    }

    const bool wasModified = session_.isModified();
    QList<Planned> committed;
    const auto revert = [&] {
        for (auto it = committed.rbegin(); it != committed.rend(); ++it)
            it->kind->set(session_, it->edit.civ, it->edit.id, it->edit.key, it->oldValue, true);
        session_.setModified(wasModified);
    };

    for (const Planned &item : planned)
    {
        if (!item.changed)
        {
            result.unchanged.append(toChange(item));
            continue;
        }
        const SetResult stored = item.kind->set(session_, item.edit.civ, item.edit.id, item.edit.key, item.edit.value, true);
        if (!stored.ok)
        {
            revert();
            result.changes.clear();
            result.unchanged.clear();
            result.error = editError(item.edit, stored.code, stored.message);
            return result;
        }
        Planned done = item;
        done.oldValue = stored.oldValue;
        done.newValue = stored.newValue;
        committed.append(done);
        result.changes.append(toChange(done));
    }

    if (committed.isEmpty())
    {
        result.ok = true;
        return result;
    }

    const QString directory = QFileInfo(savePath_).absolutePath();
    if (!QDir().mkpath(directory))
    {
        revert();
        result.changes.clear();
        result.unchanged.clear();
        result.error = makeError(QStringLiteral("save_failed"),
                                 QStringLiteral("Couldn't create %1.").arg(QDir::toNativeSeparators(directory)));
        return result;
    }
    if (isGameDataFile(gameDataset_, savePath_))
    {
        revert();
        result.changes.clear();
        result.unchanged.clear();
        result.error = makeError(QStringLiteral("game_data_protected"),
                                 QStringLiteral("Refusing to write the game's data file."));
        return result;
    }

    QString saveError;
    if (!session_.saveAs(savePath_, &saveError))
    {
        revert();
        result.changes.clear();
        result.unchanged.clear();
        result.error = makeError(QStringLiteral("save_failed"), saveError);
        return result;
    }

    result.ok = true;
    result.saved = QDir::fromNativeSeparators(QDir::cleanPath(savePath_));
    return result;
}

} // namespace newage
