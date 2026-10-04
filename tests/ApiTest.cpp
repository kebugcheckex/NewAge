#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "api/DataService.h"
#include "core/Mods.h"
#include "genie/dat/DatFile.h"

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

FieldEdit unitHitPoints(const QVariant &value)
{
    FieldEdit edit;
    edit.kind = QStringLiteral("unit");
    edit.id = 4;
    edit.civ = 1;
    edit.key = QStringLiteral("hit_points");
    edit.value = value;
    return edit;
}

int16_t hitPoints(const Session &session, int civ)
{
    return session.dat()->Civs.at(civ).Units.at(4).HitPoints;
}

} // namespace

class ApiTest : public QObject
{
    Q_OBJECT

private slots:
    void resolveSourceReadsEnvironment();
    void missingSourceIsNoDataset();
    void looseFileIsReadOnly();
    void datasetNameSelectsFile();
    void modMustExist();
    void writeNeedsAMod();
    void refusesGameDataPath();
    void applyWritesModOnly();
    void applyIsAtomic();
    void unchangedAndDryRunDoNotSave();
    void expectMismatchIsConflict();
    void unsupportedDatasetRejectsMod();
};

void ApiTest::resolveSourceReadsEnvironment()
{
    qputenv("NEWAGE_GAME", "C:/Games/AoE2");
    qputenv("NEWAGE_DATASET", "empires2_x2_p1.dat");
    qputenv("NEWAGE_MOD", "Balance");
    qputenv("NEWAGE_MODS_FOLDER", "C:/Mods");
    const DataSource filled = resolveSource({});
    QCOMPARE(filled.gameDir, QStringLiteral("C:/Games/AoE2"));
    QCOMPARE(filled.dataset, QStringLiteral("empires2_x2_p1.dat"));
    QCOMPARE(filled.mod, QStringLiteral("Balance"));
    QCOMPARE(filled.modsFolder, QStringLiteral("C:/Mods"));

    DataSource explicitSource;
    explicitSource.gameDir = QStringLiteral("D:/Other");
    QCOMPARE(resolveSource(explicitSource).gameDir, QStringLiteral("D:/Other"));
    qunsetenv("NEWAGE_GAME");
    qunsetenv("NEWAGE_DATASET");
    qunsetenv("NEWAGE_MOD");
    qunsetenv("NEWAGE_MODS_FOLDER");
}

void ApiTest::missingSourceIsNoDataset()
{
    DataService service;
    const OpenResult opened = service.open({});
    QVERIFY(!opened.ok);
    QCOMPARE(opened.error.code, QStringLiteral("no_dataset"));
    QVERIFY(!service.isOpen());

    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    DataSource source;
    source.gameDir = empty.path();
    QCOMPARE(service.open(source).error.code, QStringLiteral("no_dataset"));
}

void ApiTest::looseFileIsReadOnly()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    DataService service;
    DataSource source;
    source.datPath = kTcDat;
    QCOMPARE(service.open(source).error.code, QStringLiteral("load_failed"));

    source.versionKey = QStringLiteral("tc");
    const OpenResult opened = service.open(source);
    QVERIFY2(opened.ok, qPrintable(opened.error.message));
    QVERIFY(service.isOpen());
    QCOMPARE(service.kinds().size(), 4);
    QVERIFY(service.kind(QStringLiteral("unit")) != nullptr);
    QVERIFY(service.savePath().isEmpty());

    const int16_t original = hitPoints(service.session(), 1);
    const ApplyResult applied = service.apply({unitHitPoints(original + 1)});
    QVERIFY(!applied.ok);
    QCOMPARE(applied.error.code, QStringLiteral("mods_unsupported"));
    QCOMPARE(hitPoints(service.session(), 1), original);

    source.mod = QStringLiteral("Balance");
    QCOMPARE(service.open(source).error.code, QStringLiteral("mods_unsupported"));
}

void ApiTest::datasetNameSelectsFile()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(gameDat).absolutePath()));
    QVERIFY(QFile::copy(kHdDat, gameDat));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    source.dataset = QStringLiteral("missing.dat");
    DataService service;
    QCOMPARE(service.open(source).error.code, QStringLiteral("no_dataset"));

    source.dataset = QStringLiteral("empires2_x2_p1.dat");
    const OpenResult opened = service.open(source);
    QVERIFY2(opened.ok, qPrintable(opened.error.message));
    QCOMPARE(service.dataset().versionKey, QStringLiteral("aokhd"));
}

