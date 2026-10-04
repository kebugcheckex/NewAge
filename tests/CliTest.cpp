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

QJsonObject lookup(const QString &table, const QString &text = {})
{
    QJsonObject request = op(QStringLiteral("lookup"));
    request.insert(QStringLiteral("table"), table);
    if (!text.isEmpty())
        request.insert(QStringLiteral("text"), text);
    return request;
}

QJsonObject list(const QString &kind)
{
    QJsonObject request = op(QStringLiteral("list"));
    request.insert(QStringLiteral("kind"), kind);
    return request;
}

QList<int> itemIds(const HandlerResult &result)
{
    QList<int> ids;
    for (const QJsonValue &value : result.body.value(QStringLiteral("items")).toArray())
        ids.append(value.toObject().value(QStringLiteral("id")).toInt());
    return ids;
}

DataSource tcSource()
{
    DataSource source;
    source.datPath = kTcDat;
    source.versionKey = QStringLiteral("tc");
    return source;
}

QString errorCode(const HandlerResult &result)
{
    return result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
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
    void schemaRejectsUnknownKindBeforeOpen();
    void schemaIncludesConditionalDescriptors();
    void schemaAllKinds();
    void lookupChecksRequestBeforeOpen_data();
    void lookupChecksRequestBeforeOpen();
    void lookupUnknownCiv();
    void lookupRanksMatches();
    void lookupMatchesInternalNames();
    void lookupWholeTable();
    void lookupFixedTables();
    void listChecksRequestBeforeOpen_data();
    void listChecksRequestBeforeOpen();
    void listUnknownCiv();
    void listGlobalKinds();
    void listHidesEmptyUnitSlots();
    void listTechsByOwnerAndAvailability();
    void listPages();
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

void CliTest::schemaRejectsUnknownKindBeforeOpen()
{
    QJsonObject request = op(QStringLiteral("schema"));
    request.insert(QStringLiteral("kind"), QStringLiteral("missing"));
    const HandlerResult result = RequestHandler().handle({}, request);
    QCOMPARE(result.exitCode, 3);
    const QJsonObject error = result.body.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("unknown_kind"));
    QCOMPARE(error.value(QStringLiteral("kind")).toString(), QStringLiteral("missing"));
}

void CliTest::schemaIncludesConditionalDescriptors()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataSource source;
    source.datPath = kTcDat;
    source.versionKey = QStringLiteral("tc");
    QJsonObject request = op(QStringLiteral("schema"));
    request.insert(QStringLiteral("kind"), QStringLiteral("unit"));
    const HandlerResult result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("kind")).toString(), QStringLiteral("unit"));
    QCOMPARE(result.body.value(QStringLiteral("perCiv")).toBool(), true);
    QJsonObject hitPoints;
    QJsonObject cost;
    QJsonObject internalName;
    for (const QJsonValue &value : result.body.value(QStringLiteral("fields")).toArray())
    {
        const QJsonObject field = value.toObject();
        if (field.value(QStringLiteral("key")) == QStringLiteral("hit_points"))
            hitPoints = field;
        if (field.value(QStringLiteral("key")) == QStringLiteral("cost1.resource"))
            cost = field;
        if (field.value(QStringLiteral("key")) == QStringLiteral("internal_name"))
            internalName = field;
    }
    QCOMPARE(hitPoints.value(QStringLiteral("type")).toString(), QStringLiteral("int"));
    QCOMPARE(hitPoints.value(QStringLiteral("min")).toInt(), -32768);
    QCOMPARE(hitPoints.value(QStringLiteral("max")).toInt(), 32767);
    QCOMPARE(cost.value(QStringLiteral("conditional")).toBool(), true);
    QCOMPARE(cost.value(QStringLiteral("labelKind")).toString(), QStringLiteral("resource"));
    QCOMPARE(internalName.value(QStringLiteral("type")).toString(), QStringLiteral("string"));
    QCOMPARE(internalName.value(QStringLiteral("editable")).toBool(), false);
}

void CliTest::schemaAllKinds()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataSource source;
    source.datPath = kTcDat;
    source.versionKey = QStringLiteral("tc");
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("schema")));
    QCOMPARE(result.exitCode, 0);
    const QJsonArray kinds = result.body.value(QStringLiteral("kinds")).toArray();
    QCOMPARE(kinds.size(), 4);
    QCOMPARE(kinds.at(0).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("civ"));
    QCOMPARE(kinds.at(0).toObject().value(QStringLiteral("fields")).toArray().size(), 0);
    QCOMPARE(kinds.at(3).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
    const QJsonArray effects = kinds.at(3).toObject().value(QStringLiteral("fields")).toArray();
    bool foundTemplate = false;
    for (const QJsonValue &value : effects)
        foundTemplate |= value.toObject().value(QStringLiteral("key")) == QStringLiteral("commandN.tech");
    QVERIFY(foundTemplate);
}

