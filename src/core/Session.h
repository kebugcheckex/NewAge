#pragma once

#include <memory>

#include <QObject>
#include <QString>

#include "genie/Types.h"

namespace genie {
class DatFile;
}

namespace newage {

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
    // `error` (if given) describes why.
    bool open(const QString &datPath, const VersionProfile &profile, QString *error = nullptr);

    // Writes the data in the same format it was loaded with.
    bool saveAs(const QString &datPath, QString *error = nullptr);

    void close();

    bool isOpen() const { return dat_ != nullptr; }
    genie::DatFile *dat() const { return dat_.get(); }
    genie::GameVersion gameVersion() const { return gameVersion_; }
    const QString &datPath() const { return datPath_; }

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
    bool modified_ = false;
};

} // namespace newage
