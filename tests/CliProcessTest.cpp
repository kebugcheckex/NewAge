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
const QString kHdDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x2_p1.dat");

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
    void writeOptionsOnlyWithWrites();
    void setRejectsBadArguments();
    void setWritesAndReads();
    void applyReadsPatchFileAndStdin();
    void modsCreateNeedsTitle();
    void modsCreateMakesAMod();
    void diffRejectsBadArguments();
    void diffShowsASet();
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

// A game folder with the HD sample .dat, an empty TC one and an empty mod
// called Balance.
bool makeGame(const QTemporaryDir &game)
{
    const QString hd = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    if (!QDir().mkpath(QFileInfo(hd).absolutePath()) || !QFile::copy(kHdDat, hd))
        return false;
    QFile tc(game.filePath(QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));
    if (!tc.open(QIODevice::WriteOnly))
        return false;
    return QDir().mkpath(game.filePath(QStringLiteral("mods/Balance")));
}

QStringList withGame(const QTemporaryDir &game, const QStringList &arguments)
{
    return QStringList{QStringLiteral("--game"), game.path(), QStringLiteral("--mod"), QStringLiteral("Balance")}
           + arguments;
}

int hitPointsIn(const Response &response)
{
    const QJsonArray fields = response.body.value(QStringLiteral("items")).toArray().at(0).toObject()
                                  .value(QStringLiteral("fields")).toArray();
    return fields.at(0).toObject().value(QStringLiteral("value")).toInt(-1);
}

void CliProcessTest::writeOptionsOnlyWithWrites()
{
    Response response = run({QStringLiteral("list"), QStringLiteral("tech"), QStringLiteral("--expect"),
                             QStringLiteral("hit_points=1")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));

    response = run({QStringLiteral("get"), QStringLiteral("tech"), QStringLiteral("1"), QStringLiteral("--dry-run")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));

    response = run({QStringLiteral("apply"), QStringLiteral("-"), QStringLiteral("--expect"), QStringLiteral("a=1")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));

    response = run({QStringLiteral("info"), QStringLiteral("--title"), QStringLiteral("x")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));

    response = run({QStringLiteral("mods"), QStringLiteral("list"), QStringLiteral("--dry-run")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));
}

void CliProcessTest::setRejectsBadArguments()
{
    const auto usageFor = [](const QStringList &arguments) {
        const Response response = run(arguments);
        QCOMPARE(response.exitCode, 1);
        QCOMPARE(errorCode(response), QStringLiteral("usage"));
        QVERIFY(response.error.isEmpty());
    };
    usageFor({QStringLiteral("set")});
    usageFor({QStringLiteral("set"), QStringLiteral("unit"), QStringLiteral("4")});
    usageFor({QStringLiteral("set"), QStringLiteral("unit"), QStringLiteral("four"), QStringLiteral("a=1")});
    usageFor({QStringLiteral("set"), QStringLiteral("unit"), QStringLiteral("4"), QStringLiteral("hit_points")});
    usageFor({QStringLiteral("set"), QStringLiteral("unit"), QStringLiteral("4"), QStringLiteral("=5")});
    usageFor({QStringLiteral("set"), QStringLiteral("unit"), QStringLiteral("4"), QStringLiteral("a=1"),
              QStringLiteral("--expect"), QStringLiteral("a")});
    usageFor({QStringLiteral("set"), QStringLiteral("unit"), QStringLiteral("4"), QStringLiteral("a=1"),
              QStringLiteral("--civ"), QStringLiteral("x")});
    usageFor({QStringLiteral("apply")});
    usageFor({QStringLiteral("apply"), QStringLiteral("a.json"), QStringLiteral("b.json")});
    usageFor({QStringLiteral("apply"), QStringLiteral("missing-patch.json")});
    usageFor({QStringLiteral("mods"), QStringLiteral("create"), QStringLiteral("--mod"), QStringLiteral("Balance"),
              QStringLiteral("--title"), QStringLiteral("x")});
    usageFor({QStringLiteral("mods"), QStringLiteral("remove")});

    // A loose file has no mods to write to.
    const Response response = run({QStringLiteral("--dat"), kTcDat, QStringLiteral("--version"), QStringLiteral("tc"),
                                   QStringLiteral("set"), QStringLiteral("tech"), QStringLiteral("1"),
                                   QStringLiteral("research_time=5")});
    QCOMPARE(response.exitCode, 2);
    QCOMPARE(errorCode(response), QStringLiteral("mods_unsupported"));
}

