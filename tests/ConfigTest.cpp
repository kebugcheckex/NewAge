#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/Config.h"

using namespace newage;

namespace {

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

class ConfigTest : public QObject
{
    Q_OBJECT

private slots:
    void missingFileGivesDefaults();
    void saveThenLoad();
    void keepsUnknownEntries();
    void wrongTypeReadsAsDefault();
    void badFileFailsWithDefaults();
    void changedOnlyOnRealChange();

private:
    QTemporaryDir dir_;
};

void ConfigTest::missingFileGivesDefaults()
{
    Config config(dir_.filePath(QStringLiteral("missing.json")));
    QString error;
    QVERIFY(config.load(&error));
    QVERIFY(error.isEmpty());
    QCOMPARE(config.hideEmptyUnits(), false);
}

void ConfigTest::saveThenLoad()
{
    // Also checks that save() creates missing directories.
    const QString path = dir_.filePath(QStringLiteral("sub/dir/config.json"));
    Config config(path);
    config.setHideEmptyUnits(true);
    QString error;
    QVERIFY2(config.save(&error), qPrintable(error));
    QCOMPARE(readJson(path)[QStringLiteral("unitList")][QStringLiteral("hideEmpty")].toBool(), true);

    Config reloaded(path);
    QVERIFY2(reloaded.load(&error), qPrintable(error));
    QCOMPARE(reloaded.hideEmptyUnits(), true);
}

void ConfigTest::keepsUnknownEntries()
{
    const QString path = dir_.filePath(QStringLiteral("unknown.json"));
    writeFile(path, R"({"future": {"x": 1}, "unitList": {"hideEmpty": false, "other": "y"}})");

    Config config(path);
    QVERIFY(config.load());
    config.setHideEmptyUnits(true);
    QVERIFY(config.save());

    const QJsonObject saved = readJson(path);
    QCOMPARE(saved[QStringLiteral("future")][QStringLiteral("x")].toInt(), 1);
    QCOMPARE(saved[QStringLiteral("unitList")][QStringLiteral("other")].toString(), QStringLiteral("y"));
    QCOMPARE(saved[QStringLiteral("unitList")][QStringLiteral("hideEmpty")].toBool(), true);
}

void ConfigTest::wrongTypeReadsAsDefault()
{
    const QString path = dir_.filePath(QStringLiteral("types.json"));
    writeFile(path, R"({"unitList": {"hideEmpty": "yes"}})");

    Config config(path);
    QVERIFY(config.load());
    QCOMPARE(config.hideEmptyUnits(), false);
}

void ConfigTest::badFileFailsWithDefaults()
{
    const QString path = dir_.filePath(QStringLiteral("bad.json"));
    Config config(path);
    config.setHideEmptyUnits(true);

    writeFile(path, "{ not json");
    QString error;
    QVERIFY(!config.load(&error));
    QVERIFY(error.contains(QStringLiteral("bad.json")));
    QCOMPARE(config.hideEmptyUnits(), false);

    writeFile(path, "[true]");
    QVERIFY(!config.load(&error));
    QVERIFY(error.contains(QStringLiteral("not a JSON object")));
}

void ConfigTest::changedOnlyOnRealChange()
{
    Config config(dir_.filePath(QStringLiteral("signals.json")));
    QSignalSpy spy(&config, &Config::changed);
    config.setHideEmptyUnits(true);
    QCOMPARE(spy.count(), 1);
    config.setHideEmptyUnits(true);
    QCOMPARE(spy.count(), 1);
    config.setHideEmptyUnits(false);
    QCOMPARE(spy.count(), 2);
}

QTEST_GUILESS_MAIN(ConfigTest)
#include "ConfigTest.moc"
