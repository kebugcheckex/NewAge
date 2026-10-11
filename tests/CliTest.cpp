#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "api/RequestHandler.h"
#include "genie/dat/DatFile.h"
#include "model/EffectFields.h"
#include "model/RefNames.h"

using namespace newage;

namespace {

const QString kTcDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x1_p1.dat");
const QString kHdDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x2_p1.dat");

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

QJsonObject get(const QString &kind, const QList<int> &ids)
{
    QJsonObject request = op(QStringLiteral("get"));
    request.insert(QStringLiteral("kind"), kind);
    QJsonArray array;
    for (const int id : ids)
        array.append(id);
    request.insert(QStringLiteral("ids"), array);
    return request;
}

QStringList fieldKeys(const QJsonObject &item)
{
    QStringList keys;
    for (const QJsonValue &field : item.value(QStringLiteral("fields")).toArray())
        keys.append(field.toObject().value(QStringLiteral("key")).toString());
    return keys;
}

QJsonObject fieldByKey(const QJsonObject &item, const QString &key)
{
    for (const QJsonValue &field : item.value(QStringLiteral("fields")).toArray())
    {
        if (field.toObject().value(QStringLiteral("key")).toString() == key)
            return field.toObject();
    }
    return {};
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

QJsonObject batch(const QJsonArray &requests)
{
    QJsonObject request = op(QStringLiteral("batch"));
    request.insert(QStringLiteral("requests"), requests);
    return request;
}

QString errorCode(const QJsonValue &body)
{
    return body.toObject().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

// An HD game folder whose .dat files are empty, so any load fails. Its mods
// folder has Balance, with its own copy of the HD .dat, and Empty, without.
bool makeModdedGame(const QTemporaryDir &game)
{
    if (!writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"))
        || !writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")))
        return false;
    const QString folder = game.filePath(QStringLiteral("mods"));
    const QString balance =
        createMod(folder, {QStringLiteral("Balance"), QStringLiteral("Me"), QStringLiteral("Cheaper archers")});
    return !balance.isEmpty() && writeFile(balance, QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"))
           && !createMod(folder, {QStringLiteral("Empty"), {}, {}}).isEmpty();
}

// A game folder holding the HD sample .dat (and an empty TC one), with a mods
// folder that has an empty mod called Balance.
bool makeWritableGame(const QTemporaryDir &game)
{
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    if (!QDir().mkpath(QFileInfo(gameDat).absolutePath()) || !QFile::copy(kHdDat, gameDat)
        || !writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")))
        return false;
    return !createMod(game.filePath(QStringLiteral("mods")), {QStringLiteral("Balance"), {}, {}}).isEmpty();
}

DataSource balanceSource(const QTemporaryDir &game)
{
    DataSource source;
    source.gameDir = game.path();
    source.mod = QStringLiteral("Balance");
    return source;
}

QJsonObject setRequest(int id, int civ, const QJsonObject &values)
{
    QJsonObject request = op(QStringLiteral("set"));
    request.insert(QStringLiteral("kind"), QStringLiteral("unit"));
    request.insert(QStringLiteral("id"), id);
    request.insert(QStringLiteral("civ"), civ);
    request.insert(QStringLiteral("set"), values);
    return request;
}

QJsonObject hitPointsOf(int hitPoints)
{
    QJsonObject values;
    values.insert(QStringLiteral("hit_points"), hitPoints);
    return values;
}

// The stored hit points of unit 4 in civ 1, read through `get`.
int readHitPoints(const DataSource &source)
{
    QJsonObject request = get(QStringLiteral("unit"), {4});
    request.insert(QStringLiteral("civ"), 1);
    request.insert(QStringLiteral("fields"), QJsonArray{QStringLiteral("hit_points")});
    const HandlerResult result = RequestHandler().handle(source, request);
    if (result.exitCode != 0)
        return -1;
    return fieldByKey(result.body.value(QStringLiteral("items")).toArray().at(0).toObject(),
                      QStringLiteral("hit_points"))
        .value(QStringLiteral("value"))
        .toInt(-1);
}

QJsonObject modsCreate(const QString &title)
{
    QJsonObject request = op(QStringLiteral("mods-create"));
    request.insert(QStringLiteral("title"), title);
    return request;
}

QJsonObject diff(const QString &kind = {})
{
    QJsonObject request = op(QStringLiteral("diff"));
    if (!kind.isEmpty())
        request.insert(QStringLiteral("kind"), kind);
    return request;
}

QList<int> intList(const QJsonValue &value)
{
    QList<int> list;
    for (const QJsonValue &entry : value.toArray())
        list.append(entry.toInt());
    return list;
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
    void getChecksRequestBeforeOpen_data();
    void getChecksRequestBeforeOpen();
    void getUnknownEntities();
    void getUnitFields();
    void getFieldPatterns();
    void getCompact();
    void getEffectModes();
    void getTechActivityAndLabels();
    void batchNeedsRequests_data();
    void batchNeedsRequests();
    void batchChecksRequestsBeforeOpen();
    void batchOpenFailureFailsWhole();
    void batchRunsEachRequestInPlace();
    void batchRejectsModsList();
    void modsListReadsNoData();
    void modsListFolderOverride();
    void modsListFails_data();
    void modsListFails();
    void writesCheckRequestBeforeOpen_data();
    void writesCheckRequestBeforeOpen();
    void writeNeedsAModBeforeLoading();
    void setWritesTheMod();
    void setDryRunAndUnchangedDoNotSave();
    void setExpectConflict();
    void setRejectsBadValues();
    void applyIsAtomicAndRoundTrips();
    void applyBuildsATechAndItsEffect();
    void applyUndoesEveryEditOnFailure();
    void batchRejectsWrites();
    void modsCreateMakesAMod();
    void modsCreateFromMod();
    void modsCreateFails();
    void diffChecksRequestBeforeOpen_data();
    void diffChecksRequestBeforeOpen();
    void diffNeedsAMod();
    void diffComparesModWithGame();
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
                                  QStringLiteral("unit-type"), QStringLiteral("tech-type"),
                                  QStringLiteral("resource-mode"), QStringLiteral("tech-modifier-mode"),
                                  QStringLiteral("enable-mode"),
                                  QStringLiteral("upgrade-mode")}));
    for (const QString &table : tables.mid(4))
    {
        const HandlerResult result = RequestHandler().handle(tcSource(), lookup(table));
        QCOMPARE(result.exitCode, 0);
        const QJsonArray matches = result.body.value(QStringLiteral("matches")).toArray();
        QVERIFY2(!matches.isEmpty(), qPrintable(table));
        // upgrade-mode starts at -1 (All).
        int lastId = -2;
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

    const HandlerResult upgradeModes = RequestHandler().handle(tcSource(), lookup(QStringLiteral("upgrade-mode")));
    const QJsonArray upgrade = upgradeModes.body.value(QStringLiteral("matches")).toArray();
    QCOMPARE(upgrade.size(), 2);
    QCOMPARE(upgrade.first().toObject().value(QStringLiteral("id")).toInt(), -1);
    QCOMPARE(upgrade.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("All"));
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

void CliTest::getChecksRequestBeforeOpen_data()
{
    QTest::addColumn<QJsonObject>("request");
    QTest::addColumn<QString>("code");

    const auto with = [](QJsonObject request, const QString &key, const QJsonValue &value) {
        request.insert(key, value);
        return request;
    };
    const QJsonObject tech = get(QStringLiteral("tech"), {1});
    QTest::newRow("no kind") << with(op(QStringLiteral("get")), QStringLiteral("ids"), QJsonArray({1}))
                             << QStringLiteral("usage");
    QTest::newRow("unknown kind") << get(QStringLiteral("bogus"), {1}) << QStringLiteral("unknown_kind");
    QTest::newRow("unit without civ") << get(QStringLiteral("unit"), {1}) << QStringLiteral("usage");
    QTest::newRow("civ on effects") << with(get(QStringLiteral("effect"), {1}), QStringLiteral("civ"), 1)
                                    << QStringLiteral("usage");
    QTest::newRow("no ids") << get(QStringLiteral("tech"), {}) << QStringLiteral("usage");
    QTest::newRow("ids not an array") << with(tech, QStringLiteral("ids"), 1) << QStringLiteral("usage");
    QTest::newRow("fractional id") << with(tech, QStringLiteral("ids"), QJsonArray({1.5}))
                                   << QStringLiteral("usage");
    QTest::newRow("empty fields") << with(tech, QStringLiteral("fields"), QJsonArray()) << QStringLiteral("usage");
    QTest::newRow("blank field") << with(tech, QStringLiteral("fields"), QJsonArray({QStringLiteral(" ")}))
                                 << QStringLiteral("usage");
    QTest::newRow("field not a string") << with(tech, QStringLiteral("fields"), QJsonArray({1}))
                                        << QStringLiteral("usage");
    QTest::newRow("compact not a bool") << with(tech, QStringLiteral("compact"), 1) << QStringLiteral("usage");
}

void CliTest::getChecksRequestBeforeOpen()
{
    QFETCH(QJsonObject, request);
    QFETCH(QString, code);
    const HandlerResult result = RequestHandler().handle({}, request);
    QCOMPARE(errorCode(result), code);
    QCOMPARE(result.exitCode, exitCodeFor(code));
}

void CliTest::getUnknownEntities()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    const Session &session = service.session();
    int empty = -1;
    for (int id = 0; id < unitKind().count(session, 1) && empty < 0; ++id)
    {
        if (!unitKind().isActive(session, 1, id))
            empty = id;
    }
    QVERIFY(empty >= 0);
    const int techs = techKind().count(session, 0);

    struct Case
    {
        QString kind;
        QList<int> ids;
        int civ;
        QString code;
        QString errorKind;
        int errorId;
        int errorCiv;
    };
    const QList<Case> cases = {
        {QStringLiteral("tech"), {1}, 999, QStringLiteral("unknown_entity"), QStringLiteral("civ"), 999, -1},
        {QStringLiteral("unit"), {4}, 999, QStringLiteral("unknown_entity"), QStringLiteral("civ"), 999, -1},
        {QStringLiteral("tech"), {1, techs}, -1, QStringLiteral("unknown_entity"), QStringLiteral("tech"), techs, -1},
        {QStringLiteral("effect"), {-1}, -1, QStringLiteral("unknown_entity"), QStringLiteral("effect"), -1, -1},
        {QStringLiteral("unit"), {4, empty}, 1, QStringLiteral("inactive_entity"), QStringLiteral("unit"), empty, 1},
    };
    for (const Case &item : cases)
    {
        QJsonObject request = get(item.kind, item.ids);
        if (item.civ != -1)
            request.insert(QStringLiteral("civ"), item.civ);
        const HandlerResult result = RequestHandler().handle(tcSource(), request);
        QCOMPARE(errorCode(result), item.code);
        QCOMPARE(result.exitCode, 3);
        QVERIFY(!result.body.contains(QStringLiteral("items")));
        const QJsonObject error = result.body.value(QStringLiteral("error")).toObject();
        QCOMPARE(error.value(QStringLiteral("kind")).toString(), item.errorKind);
        // The error body leaves out negative IDs and civs.
        QCOMPARE(error.value(QStringLiteral("id")).toInt(-1), item.errorId);
        QCOMPARE(error.value(QStringLiteral("civ")).toInt(-1), item.errorCiv);
    }
}

void CliTest::getUnitFields()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    const Session &session = service.session();
    const QList<FieldValue> fields = unitKind().fields(session, 1, 4);
    QVERIFY(!fields.isEmpty());

    // Duplicates and order are kept.
    QJsonObject request = get(QStringLiteral("unit"), {4, 83, 4});
    request.insert(QStringLiteral("civ"), 1);
    const HandlerResult result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("kind")).toString(), QStringLiteral("unit"));
    QCOMPARE(result.body.value(QStringLiteral("civ")).toInt(), 1);
    QCOMPARE(itemIds(result), QList<int>({4, 83, 4}));

    const QJsonObject archer = result.body.value(QStringLiteral("items")).toArray().first().toObject();
    QCOMPARE(archer.value(QStringLiteral("name")).toString(), unitKind().name(session, 1, 4));
    QCOMPARE(archer.value(QStringLiteral("internalName")).toString(), QStringLiteral("ARCHR"));
    QVERIFY(!archer.contains(QStringLiteral("active")));
    const QJsonArray rows = archer.value(QStringLiteral("fields")).toArray();
    QCOMPARE(rows.size(), fields.size());
    bool sawFloat = false;
    bool sawLabel = false;
    for (qsizetype i = 0; i < rows.size(); ++i)
    {
        const QJsonObject row = rows.at(i).toObject();
        const FieldValue &field = fields.at(i);
        QCOMPARE(row.value(QStringLiteral("key")).toString(), field.key);
        QCOMPARE(row.value(QStringLiteral("name")).toString(), field.name);
        QCOMPARE(row.value(QStringLiteral("group")).toString(), field.group);
        QCOMPARE(row.value(QStringLiteral("type")).toString(), field.type);
        QCOMPARE(row.value(QStringLiteral("editable")).toBool(), field.editable);
        QCOMPARE(row.contains(QStringLiteral("min")), field.minimum.has_value());
        QCOMPARE(row.contains(QStringLiteral("max")), field.maximum.has_value());
        if (field.type == QLatin1String("float"))
        {
            // The shortest form that reads back as the same float.
            sawFloat = true;
            const QJsonValue value = row.value(QStringLiteral("value"));
            QCOMPARE(static_cast<float>(value.toDouble()), field.value.toFloat());
            const QByteArray json = QJsonDocument(QJsonArray({value})).toJson(QJsonDocument::Compact);
            QVERIFY2(json.size() <= 12, json.constData());
        }
        else if (field.type == QLatin1String("string"))
        {
            QCOMPARE(row.value(QStringLiteral("value")).toString(), field.value.toString());
        }
        else
        {
            QCOMPARE(row.value(QStringLiteral("value")).toInt(), field.value.toInt());
        }
        QCOMPARE(row.contains(QStringLiteral("labelKind")), field.labelKind != RefKind::None);
        QCOMPARE(row.value(QStringLiteral("label")).toString(), field.label);
        sawLabel = sawLabel || !field.label.isEmpty();
    }
    QVERIFY(sawFloat);
    QVERIFY(sawLabel);
}

void CliTest::getFieldPatterns()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    QJsonObject request = get(QStringLiteral("unit"), {4});
    request.insert(QStringLiteral("civ"), 1);
    request.insert(QStringLiteral("fields"), QJsonArray({QStringLiteral("cost*"), QStringLiteral("hit_points")}));
    HandlerResult result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    // Descriptor order, not pattern order.
    const QStringList keys = fieldKeys(result.body.value(QStringLiteral("items")).toArray().first().toObject());
    QCOMPARE(keys.first(), QStringLiteral("hit_points"));
    QVERIFY(keys.size() > 1);
    for (const QString &key : keys.mid(1))
        QVERIFY2(key.startsWith(QStringLiteral("cost")), qPrintable(key));

