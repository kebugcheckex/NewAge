#include <QApplication>
#include <QSettings>

#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NewAge"));
    QApplication::setApplicationName(QStringLiteral("NewAge"));
    QApplication::setApplicationVersion(QStringLiteral(NEWAGE_VERSION));
    // Keep settings in an .ini under %APPDATA% rather than the registry.
    QSettings::setDefaultFormat(QSettings::IniFormat);

    newage::MainWindow window;
    window.show();
    return app.exec();
}
