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
    for (const QCommandLineOption &option : {game, dataset, mod, modsFolder, dat, version, locale})
        parser.addOption(option);

    if (!parser.parse(app.arguments()))
        return usage(parser.errorText());

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty() || positional.size() > 2)
        return usage(QStringLiteral("Expected info or schema [kind]."));
    if (positional.first() == QStringLiteral("info") && positional.size() != 1)
        return usage(QStringLiteral("info takes no arguments."));
    if (positional.first() != QStringLiteral("schema") && positional.size() != 1)
        return usage(QStringLiteral("Unexpected command argument."));
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
    request.insert(QStringLiteral("op"), positional.first());
    if (positional.first() == QStringLiteral("schema") && positional.size() == 2)
        request.insert(QStringLiteral("kind"), positional.at(1));
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
