#include "api/RequestHandler.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include "genie/dat/DatFile.h"

namespace newage {

namespace {

ServiceError makeError(const QString &code, const QString &message)
{
    ServiceError error;
    error.code = code;
    error.message = message;
    return error;
}

QString jsonPath(const QString &path)
{
    if (path.isEmpty())
        return {};
    return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

QJsonObject errorBody(const ServiceError &error)
{
    QJsonObject detail;
    detail.insert(QStringLiteral("code"), error.code);
    detail.insert(QStringLiteral("message"), error.message);
    if (!error.kind.isEmpty())
        detail.insert(QStringLiteral("kind"), error.kind);
    if (error.id >= 0)
        detail.insert(QStringLiteral("id"), error.id);
    if (error.civ >= 0)
        detail.insert(QStringLiteral("civ"), error.civ);
    if (!error.key.isEmpty())
        detail.insert(QStringLiteral("key"), error.key);
    if (error.value.isValid())
        detail.insert(QStringLiteral("value"), QJsonValue::fromVariant(error.value));

    QJsonObject body;
    body.insert(QStringLiteral("error"), detail);
    return body;
}

HandlerResult failed(const ServiceError &error, const QStringList &warnings = {})
{
    HandlerResult result;
    result.body = errorBody(error);
    result.warnings = warnings;
    result.exitCode = exitCodeFor(error.code);
    return result;
}

HandlerResult usage(const QString &message)
{
    return failed(makeError(QStringLiteral("usage"), message));
}

HandlerResult succeeded(const QJsonObject &body, const QStringList &warnings = {})
{
    HandlerResult result;
    result.body = body;
    result.warnings = warnings;
    return result;
}

// Units are counted in civ 0, the same copy the GUI's status line uses when
// it reads the first civ. Global kinds ignore the civ.
QJsonObject infoObject(const DataService &service, const DataSource &source)
{
    const Session &session = service.session();
    const GameDataset &dataset = service.dataset();

    QString version = dataset.versionKey;
    if (version.isEmpty())
        version = source.versionKey;

    QString datasetName = QFileInfo(session.datPath()).fileName();
    if (!dataset.datPath.isEmpty())
        datasetName = QFileInfo(dataset.datPath).fileName();

    QJsonObject counts;
    QJsonArray kinds;
    for (const EntityKind *kind : service.kinds())
    {
        kinds.append(kind->key());
        counts.insert(kind->key(), kind->count(session, 0));
    }

    QJsonObject body;
    body.insert(QStringLiteral("game"), jsonPath(dataset.gameDir));
    body.insert(QStringLiteral("dataset"), datasetName);
    body.insert(QStringLiteral("version"), version);
    body.insert(QStringLiteral("fileVersion"), QString::fromLatin1(session.dat()->FileVersion.c_str()));
    body.insert(QStringLiteral("mod"), jsonPath(service.modDir()));
    body.insert(QStringLiteral("dat"), jsonPath(session.datPath()));
    body.insert(QStringLiteral("readOnly"), service.savePath().isEmpty());
    body.insert(QStringLiteral("counts"), counts);
    body.insert(QStringLiteral("kinds"), kinds);
    return body;
}

QString labelKindName(RefKind kind)
{
    switch (kind)
    {
    case RefKind::None: return {};
    case RefKind::Tech: return QStringLiteral("tech");
    case RefKind::Resource: return QStringLiteral("resource");
    case RefKind::Civ: return QStringLiteral("civ");
    case RefKind::Effect: return QStringLiteral("effect");
    case RefKind::Unit: return QStringLiteral("unit");
    case RefKind::UnitClass: return QStringLiteral("unit-class");
    case RefKind::Attribute: return QStringLiteral("attribute");
    case RefKind::UnitType: return QStringLiteral("unit-type");
    case RefKind::TechType: return QStringLiteral("tech-type");
    case RefKind::EffectType: return QStringLiteral("effect-type");
    }
    return {};
}

QJsonObject schemaObject(const EntityKind &kind, const Session &session)
{
    QJsonArray fields;
    for (const FieldSchema &field : kind.schema(session))
    {
        QJsonObject item;
        item.insert(QStringLiteral("key"), field.key);
        item.insert(QStringLiteral("name"), field.name);
        item.insert(QStringLiteral("group"), field.group);
        item.insert(QStringLiteral("type"), field.type);
        item.insert(QStringLiteral("editable"), field.editable);
        if (field.conditional)
            item.insert(QStringLiteral("conditional"), true);
        if (field.minimum)
            item.insert(QStringLiteral("min"), *field.minimum);
        if (field.maximum)
            item.insert(QStringLiteral("max"), *field.maximum);
        if (field.labelKind != RefKind::None)
            item.insert(QStringLiteral("labelKind"), labelKindName(field.labelKind));
        fields.append(item);
    }
    QJsonObject body;
    body.insert(QStringLiteral("kind"), kind.key());
    body.insert(QStringLiteral("perCiv"), kind.perCiv());
    body.insert(QStringLiteral("fields"), fields);
    return body;
}

QJsonObject lookupObject(const QString &table, const QString &text, int civ, const LookupResult &lookup)
{
    QJsonArray matches;
    for (const LookupMatch &match : lookup.matches)
    {
        QJsonObject item;
        item.insert(QStringLiteral("id"), match.id);
        item.insert(QStringLiteral("name"), match.name);
        if (match.internalName)
            item.insert(QStringLiteral("internalName"), *match.internalName);
        if (match.ownerCiv)
            item.insert(QStringLiteral("ownerCiv"), *match.ownerCiv);
        if (!match.match.isEmpty())
            item.insert(QStringLiteral("match"), match.match);
        matches.append(item);
    }
    QJsonObject body;
    body.insert(QStringLiteral("table"), table);
    if (!text.isEmpty())
        body.insert(QStringLiteral("query"), text);
    if (civ >= 0)
        body.insert(QStringLiteral("civ"), civ);
    body.insert(QStringLiteral("matches"), matches);
    return body;
}

QJsonObject listObject(const ListQuery &query, const ListResult &list)
{
    QJsonArray items;
    for (const ListRow &row : list.rows)
    {
        QJsonObject item;
        item.insert(QStringLiteral("id"), row.id);
        item.insert(QStringLiteral("name"), row.name);
        item.insert(QStringLiteral("internalName"), row.internalName);
        if (row.ownerCiv)
            item.insert(QStringLiteral("ownerCiv"), *row.ownerCiv);
        // Without `all` every row is active, so the flag would say nothing.
        if (query.all && row.active)
            item.insert(QStringLiteral("active"), *row.active);
        items.append(item);
    }
    QJsonObject body;
    body.insert(QStringLiteral("kind"), query.kind);
    if (query.civ >= 0)
        body.insert(QStringLiteral("civ"), query.civ);
    if (query.ownerCiv)
        body.insert(QStringLiteral("ownerCiv"), *query.ownerCiv);
    if (query.all)
        body.insert(QStringLiteral("all"), true);
    body.insert(QStringLiteral("offset"), query.offset);
    if (query.limit >= 0)
        body.insert(QStringLiteral("limit"), query.limit);
    body.insert(QStringLiteral("total"), list.total);
    body.insert(QStringLiteral("items"), items);
    return body;
}

// A JSON number that is a whole int.
bool readInt(const QJsonValue &value, int &out)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (number != std::floor(number) || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max())
        return false;
    out = static_cast<int>(number);
    return true;
}

ServiceError unknownKind(const QString &kind, const QString &message)
{
    ServiceError error = makeError(QStringLiteral("unknown_kind"), message);
    error.kind = kind;
    return error;
}

// Reads the civ of a list or get request: required for per-civ kinds,
// optional for techs (it decides availability), refused by other kinds.
// Returns a usage message, or an empty string.
QString readCiv(const QJsonObject &request, const QString &op, const EntityKind &kind, int &civ)
{
    const QJsonValue civValue = request.value(QStringLiteral("civ"));
    if (civValue.isUndefined())
        return kind.perCiv() ? QStringLiteral("%1 %2 needs a civ.").arg(op, kind.key()) : QString();
    if (!kind.perCiv() && &kind != &techKind())
        return QStringLiteral("%1 %2 takes no civ.").arg(op, kind.key());
    if (!readInt(civValue, civ))
        return QStringLiteral("civ must be an integer.");
    return {};
}

// A stored value as JSON. Floats use the shortest form that reads back as the
// same float, so 0.2f is 0.2 rather than 0.20000000298023224.
QJsonValue storedValue(const QVariant &value)
{
    if (value.typeId() == QMetaType::Float)
    {
        char buffer[32];
        const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value.toFloat());
        return QByteArray(buffer, written.ptr - buffer).toDouble();
    }
    return QJsonValue::fromVariant(value);
}

QJsonObject fieldObject(const FieldValue &field)
{
    QJsonObject item;
    item.insert(QStringLiteral("key"), field.key);
    item.insert(QStringLiteral("name"), field.name);
    item.insert(QStringLiteral("group"), field.group);
    item.insert(QStringLiteral("type"), field.type);
    item.insert(QStringLiteral("value"), storedValue(field.value));
    if (field.labelKind != RefKind::None)
    {
        item.insert(QStringLiteral("labelKind"), labelKindName(field.labelKind));
        if (!field.label.isEmpty())
            item.insert(QStringLiteral("label"), field.label);
    }
    if (!field.text.isEmpty())
        item.insert(QStringLiteral("text"), field.text);
    item.insert(QStringLiteral("editable"), field.editable);
    if (field.minimum)
        item.insert(QStringLiteral("min"), *field.minimum);
    if (field.maximum)
        item.insert(QStringLiteral("max"), *field.maximum);
    return item;
}

QJsonObject getObject(const GetQuery &query, bool compact, const GetResult &get)
{
    QJsonArray items;
    for (const GetItem &entity : get.items)
    {
        QJsonObject item;
        item.insert(QStringLiteral("id"), entity.id);
        item.insert(QStringLiteral("name"), entity.name);
        item.insert(QStringLiteral("internalName"), entity.internalName);
        if (entity.active)
            item.insert(QStringLiteral("active"), *entity.active);
        if (compact)
        {
            QJsonObject fields;
            for (const FieldValue &field : entity.fields)
                fields.insert(field.key, storedValue(field.value));
            item.insert(QStringLiteral("fields"), fields);
        }
        else
        {
            QJsonArray fields;
            for (const FieldValue &field : entity.fields)
                fields.append(fieldObject(field));
            item.insert(QStringLiteral("fields"), fields);
        }
        items.append(item);
    }
    QJsonObject body;
    body.insert(QStringLiteral("kind"), query.kind);
    if (query.civ >= 0)
        body.insert(QStringLiteral("civ"), query.civ);
    body.insert(QStringLiteral("items"), items);
    return body;
}

// A read request that passed the checks that need no data.
struct Prepared
{
    QString op;
    QString schemaKind;
    QString table;
    QString text;
    int civ = -1;
    ListQuery listQuery;
    GetQuery getQuery;
    bool compact = false;
};

// Checks `request` before any data is opened, so a malformed one fails fast.
// Returns the failure, or nothing when `out` is ready to run.
std::optional<HandlerResult> prepare(const QJsonObject &request, Prepared &out)
{
    const QJsonValue opValue = request.value(QStringLiteral("op"));
    if (!opValue.isString() || opValue.toString().isEmpty())
        return usage(QStringLiteral("Missing op."));
    const QString op = opValue.toString();
    if (op != QStringLiteral("info") && op != QStringLiteral("schema") && op != QStringLiteral("lookup")
        && op != QStringLiteral("list") && op != QStringLiteral("get"))
        return usage(QStringLiteral("Unknown op \"%1\".").arg(op));
    out.op = op;

    if (op == QStringLiteral("schema"))
    {
        const QJsonValue kindValue = request.value(QStringLiteral("kind"));
        if (!kindValue.isUndefined() && !kindValue.isString())
            return usage(QStringLiteral("schema kind must be a string."));
        out.schemaKind = kindValue.toString();
        if (!out.schemaKind.isEmpty() && !findEntityKind(out.schemaKind))
            return failed(unknownKind(out.schemaKind, QStringLiteral("Unknown kind \"%1\".").arg(out.schemaKind)));
    }

    if (op == QStringLiteral("lookup"))
    {
        const QJsonValue tableValue = request.value(QStringLiteral("table"));
        if (!tableValue.isString() || tableValue.toString().isEmpty())
            return usage(QStringLiteral("lookup needs a table."));
        const QString table = tableValue.toString();
        out.table = table;
        if (!lookupTables().contains(table))
            return failed(unknownKind(table, QStringLiteral("Unknown lookup table \"%1\".").arg(table)));
        const QJsonValue textValue = request.value(QStringLiteral("text"));
        if (!textValue.isUndefined() && !textValue.isString())
            return usage(QStringLiteral("lookup text must be a string."));
        out.text = textValue.toString().trimmed();
        const EntityKind *kind = findEntityKind(table);
        const bool perCiv = kind && kind->perCiv();
        const QJsonValue civValue = request.value(QStringLiteral("civ"));
        if (civValue.isUndefined())
        {
            if (perCiv)
                return usage(QStringLiteral("lookup %1 needs a civ.").arg(table));
        }
        else
        {
            if (!perCiv)
                return usage(QStringLiteral("lookup %1 takes no civ.").arg(table));
            if (!readInt(civValue, out.civ))
                return usage(QStringLiteral("civ must be an integer."));
        }
    }

    if (op == QStringLiteral("list"))
    {
        ListQuery &listQuery = out.listQuery;
        const QJsonValue kindValue = request.value(QStringLiteral("kind"));
        if (!kindValue.isString() || kindValue.toString().isEmpty())
            return usage(QStringLiteral("list needs a kind."));
        listQuery.kind = kindValue.toString();
        const EntityKind *kind = findEntityKind(listQuery.kind);
        if (!kind)
            return failed(unknownKind(listQuery.kind, QStringLiteral("Unknown kind \"%1\".").arg(listQuery.kind)));
        const bool isTech = kind == &techKind();
        if (const QString problem = readCiv(request, op, *kind, listQuery.civ); !problem.isEmpty())
            return usage(problem);

        const QJsonValue ownerValue = request.value(QStringLiteral("ownerCiv"));
        if (!ownerValue.isUndefined())
        {
            if (!isTech)
                return usage(QStringLiteral("list %1 takes no ownerCiv.").arg(listQuery.kind));
            int owner = -1;
            if (!readInt(ownerValue, owner))
                return usage(QStringLiteral("ownerCiv must be an integer."));
            listQuery.ownerCiv = owner;
        }

        const QJsonValue allValue = request.value(QStringLiteral("all"));
        if (!allValue.isUndefined() && !allValue.isBool())
            return usage(QStringLiteral("all must be true or false."));
        listQuery.all = allValue.toBool();

        const QJsonValue offsetValue = request.value(QStringLiteral("offset"));
        if (!offsetValue.isUndefined() && (!readInt(offsetValue, listQuery.offset) || listQuery.offset < 0))
            return usage(QStringLiteral("offset must be a non-negative integer."));
        const QJsonValue limitValue = request.value(QStringLiteral("limit"));
        if (!limitValue.isUndefined() && (!readInt(limitValue, listQuery.limit) || listQuery.limit < 0))
            return usage(QStringLiteral("limit must be a non-negative integer."));
    }

    if (op == QStringLiteral("get"))
    {
        GetQuery &getQuery = out.getQuery;
        const QJsonValue kindValue = request.value(QStringLiteral("kind"));
        if (!kindValue.isString() || kindValue.toString().isEmpty())
            return usage(QStringLiteral("get needs a kind."));
        getQuery.kind = kindValue.toString();
        const EntityKind *kind = findEntityKind(getQuery.kind);
        if (!kind)
            return failed(unknownKind(getQuery.kind, QStringLiteral("Unknown kind \"%1\".").arg(getQuery.kind)));
        if (const QString problem = readCiv(request, op, *kind, getQuery.civ); !problem.isEmpty())
            return usage(problem);

        const QJsonValue idsValue = request.value(QStringLiteral("ids"));
        if (!idsValue.isArray() || idsValue.toArray().isEmpty())
            return usage(QStringLiteral("get needs a non-empty ids array."));
        for (const QJsonValue &idValue : idsValue.toArray())
        {
            int id = -1;
            if (!readInt(idValue, id))
                return usage(QStringLiteral("ids must be integers."));
            getQuery.ids.append(id);
        }

        const QJsonValue fieldsValue = request.value(QStringLiteral("fields"));
        if (!fieldsValue.isUndefined())
        {
            if (!fieldsValue.isArray() || fieldsValue.toArray().isEmpty())
                return usage(QStringLiteral("fields must be a non-empty array of keys."));
            for (const QJsonValue &fieldValue : fieldsValue.toArray())
            {
                if (!fieldValue.isString() || fieldValue.toString().trimmed().isEmpty())
                    return usage(QStringLiteral("fields must be a non-empty array of keys."));
                getQuery.fields.append(fieldValue.toString().trimmed());
            }
        }

        const QJsonValue compactValue = request.value(QStringLiteral("compact"));
        if (!compactValue.isUndefined() && !compactValue.isBool())
            return usage(QStringLiteral("compact must be true or false."));
        out.compact = compactValue.toBool();
    }

    return std::nullopt;
}

// Runs a prepared request against open data. `source` is the resolved
// source, which info reports. Open warnings are the caller's to attach.
HandlerResult run(const Prepared &prepared, const DataService &service, const DataSource &source)
{
    const QString &op = prepared.op;
    if (op == QStringLiteral("info"))
        return succeeded(infoObject(service, source));
    if (op == QStringLiteral("lookup"))
    {
        const LookupResult lookup = service.lookup(prepared.table, prepared.text, prepared.civ);
        if (!lookup.ok)
            return failed(lookup.error);
        return succeeded(lookupObject(prepared.table, prepared.text, prepared.civ, lookup));
    }
    if (op == QStringLiteral("list"))
    {
        const ListResult list = service.list(prepared.listQuery);
        if (!list.ok)
            return failed(list.error);
        return succeeded(listObject(prepared.listQuery, list));
    }
    if (op == QStringLiteral("get"))
    {
        const GetResult get = service.get(prepared.getQuery);
        if (!get.ok)
            return failed(get.error);
        return succeeded(getObject(prepared.getQuery, prepared.compact, get));
    }
    if (!prepared.schemaKind.isEmpty())
        return succeeded(schemaObject(*service.kind(prepared.schemaKind), service.session()));
    QJsonArray kinds;
    for (const EntityKind *kind : service.kinds())
        kinds.append(schemaObject(*kind, service.session()));
    QJsonObject body;
    body.insert(QStringLiteral("kinds"), kinds);
    return succeeded(body);
}

// One open for every request that passes prepare(). Each result takes its
// request's place; a request that fails leaves the others running. The data
// is not opened when no request passes.
HandlerResult handleBatch(const DataSource &source, const QJsonObject &request)
{
    const QJsonValue requestsValue = request.value(QStringLiteral("requests"));
    if (!requestsValue.isArray() || requestsValue.toArray().isEmpty())
        return usage(QStringLiteral("batch needs a non-empty requests array."));
    const QJsonArray requests = requestsValue.toArray();

    QList<std::optional<Prepared>> prepared(requests.size());
    QList<HandlerResult> results(requests.size());
    for (qsizetype i = 0; i < requests.size(); ++i)
    {
        const QJsonValue value = requests.at(i);
        if (!value.isObject())
        {
            results[i] = usage(QStringLiteral("Batch request %1 is not an object.").arg(i));
            continue;
        }
        if (value.toObject().value(QStringLiteral("op")) == QStringLiteral("batch"))
        {
            results[i] = usage(QStringLiteral("A batch cannot contain a batch."));
            continue;
        }
        if (value.toObject().value(QStringLiteral("op")) == QStringLiteral("mods-list"))
        {
            results[i] = usage(QStringLiteral("mods-list reads no data and cannot be in a batch."));
            continue;
        }
        Prepared ready;
        if (const std::optional<HandlerResult> bad = prepare(value.toObject(), ready))
            results[i] = *bad;
        else
            prepared[i] = ready;
    }

    QStringList warnings;
    if (std::any_of(prepared.cbegin(), prepared.cend(), [](const auto &ready) { return ready.has_value(); }))
    {
        const DataSource resolved = resolveSource(source);
        DataService service;
        const OpenResult opened = service.open(resolved);
        if (!opened.ok)
            return failed(opened.error, opened.warnings);
        warnings = opened.warnings;
        for (qsizetype i = 0; i < prepared.size(); ++i)
        {
            if (prepared[i])
                results[i] = run(*prepared[i], service, resolved);
        }
    }

    QJsonArray bodies;
    int failures = 0;
    for (const HandlerResult &result : results)
    {
        bodies.append(result.body);
        if (result.exitCode != 0)
            ++failures;
    }
    QJsonObject body;
    body.insert(QStringLiteral("results"), bodies);
    body.insert(QStringLiteral("failed"), failures);
    return succeeded(body, warnings);
}

// Lists mods without opening the data, so it is quick even on DE.
HandlerResult handleModsList(const DataSource &source)
{
    const ModsResult found = listMods(source);
    if (!found.ok)
        return failed(found.error);

    QJsonArray mods;
    for (const ModEntry &entry : found.mods)
    {
        QJsonObject item;
        item.insert(QStringLiteral("title"), entry.mod.info.title);
        item.insert(QStringLiteral("dir"), jsonPath(entry.mod.dir));
        item.insert(QStringLiteral("author"), entry.mod.info.author);
        item.insert(QStringLiteral("description"), entry.mod.info.description);
        item.insert(QStringLiteral("hasDat"), entry.hasDat);
        mods.append(item);
    }
    QJsonObject body;
    body.insert(QStringLiteral("game"), jsonPath(found.dataset.gameDir));
    body.insert(QStringLiteral("dataset"), QFileInfo(found.dataset.datPath).fileName());
    body.insert(QStringLiteral("modsFolder"), jsonPath(found.modsFolder));
    if (!found.otherModsFolders.isEmpty())
    {
        QJsonArray others;
        for (const QString &folder : found.otherModsFolders)
            others.append(jsonPath(folder));
        body.insert(QStringLiteral("otherModsFolders"), others);
    }
    body.insert(QStringLiteral("mods"), mods);
    QStringList warnings;
    if (found.modsFolder.isEmpty())
        warnings.append(QStringLiteral("No mods folder found; pass --mods-folder."));
    return succeeded(body, warnings);
}

} // namespace

