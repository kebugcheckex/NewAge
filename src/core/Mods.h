#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "core/GameInstall.h"

namespace newage {

// A mod's metadata, stored in info.json in the mod folder. The keys are the
// ones DE writes for local mods.
struct ModInfo
{
    QString title;          // Names the folder of a mod NewAge creates.
    QString author;
    QString description;
};

// A mod folder found in a mods folder.
struct Mod
{
    QString dir;            // Absolute.
    ModInfo info;           // Title falls back to the folder name when info.json has none.
};

// Whether mods can be made for this data set. HD and DE read data mods with
// the game's resources/ layout; the older games have no mod folders.
bool supportsMods(const GameDataset &dataset);

// Folders the game reads local mods from, most likely first; empty if the
// data set doesn't support mods. HD: <game>/mods. DE keeps mods in the user
// profile, not the game folder: <home>/Games/Age of Empires 2 DE/<profile
// ID>/mods/local, one per profile folder that has a mods folder. Profiles the
// game has used for mods (with a mods/mod-status.json) come first, then the
// most recently changed. The folders need not exist yet.
QStringList modsFolders(const GameDataset &dataset, const QString &homeDir = QString());

// The mod folders in `modsFolder`, sorted by title.
QList<Mod> findMods(const QString &modsFolder);

// The metadata in <modDir>/info.json. The title falls back to the folder
// name.
ModInfo readModInfo(const QString &modDir);

// Writes `info` to <modDir>/info.json, keeping any other keys already there
// (DE's downloaded mods have more). Returns false on failure with `error`
// (if given) set.
bool writeModInfo(const QString &modDir, const ModInfo &info, QString *error = nullptr);

// Why `title` can't name a new mod folder in `modsFolder`, or empty if it can.
QString checkModTitle(const QString &modsFolder, const QString &title);

// Creates <modsFolder>/<title>/ (and modsFolder itself if needed) with an
// info.json. Returns the new folder, or empty on failure with `error` (if
// given) set.
QString createMod(const QString &modsFolder, const ModInfo &info, QString *error = nullptr);

// Whether `path` is one of the game's own data files (under
// <game>/resources), which saving to a mod leaves untouched.
bool isGameDataFile(const GameDataset &dataset, const QString &path);

// Where the mod in `modDir` keeps its copy of the data set's .dat:
// <modDir>/resources/_common/dat/<same file name>.
QString modDatPath(const GameDataset &dataset, const QString &modDir);

// The data set as the game sees it with the mod in `modDir` on: the mod's
// .dat, and the mod's key-value strings ahead of the game's.
GameDataset modDataset(const GameDataset &dataset, const QString &modDir, const QString &locale);

} // namespace newage
