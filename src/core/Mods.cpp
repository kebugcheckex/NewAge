#include "core/Mods.h"

#include <algorithm>

#include <QDateTime>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace newage {

namespace {

const auto kInfoFile = QStringLiteral("info.json");
const auto kDatFolder = QStringLiteral("resources/_common/dat");

// Whether `path` is `dir` or inside it.
bool isInside(const QString &path, const QString &dir)
{
    const QString relative = QDir(QDir::cleanPath(dir)).relativeFilePath(QDir::cleanPath(path));
    return !relative.startsWith(QStringLiteral("..")) && !QDir::isAbsolutePath(relative);
}

QJsonObject readInfoJson(const QString &modDir)
{
    QFile file(QDir(modDir).filePath(kInfoFile));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

bool supportsMods(const GameDataset &dataset)
{
    static const QStringList keys = {QStringLiteral("aokhd"), QStringLiteral("tc"), QStringLiteral("aoe2de")};
    // TC also comes from a CD install (data/empires2_x1_p1.dat), which has no
    // mods; HD keeps it in resources/_common/dat like its own data.
    return !dataset.gameDir.isEmpty() && keys.contains(dataset.versionKey)
           && QFileInfo(dataset.datPath).dir().dirName().compare(QStringLiteral("dat"), Qt::CaseInsensitive) == 0;
}

QStringList modsFolders(const GameDataset &dataset, const QString &homeDir)
{
    if (!supportsMods(dataset))
        return {};
    if (dataset.versionKey != QStringLiteral("aoe2de"))
        return {QDir(dataset.gameDir).filePath(QStringLiteral("mods"))};

    // DE keeps mods per player profile, in a folder named after the Steam or
    // Microsoft account ID.
    const QDir games(QDir(homeDir.isEmpty() ? QDir::homePath() : homeDir)
                         .filePath(QStringLiteral("Games/Age of Empires 2 DE")));
    // There can be several: one per account, and "0" before signing in.
    struct Profile
    {
        QString mods;
        bool used;
        QDateTime changed;
    };
    QList<Profile> profiles;
    for (const QString &profile : games.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        const QFileInfo mods(QDir(games.filePath(profile)).filePath(QStringLiteral("mods")));
        if (mods.isDir())
            profiles.append({mods.filePath(), QFileInfo::exists(QDir(mods.filePath()).filePath(
                                                  QStringLiteral("mod-status.json"))),
                             mods.lastModified()});
    }
    std::stable_sort(profiles.begin(), profiles.end(), [](const Profile &a, const Profile &b) {
        return a.used != b.used ? a.used : a.changed > b.changed;
    });
    QStringList folders;
    for (const Profile &profile : profiles)
        folders << QDir(profile.mods).filePath(QStringLiteral("local"));
    return folders;
}

QList<Mod> findMods(const QString &modsFolder)
{
    QList<Mod> mods;
    if (modsFolder.isEmpty())
        return mods;
    const QDir dir(modsFolder);
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        const QString modDir = QDir::cleanPath(dir.absoluteFilePath(entry));
        mods.append({modDir, readModInfo(modDir)});
    }
    std::sort(mods.begin(), mods.end(), [](const Mod &a, const Mod &b) {
        return a.info.title.compare(b.info.title, Qt::CaseInsensitive) < 0;
    });
    return mods;
}

ModInfo readModInfo(const QString &modDir)
{
    const QJsonObject json = readInfoJson(modDir);
    ModInfo info;
    info.title = json.value(QStringLiteral("Title")).toString();
    info.author = json.value(QStringLiteral("Author")).toString();
    info.description = json.value(QStringLiteral("Description")).toString();
    if (info.title.isEmpty())
        info.title = QFileInfo(modDir).fileName();
    return info;
}

bool writeModInfo(const QString &modDir, const ModInfo &info, QString *error)
{
    QJsonObject json = readInfoJson(modDir);
    // CacheStatus 0 is what DE writes for its own local mods.
    if (!json.contains(QStringLiteral("CacheStatus")))
        json.insert(QStringLiteral("CacheStatus"), 0);
    json.insert(QStringLiteral("Author"), info.author);
    json.insert(QStringLiteral("Description"), info.description);
    json.insert(QStringLiteral("Title"), info.title);

    QSaveFile file(QDir(modDir).filePath(kInfoFile));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(json).toJson(QJsonDocument::Compact)) < 0
        || !file.commit())
    {
        if (error)
            *error = QStringLiteral("Couldn't write %1: %2")
                         .arg(QDir::toNativeSeparators(QDir(modDir).filePath(kInfoFile)), file.errorString());
        return false;
    }
    return true;
}

QString checkModTitle(const QString &modsFolder, const QString &title)
{
    if (title.trimmed().isEmpty())
        return QStringLiteral("Enter a name.");
    if (title != title.trimmed())
        return QStringLiteral("The name can't start or end with a space.");
    if (title.endsWith(QLatin1Char('.')))
        return QStringLiteral("The name can't end with a dot.");
    static const QRegularExpression badChars(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    if (title.contains(badChars))
        return QStringLiteral("The name can't contain any of < > : \" / \\ | ? *");
    // Windows device names, also with an extension ("nul.txt").
    static const QRegularExpression reserved(QStringLiteral(R"(^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\..*)?$)"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(title).hasMatch())
        return QStringLiteral("\"%1\" is reserved by Windows.").arg(title);

    const QStringList existing = QDir(modsFolder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    const bool taken = std::any_of(existing.begin(), existing.end(), [&](const QString &entry) {
        return entry.compare(title, Qt::CaseInsensitive) == 0;
    });
    if (taken)
        return QStringLiteral("There is already a mod named \"%1\".").arg(title);
    return {};
}

QString createMod(const QString &modsFolder, const ModInfo &info, QString *error)
{
    const auto fail = [&](const QString &reason) {
        if (error)
            *error = reason;
        return QString();
    };

    if (const QString problem = checkModTitle(modsFolder, info.title); !problem.isEmpty())
        return fail(problem);
    const QString modDir = QDir::cleanPath(QDir(modsFolder).absoluteFilePath(info.title));
    if (!QDir().mkpath(modDir))
        return fail(QStringLiteral("Couldn't create the folder %1.").arg(QDir::toNativeSeparators(modDir)));

    QString reason;
    if (!writeModInfo(modDir, info, &reason))
    {
        QDir(modDir).removeRecursively();
        return fail(reason);
    }
    return modDir;
}

bool isGameDataFile(const GameDataset &dataset, const QString &path)
{
    return !dataset.gameDir.isEmpty()
           && isInside(path, QDir(dataset.gameDir).filePath(QStringLiteral("resources")));
}

QString modDatPath(const GameDataset &dataset, const QString &modDir)
{
    return QDir::cleanPath(QDir(modDir).filePath(kDatFolder + QLatin1Char('/') + QFileInfo(dataset.datPath).fileName()));
}

GameDataset modDataset(const GameDataset &dataset, const QString &modDir, const QString &locale)
{
    GameDataset mod = dataset;
    mod.title = QStringLiteral("%1: %2").arg(readModInfo(modDir).title, dataset.title);
    mod.datPath = modDatPath(dataset, modDir);
    mod.languageFiles = keyValueFiles(modDir, locale) + dataset.languageFiles;
    return mod;
}

} // namespace newage
