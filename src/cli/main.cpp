#include <cstdio>
#include <iostream>
#include <sstream>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "api/RequestHandler.h"

namespace {

void printJson(FILE *stream, const QJsonObject &object)
{
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stream);
    std::fflush(stream);
}

// Reads an integer option into the request. False when the value isn't one.
bool insertInt(QJsonObject &request, const QString &key, const QString &value)
{
    bool ok = false;
    const int number = value.toInt(&ok);
    if (ok)
        request.insert(key, number);
    return ok;
}

int usage(const QString &message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), QStringLiteral("usage"));
    error.insert(QStringLiteral("message"), message);
    QJsonObject body;
    body.insert(QStringLiteral("error"), error);
    printJson(stdout, body);
    return newage::exitCodeFor(QStringLiteral("usage"));
}

// KEY=VALUE text as a request value: a whole number or a decimal becomes a JSON
// number, anything else stays text for the field parser to reject.
QJsonValue valueFromText(const QString &text)
{
    bool ok = false;
    const qlonglong whole = text.trimmed().toLongLong(&ok);
    if (ok)
        return static_cast<double>(whole);
    const double decimal = text.trimmed().toDouble(&ok);
    if (ok)
        return decimal;
    return text;
}

// Splits KEY=VALUE arguments into `target`. False when one has no key.
bool insertAssignments(QJsonObject &target, const QStringList &assignments)
{
    for (const QString &assignment : assignments)
    {
        const qsizetype equals = assignment.indexOf(QLatin1Char('='));
        if (equals <= 0)
            return false;
        target.insert(assignment.left(equals).trimmed(), valueFromText(assignment.mid(equals + 1)));
    }
    return true;
}

// Reads a JSON document from `path`, or stdin for "-". `what` names the input
// in messages. Returns a null document and sets `problem` when it can't be
// read or parsed.
QJsonDocument readJson(const QString &path, const QString &what, QString &problem)
{
    QFile file;
    bool opened = false;
    if (path == QStringLiteral("-"))
    {
        opened = file.open(stdin, QIODevice::ReadOnly);
    }
    else
    {
        file.setFileName(path);
        opened = file.open(QIODevice::ReadOnly);
    }
    if (!opened)
    {
        problem = QStringLiteral("Cannot read %1 file \"%2\": %3").arg(what, path, file.errorString());
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        problem = QStringLiteral("%1 input is not JSON: %2 at offset %3.").arg(what, error.errorString()).arg(error.offset);
        return {};
    }
    return document;
}

// Reads the batch requests from `path`, or stdin for "-". Empty and sets
// `problem` when the input can't be read or isn't a JSON array.
QJsonArray readBatch(const QString &path, QString &problem)
{
    const QJsonDocument document = readJson(path, QStringLiteral("Batch"), problem);
    if (!problem.isEmpty())
        return {};
    if (!document.isArray())
    {
        problem = QStringLiteral("Batch input must be a JSON array of requests.");
        return {};
    }
    return document.array();
}

// Reads a patch file: a JSON object with an `edits` array.
QJsonArray readPatch(const QString &path, QString &problem)
{
    const QJsonDocument document = readJson(path, QStringLiteral("Patch"), problem);
    if (!problem.isEmpty())
        return {};
    if (!document.isObject() || !document.object().value(QStringLiteral("edits")).isArray())
    {
        problem = QStringLiteral("Patch input must be a JSON object with an edits array.");
        return {};
    }
    return document.object().value(QStringLiteral("edits")).toArray();
}

void printMessage(const QString &kind, const QString &message)
{
    QJsonObject detail;
    detail.insert(QStringLiteral("message"), message);
    QJsonObject line;
    line.insert(kind, detail);
    printJson(stderr, line);
}

// genieutils writes load diagnostics to std::cout. Keep stdout reserved for
// the one JSON response while RequestHandler opens and reads the data.
class LibraryOutputCapture
{
public:
    LibraryOutputCapture() : original_(std::cout.rdbuf(captured_.rdbuf())) {}
    ~LibraryOutputCapture() { std::cout.rdbuf(original_); }

