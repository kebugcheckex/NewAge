#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace newage {

// User preferences, stored as a JSON file of sections:
//
//   { "unitList": { "hideEmpty": true } }
//
// Missing or mistyped entries read as their defaults. Entries this build
// doesn't know about are kept and written back on save, so older and newer
// builds can share one file.
class Config : public QObject
{
    Q_OBJECT

public:
    explicit Config(const QString &path, QObject *parent = nullptr);

    // config.json in the per-user application config directory.
    static QString defaultPath();

    const QString &path() const { return path_; }

    // Replaces all entries with the file's. A missing file is not an error and
    // leaves every entry at its default. On failure the entries are cleared
    // (defaults again) and `error` (if given) describes why.
    bool load(QString *error = nullptr);

    // Writes all entries, creating the directory if needed.
    bool save(QString *error = nullptr) const;

    // Unit list: leave out unit slots that hold no unit.
    bool hideEmptyUnits() const;
    void setHideEmptyUnits(bool hide);

signals:
    // Emitted after load() and after any setter that changes a value.
    void changed();

private:
    QJsonValue value(const QString &section, const QString &key) const;
    void setValue(const QString &section, const QString &key, const QJsonValue &value);

    QString path_;
    QJsonObject root_;
};

} // namespace newage