int exitCodeFor(const QString &code)
{
    if (code.isEmpty())
        return 0;
    if (code == QStringLiteral("usage"))
        return 1;
    if (code == QStringLiteral("load_failed") || code == QStringLiteral("no_dataset")
        || code == QStringLiteral("mod_not_found") || code == QStringLiteral("mods_unsupported"))
        return 2;
    if (code == QStringLiteral("unknown_kind") || code == QStringLiteral("unknown_entity")
        || code == QStringLiteral("inactive_entity") || code == QStringLiteral("unknown_field")
        || code == QStringLiteral("not_applicable") || code == QStringLiteral("read_only")
        || code == QStringLiteral("bad_value") || code == QStringLiteral("out_of_range"))
        return 3;
    if (code == QStringLiteral("conflict"))
        return 4;
    if (code == QStringLiteral("mod_required") || code == QStringLiteral("game_data_protected")
        || code == QStringLiteral("save_failed"))
        return 5;
    return 1;
}

HandlerResult RequestHandler::handle(const DataSource &source, const QJsonObject &request)
{
    if (request.value(QStringLiteral("op")) == QStringLiteral("batch"))
        return handleBatch(source, request);
    if (request.value(QStringLiteral("op")) == QStringLiteral("mods-list"))
        return handleModsList(resolveSource(source));

    Prepared prepared;
    if (const std::optional<HandlerResult> bad = prepare(request, prepared))
        return *bad;
    const DataSource resolved = resolveSource(source);
    DataService service;
    const OpenResult opened = service.open(resolved);
    if (!opened.ok)
        return failed(opened.error, opened.warnings);
    HandlerResult result = run(prepared, service, resolved);
    result.warnings = opened.warnings;
    return result;
}

} // namespace newage