void CliTest::lookupChecksRequestBeforeOpen_data()
{
    QTest::addColumn<QJsonObject>("request");
    QTest::addColumn<QString>("code");

    QTest::newRow("no table") << op(QStringLiteral("lookup")) << QStringLiteral("usage");
    QTest::newRow("unknown table") << lookup(QStringLiteral("bogus")) << QStringLiteral("unknown_kind");
    QTest::newRow("unit without civ") << lookup(QStringLiteral("unit"), QStringLiteral("archer"))
                                      << QStringLiteral("usage");
    QJsonObject techWithCiv = lookup(QStringLiteral("tech"));
    techWithCiv.insert(QStringLiteral("civ"), 1);
    QTest::newRow("civ on a global table") << techWithCiv << QStringLiteral("usage");
    QJsonObject fractionalCiv = lookup(QStringLiteral("unit"));
    fractionalCiv.insert(QStringLiteral("civ"), 1.5);
    QTest::newRow("fractional civ") << fractionalCiv << QStringLiteral("usage");
    QJsonObject numericText = lookup(QStringLiteral("civ"));
    numericText.insert(QStringLiteral("text"), 3);
    QTest::newRow("text not a string") << numericText << QStringLiteral("usage");
}

void CliTest::lookupChecksRequestBeforeOpen()
{
    QFETCH(QJsonObject, request);
    QFETCH(QString, code);
    // No source: a request that got as far as opening would fail with no_dataset.
    const HandlerResult result = RequestHandler().handle({}, request);
    QCOMPARE(errorCode(result), code);
    QCOMPARE(result.exitCode, exitCodeFor(code));
}

void CliTest::lookupUnknownCiv()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    QJsonObject request = lookup(QStringLiteral("unit"), QStringLiteral("archer"));
    request.insert(QStringLiteral("civ"), 999);
    const HandlerResult result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 3);
    const QJsonObject error = result.body.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("unknown_entity"));
    QCOMPARE(error.value(QStringLiteral("kind")).toString(), QStringLiteral("civ"));
    QCOMPARE(error.value(QStringLiteral("id")).toInt(), 999);
}

void CliTest::lookupRanksMatches()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    const HandlerResult result =
        RequestHandler().handle(tcSource(), lookup(QStringLiteral("resource"), QStringLiteral("GOLD")));
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("table")).toString(), QStringLiteral("resource"));
    QCOMPARE(result.body.value(QStringLiteral("query")).toString(), QStringLiteral("GOLD"));
    QVERIFY(!result.body.contains(QStringLiteral("civ")));

    const QJsonArray matches = result.body.value(QStringLiteral("matches")).toArray();
    QVERIFY(matches.size() > 1);
    QCOMPARE(matches.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Gold Storage"));
    QVERIFY(!matches.first().toObject().contains(QStringLiteral("internalName")));
    // Prefixes before substrings, then by ID within each.
    int rank = 0;
    int lastId = -1;
    for (const QJsonValue &value : matches)
    {
        const QJsonObject match = value.toObject();
        const QString name = match.value(QStringLiteral("name")).toString();
        const bool prefix = name.startsWith(QStringLiteral("gold"), Qt::CaseInsensitive);
        QVERIFY(name.contains(QStringLiteral("gold"), Qt::CaseInsensitive));
        QCOMPARE(match.value(QStringLiteral("match")).toString(),
                 prefix ? QStringLiteral("prefix") : QStringLiteral("substring"));
        const int matchRank = prefix ? 1 : 2;
        QVERIFY(matchRank >= rank);
        if (matchRank != rank)
            lastId = -1;
        QVERIFY(match.value(QStringLiteral("id")).toInt() > lastId);
        rank = matchRank;
        lastId = match.value(QStringLiteral("id")).toInt();
    }
    QCOMPARE(rank, 2);
}