void CliProcessTest::setWritesAndReads()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeGame(game));
    const QStringList read = withGame(game, {QStringLiteral("get"), QStringLiteral("unit"), QStringLiteral("4"),
                                             QStringLiteral("--civ"), QStringLiteral("1"), QStringLiteral("--fields"),
                                             QStringLiteral("hit_points")});
    const int original = hitPointsIn(run(read));
    QVERIFY(original >= 0);
    const QString next = QString::number(original + 1);

    const QStringList set = withGame(game, {QStringLiteral("set"), QStringLiteral("unit"), QStringLiteral("4"),
                                            QStringLiteral("--civ"), QStringLiteral("1")});

    // A dry run, then a conflict, change nothing.
    Response response = run(set + QStringList{QStringLiteral("hit_points=") + next, QStringLiteral("--dry-run")});
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("dryRun")).toBool(), true);
    QCOMPARE(response.body.value(QStringLiteral("changes")).toArray().size(), 1);
    response = run(set + QStringList{QStringLiteral("hit_points=") + next, QStringLiteral("--expect"),
                                     QStringLiteral("hit_points=") + QString::number(original + 9)});
    QCOMPARE(response.exitCode, 4);
    QCOMPARE(errorCode(response), QStringLiteral("conflict"));
    QCOMPARE(hitPointsIn(run(read)), original);

    response = run(set + QStringList{QStringLiteral("hit_points=99999")});
    QCOMPARE(response.exitCode, 3);
    QCOMPARE(errorCode(response), QStringLiteral("out_of_range"));
    response = run(set + QStringList{QStringLiteral("hit_points=lots")});
    QCOMPARE(response.exitCode, 3);
    QCOMPARE(errorCode(response), QStringLiteral("bad_value"));

    response = run(set + QStringList{QStringLiteral("hit_points=") + next, QStringLiteral("--expect"),
                                     QStringLiteral("hit_points=") + QString::number(original)});
    QCOMPARE(response.exitCode, 0);
    const QJsonObject change = response.body.value(QStringLiteral("changes")).toArray().at(0).toObject();
    QCOMPARE(change.value(QStringLiteral("old")).toInt(), original);
    QCOMPARE(change.value(QStringLiteral("new")).toInt(), original + 1);
    QVERIFY(response.body.value(QStringLiteral("saved")).toString().endsWith(
        QStringLiteral("mods/Balance/resources/_common/dat/empires2_x2_p1.dat")));
    QCOMPARE(hitPointsIn(run(read)), original + 1);

    // The game's own file is untouched: read without the mod.
    const Response plain = run({QStringLiteral("--game"), game.path(), QStringLiteral("get"), QStringLiteral("unit"),
                                QStringLiteral("4"), QStringLiteral("--civ"), QStringLiteral("1"),
                                QStringLiteral("--fields"), QStringLiteral("hit_points")});
    QCOMPARE(hitPointsIn(plain), original);

    // A text field takes everything after "=", spaces included.
    response = run(withGame(game, {QStringLiteral("set"), QStringLiteral("tech"), QStringLiteral("1"),
                                   QStringLiteral("internal_name=Super villager 2")}));
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("changes")).toArray().at(0).toObject().value(QStringLiteral("new")),
             QJsonValue(QStringLiteral("Super villager 2")));
}

void CliProcessTest::applyReadsPatchFileAndStdin()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeGame(game));
    const Response before = run(withGame(game, {QStringLiteral("get"), QStringLiteral("unit"), QStringLiteral("4"),
                                                QStringLiteral("--civ"), QStringLiteral("1"), QStringLiteral("--fields"),
                                                QStringLiteral("hit_points")}));
    const int original = hitPointsIn(before);
    QVERIFY(original >= 0);

    const auto patch = [](int civ, int hitPoints) {
        return QStringLiteral(R"({"edits": [{"kind": "unit", "id": 4, "civ": %1, "set": {"hit_points": %2}}]})")
            .arg(civ)
            .arg(hitPoints)
            .toUtf8();
    };

    // From stdin, as a dry run.
    Response response = run(withGame(game, {QStringLiteral("apply"), QStringLiteral("-"), QStringLiteral("--dry-run")}),
                            patch(1, original + 2));
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("dryRun")).toBool(), true);
    QCOMPARE(response.body.value(QStringLiteral("saved")).toString(), QString());

    // From a file, for real.
    QTemporaryDir files;
    QVERIFY(files.isValid());
    QFile file(files.filePath(QStringLiteral("patch.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(patch(1, original + 2));
    file.close();
    response = run(withGame(game, {QStringLiteral("apply"), file.fileName()}));
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("changes")).toArray().size(), 1);
    QVERIFY(!response.body.value(QStringLiteral("saved")).toString().isEmpty());

    // Not a patch: an array, bad JSON, no edits.
    for (const QByteArray &input : {QByteArray("[]"), QByteArray("{nope"), QByteArray(R"({"edits": 3})")})
    {
        response = run(withGame(game, {QStringLiteral("apply"), QStringLiteral("-")}), input);
        QCOMPARE(response.exitCode, 1);
        QCOMPARE(errorCode(response), QStringLiteral("usage"));
    }
}

void CliProcessTest::modsCreateNeedsTitle()
{
    Response response = run({QStringLiteral("mods"), QStringLiteral("create")});
    QCOMPARE(response.exitCode, 1);
    QCOMPARE(errorCode(response), QStringLiteral("usage"));

    response = run({QStringLiteral("mods"), QStringLiteral("create"), QStringLiteral("--title"), QStringLiteral("x")});
    QCOMPARE(response.exitCode, 2);
    QCOMPARE(errorCode(response), QStringLiteral("no_dataset"));
}

