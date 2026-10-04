#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "core/GameInstall.h"
#include "core/Mods.h"
#include "core/NameProvider.h"
#include "core/Session.h"
#include "core/SpriteLibrary.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "genie/dat/Unit.h"

using namespace newage;

namespace {

// Checked-in sample of a key-value string file (CRLF, with the odd lines).
const QString kSampleStrings = QStringLiteral(NEWAGE_TEST_DATA_DIR "/key-value-strings-sample.txt");

// Creates `relative` under `dir` (parent folders too) holding `content`.
bool writeFile(const QString &dir, const QString &relative, const QByteArray &content = {})
{
    const QString path = QDir(dir).filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

QString fileName(const QString &path)
{
    return QFileInfo(path).fileName();
}

QStringList fileNames(const QStringList &paths)
{
    QStringList names;
    for (const QString &path : paths)
        names << fileName(path);
    return names;
}

} // namespace

class GameDataTest : public QObject
{
    Q_OBJECT

private slots:
    void parseKeyValueSample();
    void parseKeyValueBomAndLastLine();
    void laterFilesOnlyFillGaps();
    void unreadableFilesAreReported();
    void detectsHdLayout();
    void detectsDeLayout();
    void detectsDllLayoutCaseInsensitively();
    void detectsNothingElsewhere();
    void openDatasetLoadsNames();
    void modsFoldersFollowLayout();
    void checkModTitles();
    void createAndFindMods();
    void editModInfoKeepsOtherKeys();
    void modDatasetUsesModFiles();
    void saveIntoNewMod();
    void realInstall();
    void techIconSlpFollowsVersion();
    void unitIconSlpFollowsVersion();
    void spriteSourceFollowsInstallLayout();
    void missingSpriteFrameIsEmpty();
};

void GameDataTest::parseKeyValueSample()
{
    QFile file(kSampleStrings);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QHash<int, QString> strings = NameProvider::parseKeyValue(file.readAll());

    QCOMPARE(strings.value(5083), QStringLiteral("Archer"));
    QCOMPARE(strings.value(5142), QStringLiteral("Castle"));
    QCOMPARE(strings.value(6142), QStringLiteral("Build Castle"));
    QCOMPARE(strings.value(7000), QStringLiteral("Indented"));
    QCOMPARE(strings.value(3125), QStringLiteral("Sent to \"%s\":"));
    QCOMPARE(strings.value(7002), QStringLiteral("Line\nbreak \\ end"));
    QCOMPARE(strings.value(7003), QStringLiteral("Ch%1teau").arg(QChar(0xE2))); // UTF-8 decoded
    QCOMPARE(strings.value(7004), QStringLiteral("No closing quote"));
    // Empty strings, non-numeric keys (IDS_..., 426071_perth) and lines
    // without quotes are left out.
    QVERIFY(!strings.contains(7001));
    QVERIFY(!strings.contains(426071));
    QVERIFY(!strings.contains(7005));
    QCOMPARE(strings.size(), 8);
}

void GameDataTest::parseKeyValueBomAndLastLine()
{
    const QHash<int, QString> strings = NameProvider::parseKeyValue("\xEF\xBB\xBF" "1 \"a\"\n2 \"b\"");
    QCOMPARE(strings.value(1), QStringLiteral("a"));
    QCOMPARE(strings.value(2), QStringLiteral("b"));
}

void GameDataTest::laterFilesOnlyFillGaps()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Like key-value-modded-strings over key-value-strings.
    QVERIFY(writeFile(dir.path(), QStringLiteral("modded.txt"), "5142 \"Fortress\"\n5083 \"\"\n"));

    NameProvider names;
    QVERIFY(names.load({dir.filePath(QStringLiteral("modded.txt")), kSampleStrings}));
    QCOMPARE(names.files().size(), 2);
    QCOMPARE(names.text(5142), QStringLiteral("Fortress"));
    // An empty string doesn't hide the lower-priority one.
    QCOMPARE(names.text(5083), QStringLiteral("Archer"));
    QCOMPARE(names.text(6142), QStringLiteral("Build Castle"));
    QCOMPARE(names.text(99999), QString());
    QCOMPARE(names.text(-1), QString());

    names.clear();
    QVERIFY(names.isEmpty());
    QCOMPARE(names.text(5142), QString());
}