    // A pattern that matches no key of the kind is unknown_field.
    request.insert(QStringLiteral("fields"), QJsonArray({QStringLiteral("hit_points"), QStringLiteral("hitpoints")}));
    result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(errorCode(result), QStringLiteral("unknown_field"));
    const QJsonObject error = result.body.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("kind")).toString(), QStringLiteral("unit"));
    QCOMPARE(error.value(QStringLiteral("key")).toString(), QStringLiteral("hitpoints"));

    // Effect command keys are known through the commandN templates, whether
    // or not this effect has that many commands.
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    const genie::DatFile &dat = *service.session().dat();
    int effect = -1;
    for (int id = 0; id < int(dat.Effects.size()) && effect < 0; ++id)
    {
        if (dat.Effects[id].EffectCommands.size() >= 2)
            effect = id;
    }
    QVERIFY(effect >= 0);
    request = get(QStringLiteral("effect"), {effect});
    request.insert(QStringLiteral("fields"),
                   QJsonArray({QStringLiteral("command2.*"), QStringLiteral("command999.amount")}));
    result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    const QStringList commandKeys = fieldKeys(result.body.value(QStringLiteral("items")).toArray().first().toObject());
    QVERIFY(commandKeys.contains(QStringLiteral("command2.type")));
    for (const QString &key : commandKeys)
        QVERIFY2(key.startsWith(QStringLiteral("command2.")), qPrintable(key));

    // A command type is a number labelled like other codes.
    const QJsonObject type = fieldByKey(result.body.value(QStringLiteral("items")).toArray().first().toObject(),
                                        QStringLiteral("command2.type"));
    const int typeId = dat.Effects[effect].EffectCommands[1].Type;
    QCOMPARE(type.value(QStringLiteral("type")).toString(), QStringLiteral("int"));
    QVERIFY(type.value(QStringLiteral("value")).isDouble());
    QCOMPARE(type.value(QStringLiteral("value")).toInt(), typeId);
    QCOMPARE(type.value(QStringLiteral("labelKind")).toString(), QStringLiteral("effect-type"));
    QCOMPARE(type.value(QStringLiteral("label")).toString(), effectTypeName(service.session().gameVersion(), typeId));

    request.insert(QStringLiteral("fields"), QJsonArray({QStringLiteral("command2.bogus")}));
    QCOMPARE(errorCode(RequestHandler().handle(tcSource(), request)), QStringLiteral("unknown_field"));
}

