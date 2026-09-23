#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"

using namespace newage;

class RoundTripTest : public QObject
{
    Q_OBJECT

private slots:
    void versionProfileKeysAreUnique();
    void loadSaveRoundTrip();
};

void RoundTripTest::versionProfileKeysAreUnique()
{
    QSet<QString> keys;
    for (const VersionProfile &profile : versionProfiles())
    {
        QVERIFY2(!keys.contains(profile.key), qPrintable(profile.key));
        keys.insert(profile.key);
        QCOMPARE(findVersionProfile(profile.key), &profile);
    }
    QCOMPARE(findVersionProfile(QStringLiteral("no-such-version")), nullptr);
}

// Loads a real .dat, saves it unchanged, and compares the *decompressed*
// payloads: zlib output can differ byte-wise even when the data is identical.
void RoundTripTest::loadSaveRoundTrip()
{
    const QString datPath = qEnvironmentVariable("NEWAGE_TEST_DAT");
    if (datPath.isEmpty())
        QSKIP("Set NEWAGE_TEST_DAT to a .dat file to run this test.");

    const QString versionKey = qEnvironmentVariable("NEWAGE_TEST_VERSION", QStringLiteral("aoe2de"));
    const VersionProfile *profile = findVersionProfile(versionKey);
    QVERIFY2(profile, qPrintable(QStringLiteral("Unknown NEWAGE_TEST_VERSION: ") + versionKey));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString savedPath = tmp.filePath(QStringLiteral("saved.dat"));

    Session session;
    QString error;
    QVERIFY2(session.open(datPath, *profile, &error), qPrintable(error));
    QVERIFY(!session.dat()->Civs.empty());
    QVERIFY2(session.saveAs(savedPath, &error), qPrintable(error));

    const QString rawOriginal = tmp.filePath(QStringLiteral("original.raw"));
    const QString rawSaved = tmp.filePath(QStringLiteral("saved.raw"));
    genie::DatFile extractor;
    extractor.extractRaw(QFile::encodeName(datPath).constData(), QFile::encodeName(rawOriginal).constData());
    extractor.extractRaw(QFile::encodeName(savedPath).constData(), QFile::encodeName(rawSaved).constData());

    QFile original(rawOriginal);
    QFile saved(rawSaved);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = original.readAll();
    const QByteArray savedBytes = saved.readAll();
    QCOMPARE(savedBytes.size(), originalBytes.size());
    QVERIFY2(savedBytes == originalBytes, "Decompressed payload differs after round trip");
}

QTEST_GUILESS_MAIN(RoundTripTest)
#include "RoundTripTest.moc"