void CliProcessTest::modsCreateMakesAMod()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    for (const QString &name : {QStringLiteral("x1"), QStringLiteral("x2")})
    {
        QFile file(game.filePath(QStringLiteral("resources/_common/dat/empires2_%1_p1.dat").arg(name)));
        QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    const Response response = run({QStringLiteral("--game"), game.path(), QStringLiteral("mods"),
                                   QStringLiteral("create"), QStringLiteral("--title"), QStringLiteral("Castles"),
                                   QStringLiteral("--author"), QStringLiteral("Me"), QStringLiteral("--description"),
                                   QStringLiteral("Cheaper castles"), QStringLiteral("--from"), QStringLiteral("game")});
    QCOMPARE(response.exitCode, 0);
    QVERIFY(response.error.isEmpty());
    QVERIFY(QFileInfo(game.filePath(QStringLiteral("mods/Castles/info.json"))).isFile());
    QVERIFY(response.body.value(QStringLiteral("note")).toString().contains(QStringLiteral("disabled")));

    const Response again = run({QStringLiteral("--game"), game.path(), QStringLiteral("mods"), QStringLiteral("create"),
                                QStringLiteral("--title"), QStringLiteral("castles")});
    QCOMPARE(again.exitCode, 3);
    QCOMPARE(errorCode(again), QStringLiteral("bad_value"));

    const Response listed = run({QStringLiteral("--game"), game.path(), QStringLiteral("mods"), QStringLiteral("list")});
    const QJsonObject mod = listed.body.value(QStringLiteral("mods")).toArray().at(0).toObject();
    QCOMPARE(mod.value(QStringLiteral("title")).toString(), QStringLiteral("Castles"));
    QCOMPARE(mod.value(QStringLiteral("author")).toString(), QStringLiteral("Me"));
}

void CliProcessTest::diffRejectsBadArguments()
{
    const auto usageFor = [](const QStringList &arguments) {
        const Response response = run(arguments);
        QCOMPARE(response.exitCode, 1);
        QCOMPARE(errorCode(response), QStringLiteral("usage"));
    };
    usageFor({QStringLiteral("diff"), QStringLiteral("unit"), QStringLiteral("tech")});
    usageFor({QStringLiteral("diff"), QStringLiteral("--compact")});
    usageFor({QStringLiteral("diff"), QStringLiteral("--all")});
    usageFor({QStringLiteral("diff"), QStringLiteral("--fields"), QStringLiteral("hit_points")});
    usageFor({QStringLiteral("diff"), QStringLiteral("tech"), QStringLiteral("--civ"), QStringLiteral("1")});
    usageFor({QStringLiteral("list"), QStringLiteral("tech"), QStringLiteral("--fields"), QStringLiteral("name")});

    // Without a mod there is nothing to compare, so nothing is loaded.
    const Response response = run({QStringLiteral("diff")});
    QCOMPARE(response.exitCode, 5);
    QCOMPARE(errorCode(response), QStringLiteral("mod_required"));
}

void CliProcessTest::diffShowsASet()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeGame(game));
    const int original = hitPointsIn(run(withGame(game, {QStringLiteral("get"), QStringLiteral("unit"),
                                                         QStringLiteral("4"), QStringLiteral("--civ"),
                                                         QStringLiteral("1"), QStringLiteral("--fields"),
                                                         QStringLiteral("hit_points")})));
    QVERIFY(original >= 0);
    QCOMPARE(run(withGame(game, {QStringLiteral("set"), QStringLiteral("unit"), QStringLiteral("4"), QStringLiteral("--civ"),
                                 QStringLiteral("1"), QStringLiteral("hit_points=") + QString::number(original + 1)}))
                 .exitCode,
             0);

    const Response response = run(withGame(game, {QStringLiteral("diff"), QStringLiteral("unit"), QStringLiteral("--civ"),
                                                  QStringLiteral("1"), QStringLiteral("--fields"), QStringLiteral("hit_points"),
                                                  QStringLiteral("--limit"), QStringLiteral("5")}));
    QCOMPARE(response.exitCode, 0);
    QCOMPARE(response.body.value(QStringLiteral("kind")).toString(), QStringLiteral("unit"));
    QCOMPARE(response.body.value(QStringLiteral("civ")).toInt(), 1);
    QCOMPARE(response.body.value(QStringLiteral("limit")).toInt(), 5);
    QCOMPARE(response.body.value(QStringLiteral("total")).toInt(), 1);
    const QJsonObject item = response.body.value(QStringLiteral("items")).toArray().at(0).toObject();
    QCOMPARE(item.value(QStringLiteral("id")).toInt(), 4);
    const QJsonObject field = item.value(QStringLiteral("fields")).toArray().at(0).toObject();
    QCOMPARE(field.value(QStringLiteral("key")).toString(), QStringLiteral("hit_points"));
    QCOMPARE(field.value(QStringLiteral("mod")).toInt(), original + 1);
    QCOMPARE(field.value(QStringLiteral("game")).toInt(), original);
}

QTEST_GUILESS_MAIN(CliProcessTest)
#include "CliProcessTest.moc"
