#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>

#include "core/NameProvider.h"
#include "core/SpriteLibrary.h"
#include "genie/Types.h"

namespace genie {
class DatFile;
}

namespace newage {

struct GameDataset;
struct VersionProfile;

// Result of reading a data set off the session. Safe to build on a worker
// thread: it holds the loaded data and does not touch a Session.
class LoadResult
{
public:
    LoadResult();
    ~LoadResult();
    LoadResult(LoadResult &&) noexcept;
    LoadResult &operator=(LoadResult &&) noexcept;
    LoadResult(const LoadResult &) = delete;
    LoadResult &operator=(const LoadResult &) = delete;

    bool ok = false;
    QString error;
    QStringList warnings;

private:
    friend class Session;
    std::unique_ptr<genie::DatFile> dat;
    NameProvider names;
    genie::GameVersion version = genie::GV_None;
    QString datPath;
    QString gameDir;
};

// Owns the currently open game data and tracks whether it has unsaved edits.
// Everything that reads or writes genie data goes through here; the UI never
// holds a DatFile of its own.
class Session : public QObject
{
    Q_OBJECT

public:
    explicit Session(QObject *parent = nullptr);
    ~Session() override;

    // Replaces any open data. On failure the session is left closed and
    // `error` (if given) describes why. Opened this way there are no language
    // strings, so names() is empty.
    bool open(const QString &datPath, const VersionProfile &profile, QString *error = nullptr);

    // Opens a data set found in a game folder, together with its language
    // files. Language files that can't be read don't fail the open; they are
    // listed in `warnings` (if given) and left out of names().
    bool open(const GameDataset &dataset, QString *error = nullptr, QStringList *warnings = nullptr);

    // Reads a .dat (and, for a data set, its language files) without touching
    // this session or emitting. Safe to call from a worker thread; not safe to
    // run two at once (genieutils keeps a process-wide terrain count).
    // `progress` is called on that thread before each stage: index 0 is the
    // data file, and the rest are language files. `count` includes the data file.
    // adopt() installs a successful result and emits opened().
    using LoadProgress = std::function<void(int index, int count)>;
    static LoadResult read(const QString &datPath, const VersionProfile &profile, const LoadProgress &progress = {});
    static LoadResult read(const GameDataset &dataset, const LoadProgress &progress = {});
    void adopt(LoadResult result);

    // Writes the data in the same format it was loaded with. The data goes to
    // a temporary file next to `datPath` first, which then replaces it, so a
    // failed save leaves an existing file as it was.
    bool saveAs(const QString &datPath, QString *error = nullptr);
    // saveAs() to datPath().
    bool save(QString *error = nullptr) { return saveAs(datPath_, error); }

    void close();

    bool isOpen() const { return dat_ != nullptr; }
    genie::DatFile *dat() const { return dat_.get(); }
    genie::GameVersion gameVersion() const { return gameVersion_; }
    const QString &datPath() const { return datPath_; }
    // Language strings of the open data; empty when none were loaded.
    const NameProvider &names() const { return names_; }
    // Interface sprites from the data set's game folder, so a mod's .dat gets
    // the game's sprites, or else from an install around the opened .dat.
    // Empty, and frame() null, when neither has any. Not retargeted by Save As.
    SpriteLibrary &sprites() { return sprites_; }
    const SpriteLibrary &sprites() const { return sprites_; }

    bool isModified() const { return modified_; }
    void setModified(bool modified);

signals:
    void opened();
    void closed();
    void modifiedChanged(bool modified);

private:
    std::unique_ptr<genie::DatFile> dat_;
    genie::GameVersion gameVersion_ = genie::GV_None;
    QString datPath_;
    NameProvider names_;
    SpriteLibrary sprites_;
    bool modified_ = false;
};

} // namespace newage
