#include "core/Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace newage {

namespace {

const auto kUnitList = QStringLiteral("unitList");
const auto kHideEmpty = QStringLiteral("hideEmpty");
const auto kTechList = QStringLiteral("techList");
const auto kHideUnavailable = QStringLiteral("hideUnavailable");

} // namespace

Config::Config(const QString &path, QObject *parent)
    : QObject(parent), path_(path)
{
}

QString Config::defaultPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("config.json"));
}

bool Config::load(QString *error)
{
    root_ = {};
    const auto fail = [&](const QString &why) {
        if (error)
            *error = tr("Could not read %1: %2").arg(QDir::toNativeSeparators(path_), why);
        emit changed();
        return false;
    };

    QFile file(path_);
    if (!file.exists())
    {
        emit changed();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(tr("%1 at offset %2").arg(parseError.errorString()).arg(parseError.offset));
    if (!doc.isObject())
        return fail(tr("the top level is not a JSON object"));

    root_ = doc.object();
    emit changed();
    return true;
}

bool Config::save(QString *error) const
{
    const auto fail = [&](const QString &why) {
        if (error)
            *error = tr("Could not write %1: %2").arg(QDir::toNativeSeparators(path_), why);
        return false;
    };

    if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
        return fail(tr("cannot create the directory"));

    // QSaveFile writes to a temporary file and renames it, so a failed write
    // never leaves a truncated config behind.
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly))
        return fail(file.errorString());
    file.write(QJsonDocument(root_).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return fail(file.errorString());
    return true;
}

bool Config::hideEmptyUnits() const
{
    return value(kUnitList, kHideEmpty).toBool(false);
}

void Config::setHideEmptyUnits(bool hide)
{
    setValue(kUnitList, kHideEmpty, hide);
}

bool Config::hideUnavailableTechs() const
{
    return value(kTechList, kHideUnavailable).toBool(false);
}

void Config::setHideUnavailableTechs(bool hide)
{
    setValue(kTechList, kHideUnavailable, hide);
}

QJsonValue Config::value(const QString &section, const QString &key) const
{
    return root_.value(section).toObject().value(key);
}

void Config::setValue(const QString &section, const QString &key, const QJsonValue &value)
{
    QJsonObject object = root_.value(section).toObject();
    if (object.value(key) == value)
        return;
    object.insert(key, value);
    root_.insert(section, object);
    emit changed();
}

} // namespace newage
