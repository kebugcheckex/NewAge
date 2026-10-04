#include "api/RequestHandler.h"

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
    if (op != QStringLiteral("info"))
        return usage(QStringLiteral("Unknown op \"%1\".").arg(op));

    const DataSource resolved = resolveSource(source);
    DataService service;
    const OpenResult opened = service.open(resolved);
    if (!opened.ok)
        return failed(opened.error, opened.warnings);
    return succeeded(infoObject(service, resolved), opened.warnings);
}

} // namespace newage
