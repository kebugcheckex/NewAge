#include <algorithm>

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/GameInstall.h"
#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "model/EffectFields.h"
#include "model/EffectListModel.h"
#include "model/EffectNames.h"
#include "model/FieldTreeModel.h"
#include "model/ListFilterModel.h"
#include "model/ResourceNames.h"
#include "model/TechFields.h"
#include "model/TechListModel.h"
#include "model/UnitFields.h"
#include "model/UnitListModel.h"
#include "model/UnitNames.h"

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

// Value column of field `name` under group heading `group`.
QModelIndex fieldInGroup(const FieldTreeModel &model, const QString &group, const QString &name)
{
    for (int g = 0; g < model.rowCount(); ++g)
    {
        const QModelIndex heading = model.index(g, 0);
        if (heading.data().toString() != group)
            continue;
        for (int r = 0; r < model.rowCount(heading); ++r)
        {
            if (model.index(r, FieldTreeModel::NameColumn, heading).data().toString() == name)
                return model.index(r, FieldTreeModel::ValueColumn, heading);
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

// Keys of `fields` in table order.
template <typename T>
QStringList fieldKeys(const QList<FieldDesc<T>> &fields)
{
    QStringList keys;
    for (const FieldDesc<T> &field : fields)
        keys << field.key;
    return keys;
}

// Checks that `keys` are unique snake_case words, optionally after one dotted
// slot prefix ("cost1.amount").
void checkKeyFormat(const QStringList &keys)
{
    static const QRegularExpression format(QStringLiteral("^[a-z][a-z0-9_]*(\\.[a-z][a-z0-9_]*)?$"));
    for (const QString &key : keys)
        QVERIFY2(format.match(key).hasMatch(), qPrintable(key));
    QCOMPARE(QSet<QString>(keys.begin(), keys.end()).size(), keys.size());
}

} // namespace

class ModelTest : public QObject
{
    Q_OBJECT

private slots:
    void fieldTreeGroupsRowsInFirstSeenOrder();
    void floatsDisplayShortest();
    void unitFieldsSkipSpeedBelowType20();
    void unitTrainLocationShowsName();
    void sampleUnitValues();
    void labelsUseLanguageNames();
    void techFieldsFollowRequiredTechCount();
    void techCivAndEffectShowNames();
    void sampleTechValues();
    void techAvailabilityPerCiv();
    void techLabelsUseLanguageNames();
    void wrongVersionFailsToOpen();
    void editableFieldsRoundTrip();
    void fieldKeysAreStable();
    void typeFieldsAreCodes();
    void costFieldsSkipUnusedSlots();
    void resourceNamesPerVersion();
    void fieldTreeEditing();
    void techIconMarkedForPreview();
    void effectFieldsListCommands();
    void effectClassAndAttributeNames();
    void sampleEffectValues();
    void referenceLabelFollowsEdits();
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
    QCOMPARE(fieldValue(model, QStringLiteral("Type")), QVariant(static_cast<int>(genie::UT_EyeCandy)));

    unit.Type = genie::UT_Creatable;
    model.setObject(unitFields(), unit);
    QCOMPARE(fieldValue(model, QStringLiteral("Speed")).toFloat(), 1.5f);
}

void ModelTest::unitTrainLocationShowsName()
{
    genie::Unit unit;
    unit.Type = genie::UT_Creatable;
    unit.Creatable.TrainLocations.front().LocationID = 87;

    FieldTreeModel model;
    model.setObject(unitFields(), unit, nullptr, {}, [](RefKind kind, int id) {
        return kind == RefKind::Unit && id == 87 ? QStringLiteral("Archery Range") : QString();
    });
    const QModelIndex location = fieldIndex(model, QStringLiteral("Train location"));
    QCOMPARE(location.data(FieldTreeModel::RefKindRole).toInt(), static_cast<int>(RefKind::Unit));
    QCOMPARE(location.data().toString(), QStringLiteral("Archery Range (87)"));
    QCOMPARE(fieldValue(model, QStringLiteral("Train location")).toInt(), 87);

    // -1 is not a unit, so the number is left as-is.
    unit.Creatable.TrainLocations.front().LocationID = -1;
    model.setObject(unitFields(), unit, nullptr, {}, [](RefKind kind, int id) {
        return kind == RefKind::Unit && id == 87 ? QStringLiteral("Archery Range") : QString();
    });
    QCOMPARE(fieldText(model, QStringLiteral("Train location")), QStringLiteral("-1"));
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
    QCOMPARE(fieldValue(fields, QStringLiteral("Type")).toInt(), static_cast<int>(genie::UT_Creatable));
    QCOMPARE(fieldValue(fields, QStringLiteral("Hit points")).toInt(), 30);
    QCOMPARE(fieldValue(fields, QStringLiteral("Line of sight")).toFloat(), 6.0f);
    QCOMPARE(fieldValue(fields, QStringLiteral("Speed")).toFloat(), 0.96f);
    // The list model labels resource costs: 25 wood, 45 gold.
    units.showFields(4, fields);
    QCOMPARE(fieldText(fields, QStringLiteral("Type")), QStringLiteral("70 - Combatant"));
    const QString className = unitClassName(session.gameVersion(), archer->Class);
    QVERIFY(!className.isEmpty());
    QCOMPARE(fieldText(fields, QStringLiteral("Class")), QStringLiteral("%1 (%2)").arg(className).arg(archer->Class));
    QCOMPARE(fieldText(fields, QStringLiteral("Cost 1 resource")), QStringLiteral("Wood Storage (1)"));
    QCOMPARE(fieldText(fields, QStringLiteral("Cost 2 resource")), QStringLiteral("Gold Storage (3)"));
    const int trainAt = archer->Creatable.TrainLocations.front().LocationID;
    QCOMPARE(fieldValue(fields, QStringLiteral("Train location")).toInt(), trainAt);
    const genie::Unit *building = units.unit(trainAt);
    QVERIFY(building);
    QString trainName = session.names().text(building->LanguageDLLName);
    if (trainName.isEmpty())
        trainName = QString::fromLatin1(building->Name);
    QVERIFY(!trainName.isEmpty());
    QCOMPARE(fieldText(fields, QStringLiteral("Train location")), QStringLiteral("%1 (%2)").arg(trainName).arg(trainAt));
    QVERIFY(!fieldIndex(fields, QStringLiteral("Cost 3 resource")).isValid());

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
    QCOMPARE(fieldValue(model, QStringLiteral("Type")), QVariant(2));
    // Unused slots (-1) are left out.
    QVERIFY(!fieldValue(model, QStringLiteral("Required tech 1")).isValid());
    std::fill(tech.RequiredTechs.begin(), tech.RequiredTechs.end(), 3);
    model.setObject(techFields(), TechRef{7, tech});
    QCOMPARE(fieldValue(model, QStringLiteral("Required tech 4")).toInt(), 3);
    QVERIFY(!fieldValue(model, QStringLiteral("Required tech 5")).isValid());
    // Without a namer, references show the plain ID.
    QCOMPARE(fieldText(model, QStringLiteral("Required tech 4")), QStringLiteral("3"));

    tech.setGameVersion(genie::GV_TC);
    std::fill(tech.RequiredTechs.begin(), tech.RequiredTechs.end(), 3);
    tech.RequiredTechs.at(1) = -1;
    model.setObject(techFields(), TechRef{7, tech});
    QCOMPARE(fieldValue(model, QStringLiteral("Required tech 6")).toInt(), 3);
    QVERIFY(!fieldValue(model, QStringLiteral("Required tech 2")).isValid());

    // No research location (possible in DE): the location rows are left out.
    tech.ResearchLocations.clear();
    model.setObject(techFields(), TechRef{7, tech});
    QVERIFY(!fieldValue(model, QStringLiteral("Research time")).isValid());
}

void ModelTest::techCivAndEffectShowNames()
{
    FieldTreeModel model;
    genie::Tech tech;
    tech.Civ = 1;
    tech.EffectID = 22;
    const FieldTreeModel::RefNamer namer = [](RefKind kind, int id) {
        switch (kind)
        {
        case RefKind::Civ: return id == 1 ? QStringLiteral("Briton") : QString();
        case RefKind::Effect: return id == 22 ? QStringLiteral("Loom") : QString();
        case RefKind::Unit: return id == 109 ? QStringLiteral("Town Center") : QString();
        default: return QString();
        }
    };
    tech.ResearchLocations.front().LocationID = 109;
    model.setObject(techFields(), TechRef{7, tech}, nullptr, {}, namer);
    QCOMPARE(fieldValue(model, QStringLiteral("Civ")).toInt(), 1);
    QCOMPARE(fieldText(model, QStringLiteral("Civ")), QStringLiteral("Briton (1)"));
    QCOMPARE(fieldValue(model, QStringLiteral("Effect")).toInt(), 22);
    QCOMPARE(fieldText(model, QStringLiteral("Effect")), QStringLiteral("Loom (22)"));
    const QModelIndex effect = fieldIndex(model, QStringLiteral("Effect"));
    QCOMPARE(effect.data(FieldTreeModel::RefKindRole).toInt(), static_cast<int>(RefKind::Effect));
    QCOMPARE(effect.siblingAtColumn(FieldTreeModel::NameColumn).data(FieldTreeModel::RefKindRole).toInt(),
             static_cast<int>(RefKind::Effect));
    QCOMPARE(effect.data(Qt::ToolTipRole).toString(), QStringLiteral("Double-click to show"));
    QCOMPARE(fieldValue(model, QStringLiteral("Location")).toInt(), 109);
    QCOMPARE(fieldText(model, QStringLiteral("Location")), QStringLiteral("Town Center (109)"));

    // -1 is not an entity, so the number is left as-is.
    tech.Civ = -1;
    tech.EffectID = -1;
    tech.ResearchLocations.front().LocationID = -1;
    model.setObject(techFields(), TechRef{7, tech}, nullptr, {}, namer);
    QCOMPARE(fieldText(model, QStringLiteral("Civ")), QStringLiteral("-1"));
    QCOMPARE(fieldText(model, QStringLiteral("Effect")), QStringLiteral("-1"));
    QCOMPARE(fieldIndex(model, QStringLiteral("Effect")).data(Qt::ToolTipRole).toString(), QString());
    QCOMPARE(fieldText(model, QStringLiteral("Location")), QStringLiteral("-1"));
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
    QCOMPARE(fieldValue(fields, QStringLiteral("Type")), QVariant(0));
    QCOMPARE(fieldText(fields, QStringLiteral("Type")), QStringLiteral("0 - Regular"));
    QCOMPARE(fieldValue(fields, QStringLiteral("Civ")).toInt(), -1);
    QCOMPARE(fieldText(fields, QStringLiteral("Civ")), QStringLiteral("-1"));
    QCOMPARE(fieldValue(fields, QStringLiteral("Effect")).toInt(), 22);
    const QString effectName = QString::fromLatin1(session.dat()->Effects.at(22).Name);
    QVERIFY(!effectName.isEmpty());
    QCOMPARE(fieldText(fields, QStringLiteral("Effect")), QStringLiteral("%1 (22)").arg(effectName));
    QCOMPARE(fieldValue(fields, QStringLiteral("Required tech 1")).toInt(), 104);
    QCOMPARE(fieldText(fields, QStringLiteral("Required tech 1")), QStringLiteral("Dark Age (104)"));
    QVERIFY(!fieldValue(fields, QStringLiteral("Required tech 2")).isValid());
    QCOMPARE(fieldValue(fields, QStringLiteral("Required tech count")).toInt(), 1);
    QCOMPARE(fieldValue(fields, QStringLiteral("Cost 1 resource")).toInt(), 3);
    QCOMPARE(fieldText(fields, QStringLiteral("Cost 1 resource")), QStringLiteral("Gold Storage (3)"));
    QVERIFY(!fieldValue(fields, QStringLiteral("Cost 2 resource")).isValid());
    QCOMPARE(fieldValue(fields, QStringLiteral("Cost 1 amount")).toInt(), 50);
    QCOMPARE(fieldValue(fields, QStringLiteral("Cost 1 paid")).toInt(), 1);
    QCOMPARE(fieldValue(fields, QStringLiteral("Location")).toInt(), 109);
    const genie::Unit &building = session.dat()->Civs.at(1).Units.at(109);
    QString locationName = session.names().text(building.LanguageDLLName);
    if (locationName.isEmpty())
        locationName = QString::fromLatin1(building.Name);
    QVERIFY(!locationName.isEmpty());
    QCOMPARE(fieldText(fields, QStringLiteral("Location")), QStringLiteral("%1 (109)").arg(locationName));
    QCOMPARE(fieldValue(fields, QStringLiteral("Research time")).toInt(), 25);

    // Yeomen is the Britons' unique tech.
    techs.showFields(3, fields);
    QCOMPARE(fieldValue(fields, QStringLiteral("Civ")).toInt(), 1);
    const QString civName = QString::fromLatin1(session.dat()->Civs.at(1).Name);
    QVERIFY(!civName.isEmpty());
    QCOMPARE(fieldText(fields, QStringLiteral("Civ")), QStringLiteral("%1 (1)").arg(civName));

    // Feudal Age is an age tech.
    techs.showFields(101, fields);
    QCOMPARE(fieldText(fields, QStringLiteral("Type")), QStringLiteral("2 - Age"));

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
    // Unused cost slots (resource -1) are left out, so fill them all.
    for (auto &cost : unit.Creatable.ResourceCosts)
        cost.Type = 0;
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
    for (auto &cost : tech.ResourceCosts)
        cost.Type = 0;
    // Unused requirement slots (-1) are left out, so fill them all.
    std::fill(tech.RequiredTechs.begin(), tech.RequiredTechs.end(), int16_t(0));
    TechRef ref{0, tech};
    checkEditableFields(techFields(), ref,
                         {"Required tech 1", "Required tech 2", "Required tech 3", "Required tech 4", "Required tech 5",
                          "Required tech 6", "Cost 1 resource", "Cost 1 amount", "Cost 1 paid", "Cost 2 resource",
                          "Cost 2 amount", "Cost 2 paid", "Cost 3 resource", "Cost 3 amount", "Cost 3 paid",
                          "Location", "Research time"});
    QCOMPARE(tech.RequiredTechs.at(0), int16_t(42));
    QCOMPARE(tech.ResearchLocations.front().QueueTime, int16_t(42));

    FieldTreeModel edited;
    edited.setObject(
        techFields(), ref, nullptr,
        [&](int field, const QVariant &value) {
            const FieldDesc<TechRef> &desc = techFields().at(field);
            desc.set(ref, value);
            return desc.get(ref);
        },
        [](RefKind kind, int id) {
            if (kind == RefKind::Tech && id == 5)
                return QStringLiteral("Wheelbarrow");
            if (kind == RefKind::Unit && id == 109)
                return QStringLiteral("Town Center");
            return QString();
        });
    QVERIFY(edited.flags(fieldIndex(edited, QStringLiteral("Required tech 1"))) & Qt::ItemIsEditable);
    QVERIFY(!(edited.flags(fieldIndex(edited, QStringLiteral("Required tech count"))) & Qt::ItemIsEditable));
    QVERIFY(editField(edited, QStringLiteral("Required tech 1"), 5));
    QCOMPARE(tech.RequiredTechs.at(0), int16_t(5));
    QCOMPARE(fieldText(edited, QStringLiteral("Required tech 1")), QStringLiteral("Wheelbarrow (5)"));
    QVERIFY(edited.flags(fieldIndex(edited, QStringLiteral("Location"))) & Qt::ItemIsEditable);
    QVERIFY(editField(edited, QStringLiteral("Location"), 109));
    QCOMPARE(tech.ResearchLocations.front().LocationID, int16_t(109));
    QCOMPARE(fieldText(edited, QStringLiteral("Location")), QStringLiteral("Town Center (109)"));
}

// Keys are what scripts and the CLI use, so renaming one should be a
// deliberate change to this list.
void ModelTest::fieldKeysAreStable()
{
    const QStringList unitKeys = fieldKeys(unitFields());
    checkKeyFormat(unitKeys);
    QCOMPARE(unitKeys,
             QStringList({"id",
                          "type",
                          "class",
                          "internal_name",
                          "language_name",
                          "language_creation",
                          "hit_points",
                          "line_of_sight",
                          "speed",
                          "garrison_capacity",
                          "resource_capacity",
                          "cost1.resource",
                          "cost1.amount",
                          "cost1.paid",
                          "cost2.resource",
                          "cost2.amount",
                          "cost2.paid",
                          "cost3.resource",
                          "cost3.amount",
                          "cost3.paid",
                          "train_location",
                          "train_time",
                          "collision_size_x",
                          "collision_size_y",
                          "collision_size_z",
                          "standing_graphic1",
                          "standing_graphic2",
                          "dying_graphic",
                          "icon",
                          "enabled",
                          "hide_in_editor"}));

    const QStringList techKeys = fieldKeys(techFields());
    checkKeyFormat(techKeys);
    QCOMPARE(techKeys,
             QStringList({"id",
                          "internal_name",
                          "language_name",
                          "language_description",
                          "type",
                          "civ",
                          "effect",
                          "icon",
                          "full_tech_mode",
                          "required_tech1",
                          "required_tech2",
                          "required_tech3",
                          "required_tech4",
                          "required_tech5",
                          "required_tech6",
                          "required_tech_count",
                          "cost1.resource",
                          "cost1.amount",
                          "cost1.paid",
                          "cost2.resource",
                          "cost2.amount",
                          "cost2.paid",
                          "cost3.resource",
                          "cost3.amount",
                          "cost3.paid",
                          "research_location",
                          "research_time",
                          "button"}));

    // Effect keys depend on each command's type; every type must still give
    // unique keys within its command.
    genie::Effect effect;
    for (const int type : {0, 1, 2, 3, 4, 5, 6, 7, 8, 101, 102, 103, 99})
    {
        genie::EffectCommand command;
        command.Type = static_cast<uint8_t>(type);
        effect.EffectCommands.push_back(command);
    }
    for (const genie::GameVersion version : {genie::GV_AoE, genie::GV_TC, genie::GV_C2})
        checkKeyFormat(fieldKeys(effectFields(effect, version)));

    effect.EffectCommands.resize(2);
    effect.EffectCommands.at(1).Type = 1;
    genie::EffectCommand disable;
    disable.Type = 102;
    genie::EffectCommand unknown;
    unknown.Type = 99;
    effect.EffectCommands.push_back(disable);
    effect.EffectCommands.push_back(unknown);
    QCOMPARE(fieldKeys(effectFields(effect, genie::GV_C2)),
             QStringList({"id",
                          "internal_name",
                          "command_count",
                          "command1.type",
                          "command1.unit",
                          "command1.class",
                          "command1.attribute",
                          "command1.amount",
                          "command2.type",
                          "command2.resource",
                          "command2.mode",
                          "command2.multiply_resource",
                          "command2.amount",
                          "command3.type",
                          "command3.tech",
                          "command4.type",
                          "command4.a",
                          "command4.b",
                          "command4.c",
                          "command4.d"}));
}

// Unit and tech Type are stored numbers with a label, shown as AGE does.
void ModelTest::typeFieldsAreCodes()
{
    const FieldTreeModel::RefNamer namer = [](RefKind kind, int id) {
        if (kind == RefKind::UnitType)
            return unitTypeName(id);
        if (kind == RefKind::TechType)
            return techTypeName(id);
        return QString();
    };

    FieldTreeModel model;
    genie::Unit unit;
    unit.Type = genie::UT_Creatable;
    model.setObject(unitFields(), unit, nullptr, {}, namer);
    QModelIndex type = fieldIndex(model, QStringLiteral("Type"));
    QCOMPARE(type.data(FieldTreeModel::ValueRole), QVariant(70));
    QCOMPARE(type.data(FieldTreeModel::RefKindRole).toInt(), static_cast<int>(RefKind::UnitType));
    QCOMPARE(type.data().toString(), QStringLiteral("70 - Combatant"));

    unit.Type = 99;
    model.setObject(unitFields(), unit, nullptr, {}, namer);
    QCOMPARE(fieldText(model, QStringLiteral("Type")), QStringLiteral("99 - Unknown"));

    genie::Tech tech;
    tech.Type = 2;
    model.setObject(techFields(), TechRef{7, tech}, nullptr, {}, namer);
    type = fieldIndex(model, QStringLiteral("Type"));
    QCOMPARE(type.data(FieldTreeModel::ValueRole), QVariant(2));
    QCOMPARE(type.data(FieldTreeModel::RefKindRole).toInt(), static_cast<int>(RefKind::TechType));
    QCOMPARE(type.data().toString(), QStringLiteral("2 - Age"));
    QCOMPARE(techTypeName(0), QStringLiteral("Regular"));
    QCOMPARE(techTypeName(5), QStringLiteral("Unknown"));
}

void ModelTest::costFieldsSkipUnusedSlots()
{
    genie::Unit unit;
    unit.Type = genie::UT_Creatable;
    unit.Creatable.ResourceCosts.at(0).Type = 0;
    unit.Creatable.ResourceCosts.at(2).Type = 3;
    FieldTreeModel model;
    model.setObject(unitFields(), unit);
    QVERIFY(fieldIndex(model, QStringLiteral("Cost 1 paid")).isValid());
    QVERIFY(!fieldIndex(model, QStringLiteral("Cost 2 resource")).isValid());
    QVERIFY(!fieldIndex(model, QStringLiteral("Cost 2 amount")).isValid());
    QVERIFY(!fieldIndex(model, QStringLiteral("Cost 2 paid")).isValid());
    QCOMPARE(fieldValue(model, QStringLiteral("Cost 3 resource")).toInt(), 3);

    genie::Tech tech;
    tech.setGameVersion(genie::GV_TC);
    tech.ResourceCosts.at(1).Type = 1;
    model.setObject(techFields(), TechRef{0, tech});
    QVERIFY(!fieldIndex(model, QStringLiteral("Cost 1 amount")).isValid());
    QCOMPARE(fieldValue(model, QStringLiteral("Cost 2 resource")).toInt(), 1);
    QVERIFY(!fieldIndex(model, QStringLiteral("Cost 3 amount")).isValid());
}

void ModelTest::resourceNamesPerVersion()
{
    QCOMPARE(resourceName(genie::GV_TC, 0), QStringLiteral("Food Storage"));
    QCOMPARE(resourceName(genie::GV_TC, 3), QStringLiteral("Gold Storage"));
    // AoE and AoK name some resources differently.
    QCOMPARE(resourceName(genie::GV_RoR, 7), QStringLiteral("Artifacts Captured"));
    QCOMPARE(resourceName(genie::GV_TC, 7), QStringLiteral("Relics Captured"));
    // AoK ends at 188 (Gold Score), The Conquerors at 197.
    QCOMPARE(resourceNames(genie::GV_AoK).size(), 189);
    QCOMPARE(resourceNames(genie::GV_TC).size(), 198);
    QCOMPARE(resourceName(genie::GV_TC, 198), QString());
    QCOMPARE(resourceName(genie::GV_TC, -1), QString());
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

void ModelTest::referenceLabelFollowsEdits()
{
    FieldTreeModel model;
    const FieldTreeModel::RefNamer namer = [](RefKind kind, int id) {
        return kind == RefKind::Resource ? resourceName(genie::GV_TC, id) : QString();
    };
    FieldTreeModel::Row row{"res", "G", 3, {}, 0, -32768, 32767};
    row.ref = RefKind::Resource;
    row.label = namer(row.ref, 3);
    model.setRows({row}, [](int, const QVariant &value) { return value; }, namer);
    QCOMPARE(fieldText(model, QStringLiteral("res")), QStringLiteral("Gold Storage (3)"));
    QCOMPARE(fieldIndex(model, QStringLiteral("res")).data(Qt::EditRole), QVariant(3));

    QVERIFY(editField(model, QStringLiteral("res"), 0));
    QCOMPARE(fieldText(model, QStringLiteral("res")), QStringLiteral("Food Storage (0)"));
    // An ID without a name shows as the plain number.
    QVERIFY(editField(model, QStringLiteral("res"), 9999));
    QCOMPARE(fieldText(model, QStringLiteral("res")), QStringLiteral("9999"));
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
    QVERIFY(editField(fields, QStringLiteral("Required tech 1"), 101));
    QCOMPARE(fieldText(fields, QStringLiteral("Required tech 1")), QStringLiteral("%1 (101)").arg(techs.name(101)));
    QCOMPARE(session.dat()->Techs.at(22).RequiredTechs.at(0), int16_t(101));
    int otherBuilding = -1;
    {
        const genie::Civ &playable = session.dat()->Civs.at(1);
        for (int id = 0; id < static_cast<int>(playable.Units.size()); ++id)
        {
            if (id >= static_cast<int>(playable.UnitPointers.size()) || playable.UnitPointers[id] == 0)
                continue;
            if (playable.Units[id].Type == genie::UT_Building && id != 109)
            {
                otherBuilding = id;
                break;
            }
        }
    }
    QVERIFY(otherBuilding >= 0);
    QVERIFY(editField(fields, QStringLiteral("Location"), otherBuilding));
    QCOMPARE(session.dat()->Techs.at(22).ResearchLocations.front().LocationID, int16_t(otherBuilding));
    const QString buildingName = unitName(session, 1, otherBuilding);
    QVERIFY(!buildingName.isEmpty());
    QCOMPARE(fieldText(fields, QStringLiteral("Location")),
             QStringLiteral("%1 (%2)").arg(buildingName).arg(otherBuilding));

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
    QCOMPARE(saved.Techs.at(22).ResearchLocations.front().LocationID, int16_t(otherBuilding));
    QCOMPARE(saved.Techs.at(22).ResourceCosts.at(0).Amount, int16_t(60));
    QCOMPARE(saved.Techs.at(22).RequiredTechs.at(0), int16_t(101));

    // A failed save keeps the session's path and modified state.
    techs.showFields(22, fields);
    QVERIFY(editField(fields, QStringLiteral("Research time"), 31));
    QVERIFY(!session.saveAs(dir.filePath(QStringLiteral("missing/edited.dat")), &error));
    QVERIFY(session.isModified());
    QCOMPARE(session.datPath(), dat);
}

void ModelTest::effectFieldsListCommands()
{
    QCOMPARE(effectTypeName(genie::GV_TC, 102), QStringLiteral("102 - Disable Tech"));
    QCOMPARE(effectTypeName(genie::GV_TC, 2), QStringLiteral("2 - Enable/Disable Unit"));
    QCOMPARE(effectTypeName(genie::GV_AoE, 6), QStringLiteral("6 - Unknown"));
    QCOMPARE(effectTypeName(genie::GV_AoK, 6), QStringLiteral("6 - Resource Modifier (Multiply)"));
    QCOMPARE(effectTypeName(genie::GV_Tapsa, 101), QStringLiteral("101 - Tech Cost Modifier (Set/+/-)"));
    QCOMPARE(effectTypeName(genie::GV_TC, 10), QStringLiteral("10 - Unknown"));
    QCOMPARE(effectTypeName(genie::GV_C2, 10), QStringLiteral("10 - Team Attribute Modifier (Set)"));
    QCOMPARE(effectTypeName(genie::GV_SWGB, 10), QStringLiteral("10 - Unknown"));
    QCOMPARE(effectTypeName(genie::GV_TC, 7), QStringLiteral("7 - Unknown"));
    QCOMPARE(effectTypeName(genie::GV_C2, 7), QStringLiteral("7 - Spawn Unit"));
    QCOMPARE(effectTypeName(genie::GV_TC, 99), QStringLiteral("99 - Unknown"));

    genie::Effect effect;
    effect.Name = "Loom";
    genie::EffectCommand disable;
    disable.Type = 102;
    disable.A = 1;
    disable.D = 22;
    genie::EffectCommand unit;
    unit.Type = 2;
    unit.A = 4;
    unit.B = 0;
    unit.C = -1;
    genie::EffectCommand unknown;
    unknown.Type = 99;
    unknown.A = 1;
    unknown.B = 2;
    unknown.C = 3;
    unknown.D = 4.5f;
    effect.EffectCommands = {disable, unit, unknown};

    const FieldTreeModel::RefNamer namer = [](RefKind kind, int id) {
        if (kind == RefKind::Tech && id == 22)
            return QStringLiteral("Loom");
        if (kind == RefKind::Unit && id == 4)
            return QStringLiteral("Archer");
        return QString();
    };
    FieldTreeModel model;
    model.setObject(effectFields(effect, genie::GV_TC), EffectRef{7, effect}, nullptr, {}, namer);
    QCOMPARE(fieldValue(model, QStringLiteral("ID")).toInt(), 7);
    QCOMPARE(fieldValue(model, QStringLiteral("Internal name")).toString(), QStringLiteral("Loom"));
    QCOMPARE(fieldValue(model, QStringLiteral("Command count")).toInt(), 3);

    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Type")).data().toString(),
             QStringLiteral("102 - Disable Tech"));
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Tech")).data().toString(),
             QStringLiteral("Loom (22)"));
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Tech")).data(FieldTreeModel::RefKindRole).toInt(),
             static_cast<int>(RefKind::Tech));
    QVERIFY(!fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("A")).isValid());

    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 2"), QStringLiteral("Unit")).data().toString(),
             QStringLiteral("Archer (4)"));
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 2"), QStringLiteral("Mode")).data().toString(),
             QStringLiteral("0 - Disable"));
    QVERIFY(!fieldInGroup(model, QStringLiteral("Command 2"), QStringLiteral("C")).isValid());

    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 3"), QStringLiteral("Type")).data().toString(),
             QStringLiteral("99 - Unknown"));
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 3"), QStringLiteral("A")).data().toInt(), 1);
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 3"), QStringLiteral("D")).data().toFloat(), 4.5f);

    genie::Effect resource;
    genie::EffectCommand cost;
    cost.Type = 1;
    cost.A = 3;
    cost.B = 0;
    cost.C = 1;
    cost.D = 50;
    resource.EffectCommands = {cost};
    model.setObject(effectFields(resource, genie::GV_TC), EffectRef{1, resource});
    QVERIFY(!fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Multiply resource")).isValid());
    model.setObject(effectFields(resource, genie::GV_C2), EffectRef{1, resource});
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Multiply resource")).data().toInt(), 1);
    cost.C = -1;
    resource.EffectCommands = {cost};
    model.setObject(effectFields(resource, genie::GV_C2), EffectRef{1, resource});
    QVERIFY(!fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Multiply resource")).isValid());

    effect.EffectCommands.clear();
    effect.Name.clear();
    model.setObject(effectFields(effect, genie::GV_TC), EffectRef{0, effect});
    QCOMPARE(fieldValue(model, QStringLiteral("Command count")).toInt(), 0);
    QCOMPARE(fieldValue(model, QStringLiteral("Internal name")).toString(), QString());
    QCOMPARE(model.rowCount(), 1);

    const auto names = [](genie::GameVersion version) {
        return [version](RefKind kind, int id) {
            switch (kind)
            {
            case RefKind::Unit: return id == 4 ? QStringLiteral("Archer") : QString();
            case RefKind::UnitClass: return unitClassName(version, id);
            case RefKind::Attribute: return effectAttributeName(version, id);
            case RefKind::Tech: return id == 22 ? QStringLiteral("Loom") : QString();
            default: return QString();
            }
        };
    };

    genie::Effect modifier;
    genie::EffectCommand byClass;
    byClass.Type = 0;
    byClass.A = -1;
    byClass.B = 6;
    byClass.C = 0;
    byClass.D = -1;
    genie::EffectCommand byUnit;
    byUnit.Type = 4;
    byUnit.A = 4;
    byUnit.B = -1;
    byUnit.C = 99;
    byUnit.D = 1.5f;
    genie::EffectCommand blank;
    blank.Type = 99;
    blank.A = -1;
    blank.B = -1;
    blank.C = -1;
    blank.D = 0;
    modifier.EffectCommands = {byClass, byUnit, blank};
    model.setObject(effectFields(modifier, genie::GV_TC), EffectRef{2, modifier}, nullptr, {}, names(genie::GV_TC));

    QVERIFY(!fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Unit")).isValid());
    const QModelIndex unitClass = fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Class"));
    QCOMPARE(unitClass.data().toString(), QStringLiteral("Infantry (6)"));
    QCOMPARE(unitClass.data(FieldTreeModel::ValueRole).toInt(), 6);
    QCOMPARE(unitClass.data(FieldTreeModel::RefKindRole).toInt(), static_cast<int>(RefKind::UnitClass));
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Attribute")).data().toString(),
             QStringLiteral("Hit Points (0)"));
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Amount")).data().toString(),
             QStringLiteral("-1"));

    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 2"), QStringLiteral("Unit")).data().toString(),
             QStringLiteral("Archer (4)"));
    QVERIFY(!fieldInGroup(model, QStringLiteral("Command 2"), QStringLiteral("Class")).isValid());
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 2"), QStringLiteral("Attribute")).data().toString(),
             QStringLiteral("99"));

    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 3"), QStringLiteral("Type")).data().toString(),
             QStringLiteral("99 - Unknown"));
    QVERIFY(!fieldInGroup(model, QStringLiteral("Command 3"), QStringLiteral("A")).isValid());
    QVERIFY(!fieldInGroup(model, QStringLiteral("Command 3"), QStringLiteral("C")).isValid());
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 3"), QStringLiteral("D")).data().toFloat(), 0.0f);

    genie::Effect upgrade;
    genie::EffectCommand toUnit;
    toUnit.Type = 3;
    toUnit.A = 4;
    toUnit.B = -1;
    toUnit.C = -1;
    upgrade.EffectCommands = {toUnit};
    model.setObject(effectFields(upgrade, genie::GV_C2), EffectRef{3, upgrade}, nullptr, {}, names(genie::GV_C2));
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Mode")).data().toString(),
             QStringLiteral("-1 - All"));
    QVERIFY(!fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("To unit")).isValid());

    genie::Effect techMod;
    genie::EffectCommand action;
    action.Type = 8;
    action.A = 22;
    action.B = -1;
    action.D = 5;
    techMod.EffectCommands = {action};
    model.setObject(effectFields(techMod, genie::GV_C2), EffectRef{4, techMod}, nullptr, {}, names(genie::GV_C2));
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Action")).data().toInt(), -1);
    QCOMPARE(fieldInGroup(model, QStringLiteral("Command 1"), QStringLiteral("Tech")).data().toString(),
             QStringLiteral("Loom (22)"));
}

