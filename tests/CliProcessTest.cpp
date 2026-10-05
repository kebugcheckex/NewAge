#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString kTcDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x1_p1.dat");

struct Response
{
    int exitCode = -1;
    QJsonObject body;
    QByteArray error;
};

// `input`, when given, is written to the CLI's stdin.
Response run(const QStringList &arguments, const QByteArray &input = {})
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const QString &name : {QStringLiteral("NEWAGE_GAME"), QStringLiteral("NEWAGE_DATASET"),
                                QStringLiteral("NEWAGE_MOD"), QStringLiteral("NEWAGE_MODS_FOLDER")})
        environment.remove(name);
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(NEWAGE_CLI_PATH), arguments);
    if (!process.waitForStarted())
        return {};
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(120000))
        return {};

    Response response;
    response.exitCode = process.exitCode();
    response.error = process.readAllStandardError();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject())
        response.body = document.object();
    return response;
}

QString errorCode(const Response &response)
{
    return response.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

} // namespace

class CliProcessTest : public QObject
{
    Q_OBJECT

private slots:
    void missingCommandIsJsonUsage();
    void unknownOptionIsJsonUsage();
    void missingSourceIsJsonError();
    void infoReadsLooseFile();
    void schemaReadsLooseFile();
    void civRejectedByInfo();
    void civMustBeInteger();
    void lookupReadsLooseFile();
    void listOptionsOnlyWithList();
    void listReadsLooseFile();
    void getOptionsOnlyWithGet();
    void getReadsLooseFile();
    void batchRejectsBadInput();
    void batchReadsFileAndStdin();
    void modsListRejectsBadInput();
    void modsListReadsGameFolder();
};

void CliProcessTest::missingCommandIsJsonUsage()
{
    const Response response = run({});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));
    QVERIFY(response.error.isEmpty());
}

void CliProcessTest::unknownOptionIsJsonUsage()
{
    const Response response = run({QStringLiteral("--bogus"), QStringLiteral("info")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));
    QVERIFY(response.error.isEmpty());
}

void CliProcessTest::missingSourceIsJsonError()
{
    const Response response = run({QStringLiteral("info")});
    QCOMPARE(response.exitCode, 2);
    QCOMPARE(errorCode(response), QStringLiteral("no_dataset"));
    QVERIFY(response.error.isEmpty());
}

void CliProcessTest::infoReadsLooseFile()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    const Response response = run({QStringLiteral("--dat"), kTcDat, QStringLiteral("--version"),
                                   QStringLiteral("tc"), QStringLiteral("info")});
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("version")).toString(), QStringLiteral("tc"));
    QCOMPARE(response.body.value(QStringLiteral("readOnly")).toBool(), true);
    QVERIFY(response.body.value(QStringLiteral("counts")).toObject().value(QStringLiteral("unit")).toInt() > 0);
    for (const QByteArray &line : response.error.split('\n'))
    {
        if (line.trimmed().isEmpty())
            continue;
        const QJsonDocument diagnostic = QJsonDocument::fromJson(line);
        QVERIFY(diagnostic.isObject());
        const QJsonObject object = diagnostic.object();
        QVERIFY(object.value(QStringLiteral("warning")).isObject()
                || object.value(QStringLiteral("diagnostic")).isObject());
    }
}

void CliProcessTest::schemaReadsLooseFile()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    const Response response = run({QStringLiteral("--dat"), kTcDat, QStringLiteral("--version"),
                                   QStringLiteral("tc"), QStringLiteral("schema"), QStringLiteral("tech")});
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("kind")).toString(), QStringLiteral("tech"));
    QVERIFY(!response.body.value(QStringLiteral("fields")).toArray().isEmpty());
}

void CliProcessTest::civRejectedByInfo()
{
    const Response response = run({QStringLiteral("--civ"), QStringLiteral("1"), QStringLiteral("info")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));
}

void CliProcessTest::civMustBeInteger()
{
    const Response response = run({QStringLiteral("lookup"), QStringLiteral("unit"), QStringLiteral("--civ"),
                                   QStringLiteral("spanish")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));
}

