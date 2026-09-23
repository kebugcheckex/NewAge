#include "core/GameInstall.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>

#include "core/VersionProfile.h"

namespace newage {

namespace {

// Resolves `relative` ('/'-separated) under `dir`, matching each part
// case-insensitively. Returns the absolute path, or empty if it doesn't exist.
QString findPath(const QString &dir, const QString &relative)
{
    QString current = QDir(dir).absolutePath();
    for (const QString &part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts))
    {
        const QString exact = QDir(current).filePath(part);
        if (QFileInfo::exists(exact))
        {
            current = exact;
            continue;
        }
        const QStringList entries = QDir(current).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        auto it = std::find_if(entries.begin(), entries.end(), [&](const QString &entry) {
            return entry.compare(part, Qt::CaseInsensitive) == 0;
        });
        if (it == entries.end())
            return {};
        current = QDir(current).filePath(*it);
    }
    return QDir::cleanPath(current);
}

QStringList findPaths(const QString &dir, const QStringList &relatives)
{
    QStringList found;
    for (const QString &relative : relatives)
    {
        const QString path = findPath(dir, relative);
        if (!path.isEmpty())
            found.append(path);
    }
    return found;
}

QString titleFor(const QString &versionKey, const QString &datPath)
{
    const VersionProfile *profile = findVersionProfile(versionKey);
    return QStringLiteral("%1 (%2)").arg(profile ? profile->displayName : versionKey,
                                         QFileInfo(datPath).fileName());
}

// HD and DE: resources/_common/dat/*.dat, strings in resources/<locale>/.
// The modded file overrides the base one. DE also ships extra files (the
// Chronicles strings, campaigns) whose IDs don't overlap the base file; they
// go last, in name order.
QStringList keyValueFiles(const QString &dir, const QString &locale)
{
    const QString folder = findPath(dir, QStringLiteral("resources/%1/strings/key-value").arg(locale));
    if (folder.isEmpty())
        return {};

    const QString modded = QStringLiteral("key-value-modded-strings-utf8.txt");
    const QString base = QStringLiteral("key-value-strings-utf8.txt");
    QStringList files;
    QStringList extra;
    const QStringList entries = QDir(folder).entryList({QStringLiteral("*key-value*strings-utf8.txt")},
                                                      QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString &entry : entries)
    {
        if (entry.compare(modded, Qt::CaseInsensitive) != 0 && entry.compare(base, Qt::CaseInsensitive) != 0)
            extra.append(QDir(folder).filePath(entry));
    }
    files << findPaths(folder, {modded, base}) << extra;
    return files;
}

void detectHdOrDe(const QString &dir, const QString &locale, QList<GameDataset> *datasets)
{
    const QString x2 = findPath(dir, QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"));
    const QString x1 = findPath(dir, QStringLiteral("resources/_common/dat/empires2_x1_p1.dat"));
    if (x2.isEmpty() && x1.isEmpty())
        return;

    const QStringList strings = keyValueFiles(dir, locale);
    // HD ships the Conquerors data next to its own; DE ships only
    // empires2_x2_p1.dat.
    if (!x1.isEmpty())
    {
        if (!x2.isEmpty())
            datasets->append({titleFor(QStringLiteral("aokhd"), x2), QStringLiteral("aokhd"), x2, strings});
        datasets->append({titleFor(QStringLiteral("tc"), x1), QStringLiteral("tc"), x1, strings});
    }
    else
    {
        datasets->append({titleFor(QStringLiteral("aoe2de"), x2), QStringLiteral("aoe2de"), x2, strings});
    }
}

// Games with language DLLs, newest first within each family. Paths are from
// AGE's default buttons (OpenSaveDialog.cpp) and not yet checked against real
// installs.
struct DllLayout
{
    const char *dat;
    const char *versionKey;
    QStringList languageFiles;
};

const QList<DllLayout> &dllLayouts()
{
    static const QList<DllLayout> layouts = {
        {"data/genie_x2.dat", "ef2", {"language_x2.dll", "language_x1.dll", "language.dll"}},
        {"data/genie_x1.dat", "cc", {"language_x1.dll", "language.dll"}},
        {"data/genie.dat", "swgb", {"language.dll"}},
        {"data/empires2_x1_p1.dat", "tc", {"language_x1_p1.dll", "language_x1.dll", "language.dll"}},
        {"data/empires2.dat", "aok", {"language.dll"}},
        {"data2/empires.dat", "ror", {"languagex.dll", "language.dll"}},
        {"data/empires.dat", "aoe", {"language.dll"}},
    };
    return layouts;
}

} // namespace

QList<GameDataset> detectInstall(const QString &dir, const QString &locale)
{
    QList<GameDataset> datasets;
    if (!QFileInfo(dir).isDir())
        return datasets;

    detectHdOrDe(dir, locale, &datasets);
    if (!datasets.isEmpty())
        return datasets;

    // AoE DE keeps the original AoE .dat path, so check it before AoE.
    const QString aoeDeStrings = findPath(dir, QStringLiteral("data/Localization/%1/strings.txt").arg(locale));
    const QString aoeDeDat = findPath(dir, QStringLiteral("data/empires.dat"));
    if (!aoeDeStrings.isEmpty() && !aoeDeDat.isEmpty())
    {
        datasets.append({titleFor(QStringLiteral("aoede"), aoeDeDat), QStringLiteral("aoede"), aoeDeDat,
                         {aoeDeStrings}});
        return datasets;
    }

    for (const DllLayout &layout : dllLayouts())
    {
        const QString dat = findPath(dir, QString::fromLatin1(layout.dat));
        if (dat.isEmpty())
            continue;
        const QString key = QString::fromLatin1(layout.versionKey);
        datasets.append({titleFor(key, dat), key, dat, findPaths(dir, layout.languageFiles)});
    }
    return datasets;
}

QStringList knownDatPaths()
{
    QStringList paths = {QStringLiteral("resources/_common/dat/empires2_x2_p1.dat"),
                         QStringLiteral("resources/_common/dat/empires2_x1_p1.dat")};
    for (const DllLayout &layout : dllLayouts())
        paths.append(QString::fromLatin1(layout.dat));
    return paths;
}

} // namespace newage