void ModelTest::effectClassAndAttributeNames()
{
    QCOMPARE(unitClassName(genie::GV_AoE, 18), QStringLiteral("Priest"));
    QCOMPARE(unitClassName(genie::GV_Tapsa, 39), QStringLiteral("Slinger"));
    QCOMPARE(unitClassName(genie::GV_AoE, 40), QString());
    QCOMPARE(unitClassName(genie::GV_TC, 6), QStringLiteral("Infantry"));
    QCOMPARE(unitClassName(genie::GV_TC, 18), QStringLiteral("Monk"));
    QCOMPARE(unitClassName(genie::GV_TC, 23), QStringLiteral("Conquistador"));
    QCOMPARE(unitClassName(genie::GV_TC, 35), QStringLiteral("Petard"));
    QCOMPARE(unitClassName(genie::GV_C2, 39), QStringLiteral("Gate"));
    QCOMPARE(unitClassName(genie::GV_TC, 61), QStringLiteral("Controlled Animal"));
    QCOMPARE(unitClassName(genie::GV_TC, 62), QString());
    QCOMPARE(unitClassName(genie::GV_TC, -1), QString());
    QCOMPARE(unitClassName(genie::GV_SWGB, 1), QStringLiteral("Nerf/Bantha"));
    QCOMPARE(unitClassName(genie::GV_CC, 58), QStringLiteral("Workers"));

    QCOMPARE(effectAttributeName(genie::GV_AoE, 8), QStringLiteral("Armor (no multiply)"));
    QCOMPARE(effectAttributeName(genie::GV_AoE, 101), QString());
    QCOMPARE(effectAttributeName(genie::GV_RoR, 101), QStringLiteral("Population (set only)"));
    QCOMPARE(effectAttributeName(genie::GV_Tapsa, 101), QStringLiteral("Population (set only)"));
    QCOMPARE(effectAttributeName(genie::GV_TC, 0), QStringLiteral("Hit Points"));
    QCOMPARE(effectAttributeName(genie::GV_TC, 8), QStringLiteral("Armor"));
    QCOMPARE(effectAttributeName(genie::GV_TC, 40), QString());
    QCOMPARE(effectAttributeName(genie::GV_TC, 101), QStringLiteral("Train Time"));
    QCOMPARE(effectAttributeName(genie::GV_TC, 104), QStringLiteral("Wood Costs"));
    QCOMPARE(effectAttributeName(genie::GV_AoKA, 108), QString());
    QCOMPARE(effectAttributeName(genie::GV_AoK, 108), QStringLiteral("Garrison Heal Rate"));
    QCOMPARE(effectAttributeName(genie::GV_Cysion, 24), QString());
    QCOMPARE(effectAttributeName(genie::GV_Cysion, 109), QStringLiteral("Regeneration Rate"));
    QCOMPARE(effectAttributeName(genie::GV_C2, 40), QStringLiteral("Hero Status"));
    QCOMPARE(effectAttributeName(genie::GV_C2, 35), QString());
    QCOMPARE(effectAttributeName(genie::GV_C2, 115), QStringLiteral("Area Damage"));
    QCOMPARE(effectAttributeName(genie::GV_SWGB, 104), QStringLiteral("Carbon Costs"));
    QCOMPARE(effectAttributeName(genie::GV_SWGB, 109), QString());
    QCOMPARE(effectAttributeName(genie::GV_TC, -1), QString());

    genie::Unit unit;
    unit.Class = 6;
    FieldTreeModel units;
    units.setObject(unitFields(), unit, nullptr, {}, [](RefKind kind, int id) {
        return kind == RefKind::UnitClass ? unitClassName(genie::GV_TC, id) : QString();
    });
    QCOMPARE(fieldText(units, QStringLiteral("Class")), QStringLiteral("Infantry (6)"));
    QCOMPARE(fieldValue(units, QStringLiteral("Class")).toInt(), 6);
}