// A command Mode is a number labelled like other codes, with a label kind per
// command type.
void CliTest::getEffectModes()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    const genie::DatFile &dat = *service.session().dat();

    struct Case
    {
        int type;
        QString labelKind;
        RefKind kind;
        bool inC; // Mode is stored in C, else in B.
    };
    const Case cases[] = {
        {1, QStringLiteral("resource-mode"), RefKind::ResourceMode, false},
        {2, QStringLiteral("enable-mode"), RefKind::EnableMode, false},
        {101, QStringLiteral("tech-modifier-mode"), RefKind::TechModifierMode, true},
    };
    for (const Case &test : cases)
    {
        int effect = -1;
        int command = -1;
        for (int id = 0; id < int(dat.Effects.size()) && effect < 0; ++id)
        {
            const auto &commands = dat.Effects[id].EffectCommands;
            for (int c = 0; c < int(commands.size()) && effect < 0; ++c)
            {
                if (commands[c].Type == test.type)
                {
                    effect = id;
                    command = c;
                }
            }
        }
        QVERIFY2(effect >= 0, qPrintable(test.labelKind));
        const QString key = QStringLiteral("command%1.mode").arg(command + 1);
        QJsonObject request = get(QStringLiteral("effect"), {effect});
        request.insert(QStringLiteral("fields"), QJsonArray({key}));
        const HandlerResult result = RequestHandler().handle(tcSource(), request);
        QCOMPARE(result.exitCode, 0);
        const QJsonObject mode = fieldByKey(result.body.value(QStringLiteral("items")).toArray().first().toObject(), key);
        const genie::EffectCommand &found = dat.Effects[effect].EffectCommands[command];
        const int stored = test.inC ? found.C : found.B;
        QCOMPARE(mode.value(QStringLiteral("type")).toString(), QStringLiteral("int"));
        QVERIFY(mode.value(QStringLiteral("value")).isDouble());
        QCOMPARE(mode.value(QStringLiteral("value")).toInt(), stored);
        QCOMPARE(mode.value(QStringLiteral("labelKind")).toString(), test.labelKind);
        QCOMPARE(mode.value(QStringLiteral("label")).toString(), effectModeName(service.session().gameVersion(), test.kind, stored));
    }
}

void CliTest::getCompact()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    QJsonObject request = get(QStringLiteral("unit"), {4});
    request.insert(QStringLiteral("civ"), 1);
    request.insert(QStringLiteral("compact"), true);
    const HandlerResult result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    const QJsonObject fields = result.body.value(QStringLiteral("items"))
                                   .toArray()
                                   .first()
                                   .toObject()
                                   .value(QStringLiteral("fields"))
                                   .toObject();
    const QList<FieldValue> expected = unitKind().fields(service.session(), 1, 4);
    QCOMPARE(fields.size(), expected.size());
    for (const FieldValue &field : expected)
    {
        QVERIFY2(fields.contains(field.key), qPrintable(field.key));
        if (field.type == QLatin1String("int"))
            QCOMPARE(fields.value(field.key).toInt(), field.value.toInt());
    }
}

void CliTest::getTechActivityAndLabels()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");
    DataService service;
    QVERIFY(service.open(tcSource()).ok);
    const Session &session = service.session();
    const QList<TechAvailability> availability = techAvailability(session, 1);
    int available = -1;
    int unavailable = -1;
    for (int id = 0; id < availability.size(); ++id)
    {
        const auto &locations = session.dat()->Techs[id].ResearchLocations;
        const bool located = !locations.empty() && locations.front().LocationID >= 0;
        if (located && available < 0 && availability.at(id) == TechAvailability::Available)
            available = id;
        if (unavailable < 0 && availability.at(id) != TechAvailability::Available)
            unavailable = id;
    }
    QVERIFY(available >= 0 && unavailable >= 0);

    // Without a civ, techs have no activity and unit labels name civ 0's copy.
    QJsonObject request = get(QStringLiteral("tech"), {available, unavailable});
    request.insert(QStringLiteral("fields"), QJsonArray({QStringLiteral("research_location")}));
    HandlerResult result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    QVERIFY(!result.body.contains(QStringLiteral("civ")));
    const QJsonObject first = result.body.value(QStringLiteral("items")).toArray().first().toObject();
    QVERIFY(!first.contains(QStringLiteral("active")));
    const QJsonObject location = first.value(QStringLiteral("fields")).toArray().first().toObject();
    QCOMPARE(location.value(QStringLiteral("labelKind")).toString(), QStringLiteral("unit"));
    const int unit = location.value(QStringLiteral("value")).toInt();
    QVERIFY(!refName(session, RefKind::Unit, unit, 0).isEmpty());
    QCOMPARE(location.value(QStringLiteral("label")).toString(), refName(session, RefKind::Unit, unit, 0));

    // With a civ, each tech says whether that civ can research it.
    request.insert(QStringLiteral("civ"), 1);
    result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("civ")).toInt(), 1);
    const QJsonArray items = result.body.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("active")).toBool(), true);
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("active")).toBool(), false);
}

void CliTest::batchNeedsRequests_data()
{
    QTest::addColumn<QJsonObject>("request");
    QJsonObject notArray = op(QStringLiteral("batch"));
    notArray.insert(QStringLiteral("requests"), op(QStringLiteral("info")));
    QTest::newRow("missing") << op(QStringLiteral("batch"));
    QTest::newRow("not an array") << notArray;
    QTest::newRow("empty") << batch({});
}

void CliTest::batchNeedsRequests()
{
    QFETCH(QJsonObject, request);
    const HandlerResult result = RequestHandler().handle(tcSource(), request);
    QCOMPARE(result.exitCode, 1);
    QCOMPARE(errorCode(result), QStringLiteral("usage"));
    QVERIFY(!result.body.contains(QStringLiteral("results")));
}

// Malformed requests fail in place. When none is left to run, the data isn't
// opened: a missing file would otherwise fail the whole batch.
void CliTest::batchChecksRequestsBeforeOpen()
{
    DataSource source;
    source.datPath = QStringLiteral("missing.dat");
    source.versionKey = QStringLiteral("tc");
    QJsonObject noIds = op(QStringLiteral("get"));
    noIds.insert(QStringLiteral("kind"), QStringLiteral("unit"));
    const HandlerResult result = RequestHandler().handle(
        source, batch({42, batch({op(QStringLiteral("info"))}), op(QStringLiteral("set")), noIds,
                       lookup(QStringLiteral("bogus"))}));
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("failed")).toInt(), 5);
    const QJsonArray results = result.body.value(QStringLiteral("results")).toArray();
    QCOMPARE(results.size(), 5);
    for (int i = 0; i < 4; ++i)
        QCOMPARE(errorCode(results.at(i)), QStringLiteral("usage"));
    QCOMPARE(errorCode(results.at(4)), QStringLiteral("unknown_kind"));
    QVERIFY(results.at(0).toObject().value(QStringLiteral("error")).toObject().value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("0")));
}

void CliTest::batchOpenFailureFailsWhole()
{
    const HandlerResult result =
        RequestHandler().handle({}, batch({op(QStringLiteral("info")), op(QStringLiteral("set"))}));
    QCOMPARE(result.exitCode, 2);
    QCOMPARE(errorCode(result), QStringLiteral("no_dataset"));
    QVERIFY(!result.body.contains(QStringLiteral("results")));
}

// Each result is what the request returns on its own, failures included.
void CliTest::batchRunsEachRequestInPlace()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    QJsonObject listEffects = list(QStringLiteral("effect"));
    listEffects.insert(QStringLiteral("limit"), 2);
    QJsonObject techs = get(QStringLiteral("tech"), {3, 2});
    techs.insert(QStringLiteral("fields"), QJsonArray({QStringLiteral("research_time")}));
    QJsonObject schema = op(QStringLiteral("schema"));
    schema.insert(QStringLiteral("kind"), QStringLiteral("tech"));
    const QJsonArray requests = {op(QStringLiteral("info")),
                                 lookup(QStringLiteral("civ"), QStringLiteral("brit")),
                                 get(QStringLiteral("unit"), {4}),
                                 techs,
                                 get(QStringLiteral("tech"), {-1}),
                                 listEffects,
                                 schema};

    const HandlerResult result = RequestHandler().handle(tcSource(), batch(requests));
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("failed")).toInt(), 2);
    const QJsonArray results = result.body.value(QStringLiteral("results")).toArray();
    QCOMPARE(results.size(), requests.size());
    for (qsizetype i = 0; i < requests.size(); ++i)
    {
        const HandlerResult alone = RequestHandler().handle(tcSource(), requests.at(i).toObject());
        QVERIFY2(results.at(i).toObject() == alone.body, qPrintable(QString::number(i)));
    }
    QCOMPARE(errorCode(results.at(2)), QStringLiteral("usage"));
    QCOMPARE(errorCode(results.at(4)), QStringLiteral("unknown_entity"));
    QCOMPARE(results.at(3).toObject().value(QStringLiteral("items")).toArray().size(), 2);
}

void CliTest::batchRejectsModsList()
{
    DataSource source;
    source.datPath = QStringLiteral("missing.dat");
    source.versionKey = QStringLiteral("tc");
    const HandlerResult result = RequestHandler().handle(source, batch({op(QStringLiteral("mods-list"))}));
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("failed")).toInt(), 1);
    QCOMPARE(errorCode(result.body.value(QStringLiteral("results")).toArray().at(0)), QStringLiteral("usage"));
}