void GameDataTest::unreadableFilesAreReported()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeFile(dir.path(), QStringLiteral("language.dll"), "not a PE file"));

    NameProvider names;
    QStringList errors;
    QVERIFY(!names.load({dir.filePath(QStringLiteral("missing.txt")), dir.filePath(QStringLiteral("language.dll")),
                         kSampleStrings},
                        &errors));
    QCOMPARE(errors.size(), 2);
    QVERIFY(errors.at(0).contains(QStringLiteral("missing.txt")));
    QVERIFY(errors.at(1).contains(QStringLiteral("language.dll")));
    // The good file still loads.
    QCOMPARE(names.files(), QStringList{kSampleStrings});
    QCOMPARE(names.text(5142), QStringLiteral("Castle"));
}

void GameDataTest::detectsHdLayout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const char *file : {"resources/_common/dat/empires2_x1_p1.dat", "resources/_common/dat/empires2_x2_p1.dat",
                             "resources/en/strings/key-value/key-value-strings-utf8.txt",
                             "resources/en/strings/key-value/key-value-modded-strings-utf8.txt",
                             "resources/de/strings/key-value/key-value-strings-utf8.txt"})
        QVERIFY(writeFile(dir.path(), QString::fromLatin1(file)));

    const QList<GameDataset> datasets = detectInstall(dir.path());
    QCOMPARE(datasets.size(), 2);
    QCOMPARE(datasets.at(0).versionKey, QStringLiteral("aokhd"));
    QCOMPARE(fileName(datasets.at(0).datPath), QStringLiteral("empires2_x2_p1.dat"));
    QCOMPARE(datasets.at(1).versionKey, QStringLiteral("tc"));
    QCOMPARE(fileName(datasets.at(1).datPath), QStringLiteral("empires2_x1_p1.dat"));
    const QStringList expected = {QStringLiteral("key-value-modded-strings-utf8.txt"),
                                  QStringLiteral("key-value-strings-utf8.txt")};
    QCOMPARE(fileNames(datasets.at(0).languageFiles), expected);
    QCOMPARE(fileNames(datasets.at(1).languageFiles), expected);
    QVERIFY(datasets.at(0).languageFiles.at(0).contains(QStringLiteral("/en/")));

    // Another locale picks its own folder.
    const QList<GameDataset> german = detectInstall(dir.path(), QStringLiteral("de"));
    QCOMPARE(fileNames(german.at(0).languageFiles), QStringList{QStringLiteral("key-value-strings-utf8.txt")});
    QVERIFY(german.at(0).languageFiles.at(0).contains(QStringLiteral("/de/")));
}

void GameDataTest::detectsDeLayout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const char *file : {"resources/_common/dat/empires2_x2_p1.dat",
                             "resources/en/strings/key-value/key-value-strings-utf8.txt",
                             "resources/en/strings/key-value/key-value-modded-strings-utf8.txt",
                             "resources/en/strings/key-value/key-value-paphos-strings-utf8.txt",
                             "resources/en/strings/key-value/paris-campaign-key-value-strings-utf8.txt",
                             "resources/en/strings/key-value/readme.txt"})
        QVERIFY(writeFile(dir.path(), QString::fromLatin1(file)));

    const QList<GameDataset> datasets = detectInstall(dir.path());
    QCOMPARE(datasets.size(), 1);
    QCOMPARE(datasets.at(0).versionKey, QStringLiteral("aoe2de"));
    // Modded over base, then the extra files in name order.
    const QStringList expected = {QStringLiteral("key-value-modded-strings-utf8.txt"),
                                  QStringLiteral("key-value-strings-utf8.txt"),
                                  QStringLiteral("key-value-paphos-strings-utf8.txt"),
                                  QStringLiteral("paris-campaign-key-value-strings-utf8.txt")};
    QCOMPARE(fileNames(datasets.at(0).languageFiles), expected);
}