void ModelTest::sampleEffectValues()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    QVERIFY(openSample(session));

    EffectListModel effects(&session);
    QAbstractItemModelTester tester(&effects, QAbstractItemModelTester::FailureReportingMode::QtTest);
    effects.setCiv(1);
    const int count = static_cast<int>(session.dat()->Effects.size());
    QCOMPARE(effects.rowCount(), count);
    effects.setCiv(2);
    QCOMPARE(effects.rowCount(), count);
    effects.setCiv(1);

    const QString internal = QString::fromLatin1(session.dat()->Effects.at(22).Name);
    QVERIFY(!internal.isEmpty());
    QCOMPARE(effects.index(22).data().toString(), QStringLiteral("%1 - %2").arg(22).arg(internal));
    QCOMPARE(effects.index(22).data(Qt::ToolTipRole).toString(), internal);
    QCOMPARE(effects.index(22).data(EffectListModel::ActiveRole).toBool(), true);

    for (int row = 0; row < effects.rowCount(); ++row)
    {
        if (!session.dat()->Effects.at(row).Name.empty())
            continue;
        QCOMPARE(effects.index(row).data().toString(), QStringLiteral("%1 - (unnamed)").arg(row));
        break;
    }

    FieldTreeModel fields;
    effects.showFields(22, fields);
    QCOMPARE(fieldValue(fields, QStringLiteral("ID")).toInt(), 22);
    QCOMPARE(fieldValue(fields, QStringLiteral("Internal name")).toString(), internal);
    QCOMPARE(fieldValue(fields, QStringLiteral("Command count")).toInt(),
             static_cast<int>(session.dat()->Effects.at(22).EffectCommands.size()));
    if (!session.dat()->Effects.at(22).EffectCommands.empty())
    {
        QCOMPARE(fieldText(fields, QStringLiteral("Type")),
                 effectTypeName(session.gameVersion(), session.dat()->Effects.at(22).EffectCommands.front().Type));
    }

    effects.showFields(-1, fields);
    QCOMPARE(fields.rowCount(), 0);

    session.close();
    QCOMPARE(effects.rowCount(), 0);
    QVERIFY(!effects.effect(22));
}

