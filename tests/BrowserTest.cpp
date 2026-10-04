#include <memory>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeView>

#include "core/Config.h"
#include "core/Mods.h"
#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "model/EffectListModel.h"
#include "model/FieldTreeModel.h"
#include "model/TechListModel.h"
#include "model/UnitListModel.h"
#include "model/UnitNames.h"
#include "ui/EntityBrowser.h"
#include "ui/ModsDialog.h"
#include "ui/ModsPanel.h"
#include "ui/OptionsDialog.h"

using namespace newage;

namespace {

const QString kTcDat = QStringLiteral(NEWAGE_SAMPLE_DATA_DIR "/empires2_x1_p1.dat");

bool listsEmptySlots(const QListView *list)
{
    const QAbstractItemModel *model = list->model();
    for (int row = 0; row < model->rowCount(); ++row)
    {
        if (model->index(row, 0).data().toString().endsWith(QStringLiteral("(empty)")))
            return true;
    }
    return false;
}

// Value index of field `name` in the field view, or an invalid index.
QModelIndex valueIndex(const QTreeView *view, const QString &name)
{
    const QAbstractItemModel *model = view->model();
    for (int g = 0; g < model->rowCount(); ++g)
    {
        const QModelIndex group = model->index(g, 0);
        for (int r = 0; r < model->rowCount(group); ++r)
        {
            if (model->index(r, 0, group).data().toString() == name)
                return model->index(r, 1, group);
        }
    }
    return {};
}

// Displayed value of field `name` in the field view, or a null string.
QString shownValue(const QTreeView *view, const QString &name)
{
    return valueIndex(view, name).data().toString();
}

// Makes entity `id` current in the list, going through the filter proxy.
void selectId(QListView *list, int id)
{
    const QAbstractItemModel *model = list->model();
    for (int row = 0; row < model->rowCount(); ++row)
    {
        if (model->index(row, 0).data().toString().startsWith(QStringLiteral("%1 - ").arg(id)))
        {
            list->setCurrentIndex(model->index(row, 0));
            return;
        }
    }
    QFAIL("ID not in list");
}

// Whether entity `id` is in the list (not filtered out).
bool listsId(const QListView *list, int id)
{
    const QAbstractItemModel *model = list->model();
    for (int row = 0; row < model->rowCount(); ++row)
    {
        if (model->index(row, 0).data().toString().startsWith(QStringLiteral("%1 - ").arg(id)))
            return true;
    }
    return false;
}

std::unique_ptr<EntityBrowser> unitBrowser(Session *session, Config *config)
{
    return std::make_unique<EntityBrowser>(session, config, new UnitListModel(session), &Config::hideEmptyUnits,
                             QStringLiteral("Filter units"));
}

std::unique_ptr<EntityBrowser> techBrowser(Session *session, Config *config)
{
    return std::make_unique<EntityBrowser>(session, config, new TechListModel(session), &Config::hideUnavailableTechs,
                             QStringLiteral("Filter techs"));
}

std::unique_ptr<EntityBrowser> effectBrowser(Session *session, Config *config)
{
    return std::make_unique<EntityBrowser>(session, config, new EffectListModel(session), nullptr,
                                           QStringLiteral("Filter effects"));
}

} // namespace

class BrowserTest : public QObject
{
    Q_OBJECT

private slots:
    void browseSampleUnits();
    void hideEmptyUnits();
    void browseSampleTechs();
    void doubleClickEffectSelectsIt();
    void editFields();
    void optionsDialogEditsConfig();
    void modsPanelListsMods();
    void saveAsModDialogListsMods();
    void modInfoDialogChecksTitle();

private:
    QTemporaryDir dir_;
};

