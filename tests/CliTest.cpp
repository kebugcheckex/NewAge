#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "api/RequestHandler.h"
#include "genie/dat/DatFile.h"

using namespace newage;

namespace {

const QString kTcDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x1_p1.dat");

bool writeFile(const QString &root, const QString &relative)
{
    const QString path = QDir(root).filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly);
}

QString forwardPath(const QString &path)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

QJsonObject op(const QString &name)
{
    QJsonObject request;
    request.insert(QStringLiteral("op"), name);
    return request;
}

} // namespace

class CliTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void exitCodesMatchTheTable();
    void missingOpIsUsage();
    void unknownOpDoesNotOpen();
    void missingSourceIsNoDataset();
    void looseFileWithoutVersionIsLoadFailed();
    void modsUnsupported();
    void modNotFound();
    void infoResolvesGameFromEnvironment();
    void infoOnLooseFile();
    void infoOnGameFolder();
};

void CliTest::init()
{
    qunsetenv("NEWAGE_GAME");
    qunsetenv("NEWAGE_DATASET");
    qunsetenv("NEWAGE_MOD");
    qunsetenv("NEWAGE_MODS_FOLDER");
}

void CliTest::exitCodesMatchTheTable()
{
    QCOMPARE(exitCodeFor({}), 0);
    QCOMPARE(exitCodeFor(QStringLiteral("usage")), 1);
    QCOMPARE(exitCodeFor(QStringLiteral("load_failed")), 2);
    QCOMPARE(exitCodeFor(QStringLiteral("no_dataset")), 2);
    QCOMPARE(exitCodeFor(QStringLiteral("mod_not_found")), 2);
    QCOMPARE(exitCodeFor(QStringLiteral("mods_unsupported")), 2);
    QCOMPARE(exitCodeFor(QStringLiteral("unknown_kind")), 3);
    QCOMPARE(exitCodeFor(QStringLiteral("unknown_entity")), 3);
    QCOMPARE(exitCodeFor(QStringLiteral("inactive_entity")), 3);
    QCOMPARE(exitCodeFor(QStringLiteral("unknown_field")), 3);
    QCOMPARE(exitCodeFor(QStringLiteral("not_applicable")), 3);
    QCOMPARE(exitCodeFor(QStringLiteral("read_only")), 3);
    QCOMPARE(exitCodeFor(QStringLiteral("bad_value")), 3);
    QCOMPARE(exitCodeFor(QStringLiteral("out_of_range")), 3);
    QCOMPARE(exitCodeFor(QStringLiteral("conflict")), 4);
    QCOMPARE(exitCodeFor(QStringLiteral("mod_required")), 5);
    QCOMPARE(exitCodeFor(QStringLiteral("game_data_protected")), 5);
    QCOMPARE(exitCodeFor(QStringLiteral("save_failed")), 5);
    QCOMPARE(exitCodeFor(QStringLiteral("no_such_code")), 1);
}

void CliTest::missingOpIsUsage()
{
    const HandlerResult result = RequestHandler().handle({}, {});
    QCOMPARE(result.exitCode, 1);
    const QJsonObject error = result.body.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("usage"));
    QCOMPARE(error.value(QStringLiteral("message")).toString(), QStringLiteral("Missing op."));
    QVERIFY(!error.contains(QStringLiteral("id")));
}

void CliTest::unknownOpDoesNotOpen()
{
    DataSource source;
    source.datPath = QStringLiteral("missing.dat");
    source.versionKey = QStringLiteral("tc");
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("set")));
    QCOMPARE(result.exitCode, 1);
    QCOMPARE(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("usage"));
    QVERIFY(result.body.value(QStringLiteral("error"))
                .toObject()
                .value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("set")));
}

void CliTest::missingSourceIsNoDataset()
{
    const HandlerResult result = RequestHandler().handle({}, op(QStringLiteral("info")));
    QCOMPARE(result.exitCode, 2);
    const QJsonObject error = result.body.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("no_dataset"));
    QVERIFY(!error.value(QStringLiteral("message")).toString().isEmpty());
    QVERIFY(!error.contains(QStringLiteral("kind")));
}

void CliTest::looseFileWithoutVersionIsLoadFailed()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    DataSource source;
    source.datPath = kTcDat;
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("info")));
    QCOMPARE(result.exitCode, 2);
    QCOMPARE(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("load_failed"));
}

