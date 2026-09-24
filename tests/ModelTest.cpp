#include <algorithm>

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
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

// Value column index of field `name` in `model`, searched across all groups.
QModelIndex fieldIndex(const FieldTreeModel &model, const QString &name)
{
    for (int g = 0; g < model.rowCount(); ++g)
    {
        const QModelIndex group = model.index(g, 0);
        for (int r = 0; r < model.rowCount(group); ++r)
        {
            if (model.index(r, FieldTreeModel::NameColumn, group).data().toString() == name)
                return model.index(r, FieldTreeModel::ValueColumn, group);
        }
    }
    return {};
}

// Value of field `name` in `model`.
QVariant fieldValue(const FieldTreeModel &model, const QString &name)
{
    return fieldIndex(model, name).data(FieldTreeModel::ValueRole);
}

// Displayed text of field `name` in `model`.
QString fieldText(const FieldTreeModel &model, const QString &name)
{
    return fieldIndex(model, name).data().toString();
}

// Edits field `name` in `model` as a view would.
bool editField(FieldTreeModel &model, const QString &name, const QVariant &value)
{
    return model.setData(fieldIndex(model, name), value);
}

// Checks that every editable descriptor stores the ends of its range (or a
// fractional value for floats) and reads them back, and that the editable
// ones are exactly `expected`.
template <typename T>
void checkEditableFields(const QList<FieldDesc<T>> &fields, T &object, const QStringList &expected)
{
    QStringList editable;
    for (const FieldDesc<T> &field : fields)
    {
        if (!field.set)
            continue;
        editable << field.name;
        QVERIFY2(!field.applies || field.applies(object), qPrintable(field.name));
        if (field.get(object).typeId() == QMetaType::Float)
        {
            field.set(object, 12.625f);
            QCOMPARE(field.get(object), QVariant(12.625f));
            continue;
        }
        QCOMPARE(field.get(object).typeId(), QMetaType::Int);
        QVERIFY2(field.minimum < field.maximum, qPrintable(field.name));
        for (const int value : {field.minimum, field.maximum, 42})
        {
            field.set(object, value);
            QCOMPARE(field.get(object), QVariant(value));
        }
    }
    QCOMPARE(editable, expected);
}