void CliTest::lookupMatchesInternalNames()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    // A loose file has no language strings, so names are internal names.
    QJsonObject request = lookup(QStringLiteral("unit"), QStringLiteral("  archr "));
    request.insert(QStringLiteral("civ"), 1);
    HandlerResult result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("civ")).toInt(), 1);
    QCOMPARE(result.body.value(QStringLiteral("query")).toString(), QStringLiteral("archr"));
    QJsonArray matches = result.body.value(QStringLiteral("matches")).toArray();
    QVERIFY(!matches.isEmpty());
    const QJsonObject archer = matches.first().toObject();
    QCOMPARE(archer.value(QStringLiteral("internalName")).toString(), QStringLiteral("ARCHR"));
    QCOMPARE(archer.value(QStringLiteral("match")).toString(), QStringLiteral("exact"));
    for (qsizetype i = 1; i < matches.size(); ++i)
        QVERIFY(matches.at(i).toObject().value(QStringLiteral("match")).toString() != QStringLiteral("exact"));

    result = RequestHandler().handle(tcSource(), lookup(QStringLiteral("civ"), QStringLiteral("spanish")));
    QCOMPARE(result.exitCode, 0);
    matches = result.body.value(QStringLiteral("matches")).toArray();
    QCOMPARE(matches.size(), 1);
    const int spanish = matches.first().toObject().value(QStringLiteral("id")).toInt();

    // Techs report their owner civ; the Spanish unique tech names it.
    result = RequestHandler().handle(tcSource(), lookup(QStringLiteral("tech"), QStringLiteral("supremacy")));
    QCOMPARE(result.exitCode, 0);
    matches = result.body.value(QStringLiteral("matches")).toArray();
    QVERIFY(!matches.isEmpty());
    for (const QJsonValue &value : matches)
        QVERIFY(value.toObject().contains(QStringLiteral("ownerCiv")));
    QCOMPARE(matches.first().toObject().value(QStringLiteral("ownerCiv")).toInt(), spanish);
}

void CliTest::lookupWholeTable()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    const Session &session = service.session();

    HandlerResult result = RequestHandler().handle(tcSource(), lookup(QStringLiteral("civ")));
    QCOMPARE(result.exitCode, 0);
    QVERIFY(!result.body.contains(QStringLiteral("query")));
    QJsonArray matches = result.body.value(QStringLiteral("matches")).toArray();
    QCOMPARE(matches.size(), civKind().count(session, 0));
    for (qsizetype i = 0; i < matches.size(); ++i)
    {
        const QJsonObject match = matches.at(i).toObject();
        QCOMPARE(match.value(QStringLiteral("id")).toInt(), i);
        QVERIFY(!match.contains(QStringLiteral("match")));
    }

    // Empty unit slots are left out.
    QJsonObject request = lookup(QStringLiteral("unit"));
    request.insert(QStringLiteral("civ"), 1);
    result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    matches = result.body.value(QStringLiteral("matches")).toArray();
    int active = 0;
    for (int id = 0; id < unitKind().count(session, 1); ++id)
        active += unitKind().isActive(session, 1, id) ? 1 : 0;
    QVERIFY(active < unitKind().count(session, 1));
    QCOMPARE(matches.size(), active);
    for (const QJsonValue &value : matches)
        QVERIFY(unitKind().isActive(session, 1, value.toObject().value(QStringLiteral("id")).toInt()));
}

