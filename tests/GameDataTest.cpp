#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "core/GameInstall.h"
#include "core/NameProvider.h"
#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"

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
    void realInstall();
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
    QVERIFY2(session.open(dataset, &error, &warnings), qPrintable(error));
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

QTEST_GUILESS_MAIN(GameDataTest)
#include "GameDataTest.moc"
