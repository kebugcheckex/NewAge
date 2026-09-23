#include "core/Session.h"

#include <exception>

#include <QFile>

#include "core/VersionProfile.h"
#include "genie/dat/DatFile.h"

namespace newage {

namespace {

// genieutils opens files through std::fstream with narrow paths, so on
// Windows this is the ANSI code page. Paths outside it will fail to open.
QByteArray nativePath(const QString &path)
{
    return QFile::encodeName(path);
}

} // namespace

Session::Session(QObject *parent) : QObject(parent) {}

Session::~Session() = default;

bool Session::open(const QString &datPath, const VersionProfile &profile, QString *error)
{
    close();

    auto dat = std::make_unique<genie::DatFile>();
    // Global in genieutils; 0 means "use the version's default terrain count".
    genie::Terrain::setTerrainCount(0);
    dat->setGameVersion(profile.gameVersion);
    try
    {
        dat->load(nativePath(datPath).constData());
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QStringLiteral("Failed to load %1: %2").arg(datPath, QString::fromLocal8Bit(e.what()));
        return false;
    }
    // genieutils returns without throwing when it doesn't recognise the file
    // version, and a wrong version choice can also leave the data empty.
    if (dat->Civs.empty())
    {
        if (error)
            *error = QStringLiteral("Failed to load %1: no civilizations were read (file version \"%2\"). "
                                    "Check that the right game version is selected.")
                         .arg(datPath, QString::fromLatin1(dat->FileVersion.c_str()));
        return false;
    }

    dat_ = std::move(dat);
    // genieutils may detect a more specific version from the file header.
    gameVersion_ = dat_->getGameVersion();
    datPath_ = datPath;
    setModified(false);
    emit opened();
    return true;
}

bool Session::saveAs(const QString &datPath, QString *error)
{
    if (!dat_)
    {
        if (error)
            *error = QStringLiteral("No data is open.");
        return false;
    }

    try
    {
        dat_->saveAs(nativePath(datPath).constData());
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QStringLiteral("Failed to save %1: %2").arg(datPath, QString::fromLocal8Bit(e.what()));
        return false;
    }

    datPath_ = datPath;
    setModified(false);
    return true;
}

void Session::close()
{
    if (!dat_)
        return;

    dat_.reset();
    gameVersion_ = genie::GV_None;
    datPath_.clear();
    setModified(false);
    emit closed();
}

void Session::setModified(bool modified)
{
    if (modified_ == modified)
        return;

    modified_ = modified;
    emit modifiedChanged(modified_);
}

} // namespace newage