void BrowserTest::browseSampleUnits()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    Config config(dir_.filePath(QStringLiteral("browse.json")));
    const auto browser = unitBrowser(&session, &config);
    auto *civs = browser->findChild<QComboBox *>();
    auto *filter = browser->findChild<QLineEdit *>();
    auto *units = browser->findChild<QListView *>();
    auto *fields = browser->findChild<QTreeView *>();
    QVERIFY(civs && filter && units && fields);
    QCOMPARE(civs->count(), 0);

    QString error;
    QVERIFY2(session.open(kTcDat, *findVersionProfile(QStringLiteral("tc")), &error), qPrintable(error));
    QCOMPARE(civs->count(), static_cast<int>(session.dat()->Civs.size()));
    QCOMPARE(civs->currentIndex(), 1);
    QCOMPARE(fields->model()->rowCount(), 0);

    selectId(units, 4);
    QCOMPARE(shownValue(fields, QStringLiteral("Internal name")), QStringLiteral("ARCHR"));
    QCOMPARE(valueIndex(fields, QStringLiteral("Icon")).data(FieldTreeModel::SpriteRole).toInt(),
             static_cast<int>(SpriteKind::UnitIcon));
    QCOMPARE(shownValue(fields, QStringLiteral("Hit points")), QStringLiteral("30"));
    QCOMPARE(shownValue(fields, QStringLiteral("Speed")), QStringLiteral("0.96"));
    QVERIFY(fields->isExpanded(fields->model()->index(0, 0)));

    // Switching civ keeps the same unit selected.
    civs->setCurrentIndex(2);
    QCOMPARE(units->currentIndex().data().toString(), QStringLiteral("4 - ARCHR"));
    QCOMPARE(shownValue(fields, QStringLiteral("ID")), QStringLiteral("4"));

    filter->setText(QStringLiteral("archr"));
    QVERIFY(units->model()->rowCount() >= 1);
    QVERIFY(units->model()->rowCount() < 10);

    const QString screenshot = qEnvironmentVariable("NEWAGE_UI_SCREENSHOT");
    if (!screenshot.isEmpty())
    {
        filter->clear();
        browser->resize(900, 600);
        browser->show();
        QVERIFY(QTest::qWaitForWindowExposed(browser.get()));
        units->scrollTo(units->currentIndex(), QAbstractItemView::PositionAtCenter);
        QVERIFY(browser->grab().save(screenshot));
    }

    session.close();
    QCOMPARE(civs->count(), 0);
    QCOMPARE(units->model()->rowCount(), 0);
    QCOMPARE(fields->model()->rowCount(), 0);
}

void BrowserTest::hideEmptyUnits()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    Config config(dir_.filePath(QStringLiteral("hide.json")));
    const auto browser = unitBrowser(&session, &config);
    auto *units = browser->findChild<QListView *>();
    QString error;
    QVERIFY2(session.open(kTcDat, *findVersionProfile(QStringLiteral("tc")), &error), qPrintable(error));
    QVERIFY(listsEmptySlots(units));
    const int allRows = units->model()->rowCount();

    selectId(units, 4);
    config.setHideEmptyUnits(true);
    QVERIFY(!listsEmptySlots(units));
    QVERIFY(units->model()->rowCount() < allRows);
    // The selection survives the filter change.
    QCOMPARE(units->currentIndex().data().toString(), QStringLiteral("4 - ARCHR"));

    config.setHideEmptyUnits(false);
    QCOMPARE(units->model()->rowCount(), allRows);
}