// The game's .dat files are empty, so this passes only if nothing is loaded.
// The source's mod is ignored, because NEWAGE_MOD is often set.
void CliTest::modsListReadsNoData()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeModdedGame(game));

    DataSource source;
    source.gameDir = game.path();
    source.mod = QStringLiteral("Missing");
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("mods-list")));
    if (result.exitCode != 0)
        QFAIL(qPrintable(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
    QVERIFY(result.warnings.isEmpty());
    QCOMPARE(result.body.value(QStringLiteral("game")).toString(), forwardPath(QDir(game.path()).absolutePath()));
    QCOMPARE(result.body.value(QStringLiteral("dataset")).toString(), QStringLiteral("empires2_x2_p1.dat"));
    QCOMPARE(result.body.value(QStringLiteral("modsFolder")).toString(),
             forwardPath(game.filePath(QStringLiteral("mods"))));
    QVERIFY(!result.body.contains(QStringLiteral("otherModsFolders")));

    const QJsonArray mods = result.body.value(QStringLiteral("mods")).toArray();
    QCOMPARE(mods.size(), 2);
    const QJsonObject balance = mods.at(0).toObject();
    QCOMPARE(balance.value(QStringLiteral("title")).toString(), QStringLiteral("Balance"));
    QCOMPARE(balance.value(QStringLiteral("dir")).toString(),
             forwardPath(game.filePath(QStringLiteral("mods/Balance"))));
    QCOMPARE(balance.value(QStringLiteral("author")).toString(), QStringLiteral("Me"));
    QCOMPARE(balance.value(QStringLiteral("description")).toString(), QStringLiteral("Cheaper archers"));
    QCOMPARE(balance.value(QStringLiteral("hasDat")).toBool(), true);
    const QJsonObject empty = mods.at(1).toObject();
    QCOMPARE(empty.value(QStringLiteral("title")).toString(), QStringLiteral("Empty"));
    QCOMPARE(empty.value(QStringLiteral("author")).toString(), QString());
    QCOMPARE(empty.value(QStringLiteral("hasDat")).toBool(), false);

    // hasDat follows the data set: Balance has no copy of the TC .dat.
    source.dataset = QStringLiteral("empires2_x1_p1.dat");
    const HandlerResult tc = RequestHandler().handle(source, op(QStringLiteral("mods-list")));
    QCOMPARE(tc.exitCode, 0);
    QCOMPARE(tc.body.value(QStringLiteral("dataset")).toString(), QStringLiteral("empires2_x1_p1.dat"));
    QCOMPARE(tc.body.value(QStringLiteral("mods")).toArray().at(0).toObject().value(QStringLiteral("hasDat")).toBool(),
             false);
}

void CliTest::modsListFolderOverride()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeModdedGame(game));
    QTemporaryDir other;
    QVERIFY(other.isValid());
    QVERIFY(!createMod(other.path(), {QStringLiteral("Elsewhere"), {}, {}}).isEmpty());

    qputenv("NEWAGE_MODS_FOLDER", other.path().toUtf8());
    DataSource source;
    source.gameDir = game.path();
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("mods-list")));
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("modsFolder")).toString(), forwardPath(other.path()));
    QCOMPARE(result.body.value(QStringLiteral("otherModsFolders")).toArray(),
             QJsonArray({forwardPath(game.filePath(QStringLiteral("mods")))}));
    const QJsonArray mods = result.body.value(QStringLiteral("mods")).toArray();
    QCOMPARE(mods.size(), 1);
    QCOMPARE(mods.at(0).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Elsewhere"));
}

void CliTest::modsListFails_data()
{
    QTest::addColumn<QString>("layout");
    QTest::addColumn<QString>("dataset");
    QTest::addColumn<bool>("loose");
    QTest::addColumn<QString>("code");
    QTest::newRow("no source") << QString() << QString() << false << QStringLiteral("no_dataset");
    QTest::newRow("no data set") << QStringLiteral("readme.txt") << QString() << false << QStringLiteral("no_dataset");
    QTest::newRow("unknown data set") << QStringLiteral("resources/_common/dat/empires2_x2_p1.dat")
                                      << QStringLiteral("missing.dat") << false << QStringLiteral("no_dataset");
    QTest::newRow("cd install") << QStringLiteral("data/empires2_x1_p1.dat") << QString() << false
                                << QStringLiteral("mods_unsupported");
    QTest::newRow("loose file") << QString() << QString() << true << QStringLiteral("mods_unsupported");
}

void CliTest::modsListFails()
{
    QFETCH(QString, layout);
    QFETCH(QString, dataset);
    QFETCH(bool, loose);
    QFETCH(QString, code);

    QTemporaryDir game;
    QVERIFY(game.isValid());
    DataSource source;
    if (!layout.isEmpty())
    {
        QVERIFY(writeFile(game.path(), layout));
        source.gameDir = game.path();
        source.dataset = dataset;
    }
    if (loose)
    {
        source.datPath = kTcDat;
        source.versionKey = QStringLiteral("tc");
    }
    const HandlerResult result = RequestHandler().handle(source, op(QStringLiteral("mods-list")));
    QCOMPARE(result.exitCode, 2);
    QCOMPARE(errorCode(result), code);
}

void CliTest::writesCheckRequestBeforeOpen_data()
{
    QTest::addColumn<QJsonObject>("request");
    QTest::addColumn<int>("exitCode");
    QTest::addColumn<QString>("code");

    const QJsonObject oneValue = hitPointsOf(30);
    const auto without = [&](const QString &key) {
        QJsonObject request = setRequest(4, 1, oneValue);
        request.remove(key);
        return request;
    };
    const auto with = [&](const QString &key, const QJsonValue &value) {
        QJsonObject request = setRequest(4, 1, oneValue);
        request.insert(key, value);
        return request;
    };
    QTest::newRow("set without kind") << without(QStringLiteral("kind")) << 1 << QStringLiteral("usage");
    QTest::newRow("set unknown kind") << with(QStringLiteral("kind"), QStringLiteral("building")) << 3
                                      << QStringLiteral("unknown_kind");
    QTest::newRow("set without id") << without(QStringLiteral("id")) << 1 << QStringLiteral("usage");
    QTest::newRow("set fractional id") << with(QStringLiteral("id"), 4.5) << 1 << QStringLiteral("usage");
    QTest::newRow("unit without civ") << without(QStringLiteral("civ")) << 1 << QStringLiteral("usage");
    QTest::newRow("civ is text") << with(QStringLiteral("civ"), QStringLiteral("1")) << 1 << QStringLiteral("usage");
    QTest::newRow("tech with civ") << with(QStringLiteral("kind"), QStringLiteral("tech")) << 1 << QStringLiteral("usage");
    QTest::newRow("set without values") << without(QStringLiteral("set")) << 1 << QStringLiteral("usage");
    QTest::newRow("set empty") << with(QStringLiteral("set"), QJsonObject()) << 1 << QStringLiteral("usage");
    QTest::newRow("value is boolean")
        << with(QStringLiteral("set"), QJsonObject{{QStringLiteral("hit_points"), true}}) << 1
        << QStringLiteral("usage");
    QTest::newRow("value is null")
        << with(QStringLiteral("set"), QJsonObject{{QStringLiteral("hit_points"), QJsonValue::Null}}) << 1
        << QStringLiteral("usage");
    QTest::newRow("expect is an array") << with(QStringLiteral("expect"), QJsonArray()) << 1 << QStringLiteral("usage");
    QTest::newRow("expect key not set")
        << with(QStringLiteral("expect"), QJsonObject{{QStringLiteral("speed"), 1}}) << 1 << QStringLiteral("usage");
    QTest::newRow("expect value is text")
        << with(QStringLiteral("expect"), QJsonObject{{QStringLiteral("hit_points"), true}}) << 1
        << QStringLiteral("usage");
    QTest::newRow("dryRun is text") << with(QStringLiteral("dryRun"), QStringLiteral("yes")) << 1
                                    << QStringLiteral("usage");

    QJsonObject apply = op(QStringLiteral("apply"));
    QTest::newRow("apply without edits") << apply << 1 << QStringLiteral("usage");
    apply.insert(QStringLiteral("edits"), QJsonArray());
    QTest::newRow("apply empty edits") << apply << 1 << QStringLiteral("usage");
    apply.insert(QStringLiteral("edits"), QJsonArray{5});
    QTest::newRow("apply edit not an object") << apply << 1 << QStringLiteral("usage");
    QJsonObject bad = setRequest(4, 1, oneValue);
    bad.remove(QStringLiteral("op"));
    bad.insert(QStringLiteral("civ"), 1.5);
    apply.insert(QStringLiteral("edits"), QJsonArray{bad});
    QTest::newRow("apply bad edit") << apply << 1 << QStringLiteral("usage");
    bad.insert(QStringLiteral("civ"), 1);
    bad.insert(QStringLiteral("kind"), QStringLiteral("building"));
    apply.insert(QStringLiteral("edits"), QJsonArray{bad});
    QTest::newRow("apply unknown kind") << apply << 3 << QStringLiteral("unknown_kind");

    QJsonObject create = modsCreate(QStringLiteral("x"));
    QTest::newRow("create without title") << QJsonObject{{QStringLiteral("op"), QStringLiteral("mods-create")}} << 1
                                          << QStringLiteral("usage");
    create.insert(QStringLiteral("author"), 5);
    QTest::newRow("create author is a number") << create << 1 << QStringLiteral("usage");
}