void CliTest::lookupFixedTables()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    const QStringList tables = lookupTables();
    QCOMPARE(tables, (QStringList{QStringLiteral("civ"), QStringLiteral("unit"), QStringLiteral("tech"),
                                  QStringLiteral("effect"), QStringLiteral("resource"), QStringLiteral("unit-class"),
                                  QStringLiteral("attribute"), QStringLiteral("effect-type"),
                                  QStringLiteral("unit-type"), QStringLiteral("tech-type")}));
    for (const QString &table : tables.mid(4))
    {
        const HandlerResult result = RequestHandler().handle(tcSource(), lookup(table));
        QCOMPARE(result.exitCode, 0);
        const QJsonArray matches = result.body.value(QStringLiteral("matches")).toArray();
        QVERIFY2(!matches.isEmpty(), qPrintable(table));
        int lastId = -1;
        for (const QJsonValue &value : matches)
        {
            const QJsonObject match = value.toObject();
            QVERIFY(match.value(QStringLiteral("id")).toInt() > lastId);
            lastId = match.value(QStringLiteral("id")).toInt();
            QVERIFY2(!match.value(QStringLiteral("name")).toString().isEmpty(), qPrintable(table));
            QVERIFY(!match.contains(QStringLiteral("internalName")));
        }
    }

    const HandlerResult types = RequestHandler().handle(tcSource(), lookup(QStringLiteral("tech-type")));
    const QJsonArray techTypes = types.body.value(QStringLiteral("matches")).toArray();
    QCOMPARE(techTypes.size(), 2);
    QCOMPARE(techTypes.at(1).toObject().value(QStringLiteral("id")).toInt(), 2);
    QCOMPARE(techTypes.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Age"));

    const HandlerResult effectTypes =
        RequestHandler().handle(tcSource(), lookup(QStringLiteral("effect-type"), QStringLiteral("disable tech")));
    const QJsonArray disable = effectTypes.body.value(QStringLiteral("matches")).toArray();
    QCOMPARE(disable.size(), 1);
    QCOMPARE(disable.first().toObject().value(QStringLiteral("id")).toInt(), 102);
    QCOMPARE(disable.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Disable Tech"));
}

void CliTest::listChecksRequestBeforeOpen_data()
{
    QTest::addColumn<QJsonObject>("request");
    QTest::addColumn<QString>("code");

    const auto with = [](QJsonObject request, const QString &key, const QJsonValue &value) {
        request.insert(key, value);
        return request;
    };
    const QJsonObject unit = with(list(QStringLiteral("unit")), QStringLiteral("civ"), 1);
    QTest::newRow("no kind") << op(QStringLiteral("list")) << QStringLiteral("usage");
    QTest::newRow("unknown kind") << list(QStringLiteral("bogus")) << QStringLiteral("unknown_kind");
    QTest::newRow("unit without civ") << list(QStringLiteral("unit")) << QStringLiteral("usage");
    QTest::newRow("civ on effects") << with(list(QStringLiteral("effect")), QStringLiteral("civ"), 1)
                                    << QStringLiteral("usage");
    QTest::newRow("owner civ on units") << with(unit, QStringLiteral("ownerCiv"), 1) << QStringLiteral("usage");
    QTest::newRow("fractional owner civ") << with(list(QStringLiteral("tech")), QStringLiteral("ownerCiv"), 1.5)
                                          << QStringLiteral("usage");
    QTest::newRow("all not a bool") << with(unit, QStringLiteral("all"), 1) << QStringLiteral("usage");
    QTest::newRow("negative offset") << with(unit, QStringLiteral("offset"), -1) << QStringLiteral("usage");
    QTest::newRow("negative limit") << with(unit, QStringLiteral("limit"), -1) << QStringLiteral("usage");
}

void CliTest::listChecksRequestBeforeOpen()
{
    QFETCH(QJsonObject, request);
    QFETCH(QString, code);
    const HandlerResult result = RequestHandler().handle({}, request);
    QCOMPARE(errorCode(result), code);
    QCOMPARE(result.exitCode, exitCodeFor(code));
}

void CliTest::listUnknownCiv()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    for (const QString &key : {QStringLiteral("civ"), QStringLiteral("ownerCiv")})
    {
        QJsonObject request = list(QStringLiteral("tech"));
        request.insert(key, 999);
        const HandlerResult result = RequestHandler().handle(tcSource(), request);
        QCOMPARE(errorCode(result), QStringLiteral("unknown_entity"));
        const QJsonObject error = result.body.value(QStringLiteral("error")).toObject();
        QCOMPARE(error.value(QStringLiteral("kind")).toString(), QStringLiteral("civ"));
        QCOMPARE(error.value(QStringLiteral("id")).toInt(), 999);
    }
}

void CliTest::listGlobalKinds()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    for (const EntityKind *kind : {&civKind(), &effectKind()})
    {
        const HandlerResult result = RequestHandler().handle(tcSource(), list(kind->key()));
        QCOMPARE(result.exitCode, 0);
        QCOMPARE(result.body.value(QStringLiteral("kind")).toString(), kind->key());
        QCOMPARE(result.body.value(QStringLiteral("offset")).toInt(), 0);
        QVERIFY(!result.body.contains(QStringLiteral("limit")));
        QVERIFY(!result.body.contains(QStringLiteral("civ")));
        const int count = kind->count(service.session(), 0);
        QCOMPARE(result.body.value(QStringLiteral("total")).toInt(), count);
        const QJsonArray items = result.body.value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), count);
        for (qsizetype i = 0; i < items.size(); ++i)
        {
            const QJsonObject item = items.at(i).toObject();
            QCOMPARE(item.value(QStringLiteral("id")).toInt(), i);
            QCOMPARE(item.value(QStringLiteral("name")).toString(), kind->name(service.session(), -1, int(i)));
            QVERIFY(item.contains(QStringLiteral("internalName")));
            QVERIFY(!item.contains(QStringLiteral("ownerCiv")));
            QVERIFY(!item.contains(QStringLiteral("active")));
        }
    }
}