void BrowserTest::browseSampleTechs()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    Config config(dir_.filePath(QStringLiteral("techs.json")));
    const auto browser = techBrowser(&session, &config);
    auto *civs = browser->findChild<QComboBox *>();
    auto *filter = browser->findChild<QLineEdit *>();
    auto *techs = browser->findChild<QListView *>();
    auto *fields = browser->findChild<QTreeView *>();
    QVERIFY(civs && filter && techs && fields);
    QCOMPARE(filter->placeholderText(), QStringLiteral("Filter techs"));

    QString error;
    QVERIFY2(session.open(kTcDat, *findVersionProfile(QStringLiteral("tc")), &error), qPrintable(error));
    QCOMPARE(civs->currentIndex(), 1);
    QCOMPARE(techs->model()->rowCount(), static_cast<int>(session.dat()->Techs.size()));

    // Yeomen (tech 3) is the Britons' (civ 1) unique tech.
    selectId(techs, 3);
    QCOMPARE(shownValue(fields, QStringLiteral("Internal name")), QStringLiteral("British Yeoman"));
    const QString civName = QString::fromLatin1(session.dat()->Civs.at(1).Name);
    QCOMPARE(shownValue(fields, QStringLiteral("Civ")), QStringLiteral("%1 (1)").arg(civName));
    QCOMPARE(shownValue(fields, QStringLiteral("Cost 1 amount")), QStringLiteral("750"));
    const QModelIndex icon = valueIndex(fields, QStringLiteral("Icon"));
    QVERIFY(icon.isValid());
    QVERIFY(!icon.data().toString().isEmpty());
    QCOMPARE(icon.data(FieldTreeModel::SpriteRole).toInt(), static_cast<int>(SpriteKind::TechIcon));

    // Another civ can't research it, but it stays selected and shown.
    civs->setCurrentIndex(2);
    QCOMPARE(techs->currentIndex().data().toString(), QStringLiteral("3 - British Yeoman"));
    QCOMPARE(shownValue(fields, QStringLiteral("ID")), QStringLiteral("3"));

    // Hiding unavailable techs removes it for civ 2 but not for civ 1.
    config.setHideUnavailableTechs(true);
    QVERIFY(!listsId(techs, 3));
    QVERIFY(listsId(techs, 22));
    QVERIFY(techs->model()->rowCount() < static_cast<int>(session.dat()->Techs.size()));
    civs->setCurrentIndex(1);
    QVERIFY(listsId(techs, 3));

    filter->setText(QStringLiteral("loom"));
    QCOMPARE(techs->model()->rowCount(), 1);

    session.close();
    QCOMPARE(techs->model()->rowCount(), 0);
    QCOMPARE(fields->model()->rowCount(), 0);
}

void BrowserTest::doubleClickEffectSelectsIt()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    Config config(dir_.filePath(QStringLiteral("effects.json")));
    const auto techs = techBrowser(&session, &config);
    const auto effects = effectBrowser(&session, &config);
    QSignalSpy activated(techs.get(), &EntityBrowser::effectActivated);
    connect(techs.get(), &EntityBrowser::effectActivated, effects.get(), &EntityBrowser::selectEntity);

    auto *techList = techs->findChild<QListView *>();
    auto *techFields = techs->findChild<QTreeView *>();
    auto *effectList = effects->findChild<QListView *>();
    auto *effectFields = effects->findChild<QTreeView *>();
    auto *effectFilter = effects->findChild<QLineEdit *>();
    QVERIFY(techList && techFields && effectList && effectFields && effectFilter);

    QString error;
    QVERIFY2(session.open(kTcDat, *findVersionProfile(QStringLiteral("tc")), &error), qPrintable(error));
    QCOMPARE(effectList->model()->rowCount(), static_cast<int>(session.dat()->Effects.size()));
    QCOMPARE(effectFilter->placeholderText(), QStringLiteral("Filter effects"));

    effectFilter->setText(QStringLiteral("zzzz-no-such-effect"));
    QCOMPARE(effectList->model()->rowCount(), 0);
    effects->selectEntity(22);
    QVERIFY(effectFilter->text().isEmpty());
    QVERIFY(effectList->currentIndex().data().toString().startsWith(QStringLiteral("22 - ")));
    const QString effectName = QString::fromLatin1(session.dat()->Effects.at(22).Name);
    QCOMPARE(shownValue(effectFields, QStringLiteral("Internal name")), effectName);
    effects->selectEntity(-1);
    QVERIFY(effectList->currentIndex().data().toString().startsWith(QStringLiteral("22 - ")));

    selectId(techList, 22);
    const QModelIndex effect = valueIndex(techFields, QStringLiteral("Effect"));
    QVERIFY(effect.isValid());
    const int effectId = effect.data(FieldTreeModel::ValueRole).toInt();
    QVERIFY(effectId >= 0);
    // The view emits doubleClicked for the cell that was clicked. Both columns
    // name the same effect.
    QVERIFY(QMetaObject::invokeMethod(techFields, "doubleClicked", Q_ARG(QModelIndex, effect)));
    QCOMPARE(activated.size(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), effectId);
    QVERIFY(effectList->currentIndex().data().toString().startsWith(QStringLiteral("%1 - ").arg(effectId)));
    QVERIFY(QMetaObject::invokeMethod(techFields, "doubleClicked",
                                      Q_ARG(QModelIndex, effect.siblingAtColumn(FieldTreeModel::NameColumn))));
    QCOMPARE(activated.size(), 2);

    int none = -1;
    const auto &techData = session.dat()->Techs;
    for (int i = 0; i < static_cast<int>(techData.size()); ++i)
    {
        if (techData[i].EffectID < 0)
        {
            none = i;
            break;
        }
    }
    if (none >= 0)
    {
        selectId(techList, none);
        const QModelIndex missing = valueIndex(techFields, QStringLiteral("Effect"));
        QVERIFY(missing.isValid());
        QCOMPARE(missing.data(FieldTreeModel::ValueRole).toInt(), -1);
        QVERIFY(QMetaObject::invokeMethod(techFields, "doubleClicked", Q_ARG(QModelIndex, missing)));
        QCOMPARE(activated.size(), 2);
    }
}

