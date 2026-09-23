#include <algorithm>

#include <QAbstractItemModelTester>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "core/GameInstall.h"
#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "model/FieldTreeModel.h"
#include "model/ListFilterModel.h"
#include "model/TechFields.h"
#include "model/TechListModel.h"
#include "model/UnitFields.h"
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
    void techFieldsFollowRequiredTechCount();
    void sampleTechValues();
    void techAvailabilityPerCiv();
    void techLabelsUseLanguageNames();
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
    QCOMPARE(units.index(emptyRow).data(UnitListModel::ActiveRole).toBool(), false);
    QCOMPARE(units.index(4).data(UnitListModel::ActiveRole).toBool(), true);

    // The filter can hide them, combined with the text filter.
    ListFilterModel filter;
    QAbstractItemModelTester filterTester(&filter, QAbstractItemModelTester::FailureReportingMode::QtTest);
    filter.setSourceModel(&units);
    QCOMPARE(filter.rowCount(), units.rowCount());
    QVERIFY(filter.mapFromSource(units.index(emptyRow)).isValid());
    filter.setHideInactive(true);
    QCOMPARE(filter.rowCount(), static_cast<int>(pointers.size() - std::count(pointers.begin(), pointers.end(), 0)));
    QVERIFY(!filter.mapFromSource(units.index(emptyRow)).isValid());
    QVERIFY(filter.mapFromSource(units.index(4)).isValid());
    filter.setFilterFixedString(QStringLiteral("(empty)"));
    QCOMPARE(filter.rowCount(), 0);
    filter.setHideInactive(false);
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
    ListFilterModel filter;
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

void ModelTest::techFieldsFollowRequiredTechCount()
{
    // AoE/RoR techs have 4 required tech slots, later games 6.
    FieldTreeModel model;
    genie::Tech tech;
    tech.setGameVersion(genie::GV_RoR);
    tech.Type = 2;
    model.setObject(techFields(), TechRef{7, tech});
    QCOMPARE(fieldValue(model, QStringLiteral("ID")).toInt(), 7);
    QCOMPARE(fieldValue(model, QStringLiteral("Type")).toString(), QStringLiteral("2 - Age"));
    QVERIFY(fieldValue(model, QStringLiteral("Required tech 4")).isValid());
    QVERIFY(!fieldValue(model, QStringLiteral("Required tech 5")).isValid());

    tech.setGameVersion(genie::GV_TC);
    model.setObject(techFields(), TechRef{7, tech});
    QCOMPARE(fieldValue(model, QStringLiteral("Required tech 6")).toInt(), -1);

    // No research location (possible in DE): the location rows are left out.
    tech.ResearchLocations.clear();
    model.setObject(techFields(), TechRef{7, tech});
    QVERIFY(!fieldValue(model, QStringLiteral("Research time")).isValid());
}

void ModelTest::sampleTechValues()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    QVERIFY(openSample(session));

    TechListModel techs(&session);
    QAbstractItemModelTester tester(&techs, QAbstractItemModelTester::FailureReportingMode::QtTest);
    techs.setCiv(1);
    QCOMPARE(techs.rowCount(), static_cast<int>(session.dat()->Techs.size()));
    // Techs don't depend on the civ: the same rows for every civ.
    techs.setCiv(2);
    QCOMPARE(techs.rowCount(), static_cast<int>(session.dat()->Techs.size()));
    techs.setCiv(1);

    QCOMPARE(techs.index(22).data().toString(), QStringLiteral("22 - Loom"));
    QCOMPARE(techs.index(22).data(Qt::ToolTipRole).toString(), QStringLiteral("Loom"));

    FieldTreeModel fields;
    techs.showFields(22, fields);
    QCOMPARE(fieldValue(fields, QStringLiteral("ID")).toInt(), 22);
    QCOMPARE(fieldValue(fields, QStringLiteral("Internal name")).toString(), QStringLiteral("Loom"));
    QCOMPARE(fieldValue(fields, QStringLiteral("Type")).toString(), QStringLiteral("0 - Regular"));
    QCOMPARE(fieldValue(fields, QStringLiteral("Civ")).toInt(), -1);
    QCOMPARE(fieldValue(fields, QStringLiteral("Effect")).toInt(), 22);
    QCOMPARE(fieldValue(fields, QStringLiteral("Required tech 1")).toInt(), 104);
    QCOMPARE(fieldValue(fields, QStringLiteral("Required tech count")).toInt(), 1);
    QCOMPARE(fieldValue(fields, QStringLiteral("Cost 1 resource")).toInt(), 3);
    QCOMPARE(fieldValue(fields, QStringLiteral("Cost 1 amount")).toInt(), 50);
    QCOMPARE(fieldValue(fields, QStringLiteral("Cost 1 paid")).toInt(), 1);
    QCOMPARE(fieldValue(fields, QStringLiteral("Location")).toInt(), 109);
    QCOMPARE(fieldValue(fields, QStringLiteral("Research time")).toInt(), 25);

    // Feudal Age is an age tech.
    techs.showFields(101, fields);
    QCOMPARE(fieldValue(fields, QStringLiteral("Type")).toString(), QStringLiteral("2 - Age"));

    techs.showFields(-1, fields);
    QCOMPARE(fields.rowCount(), 0);

    session.close();
    QCOMPARE(techs.rowCount(), 0);
    QVERIFY(!techs.tech(22));
}