void ApiTest::modMustExist()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(gameDat).absolutePath()));
    QVERIFY(QFile::copy(kHdDat, gameDat));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    source.mod = QStringLiteral("No Such Mod");
    DataService service;
    QCOMPARE(service.open(source).error.code, QStringLiteral("mod_not_found"));
    QVERIFY(!service.isOpen());
}

void ApiTest::writeNeedsAMod()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(gameDat).absolutePath()));
    QVERIFY(QFile::copy(kHdDat, gameDat));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    DataService service;
    QVERIFY2(service.open(source).ok, qPrintable(service.open(source).error.message));
    const ApplyResult applied = service.apply({unitHitPoints(hitPoints(service.session(), 1) + 1)});
    QCOMPARE(applied.error.code, QStringLiteral("mod_required"));
    QVERIFY(!QFile::exists(game.filePath(QStringLiteral("mods"))));
}

void ApiTest::refusesGameDataPath()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(gameDat).absolutePath()));
    QVERIFY(QFile::copy(kHdDat, gameDat));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    source.mod = game.path();
    DataService service;
    QCOMPARE(service.open(source).error.code, QStringLiteral("game_data_protected"));
}

void ApiTest::applyWritesModOnly()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(gameDat).absolutePath()));
    QVERIFY(QFile::copy(kHdDat, gameDat));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    DataService probe;
    QVERIFY2(probe.open(source).ok, qPrintable(probe.open(source).error.message));
    QString error;
    const QString modDir = createMod(modsFolders(probe.dataset()).at(0), {QStringLiteral("Balance"), {}, {}}, &error);
    QVERIFY2(!modDir.isEmpty(), qPrintable(error));
    probe.close();

    source.mod = QStringLiteral("balance");
    DataService service;
    const OpenResult opened = service.open(source);
    QVERIFY2(opened.ok, qPrintable(opened.error.message));
    QCOMPARE(service.modDir(), QDir::cleanPath(modDir));
    QVERIFY(!QFile::exists(service.savePath()));

    const int16_t original = hitPoints(service.session(), 1);
    const int16_t otherCiv = hitPoints(service.session(), 0);
    FieldEdit points = unitHitPoints(original + 1);
    FieldEdit unknown = points;
    unknown.key = QStringLiteral("nope");
    QCOMPARE(service.apply({unknown}).error.code, QStringLiteral("unknown_field"));
    QCOMPARE(hitPoints(service.session(), 1), original);

    FieldEdit kind = points;
    kind.kind = QStringLiteral("building");
    QCOMPARE(service.apply({kind}).error.code, QStringLiteral("unknown_kind"));

    const ApplyResult applied = service.apply({points});
    QVERIFY2(applied.ok, qPrintable(applied.error.message));
    QCOMPARE(applied.changes.size(), 1);
    QCOMPARE(applied.changes.at(0).kind, QStringLiteral("unit"));
    QCOMPARE(applied.changes.at(0).id, 4);
    QCOMPARE(applied.changes.at(0).civ, 1);
    QCOMPARE(applied.changes.at(0).key, QStringLiteral("hit_points"));
    QCOMPARE(applied.changes.at(0).oldValue, QVariant(static_cast<int>(original)));
    QCOMPARE(applied.changes.at(0).newValue, QVariant(static_cast<int>(original + 1)));
    QCOMPARE(applied.saved, QDir::fromNativeSeparators(QDir::cleanPath(service.savePath())));
    QVERIFY(!service.session().isModified());

    DataService again;
    QVERIFY2(again.open(source).ok, qPrintable(again.open(source).error.message));
    QCOMPARE(hitPoints(again.session(), 1), static_cast<int16_t>(original + 1));
    QCOMPARE(hitPoints(again.session(), 0), otherCiv);

    source.mod.clear();
    DataService gameData;
    QVERIFY2(gameData.open(source).ok, qPrintable(gameData.open(source).error.message));
    QCOMPARE(hitPoints(gameData.session(), 1), original);
}