void BrowserTest::editFields()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    Config config(dir_.filePath(QStringLiteral("edit.json")));
    const auto browser = unitBrowser(&session, &config);
    auto *units = browser->findChild<QListView *>();
    auto *fields = browser->findChild<QTreeView *>();
    QString error;
    QVERIFY2(session.open(kTcDat, *findVersionProfile(QStringLiteral("tc")), &error), qPrintable(error));
    selectId(units, 4);

    // Int fields edit in a spin box limited to the member's range.
    const QModelIndex hp = valueIndex(fields, QStringLiteral("Hit points"));
    QVERIFY(hp.flags() & Qt::ItemIsEditable);
    QVERIFY(!(valueIndex(fields, QStringLiteral("Internal name")).flags() & Qt::ItemIsEditable));
    QAbstractItemDelegate *delegate = fields->itemDelegateForIndex(hp);
    std::unique_ptr<QWidget> editor(delegate->createEditor(fields->viewport(), QStyleOptionViewItem(), hp));
    auto *spin = qobject_cast<QSpinBox *>(editor.get());
    QVERIFY(spin);
    QCOMPARE(spin->minimum(), -32768);
    QCOMPARE(spin->maximum(), 32767);
    delegate->setEditorData(spin, hp);
    QCOMPARE(spin->value(), 30);
    spin->setValue(45);
    delegate->setModelData(spin, fields->model(), hp);
    QCOMPARE(shownValue(fields, QStringLiteral("Hit points")), QStringLiteral("45"));
    QCOMPARE(session.dat()->Civs.at(1).Units.at(4).HitPoints, int16_t(45));
    QVERIFY(session.isModified());

    // Floats edit as text.
    const QModelIndex speed = valueIndex(fields, QStringLiteral("Speed"));
    editor.reset(delegate->createEditor(fields->viewport(), QStyleOptionViewItem(), speed));
    auto *line = qobject_cast<QLineEdit *>(editor.get());
    QVERIFY(line);
    delegate->setEditorData(line, speed);
    QCOMPARE(line->text(), QStringLiteral("0.96"));
    line->setText(QStringLiteral("1.2"));
    delegate->setModelData(line, fields->model(), speed);
    QCOMPARE(shownValue(fields, QStringLiteral("Speed")), QStringLiteral("1.2"));
    QCOMPARE(session.dat()->Civs.at(1).Units.at(4).Speed, 1.2f);

    // The current cell stays on the value column, where F2 edits it.
    fields->setCurrentIndex(hp.siblingAtColumn(FieldTreeModel::NameColumn));
    QCOMPARE(fields->currentIndex(), hp);

    // A required tech edits as a combo of every other tech, label and ID.
    const auto techs = techBrowser(&session, &config);
    auto *techList = techs->findChild<QListView *>();
    auto *techFields = techs->findChild<QTreeView *>();
    selectId(techList, 22);
    const QModelIndex required = valueIndex(techFields, QStringLiteral("Required tech 1"));
    QVERIFY(required.flags() & Qt::ItemIsEditable);
    QVERIFY(!(valueIndex(techFields, QStringLiteral("Required tech count")).flags() & Qt::ItemIsEditable));
    QAbstractItemDelegate *techDelegate = techFields->itemDelegateForIndex(required);
    editor.reset(techDelegate->createEditor(techFields->viewport(), QStyleOptionViewItem(), required));
    auto *combo = qobject_cast<QComboBox *>(editor.get());
    QVERIFY(combo);
    auto *techModel = qobject_cast<TechListModel *>(techs->model());
    QVERIFY(techModel);
    QCOMPARE(combo->count(), techModel->rowCount() - 1);
    QVERIFY(combo->findData(22) < 0);
    const int darkAge = combo->findData(104);
    QVERIFY(darkAge >= 0);
    QCOMPARE(combo->itemText(darkAge), QStringLiteral("%1 (%2)").arg(techModel->name(104)).arg(104));
    for (int i = 0; i < combo->count(); ++i)
    {
        const int id = combo->itemData(i).toInt();
        QVERIFY(id != 22);
        QCOMPARE(combo->itemText(i), QStringLiteral("%1 (%2)").arg(techModel->name(id)).arg(id));
    }
    techDelegate->setEditorData(combo, required);
    QCOMPARE(combo->currentData().toInt(), 104);
    const int feudal = combo->findData(101);
    QVERIFY(feudal >= 0);
    combo->setCurrentIndex(feudal);
    techDelegate->setModelData(combo, techFields->model(), required);
    QCOMPARE(session.dat()->Techs.at(22).RequiredTechs.at(0), int16_t(101));
    QCOMPARE(shownValue(techFields, QStringLiteral("Required tech 1")),
             QStringLiteral("%1 (101)").arg(techModel->name(101)));

    // The same filter that hides unavailable techs takes them out of the combo.
    config.setHideUnavailableTechs(true);
    editor.reset(techDelegate->createEditor(techFields->viewport(), QStyleOptionViewItem(), required));
    combo = qobject_cast<QComboBox *>(editor.get());
    QVERIFY(combo);
    int available = 0;
    for (int id = 0; id < techModel->rowCount(); ++id)
        available += techModel->index(id).data(EntityListModel::ActiveRole).toBool();
    QCOMPARE(combo->count(), available - 1);
    QVERIFY(combo->findData(59) < 0);
    QVERIFY(combo->findData(85) < 0);
    QVERIFY(combo->findData(22) < 0);
    QVERIFY(combo->findData(3) >= 0);
    QVERIFY(combo->findData(101) >= 0);

    // Research location edits as a combo of the current civ's buildings.
    const QModelIndex location = valueIndex(techFields, QStringLiteral("Location"));
    QVERIFY(location.flags() & Qt::ItemIsEditable);
    editor.reset(techDelegate->createEditor(techFields->viewport(), QStyleOptionViewItem(), location));
    combo = qobject_cast<QComboBox *>(editor.get());
    QVERIFY(combo);
    const int civ = techs->model()->civ();
    const genie::Civ &selected = session.dat()->Civs.at(civ);
    QList<int> buildings;
    const int unitCount = static_cast<int>(selected.Units.size());
    const int pointerCount = static_cast<int>(selected.UnitPointers.size());
    for (int id = 0; id < unitCount && id < pointerCount; ++id)
    {
        if (selected.UnitPointers[id] != 0 && selected.Units[id].Type == genie::UT_Building)
            buildings.append(id);
    }
    QVERIFY(buildings.size() > 1);
    QCOMPARE(combo->count(), buildings.size());
    QVERIFY(!buildings.contains(4));
    QVERIFY(buildings.contains(109));
    for (int i = 0; i < combo->count(); ++i)
    {
        const int id = combo->itemData(i).toInt();
        QCOMPARE(id, buildings.at(i));
        QString label = unitName(session, civ, id);
        if (label.isEmpty())
            label = QStringLiteral("(unnamed)");
        QCOMPARE(combo->itemText(i), QStringLiteral("%1 (%2)").arg(label).arg(id));
    }
    techDelegate->setEditorData(combo, location);
    QCOMPARE(combo->currentData().toInt(), 109);
    const int other = buildings.at(0) == 109 ? buildings.at(1) : buildings.at(0);
    combo->setCurrentIndex(combo->findData(other));
    techDelegate->setModelData(combo, techFields->model(), location);
    QCOMPARE(session.dat()->Techs.at(22).ResearchLocations.front().LocationID, int16_t(other));
    const QString otherName = unitName(session, civ, other);
    QCOMPARE(shownValue(techFields, QStringLiteral("Location")),
             QStringLiteral("%1 (%2)").arg(otherName.isEmpty() ? QStringLiteral("(unnamed)") : otherName).arg(other));
    // The combo is parented to the view; drop it before the browser does.
    editor.reset();
}

