#include "api/DataService.h"

#include <QDir>
#include <QFileInfo>

#include "core/Mods.h"
#include "core/VersionProfile.h"

namespace newage {

namespace {

ServiceError makeError(const QString &code, const QString &message)
{
    ServiceError error;
    error.code = code;
    error.message = message;
    return error;
}

ServiceError editError(const FieldEdit &edit, const QString &code, const QString &message)
{
    ServiceError error = makeError(code, message);
    error.kind = edit.kind;
    error.id = edit.id;
    error.civ = edit.civ;
    error.key = edit.key;
    error.value = edit.value;
    return error;
}

bool sameStored(const QVariant &current, const QVariant &expected)
{
    if (current == expected)
        return true;
    if (current.typeId() == QMetaType::Float || current.typeId() == QMetaType::Double)
    {
        bool ok = false;
        const double number = expected.toDouble(&ok);
        return ok && static_cast<float>(number) == current.toFloat();
    }
    bool ok = false;
    const qlonglong number = expected.toLongLong(&ok);
    return ok && number == current.toLongLong();
}

QString resolveModDir(const GameDataset &dataset, const QString &mod, const QString &modsFolder)
{
    if (QFileInfo(mod).isDir())
        return QDir::cleanPath(QFileInfo(mod).absoluteFilePath());
    if (mod.contains(QLatin1Char('/')) || mod.contains(QLatin1Char('\\')) || QDir::isAbsolutePath(mod))
        return {};

    QString folder = modsFolder;
    if (folder.isEmpty())
    {
        const QStringList folders = modsFolders(dataset);
        if (folders.isEmpty())
            return {};
        folder = folders.first();
    }
    for (const Mod &entry : findMods(folder))
    {
        if (entry.info.title.compare(mod, Qt::CaseInsensitive) == 0)
            return entry.dir;
    }
    return {};
}

const GameDataset *selectDataset(const QList<GameDataset> &datasets, const QString &fileName)
{
    if (fileName.isEmpty())
        return datasets.isEmpty() ? nullptr : &datasets.first();
    const QString wanted = QFileInfo(fileName).fileName();
    for (const GameDataset &dataset : datasets)
    {
        if (QFileInfo(dataset.datPath).fileName().compare(wanted, Qt::CaseInsensitive) == 0)
            return &dataset;
    }
    return nullptr;
}

} // namespace

DataSource resolveSource(DataSource source)
{
    if (source.gameDir.isEmpty())
        source.gameDir = qEnvironmentVariable("NEWAGE_GAME");
    if (source.dataset.isEmpty())
        source.dataset = qEnvironmentVariable("NEWAGE_DATASET");
    if (source.mod.isEmpty())
        source.mod = qEnvironmentVariable("NEWAGE_MOD");
    if (source.modsFolder.isEmpty())
        source.modsFolder = qEnvironmentVariable("NEWAGE_MODS_FOLDER");
    if (source.locale.isEmpty())
        source.locale = QStringLiteral("en");
    return source;
}

void DataService::close()
{
    session_.close();
    gameDataset_ = {};
    modDir_.clear();
    savePath_.clear();
    warnings_.clear();
    loose_ = false;
}

OpenResult DataService::open(const DataSource &source)
{
    close();
    DataSource in = source;
    if (in.locale.isEmpty())
        in.locale = QStringLiteral("en");

    const auto fail = [&](const QString &code, const QString &message) {
        close();
        OpenResult result;
        result.error = makeError(code, message);
        return result;
    };

    if (!in.datPath.isEmpty())
    {
        if (!in.mod.isEmpty())
            return fail(QStringLiteral("mods_unsupported"), QStringLiteral("A loose data file is read-only."));
        const VersionProfile *profile = findVersionProfile(in.versionKey);
        if (!profile)
        {
            const QString message = in.versionKey.isEmpty()
                                        ? QStringLiteral("A loose data file needs a version.")
                                        : QStringLiteral("Unknown game version \"%1\".").arg(in.versionKey);
            return fail(QStringLiteral("load_failed"), message);
        }
        QString error;
        if (!session_.open(in.datPath, *profile, &error))
            return fail(QStringLiteral("load_failed"), error);
        loose_ = true;
        OpenResult result;
        result.ok = true;
        return result;
    }

    if (in.gameDir.isEmpty())
        return fail(QStringLiteral("no_dataset"), QStringLiteral("No game folder or data file."));

    const QList<GameDataset> datasets = detectInstall(in.gameDir, in.locale);
    if (datasets.isEmpty())
        return fail(QStringLiteral("no_dataset"),
                    QStringLiteral("No data set in %1.").arg(QDir::toNativeSeparators(in.gameDir)));
    const GameDataset *chosen = selectDataset(datasets, in.dataset);
    if (!chosen)
        return fail(QStringLiteral("no_dataset"),
                    QStringLiteral("No data set named \"%1\".").arg(QFileInfo(in.dataset).fileName()));
    gameDataset_ = *chosen;

    GameDataset opened = gameDataset_;
    if (!in.mod.isEmpty())
    {
        if (!supportsMods(gameDataset_))
            return fail(QStringLiteral("mods_unsupported"), QStringLiteral("This data set has no mods."));
        const QString dir = resolveModDir(gameDataset_, in.mod, in.modsFolder);
        if (dir.isEmpty())
            return fail(QStringLiteral("mod_not_found"), QStringLiteral("No mod named \"%1\".").arg(in.mod));
        const QString path = modDatPath(gameDataset_, dir);
        if (isGameDataFile(gameDataset_, path))
            return fail(QStringLiteral("game_data_protected"), QStringLiteral("Refusing to write the game's data file."));
        modDir_ = dir;
        savePath_ = path;
        opened = modDataset(gameDataset_, modDir_, in.locale);
        if (!QFileInfo::exists(opened.datPath))
            opened.datPath = gameDataset_.datPath;
    }

    QString error;
    if (!session_.open(opened, &error, &warnings_))
        return fail(QStringLiteral("load_failed"), error);

    OpenResult result;
    result.ok = true;
    result.warnings = warnings_;
    return result;
}

ServiceError DataService::writeGuard() const
{
    if (!session_.isOpen())
        return makeError(QStringLiteral("load_failed"), QStringLiteral("No data is open."));
    if (loose_ || !supportsMods(gameDataset_))
        return makeError(QStringLiteral("mods_unsupported"), QStringLiteral("This data set has no mods."));
    if (modDir_.isEmpty())
        return makeError(QStringLiteral("mod_required"), QStringLiteral("Writing needs a mod."));
    if (savePath_.isEmpty() || isGameDataFile(gameDataset_, savePath_))
        return makeError(QStringLiteral("game_data_protected"), QStringLiteral("Refusing to write the game's data file."));
    return {};
}

ApplyResult DataService::apply(const QList<FieldEdit> &edits, bool dryRun)
{
    ApplyResult result;
    if (const ServiceError blocked = writeGuard(); !blocked.code.isEmpty())
    {
        result.error = blocked;
        return result;
    }

    struct Planned
    {
        FieldEdit edit;
        EntityKind *kind = nullptr;
        QVariant oldValue;
        QVariant newValue;
        bool changed = false;
    };

    const auto toChange = [](const Planned &item) {
        FieldChange change;
        change.kind = item.edit.kind;
        change.id = item.edit.id;
        change.civ = item.kind->perCiv() ? item.edit.civ : -1;
        change.key = item.edit.key;
        change.oldValue = item.oldValue;
        change.newValue = item.newValue;
        return change;
    };

    QList<Planned> planned;
    for (const FieldEdit &edit : edits)
    {
        EntityKind *kind = findEntityKind(edit.kind);
        if (!kind)
        {
            result.error = editError(edit, QStringLiteral("unknown_kind"),
                                     QStringLiteral("Unknown kind \"%1\".").arg(edit.kind));
            return result;
        }
        const SetResult checked = kind->set(session_, edit.civ, edit.id, edit.key, edit.value, false);
        if (!checked.ok)
        {
            result.error = editError(edit, checked.code, checked.message);
            return result;
        }
        if (edit.expect && !sameStored(checked.oldValue, *edit.expect))
        {
            result.error = editError(edit, QStringLiteral("conflict"),
                                     QStringLiteral("%1 is %2, not %3.")
                                         .arg(edit.key, checked.oldValue.toString(), edit.expect->toString()));
            return result;
        }
        planned.append({edit, kind, checked.oldValue, checked.newValue, checked.changed});
    }

    if (dryRun)
    {
        result.ok = true;
        for (const Planned &item : planned)
        {
            if (item.changed)
                result.changes.append(toChange(item));
            else
                result.unchanged.append(toChange(item));
        }
        return result;
    }

    const bool wasModified = session_.isModified();
    QList<Planned> committed;
    const auto revert = [&] {
        for (auto it = committed.rbegin(); it != committed.rend(); ++it)
            it->kind->set(session_, it->edit.civ, it->edit.id, it->edit.key, it->oldValue, true);
        session_.setModified(wasModified);
    };

    for (const Planned &item : planned)
    {
        if (!item.changed)
        {
            result.unchanged.append(toChange(item));
            continue;
        }
        const SetResult stored = item.kind->set(session_, item.edit.civ, item.edit.id, item.edit.key, item.edit.value, true);
        if (!stored.ok)
        {
            revert();
            result.changes.clear();
            result.unchanged.clear();
            result.error = editError(item.edit, stored.code, stored.message);
            return result;
        }
        Planned done = item;
        done.oldValue = stored.oldValue;
        done.newValue = stored.newValue;
        committed.append(done);
        result.changes.append(toChange(done));
    }

    if (committed.isEmpty())
    {
        result.ok = true;
        return result;
    }

    const QString directory = QFileInfo(savePath_).absolutePath();
    if (!QDir().mkpath(directory))
    {
        revert();
        result.changes.clear();
        result.unchanged.clear();
        result.error = makeError(QStringLiteral("save_failed"),
                                 QStringLiteral("Couldn't create %1.").arg(QDir::toNativeSeparators(directory)));
        return result;
    }
    if (isGameDataFile(gameDataset_, savePath_))
    {
        revert();
        result.changes.clear();
        result.unchanged.clear();
        result.error = makeError(QStringLiteral("game_data_protected"),
                                 QStringLiteral("Refusing to write the game's data file."));
        return result;
    }

    QString saveError;
    if (!session_.saveAs(savePath_, &saveError))
    {
        revert();
        result.changes.clear();
        result.unchanged.clear();
        result.error = makeError(QStringLiteral("save_failed"), saveError);
        return result;
    }

    result.ok = true;
    result.saved = QDir::fromNativeSeparators(QDir::cleanPath(savePath_));
    return result;
}

} // namespace newage