void GameDataTest::detectsDllLayoutCaseInsensitively()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A TC install that also has the AoK data; one language DLL is missing.
    for (const char *file : {"Data/empires2.dat", "Data/EMPIRES2_X1_P1.DAT", "LANGUAGE.DLL", "language_x1_p1.dll"})
        QVERIFY(writeFile(dir.path(), QString::fromLatin1(file)));

    const QList<GameDataset> datasets = detectInstall(dir.path());
    QCOMPARE(datasets.size(), 2);
    QCOMPARE(datasets.at(0).versionKey, QStringLiteral("tc"));
    // The found paths exist; their spelling depends on the file system (Windows
    // keeps the case asked for, others give the case on disk).
    QVERIFY(QFileInfo::exists(datasets.at(0).datPath));
    QCOMPARE(fileName(datasets.at(0).datPath).toLower(), QStringLiteral("empires2_x1_p1.dat"));
    QCOMPARE(fileNames(datasets.at(0).languageFiles).join(QLatin1Char(',')).toLower(),
             QStringLiteral("language_x1_p1.dll,language.dll"));
    QCOMPARE(datasets.at(1).versionKey, QStringLiteral("aok"));
    QCOMPARE(fileNames(datasets.at(1).languageFiles).join(QLatin1Char(',')).toLower(), QStringLiteral("language.dll"));
}

void GameDataTest::detectsNothingElsewhere()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeFile(dir.path(), QStringLiteral("readme.txt")));
    QVERIFY(detectInstall(dir.path()).isEmpty());
    QVERIFY(detectInstall(dir.filePath(QStringLiteral("no-such-folder"))).isEmpty());
    QVERIFY(knownDatPaths().contains(QStringLiteral("data/empires2_x1_p1.dat")));
}

void GameDataTest::openDatasetLoadsNames()
{
    const QString tcDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x1_p1.dat");
    if (!QFile::exists(tcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    int opened = 0;
    QString nameOnOpened;
    const auto connection = connect(&session, &Session::opened, this, [&] {
        ++opened;
        nameOnOpened = session.names().text(5142);
    });

    QString error;
    QStringList warnings;
    const GameDataset dataset{QStringLiteral("sample"), QStringLiteral("tc"), tcDat,
                              {QStringLiteral("missing.txt"), kSampleStrings}};
    int steps = 0;
    int reportedCount = 0;
    LoadResult loaded = Session::read(dataset, [&](int, int count) {
        ++steps;
        reportedCount = count;
    });
    QCOMPARE(opened, 0);
    QCOMPARE(reportedCount, 1 + dataset.languageFiles.size());
    QCOMPARE(steps, reportedCount);
    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    warnings = loaded.warnings;
    session.adopt(std::move(loaded));
    QCOMPARE(opened, 1);
    // Names are there by the time views hear about the data.
    QCOMPARE(nameOnOpened, QStringLiteral("Castle"));
    disconnect(connection);
    QCOMPARE(warnings.size(), 1);
    QCOMPARE(session.dat()->Civs.at(1).Units.at(82).LanguageDLLName, 5142);

    session.close();
    QVERIFY(session.names().isEmpty());

    // Opening a bare .dat leaves no names behind from before.
    QVERIFY(session.open(dataset, &error));
    QVERIFY(session.open(tcDat, *findVersionProfile(QStringLiteral("tc")), &error));
    QVERIFY(session.names().isEmpty());

    // An unknown version key fails cleanly.
    QVERIFY(!session.open(GameDataset{{}, QStringLiteral("nope"), tcDat, {}}, &error));
    QVERIFY(!session.isOpen());
    QVERIFY(error.contains(QStringLiteral("nope")));
}

void GameDataTest::modsFoldersFollowLayout()
{
    QTemporaryDir hd;
    QVERIFY(hd.isValid());
    QVERIFY(writeFile(hd.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));
    QVERIFY(writeFile(hd.path(), QStringLiteral("resources/_common/dat/empires2_x2_p1.dat")));
    const QList<GameDataset> hdSets = detectInstall(hd.path());
    QCOMPARE(hdSets.size(), 2);
    for (const GameDataset &dataset : hdSets)
    {
        QCOMPARE(dataset.gameDir, QDir(hd.path()).absolutePath());
        QVERIFY(supportsMods(dataset));
        QCOMPARE(modsFolders(dataset), QStringList{QDir(hd.path()).filePath(QStringLiteral("mods"))});
    }

    // DE: in the user profile, one mods folder per player profile that has
    // one. The one the game keeps mod-status.json in goes first, ahead of the
    // "0" profile from before signing in.
    QTemporaryDir de;
    QTemporaryDir home;
    QVERIFY(de.isValid() && home.isValid());
    QVERIFY(writeFile(de.path(), QStringLiteral("resources/_common/dat/empires2_x2_p1.dat")));
    const QString games = QStringLiteral("Games/Age of Empires 2 DE/");
    QVERIFY(QDir(home.path()).mkpath(games + QStringLiteral("0/mods/local")));
    QVERIFY(writeFile(home.path(), games + QStringLiteral("76561190000000001/mods/mod-status.json"), "{}"));
    QVERIFY(QDir(home.path()).mkpath(games + QStringLiteral("76561190000000002/savegame")));
    QVERIFY(QDir(home.path()).mkpath(games + QStringLiteral("logs")));
    const GameDataset deSet = detectInstall(de.path()).at(0);
    QVERIFY(supportsMods(deSet));
    const QStringList expected = {QDir(home.path()).filePath(games + QStringLiteral("76561190000000001/mods/local")),
                                  QDir(home.path()).filePath(games + QStringLiteral("0/mods/local"))};
    QCOMPARE(modsFolders(deSet, home.path()), expected);

    // The CD-era games have no mod folders, TC included.
    QTemporaryDir tc;
    QVERIFY(tc.isValid());
    QVERIFY(writeFile(tc.path(), QStringLiteral("data/empires2_x1_p1.dat")));
    const GameDataset tcSet = detectInstall(tc.path()).at(0);
    QCOMPARE(tcSet.versionKey, QStringLiteral("tc"));
    QVERIFY(!supportsMods(tcSet));
    QVERIFY(modsFolders(tcSet).isEmpty());
    QVERIFY(!supportsMods(GameDataset{{}, QStringLiteral("aokhd"), hdSets.at(0).datPath, {}}));
}

void GameDataTest::checkModTitles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("Existing Mod")));

    QVERIFY(checkModTitle(dir.path(), QStringLiteral("My Balance Mod")).isEmpty());
    // The folder doesn't have to exist yet.
    QVERIFY(checkModTitle(dir.filePath(QStringLiteral("missing")), QStringLiteral("My Balance Mod")).isEmpty());
    for (const char *bad : {"", "   ", " leading", "trailing ", "dot.", "a/b", "a\\b", "what?", "a:b", "CON", "nul.txt",
                            "existing mod"})
        QVERIFY2(!checkModTitle(dir.path(), QString::fromLatin1(bad)).isEmpty(), bad);
    QVERIFY(checkModTitle(dir.path(), QStringLiteral("Console")).isEmpty());
}

