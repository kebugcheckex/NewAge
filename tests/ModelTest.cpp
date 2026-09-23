#include <algorithm>

#include <QAbstractItemModelTester>
#include <QFile>
#include <QTest>

#include "core/GameInstall.h"
#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "model/FieldTreeModel.h"
#include "model/UnitFields.h"
#include "model/UnitFilterModel.h"
#include "model/UnitListModel.h"

using namespace newage;

namespace {

// The Conquerors sample in the gitignored data/ folder.
const QString kTcDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x1_p1.dat");

// Value of field `name` in `model`, searched across all groups.
QVariant fieldValue(const FieldTreeModel &model, const QString &name)
{
    for (int g = 0; g < model.rowCount(); ++g)
    {
        const QModelIndex group = model.index(g, 0);
        for (int r = 0; r < model.rowCount(group); ++r)
        {
            if (model.index(r, FieldTreeModel::NameColumn, group).data().toString() == name)
                return model.index(r, FieldTreeModel::ValueColumn, group).data(Qt::UserRole);
        }
    }
    return {};
}

// Displayed text of field `name` in `model`, searched across all groups.
QString fieldText(const FieldTreeModel &model, const QString &name)
{
    for (int g = 0; g < model.rowCount(); ++g)
    {
        const QModelIndex group = model.index(g, 0);
        for (int r = 0; r < model.rowCount(group); ++r)
        {
            if (model.index(r, FieldTreeModel::NameColumn, group).data().toString() == name)
                return model.index(r, FieldTreeModel::ValueColumn, group).data().toString();
        }
    }
    return {};
}

} // namespace

class ModelTest : public QObject
{
    Q_OBJECT

private slots:
    void fieldTreeGroupsRowsInFirstSeenOrder();
    void floatsDisplayShortest();
    void unitFieldsSkipSpeedBelowType20();
    void sampleUnitValues();
    void labelsUseLanguageNames();
    void wrongVersionFailsToOpen();

private:
    bool openSample(Session &session);
};

void ModelTest::fieldTreeGroupsRowsInFirstSeenOrder()
{
    FieldTreeModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

    model.setRows({{"a", "G1", 1}, {"b", "G2", 2}, {"c", "G1", 3}});
    QCOMPARE(model.rowCount(), 2);
    const QModelIndex g1 = model.index(0, 0);
    QCOMPARE(g1.data().toString(), QStringLiteral("G1"));
    QCOMPARE(model.rowCount(g1), 2);
    QCOMPARE(model.index(1, FieldTreeModel::NameColumn, g1).data().toString(), QStringLiteral("c"));
    QCOMPARE(model.index(1, FieldTreeModel::ValueColumn, g1).data().toString(), QStringLiteral("3"));
    QCOMPARE(model.parent(model.index(1, 0, g1)), g1);

    model.clear();
    QCOMPARE(model.rowCount(), 0);
}

void ModelTest::floatsDisplayShortest()
{
    QCOMPARE(FieldTreeModel::displayText(QVariant(0.2f)), QStringLiteral("0.2"));
    QCOMPARE(FieldTreeModel::displayText(QVariant(6.0f)), QStringLiteral("6"));
    QCOMPARE(FieldTreeModel::displayText(QVariant(42)), QStringLiteral("42"));
}

void ModelTest::unitFieldsSkipSpeedBelowType20()
{
    FieldTreeModel model;
    genie::Unit unit;
    unit.Type = genie::UT_EyeCandy;
    unit.Speed = 1.5f;
    model.setObject(unitFields(), unit);
    QVERIFY(!fieldValue(model, QStringLiteral("Speed")).isValid());
    QCOMPARE(fieldValue(model, QStringLiteral("Type")).toString(), QStringLiteral("10 - Eye Candy"));

    unit.Type = genie::UT_Creatable;
    model.setObject(unitFields(), unit);
    QCOMPARE(fieldValue(model, QStringLiteral("Speed")).toFloat(), 1.5f);
}

bool ModelTest::openSample(Session &session)
{
    QString error;
    const bool opened = session.open(kTcDat, *findVersionProfile(QStringLiteral("tc")), &error);
    if (!opened)
        qWarning() << error;
    return opened;
}

