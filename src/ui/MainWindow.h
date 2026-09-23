#pragma once

#include <QMainWindow>

class QLabel;
class QStackedWidget;

namespace newage {

class Config;
class Session;
class UnitBrowser;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Config *config, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void openGameFolder();
    void openDataFile();
    void saveFileAs();
    void showOptions();
    // Returns false if the user cancelled.
    bool confirmDiscardChanges();
    void refresh();

    Config *config_;
    Session *session_;
    QStackedWidget *pages_;
    QLabel *placeholder_;
    UnitBrowser *unitBrowser_;
    QLabel *fileInfo_;
    QAction *saveAsAction_ = nullptr;
};

} // namespace newage