void CliTest::listHidesEmptyUnitSlots()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    const Session &session = service.session();
    QList<int> active;
    for (int id = 0; id < unitKind().count(session, 1); ++id)
    {
        if (unitKind().isActive(session, 1, id))
            active.append(id);
    }
    QVERIFY(active.size() < unitKind().count(session, 1));

    QJsonObject request = list(QStringLiteral("unit"));
    request.insert(QStringLiteral("civ"), 1);
    HandlerResult result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("civ")).toInt(), 1);
    QCOMPARE(result.body.value(QStringLiteral("total")).toInt(), active.size());
    QCOMPARE(itemIds(result), active);
    QVERIFY(!result.body.value(QStringLiteral("items")).toArray().first().toObject().contains(QStringLiteral("active")));

    // all keeps empty slots and says which rows are active.
    request.insert(QStringLiteral("all"), true);
    result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("all")).toBool(), true);
    const QJsonArray items = result.body.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), unitKind().count(session, 1));
    for (qsizetype id = 0; id < items.size(); ++id)
    {
        const QJsonObject item = items.at(id).toObject();
        QCOMPARE(item.value(QStringLiteral("active")).toBool(), active.contains(int(id)));
        if (!active.contains(int(id)))
            QCOMPARE(item.value(QStringLiteral("name")).toString(), QStringLiteral("(empty)"));
    }
}

void CliTest::listTechsByOwnerAndAvailability()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    const genie::DatFile &dat = *service.session().dat();
    const int spanish = service.lookup(QStringLiteral("civ"), QStringLiteral("spanish")).matches.value(0).id;
    QVERIFY(spanish > 1);

    // Without a civ nothing is hidden; ownerCiv finds the Spanish techs.
    QJsonObject request = list(QStringLiteral("tech"));
    request.insert(QStringLiteral("ownerCiv"), spanish);
    HandlerResult result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("ownerCiv")).toInt(), spanish);
    QList<int> owned;
    for (int id = 0; id < int(dat.Techs.size()); ++id)
    {
        if (dat.Techs[id].Civ == spanish)
            owned.append(id);
    }
    QVERIFY(!owned.isEmpty());
    QCOMPARE(itemIds(result), owned);
    for (const QJsonValue &value : result.body.value(QStringLiteral("items")).toArray())
        QCOMPARE(value.toObject().value(QStringLiteral("ownerCiv")).toInt(), spanish);

    // Civ 1 can't research Spanish techs: hidden, or shown inactive with all.
    request.insert(QStringLiteral("civ"), 1);
    result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("total")).toInt(), 0);
    request.insert(QStringLiteral("all"), true);
    result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(itemIds(result), owned);
    for (const QJsonValue &value : result.body.value(QStringLiteral("items")).toArray())
        QCOMPARE(value.toObject().value(QStringLiteral("active")).toBool(), false);

    // With a civ, exactly the techs it can research are listed.
    request = list(QStringLiteral("tech"));
    request.insert(QStringLiteral("civ"), 1);
    result = RequestHandler().handle(tcSource(), request);
    QList<int> available;
    const QList<TechAvailability> availability = techAvailability(service.session(), 1);
    for (int id = 0; id < availability.size(); ++id)
    {
        if (availability.at(id) == TechAvailability::Available)
            available.append(id);
    }
    QVERIFY(available.size() < availability.size());
    QCOMPARE(itemIds(result), available);
}

void CliTest::listPages()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    QJsonObject request = list(QStringLiteral("unit"));
    request.insert(QStringLiteral("civ"), 1);
    const HandlerResult whole = RequestHandler().handle(tcSource(), request);
    const QList<int> ids = itemIds(whole);
    QVERIFY(ids.size() > 10);

    request.insert(QStringLiteral("offset"), 5);
    request.insert(QStringLiteral("limit"), 3);
    HandlerResult page = RequestHandler().handle(tcSource(), request);
    QCOMPARE(page.exitCode, 0);
    QCOMPARE(page.body.value(QStringLiteral("offset")).toInt(), 5);
    QCOMPARE(page.body.value(QStringLiteral("limit")).toInt(), 3);
    QCOMPARE(page.body.value(QStringLiteral("total")).toInt(), ids.size());
    QCOMPARE(itemIds(page), ids.mid(5, 3));

    // Past the end: no rows, same total.
    request.insert(QStringLiteral("offset"), int(ids.size()));
    page = RequestHandler().handle(tcSource(), request);
    QCOMPARE(page.exitCode, 0);
    QVERIFY(itemIds(page).isEmpty());
    QCOMPARE(page.body.value(QStringLiteral("total")).toInt(), ids.size());
}

QTEST_GUILESS_MAIN(CliTest)
#include "CliTest.moc"