void BrowserTest::optionsDialogEditsConfig()
{
    Config config(dir_.filePath(QStringLiteral("dialog.json")));
    {
        OptionsDialog dialog(&config);
        auto *hideEmpty = dialog.findChild<QCheckBox *>(QStringLiteral("hideEmptyUnits"));
        auto *hideTechs = dialog.findChild<QCheckBox *>(QStringLiteral("hideUnavailableTechs"));
        QVERIFY(hideEmpty && hideTechs);
        QVERIFY(!hideEmpty->isChecked());
        QVERIFY(!hideTechs->isChecked());
        hideEmpty->setChecked(true);
        hideTechs->setChecked(true);
        dialog.reject();
        QCOMPARE(config.hideEmptyUnits(), false);
        QCOMPARE(config.hideUnavailableTechs(), false);
    }
    {
        OptionsDialog dialog(&config);
        dialog.findChild<QCheckBox *>(QStringLiteral("hideEmptyUnits"))->setChecked(true);
        dialog.accept();
        QCOMPARE(config.hideEmptyUnits(), true);
        QCOMPARE(config.hideUnavailableTechs(), false);
    }
    {
        OptionsDialog dialog(&config);
        dialog.findChild<QCheckBox *>(QStringLiteral("hideUnavailableTechs"))->setChecked(true);
        dialog.accept();
        QCOMPARE(config.hideUnavailableTechs(), true);
    }
    OptionsDialog dialog(&config);
    QVERIFY(dialog.findChild<QCheckBox *>(QStringLiteral("hideEmptyUnits"))->isChecked());
    QVERIFY(dialog.findChild<QCheckBox *>(QStringLiteral("hideUnavailableTechs"))->isChecked());
}

