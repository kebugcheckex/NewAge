#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTest>

namespace {

const QString kTcDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x1_p1.dat");

struct Response
{
    int exitCode = -1;
    QJsonObject body;
    QByteArray error;
};

Response run(const QStringList &arguments)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const QString &name : {QStringLiteral("NEWAGE_GAME"), QStringLiteral("NEWAGE_DATASET"),
                                QStringLiteral("NEWAGE_MOD"), QStringLiteral("NEWAGE_MODS_FOLDER")})
        environment.remove(name);
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(NEWAGE_CLI_PATH), arguments);
    if (!process.waitForStarted() || !process.waitForFinished(120000))
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

QTEST_GUILESS_MAIN(CliProcessTest)
#include "CliProcessTest.moc"
