#include "api/RequestHandler.h"

#include <cmath>
#include <limits>

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

HandlerResult succeeded(const QJsonObject &body, const QStringList &warnings)
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
    const QJsonValue opValue = request.value(QStringLiteral("op"));
    if (!opValue.isString() || opValue.toString().isEmpty())
        return usage(QStringLiteral("Missing op."));
    const QString op = opValue.toString();
    if (op != QStringLiteral("info") && op != QStringLiteral("schema") && op != QStringLiteral("lookup")
        && op != QStringLiteral("list"))
        return usage(QStringLiteral("Unknown op \"%1\".").arg(op));

    // Check the request before opening, so a malformed one fails fast.
    QString schemaKind;
    if (op == QStringLiteral("schema"))
    {
        const QJsonValue kindValue = request.value(QStringLiteral("kind"));
        if (!kindValue.isUndefined() && !kindValue.isString())
            return usage(QStringLiteral("schema kind must be a string."));
        schemaKind = kindValue.toString();
        if (!schemaKind.isEmpty() && !findEntityKind(schemaKind))
            return failed(unknownKind(schemaKind, QStringLiteral("Unknown kind \"%1\".").arg(schemaKind)));
    }

    QString table;
    QString text;
    int civ = -1;
    if (op == QStringLiteral("lookup"))
    {
        const QJsonValue tableValue = request.value(QStringLiteral("table"));
        if (!tableValue.isString() || tableValue.toString().isEmpty())
            return usage(QStringLiteral("lookup needs a table."));
        table = tableValue.toString();
        if (!lookupTables().contains(table))
            return failed(unknownKind(table, QStringLiteral("Unknown lookup table \"%1\".").arg(table)));
        const QJsonValue textValue = request.value(QStringLiteral("text"));
        if (!textValue.isUndefined() && !textValue.isString())
            return usage(QStringLiteral("lookup text must be a string."));
        text = textValue.toString().trimmed();
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
            if (!readInt(civValue, civ))
                return usage(QStringLiteral("civ must be an integer."));
        }
    }

    ListQuery listQuery;
    if (op == QStringLiteral("list"))
    {
        const QJsonValue kindValue = request.value(QStringLiteral("kind"));
        if (!kindValue.isString() || kindValue.toString().isEmpty())
            return usage(QStringLiteral("list needs a kind."));
        listQuery.kind = kindValue.toString();
        const EntityKind *kind = findEntityKind(listQuery.kind);
        if (!kind)
            return failed(unknownKind(listQuery.kind, QStringLiteral("Unknown kind \"%1\".").arg(listQuery.kind)));
        const bool isTech = kind == &techKind();

        // Units need a civ; techs take one to judge availability.
        const QJsonValue civValue = request.value(QStringLiteral("civ"));
        if (civValue.isUndefined())
        {
            if (kind->perCiv())
                return usage(QStringLiteral("list %1 needs a civ.").arg(listQuery.kind));
        }
        else
        {
            if (!kind->perCiv() && !isTech)
                return usage(QStringLiteral("list %1 takes no civ.").arg(listQuery.kind));
            if (!readInt(civValue, listQuery.civ))
                return usage(QStringLiteral("civ must be an integer."));
        }

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

    const DataSource resolved = resolveSource(source);
    DataService service;
    const OpenResult opened = service.open(resolved);
    if (!opened.ok)
        return failed(opened.error, opened.warnings);
    if (op == QStringLiteral("info"))
        return succeeded(infoObject(service, resolved), opened.warnings);
    if (op == QStringLiteral("lookup"))
    {
        const LookupResult lookup = service.lookup(table, text, civ);
        if (!lookup.ok)
            return failed(lookup.error, opened.warnings);
        return succeeded(lookupObject(table, text, civ, lookup), opened.warnings);
    }
    if (op == QStringLiteral("list"))
    {
        const ListResult list = service.list(listQuery);
        if (!list.ok)
            return failed(list.error, opened.warnings);
        return succeeded(listObject(listQuery, list), opened.warnings);
    }
    if (!schemaKind.isEmpty())
        return succeeded(schemaObject(*service.kind(schemaKind), service.session()), opened.warnings);
    QJsonArray kinds;
    for (const EntityKind *kind : service.kinds())
        kinds.append(schemaObject(*kind, service.session()));
    QJsonObject body;
    body.insert(QStringLiteral("kinds"), kinds);
    return succeeded(body, opened.warnings);
}

} // namespace newage