// An HD game folder with two mods: Balance, with its own .dat, and Empty,
// without. Returns the game's data set.
static GameDataset makeModdedGame(const QTemporaryDir &game)
{
    const GameDataset dataset{QStringLiteral("HD"), QStringLiteral("aokhd"),
                              game.filePath(QStringLiteral("resources/_common/dat/empires2_x2_p1.dat")), {},
                              game.path()};
    const QString folder = game.filePath(QStringLiteral("mods"));
    const QString withData = createMod(folder, {QStringLiteral("Balance"), QStringLiteral("Me"), {}});
    const QString modDat = modDatPath(dataset, withData);
    QDir().mkpath(QFileInfo(modDat).absolutePath());
    QFile dat(modDat);
    if (dat.open(QIODevice::WriteOnly))
        dat.close();
    createMod(folder, {QStringLiteral("Empty"), {}, {}});
    return dataset;
}

void BrowserTest::modsPanelListsMods()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    const GameDataset dataset = makeModdedGame(game);
    const QString folder = QDir::cleanPath(game.filePath(QStringLiteral("mods")));
    const QString balance = QDir(folder).filePath(QStringLiteral("Balance"));
    const QString empty = QDir(folder).filePath(QStringLiteral("Empty"));

    ModsPanel panel;
    panel.setGame(dataset, QString());
    QCOMPARE(panel.modsFolder(), folder);
    auto *mods = panel.findChild<QListWidget *>(QStringLiteral("mods"));
    auto *edit = panel.findChild<QPushButton *>(QStringLiteral("editMod"));
    auto *details = panel.findChild<QLabel *>(QStringLiteral("modDetails"));
    auto *more = panel.findChild<QToolButton *>(QStringLiteral("modActions"));
    QVERIFY(mods && edit && details && more);
    QAction *saveHere = more->menu()->actions().at(0);

    // The game's own data, then the mods by title. Empty, without a .dat, is
    // dimmed and says so.
    QCOMPARE(mods->count(), 3);
    QVERIFY(panel.gameDataSelected());
    QVERIFY(mods->item(0)->text().startsWith(QStringLiteral("Game data")));
    QCOMPARE(mods->item(1)->text(), QStringLiteral("Balance"));
    QCOMPARE(mods->item(2)->text(), QStringLiteral("Empty"));
    QVERIFY(mods->item(1)->toolTip().isEmpty());
    QVERIFY(!mods->item(2)->toolTip().isEmpty());
    QVERIFY(edit->isEnabled());

    QSignalSpy edits(&panel, &ModsPanel::editRequested);
    mods->setCurrentRow(1);
    QCOMPARE(panel.selectedModDir(), balance);
    QVERIFY(details->text().contains(QStringLiteral("by Me")));
    QVERIFY(!saveHere->isEnabled()); // No data open.
    emit mods->itemActivated(mods->currentItem());
    QCOMPARE(edits.size(), 1);
    QCOMPARE(edits.at(0).at(0).toString(), balance);

    // Editing Balance marks it, and Edit and Save Here are off for it.
    panel.setDataOpen(true);
    panel.setEdited(ModsPanel::Edited::Mod, balance);
    QVERIFY(mods->item(1)->font().bold());
    QVERIFY(mods->item(1)->text().endsWith(QStringLiteral("Balance")));
    QVERIFY(mods->item(1)->text() != QStringLiteral("Balance"));
    QVERIFY(!edit->isEnabled());
    QVERIFY(!saveHere->isEnabled());
    edit->click();
    QCOMPARE(edits.size(), 1);

    // Selecting another row only shows it.
    mods->setCurrentRow(2);
    QCOMPARE(panel.selectedModDir(), empty);
    QVERIFY(edit->isEnabled());
    QVERIFY(saveHere->isEnabled());
    QVERIFY(details->text().contains(QStringLiteral("starts from the game data")));
    QSignalSpy saves(&panel, &ModsPanel::saveHereRequested);
    saveHere->trigger();
    QCOMPARE(saves.size(), 1);
    QCOMPARE(saves.at(0).at(0).toString(), empty);
    edit->click();
    QCOMPARE(edits.size(), 2);
    QCOMPARE(edits.at(1).at(0).toString(), empty);

    // A reload keeps the selection and the mark.
    panel.reload();
    QCOMPARE(panel.selectedModDir(), empty);
    QVERIFY(mods->item(1)->font().bold());

    // Editing the game data selects and marks its row.
    panel.setEdited(ModsPanel::Edited::GameData);
    QVERIFY(panel.gameDataSelected());
    QVERIFY(mods->item(0)->font().bold());
    QVERIFY(!mods->item(1)->font().bold());
    QVERIFY(!edit->isEnabled());

    // A mods folder that doesn't exist yet lists only the game data.
    ModsPanel missing;
    missing.setGame(dataset, game.filePath(QStringLiteral("elsewhere")));
    QCOMPARE(missing.findChild<QListWidget *>(QStringLiteral("mods"))->count(), 1);
}

