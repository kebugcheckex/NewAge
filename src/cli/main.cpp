#include <cstdio>
#include <iostream>
#include <sstream>

#include <QCommandLineParser>
#include <QCoreApplication>
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
    for (const QCommandLineOption &option :
         {game, dataset, mod, modsFolder, dat, version, locale, civ, ownerCiv, all, limit, offset})
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
    else
    {
        return usage(QStringLiteral("Expected info, schema [kind], lookup <table> [TEXT] or list <kind>."));
    }
    const bool lists = command == QStringLiteral("list");
    if (parser.isSet(civ) && command != QStringLiteral("lookup") && !lists)
        return usage(QStringLiteral("--civ is not used by %1.").arg(command));
    for (const QCommandLineOption &option : {ownerCiv, all, limit, offset})
    {
        if (parser.isSet(option) && !lists)
            return usage(QStringLiteral("--%1 is not used by %2.").arg(option.names().first(), command));
    }
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
    request.insert(QStringLiteral("op"), command);
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