void CliTest::modsUnsupported()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(writeFile(game.path(), QStringLiteral("data/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    source.mod = QStringLiteral("Balance");
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("info")));
    QCOMPARE(result.exitCode, 2);
    QCOMPARE(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("mods_unsupported"));
}

void CliTest::modNotFound()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x2_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    source.mod = QStringLiteral("Missing");
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("info")));
    QCOMPARE(result.exitCode, 2);
    QCOMPARE(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("mod_not_found"));
}

void CliTest::infoResolvesGameFromEnvironment()
{
    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    qputenv("NEWAGE_GAME", empty.path().toUtf8());

    const HandlerResult result = RequestHandler().handle({}, op(QStringLiteral("info")));
    QCOMPARE(result.exitCode, 2);
    const QJsonObject error = result.body.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("no_dataset"));
    QVERIFY(error.value(QStringLiteral("message")).toString().contains(QDir::toNativeSeparators(empty.path())));
}

void CliTest::infoOnLooseFile()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    DataSource source;
    source.datPath = kTcDat;
    source.versionKey = QStringLiteral("tc");
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("info")));
    QCOMPARE(result.exitCode, 0);
    QVERIFY(result.warnings.isEmpty());
    QVERIFY(!result.body.contains(QStringLiteral("error")));

    QCOMPARE(result.body.value(QStringLiteral("game")).toString(), QString());
    QCOMPARE(result.body.value(QStringLiteral("dataset")).toString(), QStringLiteral("empires2_x1_p1.dat"));
    QCOMPARE(result.body.value(QStringLiteral("version")).toString(), QStringLiteral("tc"));
    QCOMPARE(result.body.value(QStringLiteral("mod")).toString(), QString());
    QCOMPARE(result.body.value(QStringLiteral("dat")).toString(), forwardPath(kTcDat));
    QCOMPARE(result.body.value(QStringLiteral("readOnly")).toBool(), true);
    QVERIFY(!result.body.value(QStringLiteral("fileVersion")).toString().isEmpty());

    const QJsonArray kinds = result.body.value(QStringLiteral("kinds")).toArray();
    QCOMPARE(kinds.size(), 4);
    QCOMPARE(kinds.at(0).toString(), QStringLiteral("civ"));
    QCOMPARE(kinds.at(1).toString(), QStringLiteral("unit"));
    QCOMPARE(kinds.at(2).toString(), QStringLiteral("tech"));
    QCOMPARE(kinds.at(3).toString(), QStringLiteral("effect"));

    DataService service;
    const OpenResult opened = service.open(source);
    QVERIFY2(opened.ok, qPrintable(opened.error.message));
    const QJsonObject counts = result.body.value(QStringLiteral("counts")).toObject();
    for (const EntityKind *kind : service.kinds())
        QCOMPARE(counts.value(kind->key()).toInt(), kind->count(service.session(), 0));
    QCOMPARE(result.body.value(QStringLiteral("fileVersion")).toString(),
             QString::fromLatin1(service.session().dat()->FileVersion.c_str()));
    QVERIFY(counts.value(QStringLiteral("civ")).toInt() > 0);
    QVERIFY(counts.value(QStringLiteral("unit")).toInt() > 0);
}

void CliTest::infoOnGameFolder()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString copied = QDir(game.path()).filePath(QStringLiteral("data/empires2_x1_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(copied).absolutePath()));
    QVERIFY(QFile::copy(kTcDat, copied));

    DataSource source;
    source.gameDir = game.path();
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("info")));
    if (result.exitCode != 0)
        QFAIL(qPrintable(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
    QCOMPARE(result.body.value(QStringLiteral("game")).toString(), forwardPath(QDir(game.path()).absolutePath()));
    QCOMPARE(result.body.value(QStringLiteral("dataset")).toString(), QStringLiteral("empires2_x1_p1.dat"));
    QCOMPARE(result.body.value(QStringLiteral("version")).toString(), QStringLiteral("tc"));
    QCOMPARE(result.body.value(QStringLiteral("mod")).toString(), QString());
    QCOMPARE(result.body.value(QStringLiteral("dat")).toString(), forwardPath(copied));
    QCOMPARE(result.body.value(QStringLiteral("readOnly")).toBool(), true);
    QVERIFY(result.body.value(QStringLiteral("counts")).toObject().value(QStringLiteral("civ")).toInt() > 0);
}

QTEST_GUILESS_MAIN(CliTest)
#include "CliTest.moc"