void BrowserTest::saveAsModDialogListsMods()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    const GameDataset dataset = makeModdedGame(game);
    const QString folder = QDir::cleanPath(game.filePath(QStringLiteral("mods")));
    const QString empty = QDir(folder).filePath(QStringLiteral("Empty"));

    SaveAsModDialog dialog(dataset, folder, empty);
    auto *mods = dialog.findChild<QListWidget *>(QStringLiteral("mods"));
    auto *save = dialog.findChild<QPushButton *>(QStringLiteral("saveToMod"));
    auto *details = dialog.findChild<QLabel *>(QStringLiteral("modDetails"));
    QVERIFY(mods && save && details);
    QCOMPARE(mods->count(), 2);
    QVERIFY(save->isDefault());
    // The mod being edited is selected first.
    QCOMPARE(dialog.modDir(), empty);
    QVERIFY(save->isEnabled());
    mods->setCurrentRow(0);
    QVERIFY(details->text().contains(QStringLiteral("by Me")));
    QVERIFY(details->text().contains(QStringLiteral("Has its own")));

    save->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.modDir(), QDir(folder).filePath(QStringLiteral("Balance")));

    // An empty folder offers nothing to save into.
    SaveAsModDialog none(dataset, game.filePath(QStringLiteral("elsewhere")));
    QCOMPARE(none.findChild<QListWidget *>(QStringLiteral("mods"))->count(), 0);
    QVERIFY(!none.findChild<QPushButton *>(QStringLiteral("saveToMod"))->isEnabled());
}

