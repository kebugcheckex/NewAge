#include <memory>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

#include "core/Config.h"
#include "core/Session.h"
#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"
#include "model/TechListModel.h"
#include "model/UnitListModel.h"
#include "ui/EntityBrowser.h"
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

// Displayed value of field `name` in the field view, or a null string.
QString shownValue(const QTreeView *view, const QString &name)
{
    const QAbstractItemModel *model = view->model();
    for (int g = 0; g < model->rowCount(); ++g)
    {
        const QModelIndex group = model->index(g, 0);
        for (int r = 0; r < model->rowCount(group); ++r)
        {
            if (model->index(r, 0, group).data().toString() == name)
                return model->index(r, 1, group).data().toString();
        }
    }
    return {};
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

} // namespace

class BrowserTest : public QObject
{
    Q_OBJECT

private slots:
    void browseSampleUnits();
    void hideEmptyUnits();
    void browseSampleTechs();
    void optionsDialogEditsConfig();

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
    QCOMPARE(shownValue(fields, QStringLiteral("Civ")), QStringLiteral("1"));
    QCOMPARE(shownValue(fields, QStringLiteral("Cost 1 amount")), QStringLiteral("750"));

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

QTEST_MAIN(BrowserTest)
#include "BrowserTest.moc"