template <typename T>
const FieldDesc<T> &findField(const QList<FieldDesc<T>> &fields, const QString &name)
{
    return *std::find_if(fields.begin(), fields.end(), [&](const FieldDesc<T> &f) { return f.name == name; });
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
    void editableFieldsRoundTrip();
    void fieldTreeEditing();
    void editAndSaveSample();

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

void ModelTest::editableFieldsRoundTrip()
{
    genie::Unit unit;
    unit.Type = genie::UT_Creatable;
    checkEditableFields(unitFields(), unit,
                        {"Hit points", "Line of sight", "Speed", "Cost 1 resource", "Cost 1 amount", "Cost 1 paid",
                         "Cost 2 resource", "Cost 2 amount", "Cost 2 paid", "Cost 3 resource", "Cost 3 amount",
                         "Cost 3 paid", "Train time"});
    QCOMPARE(unit.HitPoints, int16_t(42));
    QCOMPARE(unit.Creatable.TrainLocations.front().QueueTime, int16_t(42));

    // Ranges follow the member types: int16_t hit points, uint8_t tech "paid".
    QCOMPARE(findField(unitFields(), QStringLiteral("Hit points")).minimum, -32768);
    QCOMPARE(findField(unitFields(), QStringLiteral("Hit points")).maximum, 32767);
    QCOMPARE(findField(techFields(), QStringLiteral("Cost 1 paid")).minimum, 0);
    QCOMPARE(findField(techFields(), QStringLiteral("Cost 1 paid")).maximum, 255);

    // The Creatable fields don't exist below Type 70.
    unit.Type = genie::UT_Bird;
    FieldTreeModel model;
    model.setObject(unitFields(), unit);
    QVERIFY(!fieldIndex(model, QStringLiteral("Cost 1 amount")).isValid());
    QVERIFY(!fieldIndex(model, QStringLiteral("Train time")).isValid());

    genie::Tech tech;
    tech.setGameVersion(genie::GV_TC);
    TechRef ref{0, tech};
    checkEditableFields(techFields(), ref,
                        {"Cost 1 resource", "Cost 1 amount", "Cost 1 paid", "Cost 2 resource", "Cost 2 amount",
                         "Cost 2 paid", "Cost 3 resource", "Cost 3 amount", "Cost 3 paid", "Research time"});
    QCOMPARE(tech.ResearchLocations.front().QueueTime, int16_t(42));
}

void ModelTest::fieldTreeEditing()
{
    FieldTreeModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QList<FieldTreeModel::Row> rows = {
        {"hp", "G", 30, {}, 0, -100, 100},
        {"los", "G", 6.0f, {}, 1},
        {"name", "G", QStringLiteral("ARCHR")},
    };

    // Without a writer nothing is editable.
    model.setRows(rows);
    QVERIFY(!(model.flags(fieldIndex(model, QStringLiteral("hp"))) & Qt::ItemIsEditable));
    QVERIFY(!editField(model, QStringLiteral("hp"), 35));

    QList<std::pair<int, QVariant>> writes;
    bool objectGone = false;
    model.setRows(rows, [&](int field, const QVariant &value) -> QVariant {
        if (objectGone)
            return {};
        writes.append({field, value});
        return value;
    });
    const QModelIndex hp = fieldIndex(model, QStringLiteral("hp"));
    QVERIFY(model.flags(hp) & Qt::ItemIsEditable);
    QVERIFY(!(model.flags(hp.siblingAtColumn(FieldTreeModel::NameColumn)) & Qt::ItemIsEditable));
    QVERIFY(!(model.flags(fieldIndex(model, QStringLiteral("name"))) & Qt::ItemIsEditable));
    QVERIFY(!(model.flags(model.index(0, 0)) & Qt::ItemIsEditable));
    QCOMPARE(hp.data(FieldTreeModel::MinimumRole).toInt(), -100);
    QCOMPARE(hp.data(FieldTreeModel::MaximumRole).toInt(), 100);
    QCOMPARE(hp.data(Qt::EditRole), QVariant(30));
    // Floats edit as their shortest text.
    QCOMPARE(fieldIndex(model, QStringLiteral("los")).data(Qt::EditRole), QVariant(QStringLiteral("6")));

    // Bad input is refused without reaching the writer.
    QVERIFY(!editField(model, QStringLiteral("hp"), QStringLiteral("abc")));
    QVERIFY(!editField(model, QStringLiteral("hp"), QStringLiteral("2.5")));
    QVERIFY(!editField(model, QStringLiteral("hp"), 101));
    QVERIFY(!editField(model, QStringLiteral("los"), QStringLiteral("inf")));
    QVERIFY(!editField(model, QStringLiteral("los"), QString()));
    QVERIFY(!editField(model, QStringLiteral("name"), QStringLiteral("X")));
    // The current value is accepted as a no-op.
    QVERIFY(editField(model, QStringLiteral("hp"), 30));
    QVERIFY(writes.isEmpty());

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QVERIFY(editField(model, QStringLiteral("hp"), QStringLiteral(" -100 ")));
    QVERIFY(editField(model, QStringLiteral("los"), QStringLiteral("0.2")));
    QCOMPARE(writes.size(), 2);
    QCOMPARE(writes.at(0), std::make_pair(0, QVariant(-100)));
    QCOMPARE(writes.at(1), std::make_pair(1, QVariant(0.2f)));
    QCOMPARE(changed.size(), 2);
    QCOMPARE(fieldText(model, QStringLiteral("hp")), QStringLiteral("-100"));
    QCOMPARE(fieldText(model, QStringLiteral("los")), QStringLiteral("0.2"));

    // A failed write leaves the shown value alone.
    objectGone = true;
    QVERIFY(!editField(model, QStringLiteral("hp"), 50));
    QCOMPARE(fieldValue(model, QStringLiteral("hp")).toInt(), -100);
}

void ModelTest::editAndSaveSample()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    // Work on a copy, since saving writes back to the file.
    QTemporaryDir dir;
    const QString dat = dir.filePath(QStringLiteral("edited.dat"));
    QVERIFY(QFile::copy(kTcDat, dat));
    const VersionProfile &tc = *findVersionProfile(QStringLiteral("tc"));
    Session session;
    QString error;
    QVERIFY2(session.open(dat, tc, &error), qPrintable(error));

    // Archer (unit 4) of civ 1: only that civ's copy changes.
    UnitListModel units(&session);
    units.setCiv(1);
    QSignalSpy unitChanged(&units, &QAbstractItemModel::dataChanged);
    FieldTreeModel fields;
    units.showFields(4, fields);
    QVERIFY(!session.isModified());
    QVERIFY(editField(fields, QStringLiteral("Hit points"), 35));
    QVERIFY(session.isModified());
    QCOMPARE(unitChanged.size(), 1);
    QVERIFY(editField(fields, QStringLiteral("Line of sight"), QStringLiteral("7.5")));
    QVERIFY(editField(fields, QStringLiteral("Cost 1 amount"), 99));
    QVERIFY(editField(fields, QStringLiteral("Train time"), 40));
    QCOMPARE(fieldText(fields, QStringLiteral("Hit points")), QStringLiteral("35"));
    const genie::DatFile &data = *session.dat();
    QCOMPARE(data.Civs.at(1).Units.at(4).HitPoints, int16_t(35));
    QCOMPARE(data.Civs.at(2).Units.at(4).HitPoints, int16_t(30));
    // Text and ID fields stay read-only.
    QVERIFY(!editField(fields, QStringLiteral("Internal name"), QStringLiteral("X")));
    QVERIFY(!editField(fields, QStringLiteral("Language name"), 1));

    // Once the list shows another civ, the old field rows no longer write.
    units.setCiv(2);
    QVERIFY(!editField(fields, QStringLiteral("Hit points"), 36));
    QCOMPARE(data.Civs.at(2).Units.at(4).HitPoints, int16_t(30));

    // Loom (tech 22).
    TechListModel techs(&session);
    techs.setCiv(1);
    techs.showFields(22, fields);
    QVERIFY(editField(fields, QStringLiteral("Research time"), 30));
    QVERIFY(editField(fields, QStringLiteral("Cost 1 amount"), 60));

    QVERIFY2(session.save(&error), qPrintable(error));
    QVERIFY(!session.isModified());
    // The temporary file was renamed over the original.
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("edited.dat")});

    Session reopened;
    QVERIFY2(reopened.open(dat, tc, &error), qPrintable(error));
    const genie::DatFile &saved = *reopened.dat();
    const genie::Unit &archer = saved.Civs.at(1).Units.at(4);
    QCOMPARE(archer.HitPoints, int16_t(35));
    QCOMPARE(archer.LineOfSight, 7.5f);
    QCOMPARE(archer.Creatable.ResourceCosts.at(0).Amount, int16_t(99));
    QCOMPARE(archer.Creatable.TrainLocations.front().QueueTime, int16_t(40));
    QCOMPARE(saved.Civs.at(2).Units.at(4).HitPoints, int16_t(30));
    QCOMPARE(saved.Techs.at(22).ResearchLocations.front().QueueTime, int16_t(30));
    QCOMPARE(saved.Techs.at(22).ResourceCosts.at(0).Amount, int16_t(60));

    // A failed save keeps the session's path and modified state.
    techs.showFields(22, fields);
    QVERIFY(editField(fields, QStringLiteral("Research time"), 31));
    QVERIFY(!session.saveAs(dir.filePath(QStringLiteral("missing/edited.dat")), &error));
    QVERIFY(session.isModified());
    QCOMPARE(session.datPath(), dat);
}

QTEST_GUILESS_MAIN(ModelTest)
#include "ModelTest.moc"