void ModelTest::techIconMarkedForPreview()
{
    genie::Tech tech;
    tech.IconID = 18;
    FieldTreeModel model;
    model.setObject(techFields(), TechRef{1, tech});
    const QModelIndex icon = fieldIndex(model, QStringLiteral("Icon"));
    QCOMPARE(icon.data().toString(), QStringLiteral("18"));
    QCOMPARE(icon.data(FieldTreeModel::SpriteRole).toInt(), static_cast<int>(SpriteKind::TechIcon));
    QCOMPARE(icon.siblingAtColumn(FieldTreeModel::NameColumn).data(FieldTreeModel::SpriteRole).toInt(),
             static_cast<int>(SpriteKind::TechIcon));
    QCOMPARE(fieldIndex(model, QStringLiteral("Effect")).data(FieldTreeModel::SpriteRole).toInt(),
             static_cast<int>(SpriteKind::None));

    // Unit icons are a different sprite, so the shared preview can tell them apart.
    genie::Unit unit;
    unit.IconID = 5;
    model.setObject(unitFields(), unit);
    const QModelIndex unitIcon = fieldIndex(model, QStringLiteral("Icon"));
    QCOMPARE(unitIcon.data().toString(), QStringLiteral("5"));
    QCOMPARE(unitIcon.data(FieldTreeModel::SpriteRole).toInt(), static_cast<int>(SpriteKind::UnitIcon));
    QCOMPARE(unitIcon.siblingAtColumn(FieldTreeModel::NameColumn).data(FieldTreeModel::SpriteRole).toInt(),
             static_cast<int>(SpriteKind::UnitIcon));
    QCOMPARE(fieldIndex(model, QStringLiteral("Standing graphic 1")).data(FieldTreeModel::SpriteRole).toInt(),
             static_cast<int>(SpriteKind::None));
}

QTEST_GUILESS_MAIN(ModelTest)
#include "ModelTest.moc"
