#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace newage {

// One openable data set found in a game folder: a .dat file, the game version
// to read it with, and the language files that go with it.
struct GameDataset
{
    QString title;          // Shown in the UI, e.g. "Age of Kings HD (empires2_x2_p1.dat)".
    QString versionKey;     // VersionProfile key.
    QString datPath;        // Absolute.
    QStringList languageFiles; // Absolute, highest priority first; only files that exist.
};

// Finds the data sets in a game installation folder by looking for the data
// and language files of each known layout (see docs/PLAN.md, M2b). Newest
// first; empty if the folder matches no layout. File names are matched
// case-insensitively, since older games mix "data" and "Data".
//
// `locale` picks the language folder for games that ship several (HD, DE).
QList<GameDataset> detectInstall(const QString &dir, const QString &locale = QStringLiteral("en"));

// The .dat paths detectInstall() looks for, relative to the game folder, for
// telling the user what was expected.
QStringList knownDatPaths();

} // namespace newage
