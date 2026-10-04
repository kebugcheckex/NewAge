#include "core/Session.h"

#include <exception>
#include <filesystem>
#include <system_error>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include "core/GameInstall.h"
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

LoadResult::LoadResult() = default;

LoadResult::~LoadResult() = default;

LoadResult::LoadResult(LoadResult &&) noexcept = default;

LoadResult &LoadResult::operator=(LoadResult &&) noexcept = default;

Session::Session(QObject *parent) : QObject(parent) {}

Session::~Session() = default;

bool Session::open(const QString &datPath, const VersionProfile &profile, QString *error)
{
    LoadResult result = read(datPath, profile);
    if (!result.ok)
    {
        if (error)
            *error = result.error;
        close();
        return false;
    }
    adopt(std::move(result));
    return true;
}

bool Session::open(const GameDataset &dataset, QString *error, QStringList *warnings)
{
    LoadResult result = read(dataset);
    if (warnings)
        *warnings = result.warnings;
    if (!result.ok)
    {
        if (error)
            *error = result.error;
        close();
        return false;
    }
    adopt(std::move(result));
    return true;
}

LoadResult Session::read(const QString &datPath, const VersionProfile &profile, const LoadProgress &progress)
{
    if (progress)
        progress(0, 1);

    LoadResult result;
    result.datPath = datPath;
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
        result.error = QStringLiteral("Failed to load %1: %2").arg(datPath, QString::fromLocal8Bit(e.what()));
        return result;
    }
    // genieutils returns without throwing when it doesn't recognise the file
    // version, and a wrong version choice can also leave the data empty.
    if (dat->Civs.empty())
    {
        result.error = QStringLiteral("Failed to load %1: no civilizations were read (file version \"%2\"). "
                                      "Check that the right game version is selected.")
                           .arg(datPath, QString::fromLatin1(dat->FileVersion.c_str()));
        return result;
    }

    // The data is all in memory now. genieutils keeps the file open until
    // the next load, which would get in the way of saving over it.
    dat->freelock();
    result.dat = std::move(dat);
    // genieutils may detect a more specific version from the file header.
    result.version = result.dat->getGameVersion();
    result.ok = true;
    return result;
}

LoadResult Session::read(const GameDataset &dataset, const LoadProgress &progress)
{
    const VersionProfile *profile = findVersionProfile(dataset.versionKey);
    if (!profile)
    {
        LoadResult result;
        result.error = QStringLiteral("Unknown game version \"%1\".").arg(dataset.versionKey);
        return result;
    }

    const int total = 1 + dataset.languageFiles.size();
    LoadResult result = read(dataset.datPath, *profile, [&](int, int) {
        if (progress)
            progress(0, total);
    });
    if (!result.ok)
        return result;
    result.gameDir = dataset.gameDir;
    result.names.load(dataset.languageFiles, &result.warnings, [&](int index, int) {
        if (progress)
            progress(index + 1, total);
    });
    return result;
}

void Session::adopt(LoadResult result)
{
    close();
    if (!result.ok || !result.dat)
        return;
    dat_ = std::move(result.dat);
    names_ = std::move(result.names);
    gameVersion_ = result.version;
    datPath_ = result.datPath;
    sprites_.setSource(datPath_, gameVersion_, result.gameDir);
    setModified(false);
    emit opened();
}

bool Session::saveAs(const QString &datPath, QString *error)
{
    if (!dat_)
    {
        if (error)
            *error = QStringLiteral("No data is open.");
        return false;
    }

    const auto fail = [&](const QString &reason) {
        if (error)
            *error = QStringLiteral("Failed to save %1: %2").arg(datPath, reason);
        return false;
    };

    // Reserve a temporary name in the target folder, so the final rename
    // doesn't cross file systems. genieutils opens the file itself.
    QString tempPath;
    {
        QTemporaryFile temp(QFileInfo(datPath).absoluteDir().filePath(QStringLiteral(".newage-XXXXXX.tmp")));
        if (!temp.open())
            return fail(temp.errorString());
        // QTemporaryFile::close() keeps the handle open, which would block the
        // rename below on Windows; only destroying the object releases it.
        temp.setAutoRemove(false);
        tempPath = temp.fileName();
    }
    const auto failAndRemove = [&](const QString &reason) {
        QFile::remove(tempPath);
        return fail(reason);
    };
    // Temporary files are private to the owner; a saved file shouldn't be.
    QFile::setPermissions(tempPath, QFile::exists(datPath) ? QFile::permissions(datPath)
                                                           : QFile::ReadOwner | QFile::WriteOwner
                                                                 | QFile::ReadGroup | QFile::ReadOther);

    try
    {
        dat_->saveAs(nativePath(tempPath).constData());
    }
    catch (const std::exception &e)
    {
        return failAndRemove(QString::fromLocal8Bit(e.what()));
    }

    // Replaces an existing file in one step (MoveFileEx on Windows), unlike
    // QFile::rename.
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(tempPath.toStdU16String()),
                            std::filesystem::path(datPath.toStdU16String()), ec);
    if (ec)
        return failAndRemove(QString::fromLocal8Bit(ec.message()));

    datPath_ = datPath;
    setModified(false);
    return true;
}

void Session::close()
{
    if (!dat_)
        return;

    dat_.reset();
    names_.clear();
    sprites_.clear();
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
