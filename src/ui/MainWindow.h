#pragma once

#include <QMainWindow>

class QLabel;
class QStackedWidget;
class QTabWidget;

namespace newage {

class Config;
class Session;

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
    // Units and Techs tabs, shown while data is open.
    QTabWidget *browsers_;
    QLabel *fileInfo_;
    QAction *saveAsAction_ = nullptr;
};

} // namespace newage