void BrowserTest::modInfoDialogChecksTitle()
{
    QVERIFY(QDir(dir_.path()).mkpath(QStringLiteral("mods/Taken")));
    ModInfoDialog dialog(dir_.filePath(QStringLiteral("mods")), {QStringLiteral("Game data"), QStringLiteral("Open")});
    auto *title = dialog.findChild<QLineEdit *>(QStringLiteral("modTitle"));
    auto *start = dialog.findChild<QComboBox *>(QStringLiteral("modStart"));
    QPushButton *ok = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    QVERIFY(title && start && ok);
    QVERIFY(!ok->isEnabled());
    title->setText(QStringLiteral("taken"));
    QVERIFY(!ok->isEnabled());
    title->setText(QStringLiteral("bad|name"));
    QVERIFY(!ok->isEnabled());
    title->setText(QStringLiteral("My Balance Mod"));
    QVERIFY(ok->isEnabled());
    QCOMPARE(dialog.title(), QStringLiteral("My Balance Mod"));
    QCOMPARE(dialog.startIndex(), 0);
    start->setCurrentIndex(1);
    QCOMPARE(dialog.startIndex(), 1);

    // No start choices, no combo.
    ModInfoDialog plain(dir_.filePath(QStringLiteral("mods")));
    QVERIFY(!plain.findChild<QComboBox *>(QStringLiteral("modStart")));
    QCOMPARE(plain.startIndex(), -1);

    // Editing keeps the folder, so a name like an existing folder's is fine;
    // only an empty one isn't.
    ModInfoDialog edit(ModInfo{QStringLiteral("Taken"), QStringLiteral("Me"), QStringLiteral("Text")});
    auto *editTitle = edit.findChild<QLineEdit *>(QStringLiteral("modTitle"));
    QPushButton *editOk = edit.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    QVERIFY(editOk->isEnabled());
    QCOMPARE(edit.info().author, QStringLiteral("Me"));
    QCOMPARE(edit.info().description, QStringLiteral("Text"));
    editTitle->setText(QStringLiteral("  "));
    QVERIFY(!editOk->isEnabled());
    editTitle->setText(QStringLiteral(" Renamed "));
    QVERIFY(editOk->isEnabled());
    QCOMPARE(edit.title(), QStringLiteral("Renamed"));
}

QTEST_MAIN(BrowserTest)
#include "BrowserTest.moc"