void CliProcessTest::lookupReadsLooseFile()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    const Response response = run({QStringLiteral("--dat"), kTcDat, QStringLiteral("--version"), QStringLiteral("tc"),
                                   QStringLiteral("lookup"), QStringLiteral("unit"), QStringLiteral("archr"),
                                   QStringLiteral("--civ"), QStringLiteral("1")});
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("table")).toString(), QStringLiteral("unit"));
    QCOMPARE(response.body.value(QStringLiteral("civ")).toInt(), 1);
    const QJsonArray matches = response.body.value(QStringLiteral("matches")).toArray();
    QVERIFY(!matches.isEmpty());
    QCOMPARE(matches.first().toObject().value(QStringLiteral("internalName")).toString(), QStringLiteral("ARCHR"));
}

void CliProcessTest::listOptionsOnlyWithList()
{
    Response response = run({QStringLiteral("lookup"), QStringLiteral("civ"), QStringLiteral("--all")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));

    response = run({QStringLiteral("list"), QStringLiteral("tech"), QStringLiteral("--limit"), QStringLiteral("ten")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));
}

void CliProcessTest::listReadsLooseFile()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    const Response response = run({QStringLiteral("--dat"), kTcDat, QStringLiteral("--version"), QStringLiteral("tc"),
                                   QStringLiteral("list"), QStringLiteral("tech"), QStringLiteral("--civ"),
                                   QStringLiteral("1"), QStringLiteral("--all"), QStringLiteral("--offset"),
                                   QStringLiteral("1"), QStringLiteral("--limit"), QStringLiteral("2")});
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("kind")).toString(), QStringLiteral("tech"));
    QCOMPARE(response.body.value(QStringLiteral("civ")).toInt(), 1);
    QCOMPARE(response.body.value(QStringLiteral("all")).toBool(), true);
    const QJsonArray items = response.body.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.first().toObject().value(QStringLiteral("id")).toInt(), 1);
    QVERIFY(items.first().toObject().contains(QStringLiteral("active")));
}

void CliProcessTest::getOptionsOnlyWithGet()
{
    Response response = run({QStringLiteral("list"), QStringLiteral("tech"), QStringLiteral("--compact")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));

    response = run({QStringLiteral("get"), QStringLiteral("tech")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));

    response = run({QStringLiteral("get"), QStringLiteral("tech"), QStringLiteral("ten")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));
}

void CliProcessTest::getReadsLooseFile()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    Response response = run({QStringLiteral("--dat"), kTcDat, QStringLiteral("--version"), QStringLiteral("tc"),
                             QStringLiteral("get"), QStringLiteral("unit"), QStringLiteral("4"), QStringLiteral("--civ"),
                             QStringLiteral("1"), QStringLiteral("--fields"), QStringLiteral("hit_points, cost1.*")});
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("kind")).toString(), QStringLiteral("unit"));
    QCOMPARE(response.body.value(QStringLiteral("civ")).toInt(), 1);
    const QJsonArray items = response.body.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 1);
    const QJsonObject archer = items.first().toObject();
    QCOMPARE(archer.value(QStringLiteral("internalName")).toString(), QStringLiteral("ARCHR"));
    QStringList keys;
    for (const QJsonValue &field : archer.value(QStringLiteral("fields")).toArray())
        keys.append(field.toObject().value(QStringLiteral("key")).toString());
    QCOMPARE(keys, QStringList({QStringLiteral("hit_points"), QStringLiteral("cost1.resource"),
                                QStringLiteral("cost1.amount"), QStringLiteral("cost1.paid")}));

    response = run({QStringLiteral("--dat"), kTcDat, QStringLiteral("--version"), QStringLiteral("tc"),
                    QStringLiteral("get"), QStringLiteral("tech"), QStringLiteral("3"), QStringLiteral("2"),
                    QStringLiteral("--fields"), QStringLiteral("research_time"), QStringLiteral("--compact")});
    QCOMPARE(response.exitCode, 0);
    const QJsonArray techs = response.body.value(QStringLiteral("items")).toArray();
    QCOMPARE(techs.size(), 2);
    QCOMPARE(techs.at(0).toObject().value(QStringLiteral("id")).toInt(), 3);
    QCOMPARE(techs.at(1).toObject().value(QStringLiteral("id")).toInt(), 2);
    const QJsonObject fields = techs.at(0).toObject().value(QStringLiteral("fields")).toObject();
    QCOMPARE(fields.keys(), QStringList({QStringLiteral("research_time")}));
    QVERIFY(fields.value(QStringLiteral("research_time")).isDouble());
}

