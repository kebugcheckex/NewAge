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
#include "ui/OptionsDialog.h"
#include "ui/UnitBrowser.h"

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

// Makes unit `unit` current in the list, going through the filter proxy.
void selectUnit(QListView *list, int unit)
{
    const QAbstractItemModel *model = list->model();
    for (int row = 0; row < model->rowCount(); ++row)
    {
        if (model->index(row, 0).data().toString().startsWith(QStringLiteral("%1 - ").arg(unit)))
        {
            list->setCurrentIndex(model->index(row, 0));
            return;
        }
    }
    QFAIL("Unit not in list");
}

} // namespace

class UnitBrowserTest : public QObject
{
    Q_OBJECT

private slots:
    void browseSampleUnits();
    void hideEmptyUnits();
    void optionsDialogEditsConfig();

private:
    QTemporaryDir dir_;
};

void UnitBrowserTest::browseSampleUnits()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    Config config(dir_.filePath(QStringLiteral("browse.json")));
    UnitBrowser browser(&session, &config);
    auto *civs = browser.findChild<QComboBox *>();
    auto *filter = browser.findChild<QLineEdit *>();
    auto *units = browser.findChild<QListView *>();
    auto *fields = browser.findChild<QTreeView *>();
    QVERIFY(civs && filter && units && fields);
    QCOMPARE(civs->count(), 0);

    QString error;
    QVERIFY2(session.open(kTcDat, *findVersionProfile(QStringLiteral("tc")), &error), qPrintable(error));
    QCOMPARE(civs->count(), static_cast<int>(session.dat()->Civs.size()));
    QCOMPARE(civs->currentIndex(), 1);
    QCOMPARE(fields->model()->rowCount(), 0);

    selectUnit(units, 4);
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
        browser.resize(900, 600);
        browser.show();
        QVERIFY(QTest::qWaitForWindowExposed(&browser));
        units->scrollTo(units->currentIndex(), QAbstractItemView::PositionAtCenter);
        QVERIFY(browser.grab().save(screenshot));
    }

    session.close();
    QCOMPARE(civs->count(), 0);
    QCOMPARE(units->model()->rowCount(), 0);
    QCOMPARE(fields->model()->rowCount(), 0);
}

void UnitBrowserTest::hideEmptyUnits()
{
    if (!QFile::exists(kTcDat))
        QSKIP("Sample data/empires2_x1_p1.dat not present.");

    Session session;
    Config config(dir_.filePath(QStringLiteral("hide.json")));
    UnitBrowser browser(&session, &config);
    auto *units = browser.findChild<QListView *>();
    QString error;
    QVERIFY2(session.open(kTcDat, *findVersionProfile(QStringLiteral("tc")), &error), qPrintable(error));
    QVERIFY(listsEmptySlots(units));
    const int allRows = units->model()->rowCount();

    selectUnit(units, 4);
    config.setHideEmptyUnits(true);
    QVERIFY(!listsEmptySlots(units));
    QVERIFY(units->model()->rowCount() < allRows);
    // The selection survives the filter change.
    QCOMPARE(units->currentIndex().data().toString(), QStringLiteral("4 - ARCHR"));

    config.setHideEmptyUnits(false);
    QCOMPARE(units->model()->rowCount(), allRows);
}

void UnitBrowserTest::optionsDialogEditsConfig()
{
    Config config(dir_.filePath(QStringLiteral("dialog.json")));
    {
        OptionsDialog dialog(&config);
        auto *hideEmpty = dialog.findChild<QCheckBox *>();
        QVERIFY(hideEmpty);
        QVERIFY(!hideEmpty->isChecked());
        hideEmpty->setChecked(true);
        dialog.reject();
        QCOMPARE(config.hideEmptyUnits(), false);
    }
    {
        OptionsDialog dialog(&config);
        dialog.findChild<QCheckBox *>()->setChecked(true);
        dialog.accept();
        QCOMPARE(config.hideEmptyUnits(), true);
    }
    OptionsDialog dialog(&config);
    QVERIFY(dialog.findChild<QCheckBox *>()->isChecked());
}

QTEST_MAIN(UnitBrowserTest)
#include "UnitBrowserTest.moc"
