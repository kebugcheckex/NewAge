#pragma once

#include <optional>

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "core/GameInstall.h"
#include "core/Session.h"
#include "model/EntityKind.h"

namespace newage {

// Where data is read from, and where a write would go. Empty strings are
// unset. A loose `datPath` is read-only and ignores `gameDir`. Otherwise
// `gameDir` is detected with detectInstall; `dataset` picks a file name when
// the folder has several (default: the newest). `mod` is a mod title or an
// existing directory.
struct DataSource
{
    QString gameDir;
    QString dataset;
    QString mod;
    QString modsFolder;
    QString datPath;
    QString versionKey;
    QString locale = QStringLiteral("en");
};

// Fills empty game, dataset, mod and mods-folder fields from NEWAGE_GAME,
// NEWAGE_DATASET, NEWAGE_MOD and NEWAGE_MODS_FOLDER. open() does not do this,
// so a caller that wants the process environment asks for it.
DataSource resolveSource(DataSource source);

// One assignment. `civ` is required for a per-civ kind and ignored otherwise.
// `expect`, when set, is compare-and-set against the loaded value.
struct FieldEdit
{
    QString kind;
    int id = -1;
    int civ = -1;
    QString key;
    QVariant value;
    std::optional<QVariant> expect;
};

// One field an apply would change, or left alone because it already held
// `newValue`. `civ` is -1 for a global kind.
struct FieldChange
{
    QString kind;
    int id = -1;
    int civ = -1;
    QString key;
    QVariant oldValue;
    QVariant newValue;
};

// A failed open or apply. `kind`, `id`, `civ`, `key` and `value` name the edit
// that failed, when there is one.
struct ServiceError
{
    QString code;
    QString message;
    QString kind;
    int id = -1;
    int civ = -1;
    QString key;
    QVariant value;
};

struct OpenResult
{
    bool ok = false;
    ServiceError error;
    QStringList warnings;
};

struct ApplyResult
{
    bool ok = false;
    ServiceError error;
    // Path written. Empty when nothing changed, on a dry run, or on failure.
    QString saved;
    QList<FieldChange> changes;
    QList<FieldChange> unchanged;
};

// Owns one Session and is the only way a caller outside the GUI opens data
// and writes it. Writes go to the open mod's .dat, never to a game file.
class DataService
{
public:
    DataService() = default;

    DataService(const DataService &) = delete;
    DataService &operator=(const DataService &) = delete;

    OpenResult open(const DataSource &source);
    void close();

    bool isOpen() const { return session_.isOpen(); }
    Session &session() { return session_; }
    const Session &session() const { return session_; }

    // The game's data set. Empty after a loose-file open.
    const GameDataset &dataset() const { return gameDataset_; }
    const QString &modDir() const { return modDir_; }
    // Where apply() would write. Empty when the source is read-only.
    const QString &savePath() const { return savePath_; }
    const QStringList &warnings() const { return warnings_; }

    QList<EntityKind *> kinds() const { return entityKinds(); }
    EntityKind *kind(const QString &key) const { return findEntityKind(key); }

    // Checks every edit against the loaded data, then stores the ones that
    // change a value, then saves once. A failed check writes nothing. A dry
    // run reports the same change list and does not save.
    ApplyResult apply(const QList<FieldEdit> &edits, bool dryRun = false);

private:
    ServiceError writeGuard() const;

    Session session_;
    GameDataset gameDataset_;
    QString modDir_;
    QString savePath_;
    QStringList warnings_;
    bool loose_ = false;
};

} // namespace newage
