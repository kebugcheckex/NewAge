#include <QApplication>
#include <QMessageBox>
#include <QSettings>

#include "core/Config.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NewAge"));
    QApplication::setApplicationName(QStringLiteral("NewAge"));
    QApplication::setApplicationVersion(QStringLiteral(NEWAGE_VERSION));
    // Keep settings in an .ini under %APPDATA% rather than the registry.
    QSettings::setDefaultFormat(QSettings::IniFormat);

    newage::Config config(newage::Config::defaultPath());
    QString error;
    if (!config.load(&error))
    {
        QMessageBox::warning(nullptr, QApplication::translate("main", "Options not loaded"),
                             QApplication::translate("main", "%1\n\nDefault options are used. Changing options "
                                                             "will overwrite the file.")
                                 .arg(error));
    }

    newage::MainWindow window(&config);
    window.show();
    return app.exec();
}