void GameDataTest::createAndFindMods()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = dir.filePath(QStringLiteral("mods"));
    QVERIFY(findMods(folder).isEmpty());

    QString error;
    const QString modDir = createMod(
        folder, {QStringLiteral("Zebra Balance"), QStringLiteral("Someone"), QStringLiteral("Line one\nLine two")},
        &error);
    QVERIFY2(!modDir.isEmpty(), qPrintable(error));
    QCOMPARE(QFileInfo(modDir).fileName(), QStringLiteral("Zebra Balance"));
    QFile info(QDir(modDir).filePath(QStringLiteral("info.json")));
    QVERIFY(info.open(QIODevice::ReadOnly));
    const QJsonObject json = QJsonDocument::fromJson(info.readAll()).object();
    QCOMPARE(json.value(QStringLiteral("Title")).toString(), QStringLiteral("Zebra Balance"));
    QCOMPARE(json.value(QStringLiteral("Author")).toString(), QStringLiteral("Someone"));
    QCOMPARE(json.value(QStringLiteral("Description")).toString(), QStringLiteral("Line one\nLine two"));
    // The keys DE writes for its own local mods, and no others.
    QCOMPARE(json.keys(), (QStringList{QStringLiteral("Author"), QStringLiteral("CacheStatus"),
                                       QStringLiteral("Description"), QStringLiteral("Title")}));
    QCOMPARE(json.value(QStringLiteral("CacheStatus")).toInt(-1), 0);

    // A second one with the same name (any case) is refused.
    QVERIFY(createMod(folder, {QStringLiteral("zebra balance"), {}, {}}, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("already")));

    // A folder without info.json is listed under its folder name.
    QVERIFY(QDir(folder).mkdir(QStringLiteral("apple")));
    const QList<Mod> mods = findMods(folder);
    QCOMPARE(mods.size(), 2);
    QCOMPARE(mods.at(0).info.title, QStringLiteral("apple"));
    QVERIFY(mods.at(0).info.author.isEmpty());
    QCOMPARE(mods.at(1).info.title, QStringLiteral("Zebra Balance"));
    QCOMPARE(mods.at(1).info.author, QStringLiteral("Someone"));
    QCOMPARE(mods.at(1).dir, modDir);
}

void GameDataTest::editModInfoKeepsOtherKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Like a mod DE downloaded, with more keys than NewAge writes.
    QVERIFY(writeFile(dir.path(), QStringLiteral("Downloaded/info.json"),
                      R"({"Author":"Them","CacheStatus":1,"Description":"Old","ModId":12345,"Title":"Old Name"})"));
    const QString modDir = dir.filePath(QStringLiteral("Downloaded"));
    QCOMPARE(readModInfo(modDir).title, QStringLiteral("Old Name"));

    QString error;
    QVERIFY2(writeModInfo(modDir, {QStringLiteral("New Name"), QStringLiteral("Me"), QStringLiteral("New")}, &error),
             qPrintable(error));
    QFile info(QDir(modDir).filePath(QStringLiteral("info.json")));
    QVERIFY(info.open(QIODevice::ReadOnly));
    const QJsonObject json = QJsonDocument::fromJson(info.readAll()).object();
    QCOMPARE(json.value(QStringLiteral("Title")).toString(), QStringLiteral("New Name"));
    QCOMPARE(json.value(QStringLiteral("Author")).toString(), QStringLiteral("Me"));
    QCOMPARE(json.value(QStringLiteral("Description")).toString(), QStringLiteral("New"));
    QCOMPARE(json.value(QStringLiteral("CacheStatus")).toInt(), 1);
    QCOMPARE(json.value(QStringLiteral("ModId")).toInt(), 12345);
    // The folder keeps its name.
    QCOMPARE(findMods(dir.path()).at(0).dir, QDir::cleanPath(modDir));
    QCOMPARE(findMods(dir.path()).at(0).info.title, QStringLiteral("New Name"));

    // No folder lists nothing (not the current directory).
    QVERIFY(findMods(QString()).isEmpty());
}

void GameDataTest::modDatasetUsesModFiles()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x2_p1.dat")));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/en/strings/key-value/key-value-strings-utf8.txt")));
    const GameDataset dataset = detectInstall(game.path()).at(0);
    const QString modDir = QDir(game.path()).filePath(QStringLiteral("mods/My Mod"));
    QVERIFY(writeFile(modDir, QStringLiteral("resources/en/strings/key-value/key-value-modded-strings-utf8.txt")));

    const QString modDat = modDatPath(dataset, modDir);
    QCOMPARE(modDat, QDir(modDir).filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat")));
    QVERIFY(isGameDataFile(dataset, dataset.datPath));
    QVERIFY(!isGameDataFile(dataset, modDat));
    QVERIFY(!isGameDataFile(GameDataset{}, dataset.datPath));

    const GameDataset mod = modDataset(dataset, modDir, QStringLiteral("en"));
    QCOMPARE(mod.datPath, modDat);
    QCOMPARE(mod.versionKey, dataset.versionKey);
    QCOMPARE(mod.gameDir, dataset.gameDir);
    QVERIFY(mod.title.startsWith(QStringLiteral("My Mod: ")));
    // The mod's strings win over the game's.
    QCOMPARE(mod.languageFiles.size(), 2);
    QVERIFY(mod.languageFiles.at(0).startsWith(modDir));
    QCOMPARE(mod.languageFiles.at(1), dataset.languageFiles.at(0));
    QVERIFY(supportsMods(mod));
}