// The source has no data, so a request that reached the open would be
// no_dataset, never these.
void CliTest::writesCheckRequestBeforeOpen()
{
    QFETCH(QJsonObject, request);
    QFETCH(int, exitCode);
    QFETCH(QString, code);

    DataSource source;
    source.mod = QStringLiteral("Balance");
    const HandlerResult result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, exitCode);
    QCOMPARE(errorCode(result), code);
}

// The game's .dat files are empty, so a load would be load_failed.
void CliTest::writeNeedsAModBeforeLoading()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeModdedGame(game));

    DataSource source;
    source.gameDir = game.path();
    HandlerResult result = RequestHandler().handle(source, setRequest(4, 1, hitPointsOf(30)));
    QCOMPARE(result.exitCode, 5);
    QCOMPARE(errorCode(result), QStringLiteral("mod_required"));

    QJsonObject patch = op(QStringLiteral("apply"));
    QJsonObject edit = setRequest(4, 1, hitPointsOf(30));
    edit.remove(QStringLiteral("op"));
    patch.insert(QStringLiteral("edits"), QJsonArray{edit});
    result = RequestHandler().handle(source, patch);
    QCOMPARE(result.exitCode, 5);
    QCOMPARE(errorCode(result), QStringLiteral("mod_required"));

    // NEWAGE_MOD counts as the mod, so this one gets as far as the load.
    source.mod = QStringLiteral("Balance");
    QCOMPARE(errorCode(RequestHandler().handle(source, setRequest(4, 1, hitPointsOf(30)))),
             QStringLiteral("load_failed"));

    DataSource loose = tcSource();
    result = RequestHandler().handle(loose, setRequest(4, 1, hitPointsOf(30)));
    QCOMPARE(result.exitCode, 2);
    QCOMPARE(errorCode(result), QStringLiteral("mods_unsupported"));
}

void CliTest::setWritesTheMod()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeWritableGame(game));
    const DataSource source = balanceSource(game);
    const int original = readHitPoints(source);
    QVERIFY(original >= 0);
    const QString savePath = game.filePath(QStringLiteral("mods/Balance/resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(!QFile::exists(savePath));

    QJsonObject request = setRequest(4, 1, hitPointsOf(original + 1));
    request.insert(QStringLiteral("expect"), hitPointsOf(original));
    const HandlerResult result = RequestHandler().handle(source, request);
    if (result.exitCode != 0)
        QFAIL(qPrintable(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
    QCOMPARE(result.body.value(QStringLiteral("saved")).toString(), forwardPath(savePath));
    QVERIFY(!result.body.contains(QStringLiteral("dryRun")));
    QCOMPARE(result.body.value(QStringLiteral("unchanged")).toArray().size(), 0);
    const QJsonArray changes = result.body.value(QStringLiteral("changes")).toArray();
    QCOMPARE(changes.size(), 1);
    const QJsonObject change = changes.at(0).toObject();
    QCOMPARE(change.value(QStringLiteral("kind")).toString(), QStringLiteral("unit"));
    QCOMPARE(change.value(QStringLiteral("id")).toInt(), 4);
    QCOMPARE(change.value(QStringLiteral("civ")).toInt(), 1);
    QCOMPARE(change.value(QStringLiteral("key")).toString(), QStringLiteral("hit_points"));
    QCOMPARE(change.value(QStringLiteral("old")).toInt(), original);
    QCOMPARE(change.value(QStringLiteral("new")).toInt(), original + 1);

    QVERIFY(QFile::exists(savePath));
    QCOMPARE(readHitPoints(source), original + 1);
    DataSource gameOnly = source;
    gameOnly.mod.clear();
    QCOMPARE(readHitPoints(gameOnly), original);

    // A result's changes read back as a patch: old as expect undoes it.
    QJsonObject edit;
    for (const QString &key : {QStringLiteral("kind"), QStringLiteral("id"), QStringLiteral("civ")})
        edit.insert(key, change.value(key));
    edit.insert(QStringLiteral("set"), QJsonObject{{change.value(QStringLiteral("key")).toString(), change.value(QStringLiteral("old"))}});
    edit.insert(QStringLiteral("expect"), QJsonObject{{change.value(QStringLiteral("key")).toString(), change.value(QStringLiteral("new"))}});
    QJsonObject undo = op(QStringLiteral("apply"));
    undo.insert(QStringLiteral("edits"), QJsonArray{edit});
    QCOMPARE(RequestHandler().handle(source, undo).exitCode, 0);
    QCOMPARE(readHitPoints(source), original);
}

void CliTest::setDryRunAndUnchangedDoNotSave()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeWritableGame(game));
    const DataSource source = balanceSource(game);
    const int original = readHitPoints(source);
    const QString savePath = game.filePath(QStringLiteral("mods/Balance/resources/_common/dat/empires2_x2_p1.dat"));

    QJsonObject request = setRequest(4, 1, hitPointsOf(original + 1));
    request.insert(QStringLiteral("dryRun"), true);
    HandlerResult result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("dryRun")).toBool(), true);
    QCOMPARE(result.body.value(QStringLiteral("saved")).toString(), QString());
    QCOMPARE(result.body.value(QStringLiteral("changes")).toArray().size(), 1);
    QVERIFY(!QFile::exists(savePath));

    // Text values are read like JSON numbers, as the command line sends them.
    request = setRequest(4, 1, QJsonObject{{QStringLiteral("hit_points"), QString::number(original)}});
    result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("saved")).toString(), QString());
    QCOMPARE(result.body.value(QStringLiteral("changes")).toArray().size(), 0);
    QCOMPARE(result.body.value(QStringLiteral("unchanged")).toArray().size(), 1);
    QVERIFY(!QFile::exists(savePath));
}

void CliTest::setExpectConflict()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeWritableGame(game));
    const DataSource source = balanceSource(game);
    const int original = readHitPoints(source);

    QJsonObject request = setRequest(4, 1, hitPointsOf(original + 1));
    request.insert(QStringLiteral("expect"), hitPointsOf(original + 5));
    const HandlerResult result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, 4);
    QCOMPARE(errorCode(result), QStringLiteral("conflict"));
    const QJsonObject error = result.body.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("key")).toString(), QStringLiteral("hit_points"));
    QCOMPARE(error.value(QStringLiteral("civ")).toInt(), 1);
    QVERIFY(!QFile::exists(game.filePath(QStringLiteral("mods/Balance/resources/_common/dat/empires2_x2_p1.dat"))));
}

void CliTest::setRejectsBadValues()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeWritableGame(game));
    const DataSource source = balanceSource(game);

    struct Case
    {
        QJsonObject request;
        QString code;
    };
    const QList<Case> cases = {
        {setRequest(4, 1, hitPointsOf(99999)), QStringLiteral("out_of_range")},
        {setRequest(4, 1, QJsonObject{{QStringLiteral("hit_points"), 1.5}}), QStringLiteral("bad_value")},
        {setRequest(4, 1, QJsonObject{{QStringLiteral("hit_points"), QStringLiteral("many")}}), QStringLiteral("bad_value")},
        {setRequest(4, 1, QJsonObject{{QStringLiteral("nope"), 1}}), QStringLiteral("unknown_field")},
        {setRequest(4, 1000, hitPointsOf(30)), QStringLiteral("unknown_entity")},
        {setRequest(100000, 1, hitPointsOf(30)), QStringLiteral("unknown_entity")},
    };
    for (const Case &entry : cases)
    {
        const HandlerResult result = RequestHandler().handle(source, entry.request);
        QCOMPARE(result.exitCode, 3);
        QCOMPARE(errorCode(result), entry.code);
    }
    QVERIFY(!QFile::exists(game.filePath(QStringLiteral("mods/Balance/resources/_common/dat/empires2_x2_p1.dat"))));
}

void CliTest::applyIsAtomicAndRoundTrips()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeWritableGame(game));
    const DataSource source = balanceSource(game);
    const QString savePath = game.filePath(QStringLiteral("mods/Balance/resources/_common/dat/empires2_x2_p1.dat"));
    const int original = readHitPoints(source);

    const auto edit = [](int civ, int hitPoints) {
        QJsonObject object = setRequest(4, civ, hitPointsOf(hitPoints));
        object.remove(QStringLiteral("op"));
        return object;
    };
    QJsonObject patch = op(QStringLiteral("apply"));
    patch.insert(QStringLiteral("edits"), QJsonArray{edit(1, original + 1), edit(2, 99999)});
    HandlerResult result = RequestHandler().handle(source, patch);
    QCOMPARE(result.exitCode, 3);
    QCOMPARE(errorCode(result), QStringLiteral("out_of_range"));
    QCOMPARE(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("civ")).toInt(), 2);
    QVERIFY(!QFile::exists(savePath));
    QCOMPARE(readHitPoints(source), original);

    // A conflict in the last edit also stops the first.
    QJsonObject conflicting = edit(2, 40);
    conflicting.insert(QStringLiteral("expect"), hitPointsOf(-7));
    patch.insert(QStringLiteral("edits"), QJsonArray{edit(1, original + 1), conflicting});
    result = RequestHandler().handle(source, patch);
    QCOMPARE(result.exitCode, 4);
    QVERIFY(!QFile::exists(savePath));

    // One civ per edit: both are written by a single save.
    patch.insert(QStringLiteral("edits"), QJsonArray{edit(1, original + 1), edit(2, original + 2)});
    result = RequestHandler().handle(source, patch);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("changes")).toArray().size(), 2);
    QCOMPARE(result.body.value(QStringLiteral("saved")).toString(), forwardPath(savePath));
    QCOMPARE(readHitPoints(source), original + 1);

    // A no-op apply leaves the file as it is.
    const QDateTime saved = QFileInfo(savePath).lastModified();
    patch.insert(QStringLiteral("edits"), QJsonArray{edit(1, original + 1)});
    result = RequestHandler().handle(source, patch);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("saved")).toString(), QString());
    QCOMPARE(QFileInfo(savePath).lastModified(), saved);
}

