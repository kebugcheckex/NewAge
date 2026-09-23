#pragma once

#include <QMainWindow>

class QLabel;

namespace newage {

class Session;

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
    QLabel *summary_;
    QAction *saveAsAction_ = nullptr;
};

} // namespace newage
