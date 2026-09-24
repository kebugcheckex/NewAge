#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>

#include "core/NameProvider.h"
#include "genie/Types.h"

namespace genie {
class DatFile;
}

namespace newage {

struct GameDataset;
struct VersionProfile;

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

    bool isModified() const { return modified_; }
    void setModified(bool modified);

signals:
    void opened();
    void closed();
    void modifiedChanged(bool modified);

private:
    // Loads the .dat into this closed session without emitting opened().
    bool loadDat(const QString &datPath, const VersionProfile &profile, QString *error);

    std::unique_ptr<genie::DatFile> dat_;
    genie::GameVersion gameVersion_ = genie::GV_None;
    QString datPath_;
    NameProvider names_;
    bool modified_ = false;
};

} // namespace newage