void ModelTest::sampleUnitValues()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    QVERIFY(openSample(session));

    UnitListModel units(&session);
    QAbstractItemModelTester tester(&units, QAbstractItemModelTester::FailureReportingMode::QtTest);
    units.setCiv(1);
    QCOMPARE(units.rowCount(), static_cast<int>(session.dat()->Civs.at(1).Units.size()));

    // Archer.
    const genie::Unit *archer = units.unit(4);
    QVERIFY(archer);
    QCOMPARE(units.index(4).data().toString(), QStringLiteral("4 - ARCHR"));

    FieldTreeModel fields;
    fields.setObject(unitFields(), *archer);
    QCOMPARE(fieldValue(fields, QStringLiteral("ID")).toInt(), 4);
    QCOMPARE(fieldValue(fields, QStringLiteral("Type")).toString(), QStringLiteral("70 - Combatant"));
    QCOMPARE(fieldValue(fields, QStringLiteral("Hit points")).toInt(), 30);
    QCOMPARE(fieldValue(fields, QStringLiteral("Line of sight")).toFloat(), 6.0f);
    QCOMPARE(fieldValue(fields, QStringLiteral("Speed")).toFloat(), 0.96f);

    // Empty slots are listed but can't be selected.
    const auto &pointers = session.dat()->Civs.at(1).UnitPointers;
    const auto empty = std::find(pointers.begin(), pointers.end(), 0);
    QVERIFY(empty != pointers.end());
    const int emptyRow = static_cast<int>(empty - pointers.begin());
    QVERIFY(!units.unit(emptyRow));
    QVERIFY(!(units.flags(units.index(emptyRow)) & Qt::ItemIsSelectable));
    QCOMPARE(units.index(emptyRow).data(UnitListModel::HasUnitRole).toBool(), false);
    QCOMPARE(units.index(4).data(UnitListModel::HasUnitRole).toBool(), true);

    // The filter can hide them, combined with the text filter.
    UnitFilterModel filter;
    QAbstractItemModelTester filterTester(&filter, QAbstractItemModelTester::FailureReportingMode::QtTest);
    filter.setSourceModel(&units);
    QCOMPARE(filter.rowCount(), units.rowCount());
    QVERIFY(filter.mapFromSource(units.index(emptyRow)).isValid());
    filter.setHideEmpty(true);
    QCOMPARE(filter.rowCount(), static_cast<int>(pointers.size() - std::count(pointers.begin(), pointers.end(), 0)));
    QVERIFY(!filter.mapFromSource(units.index(emptyRow)).isValid());
    QVERIFY(filter.mapFromSource(units.index(4)).isValid());
    filter.setFilterFixedString(QStringLiteral("(empty)"));
    QCOMPARE(filter.rowCount(), 0);
    filter.setHideEmpty(false);
    QVERIFY(filter.rowCount() > 0);

    session.close();
    QCOMPARE(units.rowCount(), 0);
}

void ModelTest::labelsUseLanguageNames()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    // The TC sample with the checked-in strings snippet as its language file.
    Session session;
    QString error;
    const GameDataset dataset{QStringLiteral("sample"), QStringLiteral("tc"), kTcDat,
                              {QStringLiteral(NEWAGE_TEST_DATA_DIR "/key-value-strings-sample.txt")}};
    QVERIFY2(session.open(dataset, &error), qPrintable(error));

    UnitListModel units(&session);
    units.setCiv(1);
    QCOMPARE(units.index(82).data().toString(), QStringLiteral("82 - Castle"));
    QCOMPARE(units.index(4).data().toString(), QStringLiteral("4 - Archer"));
    QCOMPARE(units.index(82).data(Qt::ToolTipRole).toString(), QStringLiteral("CSTL"));
    // No string for this ID in the snippet: falls back to the internal name.
    const genie::Unit *villager = units.unit(83);
    QVERIFY(villager && session.names().text(villager->LanguageDLLName).isEmpty());
    QCOMPARE(units.index(83).data().toString(), QStringLiteral("83 - %1").arg(QString::fromLatin1(villager->Name)));

    // The text filter matches the language name and the internal name.
    UnitFilterModel filter;
    filter.setSourceModel(&units);
    filter.setFilterFixedString(QStringLiteral("cstl"));
    QVERIFY(filter.mapFromSource(units.index(82)).isValid());
    filter.setFilterFixedString(QStringLiteral("castle"));
    QVERIFY(filter.mapFromSource(units.index(82)).isValid());
    QVERIFY(!filter.mapFromSource(units.index(4)).isValid());

    // String ID fields show their text; the raw value is unchanged.
    FieldTreeModel fields;
    fields.setObject(unitFields(), *units.unit(82), &session.names());
    QCOMPARE(fieldValue(fields, QStringLiteral("Language name")).toInt(), 5142);
    QCOMPARE(fieldText(fields, QStringLiteral("Language name")), QStringLiteral("5142 \"Castle\""));
    QCOMPARE(fieldText(fields, QStringLiteral("Language creation")), QStringLiteral("6142 \"Build Castle\""));
    QCOMPARE(fieldText(fields, QStringLiteral("Hit points")), QStringLiteral("4800"));
}

void ModelTest::wrongVersionFailsToOpen()
{
    const QString hdDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x2_p1.dat");
    if (!QFile::exists(hdDat))
        QSKIP("Sample data/empires2_x2_p1.dat not present.");

    // An HD file read as TC parses to no civs instead of throwing.
    Session session;
    QString error;
    QVERIFY(!session.open(hdDat, *findVersionProfile(QStringLiteral("tc")), &error));
    QVERIFY(!session.isOpen());
    QVERIFY(error.contains(QStringLiteral("no civilizations")));
}

QTEST_GUILESS_MAIN(ModelTest)
#include "ModelTest.moc"