void CliTest::applyBuildsATechAndItsEffect()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeWritableGame(game));
    const DataSource source = balanceSource(game);
    const auto readItem = [&](const QString &kind, int id) {
        const HandlerResult result = RequestHandler().handle(source, get(kind, {id}));
        return result.body.value(QStringLiteral("items")).toArray().at(0).toObject();
    };
    const auto value = [](const QJsonObject &item, const QString &key) {
        return fieldByKey(item, key).value(QStringLiteral("value"));
    };
    const int count = value(readItem(QStringLiteral("effect"), 0), QStringLiteral("command_count")).toInt();

    // Keys sort as "command5.amount" < "command_count", so the count has to
    // be written first on purpose: it makes the new commands exist.
    const QString first = QStringLiteral("command%1.").arg(count + 1);
    const QString second = QStringLiteral("command%1.").arg(count + 2);
    const QJsonObject effect{{QStringLiteral("kind"), QStringLiteral("effect")},
                             {QStringLiteral("id"), 0},
                             {QStringLiteral("set"),
                              QJsonObject{{QStringLiteral("internal_name"), QStringLiteral("Super villager")},
                                          {QStringLiteral("command_count"), count + 2},
                                          {first + QStringLiteral("type"), 4},
                                          {first + QStringLiteral("class"), 4},
                                          {first + QStringLiteral("attribute"), 0},
                                          {first + QStringLiteral("amount"), 25},
                                          {second + QStringLiteral("type"), 102},
                                          {second + QStringLiteral("tech"), 22}}}};
    const QJsonObject tech{{QStringLiteral("kind"), QStringLiteral("tech")},
                           {QStringLiteral("id"), 0},
                           {QStringLiteral("set"),
                            QJsonObject{{QStringLiteral("internal_name"), QStringLiteral("Super villager")},
                                        {QStringLiteral("effect"), 0},
                                        {QStringLiteral("civ"), 2},
                                        {QStringLiteral("icon"), 33},
                                        {QStringLiteral("button"), 7},
                                        {QStringLiteral("language_help"), 107368},
                                        {QStringLiteral("cost3.resource"), 0},
                                        {QStringLiteral("cost3.amount"), 10}}}};

    QJsonObject patch = op(QStringLiteral("apply"));
    patch.insert(QStringLiteral("edits"), QJsonArray{effect, tech});
    const HandlerResult result = RequestHandler().handle(source, patch);
    if (result.exitCode != 0)
        QFAIL(qPrintable(QJsonDocument(result.body).toJson()));
    const QJsonArray changes = result.body.value(QStringLiteral("changes")).toArray();
    QCOMPARE(changes.at(0).toObject().value(QStringLiteral("key")).toString(), QStringLiteral("command_count"));

    const QJsonObject savedEffect = readItem(QStringLiteral("effect"), 0);
    QCOMPARE(value(savedEffect, QStringLiteral("internal_name")).toString(), QStringLiteral("Super villager"));
    QCOMPARE(value(savedEffect, QStringLiteral("command_count")).toInt(), count + 2);
    QCOMPARE(value(savedEffect, first + QStringLiteral("type")).toInt(), 4);
    QCOMPARE(value(savedEffect, first + QStringLiteral("class")).toInt(), 4);
    QCOMPARE(value(savedEffect, first + QStringLiteral("amount")).toDouble(), 25.0);
    // The unit slot was never written, so it stays unused.
    QVERIFY(fieldByKey(savedEffect, first + QStringLiteral("unit")).isEmpty());
    QCOMPARE(value(savedEffect, second + QStringLiteral("tech")).toInt(), 22);

    const QJsonObject savedTech = readItem(QStringLiteral("tech"), 0);
    QCOMPARE(value(savedTech, QStringLiteral("internal_name")).toString(), QStringLiteral("Super villager"));
    QCOMPARE(value(savedTech, QStringLiteral("effect")).toInt(), 0);
    QCOMPARE(value(savedTech, QStringLiteral("civ")).toInt(), 2);
    QCOMPARE(value(savedTech, QStringLiteral("icon")).toInt(), 33);
    QCOMPARE(value(savedTech, QStringLiteral("button")).toInt(), 7);
    QCOMPARE(value(savedTech, QStringLiteral("language_help")).toInt(), 107368);
    QCOMPARE(value(savedTech, QStringLiteral("cost3.resource")).toInt(), 0);
    QCOMPARE(value(savedTech, QStringLiteral("cost3.amount")).toInt(), 10);

    // HD stores effect names in 31 bytes; text must be Latin-1.
    const auto rename = [&](const QJsonValue &name) {
        QJsonObject request = op(QStringLiteral("set"));
        request.insert(QStringLiteral("kind"), QStringLiteral("effect"));
        request.insert(QStringLiteral("id"), 0);
        request.insert(QStringLiteral("set"), QJsonObject{{QStringLiteral("internal_name"), name}});
        return errorCode(RequestHandler().handle(source, request));
    };
    QCOMPARE(rename(QString(31, QLatin1Char('x'))), QStringLiteral("out_of_range"));
    QCOMPARE(rename(QStringLiteral("\u4e2d")), QStringLiteral("bad_value"));
    QCOMPARE(rename(5), QStringLiteral("bad_value"));
}

void CliTest::applyUndoesEveryEditOnFailure()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeWritableGame(game));
    const QString savePath = game.filePath(QStringLiteral("mods/Balance/resources/_common/dat/empires2_x2_p1.dat"));

    DataService service;
    QVERIFY(service.open(balanceSource(game)).ok);
    const genie::Effect before = service.session().dat()->Effects.at(0);
    const int count = static_cast<int>(before.EffectCommands.size());
    const QString added = QStringLiteral("command%1.type").arg(count + 1);

    // Each edit sees the ones before it; a later failure undoes them all,
    // including the commands the count removed.
    ApplyResult result = service.apply({{QStringLiteral("effect"), 0, -1, QStringLiteral("command_count"), 0},
                                        {QStringLiteral("effect"), 0, -1, QStringLiteral("command_count"), count + 1},
                                        {QStringLiteral("effect"), 0, -1, added, 4},
                                        {QStringLiteral("effect"), 0, -1, QStringLiteral("nope"), 1}});
    QVERIFY(!result.ok);
    QCOMPARE(result.error.code, QStringLiteral("unknown_field"));
    QVERIFY(result.changes.isEmpty());
    QVERIFY(!service.session().isModified());
    QVERIFY(!QFile::exists(savePath));
    const genie::Effect &after = service.session().dat()->Effects.at(0);
    QCOMPARE(static_cast<int>(after.EffectCommands.size()), count);
    for (int i = 0; i < count; ++i)
        QCOMPARE(int(after.EffectCommands.at(i).Type), int(before.EffectCommands.at(i).Type));

    // An expect is checked against what the earlier edits left.
    FieldEdit expecting{QStringLiteral("effect"), 0, -1, added, 5};
    expecting.expect = 4;
    result = service.apply({{QStringLiteral("effect"), 0, -1, QStringLiteral("command_count"), count + 1},
                            {QStringLiteral("effect"), 0, -1, added, 4},
                            expecting},
                           true);
    QVERIFY(result.ok);
    QCOMPARE(result.changes.size(), 3);
    QCOMPARE(result.changes.at(2).oldValue, QVariant(4));
    // A dry run undoes its edits too.
    QCOMPARE(static_cast<int>(service.session().dat()->Effects.at(0).EffectCommands.size()), count);
    QVERIFY(!service.session().isModified());
    QVERIFY(!QFile::exists(savePath));
}

void CliTest::batchRejectsWrites()
{
    DataSource source;
    source.datPath = QStringLiteral("missing.dat");
    source.versionKey = QStringLiteral("tc");
    QJsonObject apply = op(QStringLiteral("apply"));
    const HandlerResult result = RequestHandler().handle(
        source, batch({setRequest(4, 1, hitPointsOf(30)), apply, modsCreate(QStringLiteral("x"))}));
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("failed")).toInt(), 3);
    for (const QJsonValue &entry : result.body.value(QStringLiteral("results")).toArray())
    {
        QCOMPARE(errorCode(entry), QStringLiteral("usage"));
        QVERIFY(entry.toObject().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().contains(
            QStringLiteral("cannot be in a batch")));
    }
}