// The steps File > Save to Mod takes: a new mod, its dat folder, then a save
// that leaves the game's file alone.
void GameDataTest::saveIntoNewMod()
{
    const QString hdDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x2_p1.dat");
    if (!QFile::exists(hdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString gameDat = game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    QVERIFY(QDir().mkpath(QFileInfo(gameDat).absolutePath()));
    QVERIFY(QFile::copy(hdDat, gameDat));
    QVERIFY(writeFile(game.path(), QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")));
    const GameDataset dataset = detectInstall(game.path()).at(0);
    QCOMPARE(dataset.versionKey, QStringLiteral("aokhd"));

    Session session;
    QString error;
    QVERIFY2(session.open(dataset, &error), qPrintable(error));
    const auto original = session.dat()->Civs.at(1).Units.at(82).HitPoints;
    session.dat()->Civs.at(1).Units.at(82).HitPoints = original + 1;

    const QString modDir = createMod(modsFolders(dataset).at(0), {QStringLiteral("My Balance Mod"), {}, {}}, &error);
    QVERIFY2(!modDir.isEmpty(), qPrintable(error));
    const QString modDat = modDatPath(dataset, modDir);
    QVERIFY(QDir().mkpath(QFileInfo(modDat).absolutePath()));
    QVERIFY2(session.saveAs(modDat, &error), qPrintable(error));
    QCOMPARE(QDir::fromNativeSeparators(modDat),
             game.filePath(QStringLiteral("mods/My Balance Mod/resources/_common/dat/empires2_x2_p1.dat")));

    Session reopened;
    QVERIFY2(reopened.open(modDataset(dataset, modDir, QStringLiteral("en")), &error), qPrintable(error));
    QCOMPARE(reopened.dat()->Civs.at(1).Units.at(82).HitPoints, original + 1);
    QVERIFY2(reopened.open(dataset, &error), qPrintable(error));
    QCOMPARE(reopened.dat()->Civs.at(1).Units.at(82).HitPoints, original);
}

// Runs on a real game folder when NEWAGE_TEST_GAME_DIR is set: every data set
// found must open, and unit 82 must be named "Castle" (all AoE2 versions).
void GameDataTest::realInstall()
{
    const QString dir = qEnvironmentVariable("NEWAGE_TEST_GAME_DIR");
    if (dir.isEmpty())
        QSKIP("Set NEWAGE_TEST_GAME_DIR to a game folder to run this test.");

    const QList<GameDataset> datasets = detectInstall(dir);
    QVERIFY2(!datasets.isEmpty(), qPrintable(dir));
    for (const GameDataset &dataset : datasets)
    {
        qInfo().noquote() << dataset.title << "-" << dataset.languageFiles.size() << "language files";

        // Strings first, so a .dat genieutils can't read doesn't hide them.
        NameProvider names;
        QStringList errors;
        QVERIFY2(names.load(dataset.languageFiles, &errors), qPrintable(errors.join(QLatin1Char('\n'))));
        QCOMPARE(names.text(5142), QStringLiteral("Castle"));

        Session session;
        QString error;
        QVERIFY2(session.open(dataset, &error), qPrintable(error));
        const genie::Unit &castle = session.dat()->Civs.at(1).Units.at(82);
        QCOMPARE(session.names().text(castle.LanguageDLLName), QStringLiteral("Castle"));
    }
}

void GameDataTest::techIconSlpFollowsVersion()
{
    QCOMPARE(SpriteLibrary::techIconSlpId(genie::GV_AoE, 3), 50729);
    QCOMPARE(SpriteLibrary::techIconSlpId(genie::GV_TC, 3), 50729);
    QCOMPARE(SpriteLibrary::techIconSlpId(genie::GV_Cysion, 1), 50729);
    QCOMPARE(SpriteLibrary::techIconSlpId(genie::GV_C2, 9), 50729);
    QCOMPARE(SpriteLibrary::techIconSlpId(genie::GV_SWGB, 2), 50691);
    QCOMPARE(SpriteLibrary::techIconSlpId(genie::GV_SWGB, -1), 50689);
    QCOMPARE(SpriteLibrary::techIconSlpId(genie::GV_CC, 1), 53261);
    QCOMPARE(SpriteLibrary::techIconSlpId(genie::GV_CCV, 0), 53260);
    QCOMPARE(SpriteLibrary::techIconSlpId(genie::GV_CCV2, 4), 53364);
}

void GameDataTest::unitIconSlpFollowsVersion()
{
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_AoE, 3, genie::UT_Creatable, 4), 50730);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_TC, 2, genie::UT_Building, 3), 50706);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_TC, 9, genie::UT_Building, 51), 50730);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_Cysion, 1, genie::UT_Building, 54), 50730);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_C2, 1, genie::UT_Building, 27), 50705);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_SWGB, 1, genie::UT_Building, 18), 50705);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_SWGB, 2, genie::UT_Building, 34), 50735);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_SWGB, 2, genie::UT_Creatable, 4), 50735);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_SWGB, -1, genie::UT_Building, 36), 50733);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_CC, 1, genie::UT_Building, 18), 53241);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_CC, 1, genie::UT_Building, 34), 53251);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_CCV, 0, genie::UT_Creatable, 0), 53250);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_CCV2, 4, genie::UT_Building, 18), 53304);
    QCOMPARE(SpriteLibrary::unitIconSlpId(genie::GV_CCV2, 4, genie::UT_Creatable, 0), 53334);

    QCOMPARE(SpriteLibrary::unitIconFrame(18, genie::UT_Creatable, 2), 18);
    QCOMPARE(SpriteLibrary::unitIconFrame(18, genie::UT_Building, 0), 18);
    QCOMPARE(SpriteLibrary::unitIconFrame(18, genie::UT_Building, 3), 21);
}