    QStringList lines() const
    {
        QStringList result;
        for (const QString &line : QString::fromLocal8Bit(captured_.str()).split(QLatin1Char('\n')))
        {
            if (!line.trimmed().isEmpty())
                result.append(line.trimmed());
        }
        return result;
    }

private:
    std::ostringstream captured_;
    std::streambuf *original_;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("newage-cli"));

    QCommandLineParser parser;
    const QCommandLineOption game(QStringLiteral("game"), QStringLiteral("Game install folder."),
                                  QStringLiteral("DIR"));
    const QCommandLineOption dataset(QStringLiteral("dataset"), QStringLiteral("Data set file name."),
                                     QStringLiteral("FILE"));
    const QCommandLineOption mod(QStringLiteral("mod"), QStringLiteral("Mod title or folder."),
                                 QStringLiteral("NAME|DIR"));
    const QCommandLineOption modsFolder(QStringLiteral("mods-folder"), QStringLiteral("Mods folder."),
                                        QStringLiteral("DIR"));
    const QCommandLineOption dat(QStringLiteral("dat"), QStringLiteral("Loose .dat file."),
                                 QStringLiteral("FILE"));
    const QCommandLineOption version(QStringLiteral("version"), QStringLiteral("Version key for --dat."),
                                     QStringLiteral("KEY"));
    const QCommandLineOption locale(QStringLiteral("locale"), QStringLiteral("Language folder (default: en)."),
                                    QStringLiteral("CODE"));
    const QCommandLineOption civ(QStringLiteral("civ"), QStringLiteral("Civ ID for per-civ data."),
                                 QStringLiteral("N"));
    const QCommandLineOption ownerCiv(QStringLiteral("owner-civ"), QStringLiteral("List techs owned by this civ."),
                                      QStringLiteral("N"));
    const QCommandLineOption all(QStringLiteral("all"), QStringLiteral("List inactive entities too."));
    const QCommandLineOption limit(QStringLiteral("limit"), QStringLiteral("At most N rows."), QStringLiteral("N"));
    const QCommandLineOption offset(QStringLiteral("offset"), QStringLiteral("Skip the first N rows."),
                                    QStringLiteral("N"));
    const QCommandLineOption fields(QStringLiteral("fields"), QStringLiteral("Field keys or patterns, comma-separated."),
                                    QStringLiteral("KEYS"));
    const QCommandLineOption compact(QStringLiteral("compact"), QStringLiteral("Fields as a key-to-value object."));
    const QCommandLineOption expect(QStringLiteral("expect"), QStringLiteral("Value the field must hold (repeatable)."),
                                    QStringLiteral("KEY=VALUE"));
    const QCommandLineOption dryRun(QStringLiteral("dry-run"), QStringLiteral("Check and report without saving."));
    const QCommandLineOption title(QStringLiteral("title"), QStringLiteral("Title of the new mod."),
                                   QStringLiteral("TITLE"));
    const QCommandLineOption author(QStringLiteral("author"), QStringLiteral("Author of the new mod."),
                                    QStringLiteral("AUTHOR"));
    const QCommandLineOption description(QStringLiteral("description"), QStringLiteral("Description of the new mod."),
                                         QStringLiteral("TEXT"));
    const QCommandLineOption from(QStringLiteral("from"), QStringLiteral("game (default) or mod:NAME."),
                                  QStringLiteral("SOURCE"));
    for (const QCommandLineOption &option : {game, dataset, mod, modsFolder, dat, version, locale, civ, ownerCiv, all,
                                             limit, offset, fields, compact, expect, dryRun, title, author, description,
                                             from})
        parser.addOption(option);

    if (!parser.parse(app.arguments()))
        return usage(parser.errorText());

    const QStringList positional = parser.positionalArguments();
    const QString command = positional.value(0);
    if (command == QStringLiteral("info"))
    {
        if (positional.size() != 1)
            return usage(QStringLiteral("info takes no arguments."));
    }
    else if (command == QStringLiteral("schema"))
    {
        if (positional.size() > 2)
            return usage(QStringLiteral("Expected schema [kind]."));
    }
    else if (command == QStringLiteral("lookup"))
    {
        if (positional.size() < 2 || positional.size() > 3)
            return usage(QStringLiteral("Expected lookup <table> [TEXT]."));
    }
    else if (command == QStringLiteral("list"))
    {
        if (positional.size() != 2)
            return usage(QStringLiteral("Expected list <kind>."));
    }
    else if (command == QStringLiteral("get"))
    {
        if (positional.size() < 3)
            return usage(QStringLiteral("Expected get <kind> <id>..."));
    }
    else if (command == QStringLiteral("batch"))
    {
        if (positional.size() != 2)
            return usage(QStringLiteral("Expected batch FILE|-."));
    }
    else if (command == QStringLiteral("set"))
    {
        if (positional.size() < 4)
            return usage(QStringLiteral("Expected set <kind> <id> KEY=VALUE..."));
    }
    else if (command == QStringLiteral("apply"))
    {
        if (positional.size() != 2)
            return usage(QStringLiteral("Expected apply PATCH.json|-."));
    }
    else if (command == QStringLiteral("tech"))
    {
        if (positional.size() != 3 || positional.at(1) != QStringLiteral("create"))
            return usage(QStringLiteral("Expected tech create SPEC.json|-."));
    }
    else if (command == QStringLiteral("diff"))
    {
        if (positional.size() > 2)
            return usage(QStringLiteral("Expected diff [kind]."));
    }
    else if (command == QStringLiteral("mods"))
    {
        const QString subcommand = positional.value(1);
        if (positional.size() != 2 || (subcommand != QStringLiteral("list") && subcommand != QStringLiteral("create")))
            return usage(QStringLiteral("Expected mods list or mods create."));
        if (parser.isSet(mod))
            return usage(QStringLiteral("--mod is not used by mods %1.").arg(subcommand));
    }
    else
    {
        return usage(QStringLiteral("Expected info, schema [kind], lookup <table> [TEXT], list <kind>, "
                                    "get <kind> <id>..., batch FILE|-, set <kind> <id> KEY=VALUE..., "
                                    "apply PATCH.json|-, tech create SPEC.json|-, diff [kind], mods list or mods create."));
    }
    // How messages name the command: "mods list", not "mods".
    const QString commandName = command == QStringLiteral("mods") || command == QStringLiteral("tech")
                                    ? command + QLatin1Char(' ') + positional.at(1) : command;
    const bool lists = command == QStringLiteral("list");
    const bool gets = command == QStringLiteral("get");
    const bool sets = command == QStringLiteral("set");
    const bool applies = command == QStringLiteral("apply");
    const bool techCreates = command == QStringLiteral("tech");
    const bool creates = command == QStringLiteral("mods") && positional.at(1) == QStringLiteral("create");
    const bool diffs = command == QStringLiteral("diff");
    if (parser.isSet(civ) && command != QStringLiteral("lookup") && !lists && !gets && !sets && !diffs)
        return usage(QStringLiteral("--civ is not used by %1.").arg(commandName));
    if (parser.isSet(expect) && !sets)
        return usage(QStringLiteral("--expect is not used by %1.").arg(commandName));
    if (parser.isSet(dryRun) && !sets && !applies && !techCreates)
        return usage(QStringLiteral("--dry-run is not used by %1.").arg(commandName));
    for (const QCommandLineOption &option : {title, author, description, from})
    {
        if (parser.isSet(option) && !creates)
            return usage(QStringLiteral("--%1 is not used by %2.").arg(option.names().first(), commandName));
    }
    if (creates && !parser.isSet(title))
        return usage(QStringLiteral("mods create needs --title."));
    for (const QCommandLineOption &option : {ownerCiv, all})
    {
        if (parser.isSet(option) && !lists)
            return usage(QStringLiteral("--%1 is not used by %2.").arg(option.names().first(), commandName));
    }
    for (const QCommandLineOption &option : {limit, offset})
    {
        if (parser.isSet(option) && !lists && !diffs)
            return usage(QStringLiteral("--%1 is not used by %2.").arg(option.names().first(), commandName));
    }
    if (parser.isSet(fields) && !gets && !diffs)
        return usage(QStringLiteral("--fields is not used by %1.").arg(commandName));
    if (parser.isSet(compact) && !gets)
        return usage(QStringLiteral("--compact is not used by %1.").arg(commandName));
    if (parser.isSet(dat) && (parser.isSet(game) || parser.isSet(dataset)))
        return usage(QStringLiteral("--dat cannot be combined with --game or --dataset."));
    if (parser.isSet(dat) != parser.isSet(version))
        return usage(QStringLiteral("--dat and --version must be used together."));

    newage::DataSource source;
    source.gameDir = parser.value(game);
    source.dataset = parser.value(dataset);
    source.mod = parser.value(mod);
    source.modsFolder = parser.value(modsFolder);
    source.datPath = parser.value(dat);
    source.versionKey = parser.value(version);
    if (parser.isSet(locale))
        source.locale = parser.value(locale);

    QJsonObject request;
    request.insert(QStringLiteral("op"),
                   command == QStringLiteral("mods") || command == QStringLiteral("tech")
                       ? command + QLatin1Char('-') + positional.at(1) : command);
    if (command == QStringLiteral("schema") && positional.size() == 2)
        request.insert(QStringLiteral("kind"), positional.at(1));
    if (command == QStringLiteral("lookup"))
    {
        request.insert(QStringLiteral("table"), positional.at(1));
        if (positional.size() == 3)
            request.insert(QStringLiteral("text"), positional.at(2));
    }
    if (lists)
    {
        request.insert(QStringLiteral("kind"), positional.at(1));
        if (parser.isSet(all))
            request.insert(QStringLiteral("all"), true);
    }
    if (gets)
    {
        request.insert(QStringLiteral("kind"), positional.at(1));
        QJsonArray ids;
        for (const QString &text : positional.mid(2))
        {
            bool ok = false;
            const int id = text.toInt(&ok);
            if (!ok)
                return usage(QStringLiteral("get IDs must be integers, not \"%1\".").arg(text));
            ids.append(id);
        }
        request.insert(QStringLiteral("ids"), ids);
        if (parser.isSet(compact))
            request.insert(QStringLiteral("compact"), true);
    }
    if (diffs && positional.size() == 2)
        request.insert(QStringLiteral("kind"), positional.at(1));
    if (parser.isSet(fields))
    {
        QJsonArray keys;
        for (const QString &key : parser.value(fields).split(QLatin1Char(',')))
            keys.append(key.trimmed());
        request.insert(QStringLiteral("fields"), keys);
    }
    if (sets)
    {
        request.insert(QStringLiteral("kind"), positional.at(1));
        bool ok = false;
        const int id = positional.at(2).toInt(&ok);
        if (!ok)
            return usage(QStringLiteral("set ID must be an integer, not \"%1\".").arg(positional.at(2)));
        request.insert(QStringLiteral("id"), id);
        QJsonObject assignments;
        if (!insertAssignments(assignments, positional.mid(3)))
            return usage(QStringLiteral("set arguments are KEY=VALUE."));
        request.insert(QStringLiteral("set"), assignments);
        if (parser.isSet(expect))
        {
            QJsonObject expected;
            if (!insertAssignments(expected, parser.values(expect)))
                return usage(QStringLiteral("--expect is KEY=VALUE."));
            request.insert(QStringLiteral("expect"), expected);
        }
    }
    if (applies)
    {
        QString problem;
        const QJsonArray edits = readPatch(positional.at(1), problem);
        if (!problem.isEmpty())
            return usage(problem);
        request.insert(QStringLiteral("edits"), edits);
    }
    if (techCreates)
    {
        QString problem;
        const QJsonDocument document = readJson(positional.at(2), QStringLiteral("Tech spec"), problem);
        if (!problem.isEmpty())
            return usage(problem);
        if (!document.isObject())
            return usage(QStringLiteral("Tech spec input must be a JSON object."));
        request.insert(QStringLiteral("spec"), document.object());
    }
    if (sets || applies || techCreates)
    {
        if (parser.isSet(dryRun))
            request.insert(QStringLiteral("dryRun"), true);
    }
    if (creates)
    {
        request.insert(QStringLiteral("title"), parser.value(title));
        if (parser.isSet(author))
            request.insert(QStringLiteral("author"), parser.value(author));
        if (parser.isSet(description))
            request.insert(QStringLiteral("description"), parser.value(description));
        if (parser.isSet(from))
            request.insert(QStringLiteral("from"), parser.value(from));
    }
    if (command == QStringLiteral("batch"))
    {
        QString problem;
        const QJsonArray requests = readBatch(positional.at(1), problem);
        if (!problem.isEmpty())
            return usage(problem);
        request.insert(QStringLiteral("requests"), requests);
    }
    if (parser.isSet(civ) && !insertInt(request, QStringLiteral("civ"), parser.value(civ)))
        return usage(QStringLiteral("--civ needs an integer."));
    if (parser.isSet(ownerCiv) && !insertInt(request, QStringLiteral("ownerCiv"), parser.value(ownerCiv)))
        return usage(QStringLiteral("--owner-civ needs an integer."));
    if (parser.isSet(limit) && !insertInt(request, QStringLiteral("limit"), parser.value(limit)))
        return usage(QStringLiteral("--limit needs an integer."));
    if (parser.isSet(offset) && !insertInt(request, QStringLiteral("offset"), parser.value(offset)))
        return usage(QStringLiteral("--offset needs an integer."));
    newage::HandlerResult result;
    QStringList libraryOutput;
    {
        LibraryOutputCapture capture;
        result = newage::RequestHandler().handle(source, request);
        libraryOutput = capture.lines();
    }
    for (const QString &warning : result.warnings)
        printMessage(QStringLiteral("warning"), warning);
    for (const QString &line : libraryOutput)
        printMessage(QStringLiteral("diagnostic"), line);
    printJson(stdout, result.body);
    return result.exitCode;
}