void CliProcessTest::batchRejectsBadInput()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString notJson = dir.filePath(QStringLiteral("bad.json"));
    QFile file(notJson);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[{\"op\": ");
    file.close();

    const QList<Response> responses = {
        run({QStringLiteral("batch")}),
        run({QStringLiteral("batch"), dir.filePath(QStringLiteral("missing.json"))}),
        run({QStringLiteral("batch"), notJson}),
        run({QStringLiteral("batch"), QStringLiteral("-")}, R"({"op": "info"})"),
        run({QStringLiteral("batch"), QStringLiteral("-"), QStringLiteral("--civ"), QStringLiteral("1")}, "[]"),
    };
    for (const Response &response : responses)
    {
        QCOMPARE(response.exitCode, 1);
        QCOMPARE(errorCode(response), QStringLiteral("usage"));
    }
}

void CliProcessTest::batchReadsFileAndStdin()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    const QByteArray requests = R"([
        {"op": "lookup", "table": "civ", "text": "brit"},
        {"op": "get", "kind": "tech", "ids": [3, 2], "fields": ["research_time"], "compact": true},
        {"op": "get", "kind": "unit", "ids": [4]}
    ])";
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("requests.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(requests);
    file.close();

    const QStringList source = {QStringLiteral("--dat"), kTcDat, QStringLiteral("--version"), QStringLiteral("tc")};
    const Response fromFile = run(source + QStringList{QStringLiteral("batch"), path});
    const Response fromStdin = run(source + QStringList{QStringLiteral("batch"), QStringLiteral("-")}, requests);
    for (const Response &response : {fromFile, fromStdin})
    {
        QCOMPARE(response.exitCode, 0);
        QCOMPARE(response.body.value(QStringLiteral("failed")).toInt(), 1);
        const QJsonArray results = response.body.value(QStringLiteral("results")).toArray();
        QCOMPARE(results.size(), 3);
        QCOMPARE(results.at(0).toObject().value(QStringLiteral("table")).toString(), QStringLiteral("civ"));
        QCOMPARE(results.at(1).toObject().value(QStringLiteral("items")).toArray().size(), 2);
        QCOMPARE(results.at(2).toObject().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
                 QStringLiteral("usage"));
    }
    QCOMPARE(fromFile.body, fromStdin.body);
}

void CliProcessTest::modsListRejectsBadInput()
{
    const QList<Response> responses = {
        run({QStringLiteral("mods")}),
        run({QStringLiteral("mods"), QStringLiteral("create")}),
        run({QStringLiteral("mods"), QStringLiteral("list"), QStringLiteral("extra")}),
        run({QStringLiteral("mods"), QStringLiteral("list"), QStringLiteral("--mod"), QStringLiteral("Balance")}),
        run({QStringLiteral("mods"), QStringLiteral("list"), QStringLiteral("--civ"), QStringLiteral("1")}),
    };
    for (const Response &response : responses)
    {
        QCOMPARE(response.exitCode, 1);
        QCOMPARE(errorCode(response), QStringLiteral("usage"));
    }
    QVERIFY(responses.at(4).body.value(QStringLiteral("error")).toObject().value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("mods list")));
}

// An HD folder with empty .dat files: mods list reads none of them.
void CliProcessTest::modsListReadsGameFolder()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    for (const QString &relative : {QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"),
                                    QStringLiteral("resources/_common/dat/empires2_x1_p1.dat"),
                                    QStringLiteral("mods/Balance/info.json")})
    {
        const QString path = game.filePath(relative);
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        if (relative.endsWith(QStringLiteral(".json")))
            file.write(R"({"Title": "Balance", "Author": "Me"})");
    }

    const Response response = run({QStringLiteral("--game"), game.path(), QStringLiteral("mods"), QStringLiteral("list")});
    QCOMPARE(response.exitCode, 0);
    QVERIFY(response.error.isEmpty());
    const QJsonArray mods = response.body.value(QStringLiteral("mods")).toArray();
    QCOMPARE(mods.size(), 1);
    QCOMPARE(mods.at(0).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Balance"));
    QCOMPARE(mods.at(0).toObject().value(QStringLiteral("author")).toString(), QStringLiteral("Me"));
    QCOMPARE(mods.at(0).toObject().value(QStringLiteral("hasDat")).toBool(), false);
}

QTEST_GUILESS_MAIN(CliProcessTest)
#include "CliProcessTest.moc"