void ApiTest::applyIsAtomic()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(gameDat).absolutePath()));
    QVERIFY(QFile::copy(kHdDat, gameDat));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    DataService probe;
    QVERIFY(probe.open(source).ok);
    QString error;
    QVERIFY(!createMod(modsFolders(probe.dataset()).at(0), {QStringLiteral("Balance"), {}, {}}, &error).isEmpty());
    probe.close();

    source.mod = QStringLiteral("Balance");
    DataService service;
    QVERIFY2(service.open(source).ok, qPrintable(service.open(source).error.message));

    const int16_t original = hitPoints(service.session(), 1);
    FieldEdit bad = unitHitPoints(99999);
    const ApplyResult applied = service.apply({unitHitPoints(original + 1), bad});
    QCOMPARE(applied.error.code, QStringLiteral("out_of_range"));
    QCOMPARE(applied.error.key, QStringLiteral("hit_points"));
    QVERIFY(applied.saved.isEmpty());
    QVERIFY(!QFile::exists(service.savePath()));
    QVERIFY(!service.session().isModified());
    QCOMPARE(hitPoints(service.session(), 1), original);
}

void ApiTest::unchangedAndDryRunDoNotSave()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(gameDat).absolutePath()));
    QVERIFY(QFile::copy(kHdDat, gameDat));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    DataService probe;
    QVERIFY(probe.open(source).ok);
    QString error;
    QVERIFY(!createMod(modsFolders(probe.dataset()).at(0), {QStringLiteral("Balance"), {}, {}}, &error).isEmpty());
    probe.close();

    source.mod = QStringLiteral("Balance");
    DataService service;
    QVERIFY2(service.open(source).ok, qPrintable(service.open(source).error.message));

    const int16_t original = hitPoints(service.session(), 1);
    const ApplyResult same = service.apply({unitHitPoints(static_cast<int>(original))});
    QVERIFY2(same.ok, qPrintable(same.error.message));
    QCOMPARE(same.changes.size(), 0);
    QCOMPARE(same.unchanged.size(), 1);
    QVERIFY(same.saved.isEmpty());
    QVERIFY(!QFile::exists(service.savePath()));
    QVERIFY(!service.session().isModified());

    const ApplyResult dry = service.apply({unitHitPoints(original + 1)}, true);
    QVERIFY2(dry.ok, qPrintable(dry.error.message));
    QCOMPARE(dry.changes.size(), 1);
    QCOMPARE(dry.changes.at(0).oldValue, QVariant(static_cast<int>(original)));
    QCOMPARE(dry.changes.at(0).newValue, QVariant(static_cast<int>(original + 1)));
    QVERIFY(dry.saved.isEmpty());
    QVERIFY(!QFile::exists(service.savePath()));
    QCOMPARE(hitPoints(service.session(), 1), original);
}

void ApiTest::expectMismatchIsConflict()
{
    if (!QFile::exists(kHdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(gameDat).absolutePath()));
    QVERIFY(QFile::copy(kHdDat, gameDat));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    DataService probe;
    QVERIFY(probe.open(source).ok);
    QString error;
    QVERIFY(!createMod(modsFolders(probe.dataset()).at(0), {QStringLiteral("Balance"), {}, {}}, &error).isEmpty());
    probe.close();

    source.mod = QStringLiteral("Balance");
    DataService service;
    QVERIFY2(service.open(source).ok, qPrintable(service.open(source).error.message));

    const int16_t original = hitPoints(service.session(), 1);
    FieldEdit edit = unitHitPoints(original + 1);
    edit.expect = original - 1;
    const ApplyResult applied = service.apply({edit});
    QCOMPARE(applied.error.code, QStringLiteral("conflict"));
    QCOMPARE(applied.error.id, 4);
    QCOMPARE(applied.error.civ, 1);
    QVERIFY(!service.session().isModified());
    QCOMPARE(hitPoints(service.session(), 1), original);
}

void ApiTest::unsupportedDatasetRejectsMod()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(writeFile(game.path(), QStringLiteral("data/empires2_x1_p1.dat")));

    DataSource source;
    source.gameDir = game.path();
    source.mod = QStringLiteral("Balance");
    DataService service;
    QCOMPARE(service.open(source).error.code, QStringLiteral("mods_unsupported"));
}

QTEST_GUILESS_MAIN(ApiTest)
#include "ApiTest.moc"