void GameDataTest::spriteSourceFollowsInstallLayout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto folderName = [](const QString &path) {
        return QFileInfo(path).fileName().toLower();
    };

    QVERIFY(writeFile(dir.path(), QStringLiteral("data/empires2.dat")));
    QVERIFY(writeFile(dir.path(), QStringLiteral("data/interfac.drs"), QByteArray(8, '\0')));
    const SpriteSource classic =
        locateSpriteSource(QDir(dir.path()).filePath(QStringLiteral("data/empires2.dat")), genie::GV_TC);
    QCOMPARE(classic.looseFolder, QString());
    QCOMPARE(classic.drsFolders.size(), 1);
    QCOMPARE(folderName(classic.drsFolders.at(0)), QStringLiteral("data"));

    QVERIFY(writeFile(dir.path(), QStringLiteral("data2/empires.dat")));
    QVERIFY(writeFile(dir.path(), QStringLiteral("data2/interfac.drs"), QByteArray(8, '\0')));
    const SpriteSource ror =
        locateSpriteSource(QDir(dir.path()).filePath(QStringLiteral("data2/empires.dat")), genie::GV_RoR);
    QCOMPARE(ror.drsFolders.size(), 2);
    QCOMPARE(folderName(ror.drsFolders.at(0)), QStringLiteral("data2"));
    QCOMPARE(folderName(ror.drsFolders.at(1)), QStringLiteral("data"));

    QVERIFY(writeFile(dir.path(), QStringLiteral("resources/_common/dat/empires2_x2_p1.dat")));
    QVERIFY(writeFile(dir.path(), QStringLiteral("resources/_common/drs/interface/50500.bina")));
    const SpriteSource hd = locateSpriteSource(
        QDir(dir.path()).filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat")), genie::GV_Cysion);
    QVERIFY(folderName(hd.looseFolder) == QStringLiteral("drs"));
    QVERIFY(hd.drsFolders.isEmpty());

    // A DE mod's .dat is in the user profile, outside the install, so only
    // the game folder finds the sprites.
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    const QString modDat = QStringLiteral("mods/local/My Mod/resources/_common/dat/empires2_x2_p1.dat");
    QVERIFY(writeFile(profile.path(), modDat));
    const QString modDatPath = QDir(profile.path()).filePath(modDat);
    QVERIFY(locateSpriteSource(modDatPath, genie::GV_C32).isEmpty());
    const SpriteSource mod = locateSpriteSource(modDatPath, genie::GV_C32, dir.path());
    QCOMPARE(mod.looseFolder, hd.looseFolder);

    QVERIFY(writeFile(dir.path(), QStringLiteral("Data/empires.dat")));
    QVERIFY(writeFile(dir.path(), QStringLiteral("Data/DRS/interfac.drs"), QByteArray(8, '\0')));
    const SpriteSource aoeDe =
        locateSpriteSource(QDir(dir.path()).filePath(QStringLiteral("Data/empires.dat")), genie::GV_Tapsa);
    QCOMPARE(aoeDe.drsFolders.size(), 1);
    QCOMPARE(folderName(aoeDe.drsFolders.at(0)), QStringLiteral("drs"));
}

void GameDataTest::missingSpriteFrameIsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dat = QDir(dir.path()).filePath(QStringLiteral("loose.dat"));
    QVERIFY(writeFile(dir.path(), QStringLiteral("loose.dat")));

    SpriteLibrary library;
    library.setSource(dat, genie::GV_TC);
    QVERIFY(library.frame(50729, 0).isNull());
    QVERIFY(library.frame(50729, -1).isNull());

    QVERIFY(writeFile(dir.path(), QStringLiteral("interfac.drs")));
    library.setSource(dat, genie::GV_TC);
    QVERIFY(library.frame(50729, 0).isNull());
}

QTEST_GUILESS_MAIN(GameDataTest)
#include "GameDataTest.moc"