// The game's .dat files are empty, so this passes only if nothing is loaded.
void CliTest::modsCreateMakesAMod()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeModdedGame(game));
    DataSource source;
    source.gameDir = game.path();
    source.mod = QStringLiteral("Balance");

    QJsonObject request = modsCreate(QStringLiteral("Castles"));
    request.insert(QStringLiteral("author"), QStringLiteral("Me"));
    request.insert(QStringLiteral("description"), QStringLiteral("Cheaper castles"));
    const HandlerResult result = RequestHandler().handle(source, request);
    if (result.exitCode != 0)
        QFAIL(qPrintable(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
    QCOMPARE(result.body.value(QStringLiteral("title")).toString(), QStringLiteral("Castles"));
    QCOMPARE(result.body.value(QStringLiteral("dir")).toString(), forwardPath(game.filePath(QStringLiteral("mods/Castles"))));
    QCOMPARE(result.body.value(QStringLiteral("dat")).toString(),
             forwardPath(game.filePath(QStringLiteral("mods/Castles/resources/_common/dat/empires2_x2_p1.dat"))));
    QCOMPARE(result.body.value(QStringLiteral("hasDat")).toBool(), false);
    QVERIFY(result.body.value(QStringLiteral("note")).toString().contains(QStringLiteral("disabled")));
    QVERIFY(!QFile::exists(game.filePath(QStringLiteral("mods/Castles/resources"))));

    const HandlerResult listed = RequestHandler().handle(source, op(QStringLiteral("mods-list")));
    QCOMPARE(listed.exitCode, 0);
    const QJsonArray mods = listed.body.value(QStringLiteral("mods")).toArray();
    QCOMPARE(mods.size(), 3);
    QCOMPARE(mods.at(1).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Castles"));
    QCOMPARE(mods.at(1).toObject().value(QStringLiteral("author")).toString(), QStringLiteral("Me"));
    QCOMPARE(mods.at(1).toObject().value(QStringLiteral("description")).toString(), QStringLiteral("Cheaper castles"));
}

void CliTest::modsCreateFromMod()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeModdedGame(game));
    DataSource source;
    source.gameDir = game.path();

    QJsonObject request = modsCreate(QStringLiteral("Copy"));
    request.insert(QStringLiteral("from"), QStringLiteral("mod:balance"));
    HandlerResult result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("hasDat")).toBool(), true);
    QVERIFY(QFile::exists(game.filePath(QStringLiteral("mods/Copy/resources/_common/dat/empires2_x2_p1.dat"))));

    // A source mod without a .dat gives a mod without one.
    request = modsCreate(QStringLiteral("FromEmpty"));
    request.insert(QStringLiteral("from"), QStringLiteral("mod:Empty"));
    result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("hasDat")).toBool(), false);

    request = modsCreate(QStringLiteral("FromGame"));
    request.insert(QStringLiteral("from"), QStringLiteral("game"));
    QCOMPARE(RequestHandler().handle(source, request).exitCode, 0);
}

void CliTest::modsCreateFails()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeModdedGame(game));
    DataSource source;
    source.gameDir = game.path();

    const auto failsWith = [&](const DataSource &where, const QJsonObject &request, int exitCode, const QString &code) {
        const HandlerResult result = RequestHandler().handle(where, request);
        QCOMPARE(result.exitCode, exitCode);
        QCOMPARE(errorCode(result), code);
    };

    failsWith(source, modsCreate(QStringLiteral("balance")), 3, QStringLiteral("bad_value"));
    failsWith(source, modsCreate(QStringLiteral("a/b")), 3, QStringLiteral("bad_value"));
    failsWith(source, modsCreate(QStringLiteral(" padded ")), 3, QStringLiteral("bad_value"));

    QJsonObject missing = modsCreate(QStringLiteral("Fresh"));
    missing.insert(QStringLiteral("from"), QStringLiteral("mod:Nope"));
    failsWith(source, missing, 2, QStringLiteral("mod_not_found"));
    missing.insert(QStringLiteral("from"), QStringLiteral("Balance"));
    failsWith(source, missing, 1, QStringLiteral("usage"));
    QVERIFY(!QFile::exists(game.filePath(QStringLiteral("mods/Fresh"))));

    failsWith(DataSource{}, modsCreate(QStringLiteral("Fresh")), 2, QStringLiteral("no_dataset"));
    failsWith(tcSource(), modsCreate(QStringLiteral("Fresh")), 2, QStringLiteral("mods_unsupported"));

    QTemporaryDir cd;
    QVERIFY(cd.isValid());
    QVERIFY(writeFile(cd.path(), QStringLiteral("data/empires2_x1_p1.dat")));
    DataSource cdSource;
    cdSource.gameDir = cd.path();
    failsWith(cdSource, modsCreate(QStringLiteral("Fresh")), 2, QStringLiteral("mods_unsupported"));
}

void CliTest::diffChecksRequestBeforeOpen_data()
{
    QTest::addColumn<QJsonObject>("request");
    QTest::addColumn<int>("exitCode");
    QTest::addColumn<QString>("code");

    const auto with = [](QJsonObject request, const QString &key, const QJsonValue &value) {
        request.insert(key, value);
        return request;
    };
    QTest::newRow("kind is a number") << with(diff(), QStringLiteral("kind"), 4) << 1 << QStringLiteral("usage");
    QTest::newRow("unknown kind") << diff(QStringLiteral("building")) << 3 << QStringLiteral("unknown_kind");
    QTest::newRow("tech with civ") << with(diff(QStringLiteral("tech")), QStringLiteral("civ"), 1) << 1
                                   << QStringLiteral("usage");
    QTest::newRow("negative civ") << with(diff(), QStringLiteral("civ"), -1) << 1 << QStringLiteral("usage");
    QTest::newRow("fields without kind") << with(diff(), QStringLiteral("fields"), QJsonArray{QStringLiteral("hit_points")})
                                         << 1 << QStringLiteral("usage");
    QTest::newRow("fields empty") << with(diff(QStringLiteral("unit")), QStringLiteral("fields"), QJsonArray()) << 1
                                  << QStringLiteral("usage");
    QTest::newRow("field is a number") << with(diff(QStringLiteral("unit")), QStringLiteral("fields"), QJsonArray{5}) << 1
                                       << QStringLiteral("usage");
    QTest::newRow("negative offset") << with(diff(), QStringLiteral("offset"), -1) << 1 << QStringLiteral("usage");
    QTest::newRow("limit is text") << with(diff(), QStringLiteral("limit"), QStringLiteral("5")) << 1
                                   << QStringLiteral("usage");
}

// The source has no data, so a request that reached the open would be
// no_dataset, never these.
void CliTest::diffChecksRequestBeforeOpen()
{
    QFETCH(QJsonObject, request);
    QFETCH(int, exitCode);
    QFETCH(QString, code);

    DataSource source;
    source.mod = QStringLiteral("Balance");
    const HandlerResult result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, exitCode);
    QCOMPARE(errorCode(result), code);
}

// The game's .dat files are empty, so a load would be load_failed.
void CliTest::diffNeedsAMod()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeModdedGame(game));

    DataSource source;
    source.gameDir = game.path();
    HandlerResult result = RequestHandler().handle(source, diff());
    QCOMPARE(result.exitCode, 5);
    QCOMPARE(errorCode(result), QStringLiteral("mod_required"));

    source.mod = QStringLiteral("Nope");
    QCOMPARE(errorCode(RequestHandler().handle(source, diff())), QStringLiteral("mod_not_found"));
    source.mod = QStringLiteral("Balance");
    QCOMPARE(errorCode(RequestHandler().handle(source, diff())), QStringLiteral("load_failed"));

    result = RequestHandler().handle(tcSource(), diff());
    QCOMPARE(result.exitCode, 2);
    QCOMPARE(errorCode(result), QStringLiteral("mods_unsupported"));

    result = RequestHandler().handle(source, batch({diff()}));
    QCOMPARE(result.exitCode, 0);
    const QJsonValue entry = result.body.value(QStringLiteral("results")).toArray().at(0);
    QCOMPARE(errorCode(entry), QStringLiteral("usage"));
    QVERIFY(entry.toObject().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().contains(
        QStringLiteral("cannot be in a batch")));
}

