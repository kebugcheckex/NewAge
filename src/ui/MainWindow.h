#pragma once

#include <QMainWindow>

class QLabel;
class QStackedWidget;

namespace newage {

class Session;
class UnitBrowser;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void openFile();
    void saveFileAs();
    // Returns false if the user cancelled.
    bool confirmDiscardChanges();
    void refresh();

    Session *session_;
    QStackedWidget *pages_;
    QLabel *placeholder_;
    UnitBrowser *unitBrowser_;
    QLabel *fileInfo_;
    QAction *saveAsAction_ = nullptr;
};

} // namespace newage