void ModelTest::techAvailabilityPerCiv()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    QVERIFY(openSample(session));
    const genie::DatFile &dat = *session.dat();
    using Availability = TechListModel::Availability;

    // Tech 3 is the Britons' Yeomen, tech 59 the Japanese Kataparuto, and the
    // Britons' tech tree disables tech 85; Loom (22) is common to all.
    QCOMPARE(dat.Techs.at(3).Civ, 1);
    QCOMPARE(dat.Techs.at(59).Civ, 5);
    QCOMPARE(dat.Techs.at(85).Civ, -1);

    TechListModel techs(&session);
    techs.setCiv(1);
    QCOMPARE(techs.availability(22), Availability::Available);
    QCOMPARE(techs.availability(3), Availability::Available);
    QCOMPARE(techs.availability(59), Availability::OtherCiv);
    QCOMPARE(techs.availability(85), Availability::DisabledByTechTree);
    QCOMPARE(techs.index(3).data(TechListModel::ActiveRole).toBool(), true);
    QCOMPARE(techs.index(59).data(TechListModel::ActiveRole).toBool(), false);
    QCOMPARE(techs.index(59).data(Qt::ToolTipRole).toString(),
             QStringLiteral("Japanese Kataparuto\nOnly for civ 5 - %1").arg(QString::fromLatin1(dat.Civs.at(5).Name)));
    QCOMPARE(techs.index(85).data(Qt::ToolTipRole).toString(),
             QStringLiteral("%1\nDisabled by this civ's tech tree").arg(QString::fromLatin1(dat.Techs.at(85).Name)));
    // Unavailable techs stay selectable: their data can still be looked at.
    QVERIFY(techs.flags(techs.index(59)) & Qt::ItemIsSelectable);

    techs.setCiv(2);
    QCOMPARE(techs.availability(3), Availability::OtherCiv);
    QCOMPARE(techs.availability(85), Availability::Available);

    // The filter can hide them, combined with the text filter.
    techs.setCiv(1);
    ListFilterModel filter;
    QAbstractItemModelTester filterTester(&filter, QAbstractItemModelTester::FailureReportingMode::QtTest);
    filter.setSourceModel(&techs);
    QCOMPARE(filter.rowCount(), techs.rowCount());
    filter.setHideInactive(true);
    int available = 0;
    for (int row = 0; row < techs.rowCount(); ++row)
        available += techs.availability(row) == Availability::Available;
    QCOMPARE(filter.rowCount(), available);
    QVERIFY(!filter.mapFromSource(techs.index(59)).isValid());
    QVERIFY(!filter.mapFromSource(techs.index(85)).isValid());
    QVERIFY(filter.mapFromSource(techs.index(3)).isValid());
    filter.setFilterFixedString(QStringLiteral("kataparuto"));
    QCOMPARE(filter.rowCount(), 0);
    filter.setHideInactive(false);
    QCOMPARE(filter.rowCount(), 1);
}

void ModelTest::techLabelsUseLanguageNames()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    // The TC sample with a small strings file for Yeomen (tech 3).
    QTemporaryDir dir;
    const QString strings = dir.filePath(QStringLiteral("strings.txt"));
    {
        QFile file(strings);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("7419 \"Yeomen\"\n8419 \"Research Yeomen\"\n");
    }
    Session session;
    QString error;
    const GameDataset dataset{QStringLiteral("sample"), QStringLiteral("tc"), kTcDat, {strings}};
    QVERIFY2(session.open(dataset, &error), qPrintable(error));

    TechListModel techs(&session);
    techs.setCiv(1);
    QCOMPARE(techs.index(3).data().toString(), QStringLiteral("3 - Yeomen"));
    QCOMPARE(techs.index(3).data(Qt::ToolTipRole).toString(), QStringLiteral("British Yeoman"));
    // No string for Loom: falls back to the internal name.
    QCOMPARE(techs.index(22).data().toString(), QStringLiteral("22 - Loom"));

    // The text filter matches the language name and the internal name.
    ListFilterModel filter;
    filter.setSourceModel(&techs);
    filter.setFilterFixedString(QStringLiteral("yeomen"));
    QVERIFY(filter.mapFromSource(techs.index(3)).isValid());
    filter.setFilterFixedString(QStringLiteral("british yeoman"));
    QVERIFY(filter.mapFromSource(techs.index(3)).isValid());

    FieldTreeModel fields;
    techs.showFields(3, fields);
    QCOMPARE(fieldText(fields, QStringLiteral("Language name")), QStringLiteral("7419 \"Yeomen\""));
    QCOMPARE(fieldText(fields, QStringLiteral("Language description")),
             QStringLiteral("8419 \"Research Yeomen\""));
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