// One mod .dat with an edit of each kind diff reports, made with genieutils
// directly because some (an emptied slot, a removed tech) are not field sets.
void CliTest::diffComparesModWithGame()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(makeWritableGame(game));
    const DataSource source = balanceSource(game);
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    const QString modDat = game.filePath(QStringLiteral("mods/Balance/resources/_common/dat/empires2_x2_p1.dat"));

    // Without its own .dat the mod is the game's data: nothing to compare.
    HandlerResult result = RequestHandler().handle(source, diff());
    if (result.exitCode != 0)
        QFAIL(qPrintable(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
    QCOMPARE(result.body.value(QStringLiteral("hasDat")).toBool(), false);
    QCOMPARE(result.body.value(QStringLiteral("modDat")).toString(), forwardPath(modDat));
    QCOMPARE(result.body.value(QStringLiteral("gameDat")).toString(), forwardPath(gameDat));
    QCOMPARE(result.body.value(QStringLiteral("total")).toInt(), 0);
    QCOMPARE(result.body.value(QStringLiteral("items")).toArray().size(), 0);
    const QJsonObject techCount = result.body.value(QStringLiteral("counts")).toObject().value(QStringLiteral("tech")).toObject();
    QCOMPARE(techCount.value(QStringLiteral("mod")), techCount.value(QStringLiteral("game")));
    QVERIFY(!QFile::exists(modDat));

    Session session;
    QString error;
    const GameDataset dataset = detectInstall(game.path()).at(0);
    QVERIFY2(session.open(dataset, &error), qPrintable(error));
    genie::DatFile &dat = *session.dat();
    const int techCountBefore = static_cast<int>(dat.Techs.size());
    QCOMPARE(dat.Civs.at(1).Units.at(4).HitPoints, dat.Civs.at(2).Units.at(4).HitPoints);
    const int hitPoints = dat.Civs.at(1).Units.at(4).HitPoints;
    QVERIFY(dat.Civs.at(4).UnitPointers.at(4) != 0);
    int effectId = 0;
    while (effectId < static_cast<int>(dat.Effects.size()) && dat.Effects.at(effectId).EffectCommands.empty())
        ++effectId;
    QVERIFY(effectId < static_cast<int>(dat.Effects.size()));
    const int commands = static_cast<int>(dat.Effects.at(effectId).EffectCommands.size());
    const int commandType = dat.Effects.at(effectId).EffectCommands.front().Type;
    const QString effectName = QString::fromLatin1(dat.Effects.at(effectId).Name);
    QVERIFY(!effectName.isEmpty());

    dat.Civs.at(1).Units.at(4).HitPoints = hitPoints + 7;
    dat.Civs.at(2).Units.at(4).HitPoints = hitPoints + 7;
    dat.Civs.at(3).Units.at(4).Type50.DisplayedAttack += 1;
    dat.Civs.at(4).UnitPointers.at(4) = 0;
    dat.Techs.pop_back();
    dat.Effects.at(effectId).EffectCommands.push_back(dat.Effects.at(effectId).EffectCommands.front());
    dat.Effects.at(effectId).Name.clear();
    QVERIFY(QDir().mkpath(QFileInfo(modDat).absolutePath()));
    QVERIFY2(session.saveAs(modDat, &error), qPrintable(error));
    const QDateTime gameWritten = QFileInfo(gameDat).lastModified();
    const QDateTime modWritten = QFileInfo(modDat).lastModified();

    result = RequestHandler().handle(source, diff());
    if (result.exitCode != 0)
        QFAIL(qPrintable(result.body.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
    QCOMPARE(result.body.value(QStringLiteral("hasDat")).toBool(), true);
    const QJsonObject counts = result.body.value(QStringLiteral("counts")).toObject();
    QCOMPARE(counts.value(QStringLiteral("tech")).toObject().value(QStringLiteral("mod")).toInt(), techCountBefore - 1);
    QCOMPARE(counts.value(QStringLiteral("tech")).toObject().value(QStringLiteral("game")).toInt(), techCountBefore);
    QCOMPARE(result.body.value(QStringLiteral("changed")).toInt(), 3);
    QCOMPARE(result.body.value(QStringLiteral("onlyInGame")).toInt(), 2);
    QCOMPARE(result.body.value(QStringLiteral("onlyInMod")).toInt(), 0);
    QCOMPARE(result.body.value(QStringLiteral("total")).toInt(), 5);
    const QJsonArray items = result.body.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 5);

    // Kinds in registry order, then IDs; civs with the same change share an item.
    const QJsonObject both = items.at(0).toObject();
    QCOMPARE(both.value(QStringLiteral("status")).toString(), QStringLiteral("changed"));
    QCOMPARE(both.value(QStringLiteral("kind")).toString(), QStringLiteral("unit"));
    QCOMPARE(both.value(QStringLiteral("id")).toInt(), 4);
    QCOMPARE(intList(both.value(QStringLiteral("civs"))), QList<int>({1, 2}));
    QVERIFY(!both.value(QStringLiteral("name")).toString().isEmpty());
    QCOMPARE(fieldKeys(both), QStringList{QStringLiteral("hit_points")});
    QCOMPARE(fieldByKey(both, QStringLiteral("hit_points")).value(QStringLiteral("mod")).toInt(), hitPoints + 7);
    QCOMPARE(fieldByKey(both, QStringLiteral("hit_points")).value(QStringLiteral("game")).toInt(), hitPoints);
    QVERIFY(!both.contains(QStringLiteral("unlisted")));

    const QJsonObject attack = items.at(1).toObject();
    QCOMPARE(intList(attack.value(QStringLiteral("civs"))), QList<int>({3}));
    QCOMPARE(fieldKeys(attack), QStringList());
    QCOMPARE(attack.value(QStringLiteral("unlisted")).toBool(), true);

    const QJsonObject emptied = items.at(2).toObject();
    QCOMPARE(emptied.value(QStringLiteral("status")).toString(), QStringLiteral("onlyInGame"));
    QCOMPARE(intList(emptied.value(QStringLiteral("civs"))), QList<int>({4}));
    QCOMPARE(emptied.value(QStringLiteral("name")), both.value(QStringLiteral("name")));
    QVERIFY(!emptied.contains(QStringLiteral("fields")));

    const QJsonObject removed = items.at(3).toObject();
    QCOMPARE(removed.value(QStringLiteral("status")).toString(), QStringLiteral("onlyInGame"));
    QCOMPARE(removed.value(QStringLiteral("kind")).toString(), QStringLiteral("tech"));
    QCOMPARE(removed.value(QStringLiteral("id")).toInt(), techCountBefore - 1);
    QVERIFY(!removed.contains(QStringLiteral("civs")));

    // The added command's fields exist only in the mod. A blank internal name
    // looks like a placeholder, so the game's name is used.
    const QJsonObject effect = items.at(4).toObject();
    QCOMPARE(effect.value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
    QCOMPARE(effect.value(QStringLiteral("id")).toInt(), effectId);
    QCOMPARE(effect.value(QStringLiteral("internalName")).toString(), effectName);
    QCOMPARE(fieldByKey(effect, QStringLiteral("internal_name")).value(QStringLiteral("mod")).toString(), QString());
    const QJsonObject type = fieldByKey(effect, QStringLiteral("command%1.type").arg(commands + 1));
    QCOMPARE(type.value(QStringLiteral("mod")).toInt(), commandType);
    QVERIFY(type.value(QStringLiteral("game")).isNull());
    QCOMPARE(type.value(QStringLiteral("labelKind")).toString(), QStringLiteral("effect-type"));
    QVERIFY(type.contains(QStringLiteral("modLabel")));
    QVERIFY(!type.contains(QStringLiteral("gameLabel")));

    // Field patterns skip the unlisted check; presence still counts. Paging
    // counts every item and returns a slice.
    QJsonObject request = diff(QStringLiteral("unit"));
    request.insert(QStringLiteral("fields"), QJsonArray{QStringLiteral("hit*")});
    request.insert(QStringLiteral("offset"), 1);
    request.insert(QStringLiteral("limit"), 1);
    result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("kind")).toString(), QStringLiteral("unit"));
    QCOMPARE(result.body.value(QStringLiteral("fields")).toArray(), QJsonArray{QStringLiteral("hit*")});
    QCOMPARE(result.body.value(QStringLiteral("changed")).toInt(), 1);
    QCOMPARE(result.body.value(QStringLiteral("total")).toInt(), 2);
    QCOMPARE(result.body.value(QStringLiteral("offset")).toInt(), 1);
    QCOMPARE(result.body.value(QStringLiteral("limit")).toInt(), 1);
    const QJsonArray page = result.body.value(QStringLiteral("items")).toArray();
    QCOMPARE(page.size(), 1);
    QCOMPARE(intList(page.at(0).toObject().value(QStringLiteral("civs"))), QList<int>({4}));
    request.remove(QStringLiteral("offset"));
    request.remove(QStringLiteral("limit"));
    request.insert(QStringLiteral("fields"), QJsonArray{QStringLiteral("nope")});
    QCOMPARE(errorCode(RequestHandler().handle(source, request)), QStringLiteral("unknown_field"));

    request = diff(QStringLiteral("unit"));
    request.insert(QStringLiteral("civ"), 3);
    result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.body.value(QStringLiteral("civ")).toInt(), 3);
    QCOMPARE(result.body.value(QStringLiteral("total")).toInt(), 1);
    QCOMPARE(result.body.value(QStringLiteral("items")).toArray().at(0).toObject().value(QStringLiteral("unlisted")).toBool(), true);
    request.insert(QStringLiteral("civ"), 1000);
    result = RequestHandler().handle(source, request);
    QCOMPARE(result.exitCode, 3);
    QCOMPARE(errorCode(result), QStringLiteral("unknown_entity"));

    QCOMPARE(QFileInfo(gameDat).lastModified(), gameWritten);
    QCOMPARE(QFileInfo(modDat).lastModified(), modWritten);
}

QTEST_GUILESS_MAIN(CliTest)
#include "CliTest.moc"
