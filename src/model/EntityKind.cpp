#include "model/EntityKind.h"

#include <cmath>

#include <QSet>

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/EffectFields.h"
#include "model/RefNames.h"
#include "model/TechFields.h"
#include "model/UnitFields.h"
#include "model/UnitNames.h"

namespace newage {

namespace {

// Effect command type "disable tech"; D is the tech ID.
constexpr int kDisableTech = 102;

SetResult rejected(const QString &code, const QString &message)
{
    SetResult result;
    result.code = code;
    result.message = message;
    return result;
}

SetResult unchanged(const QVariant &value)
{
    SetResult result;
    result.ok = true;
    result.oldValue = value;
    result.newValue = value;
    return result;
}

QString fieldTypeName(const QVariant &value)
{
    switch (value.typeId())
    {
    case QMetaType::Float:
    case QMetaType::Double:
        return QStringLiteral("float");
    case QMetaType::QString:
        return QStringLiteral("string");
    default:
        return QStringLiteral("int");
    }
}

QString labelOrUnnamed(const QString &name)
{
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}

bool openCiv(const Session &session, int civ)
{
    return session.isOpen() && civ >= 0 && civ < static_cast<int>(session.dat()->Civs.size());
}

TechAvailability classify(int techCiv, int civ, bool disabled)
{
    if (techCiv >= 0 && techCiv != civ)
        return TechAvailability::OtherCiv;
    if (disabled)
        return TechAvailability::DisabledByTechTree;
    return TechAvailability::Available;
}

QSet<int> techsDisabledByTree(const genie::DatFile &dat, int civ)
{
    QSet<int> disabled;
    const int techTree = dat.Civs.at(civ).TechTreeID;
    if (techTree < 0 || techTree >= static_cast<int>(dat.Effects.size()))
        return disabled;
    for (const genie::EffectCommand &command : dat.Effects[techTree].EffectCommands)
    {
        if (command.Type == kDisableTech)
            disabled.insert(static_cast<int>(std::lround(command.D)));
    }
    return disabled;
}

template <typename T>
QList<FieldValue> snapshot(const QList<FieldDesc<T>> &fields, const T &object, const Session &session, int civ)
{
    QList<FieldValue> rows;
    for (const FieldDesc<T> &field : fields)
    {
        if (field.applies && !field.applies(object))
            continue;
        FieldValue row;
        row.key = field.key;
        row.name = field.name;
        row.group = field.group;
        row.value = field.get(object);
        row.type = fieldTypeName(row.value);
        row.editable = static_cast<bool>(field.set);
        if (row.editable && row.type == QLatin1String("int"))
        {
            row.minimum = field.minimum;
            row.maximum = field.maximum;
        }
        row.labelKind = field.ref;
        if (field.ref != RefKind::None)
            row.label = refName(session, field.ref, row.value.toInt(), civ);
        if (field.isStringId)
            row.text = session.names().text(row.value.toInt());
        rows.append(row);
    }
    return rows;
}

template <typename T>
SetResult store(Session &session, T &object, const QList<FieldDesc<T>> &fields, const QString &key,
                const QVariant &input, bool commit)
{
    const FieldDesc<T> *found = nullptr;
    for (const FieldDesc<T> &field : fields)
    {
        if (field.key == key)
        {
            found = &field;
            break;
        }
    }
    if (!found)
        return rejected(QStringLiteral("unknown_field"), key);
    if (found->applies && !found->applies(object))
        return rejected(QStringLiteral("not_applicable"), key);
    if (!found->set)
        return rejected(QStringLiteral("read_only"), key);

    const QVariant current = found->get(object);
    const int typeId = current.typeId() == QMetaType::Float ? QMetaType::Float : QMetaType::Int;
    const ParsedField parsed = parseFieldValue({typeId, found->minimum, found->maximum}, input);
    if (!parsed.code.isEmpty())
    {
        const QString message = parsed.code == QLatin1String("out_of_range")
                                    ? QStringLiteral("%1 %2").arg(key, parsed.message)
                                    : parsed.message;
        return rejected(parsed.code, message);
    }
    if (parsed.value == current)
        return unchanged(current);

    SetResult result;
    result.ok = true;
    result.changed = true;
    result.oldValue = current;
    result.newValue = parsed.value;
    if (!commit)
        return result;

    found->set(object, parsed.value);
    session.setModified(true);
    result.newValue = found->get(object);
    return result;
}

class CivKind : public EntityKind
{
public:
    QString key() const override { return QStringLiteral("civ"); }
    bool perCiv() const override { return false; }

    int count(const Session &session, int) const override
    {
        return session.isOpen() ? static_cast<int>(session.dat()->Civs.size()) : 0;
    }

    bool isActive(const Session &session, int, int id) const override
    {
        return session.isOpen() && id >= 0 && id < count(session, 0);
    }

    QString name(const Session &session, int civ, int id) const override
    {
        if (!isActive(session, civ, id))
            return {};
        return labelOrUnnamed(internalName(session, civ, id));
    }

    QString internalName(const Session &session, int, int id) const override
    {
        if (!isActive(session, 0, id))
            return {};
        return QString::fromLatin1(session.dat()->Civs[id].Name);
    }

    QList<FieldValue> fields(const Session &, int, int) const override { return {}; }

    SetResult set(Session &session, int civ, int id, const QString &key, const QVariant &, bool) override
    {
        if (!isActive(session, civ, id))
            return rejected(QStringLiteral("unknown_entity"), QStringLiteral("no such entity"));
        return rejected(QStringLiteral("unknown_field"), key);
    }
};

class UnitKind : public EntityKind
{
public:
    QString key() const override { return QStringLiteral("unit"); }
    bool perCiv() const override { return true; }

    int count(const Session &session, int civ) const override
    {
        return openCiv(session, civ) ? static_cast<int>(session.dat()->Civs[civ].Units.size()) : 0;
    }

    bool isActive(const Session &session, int civ, int id) const override
    {
        if (!openCiv(session, civ) || id < 0)
            return false;
        const genie::Civ &selected = session.dat()->Civs[civ];
        return id < static_cast<int>(selected.Units.size()) && id < static_cast<int>(selected.UnitPointers.size())
               && selected.UnitPointers[id] != 0;
    }

    QString name(const Session &session, int civ, int id) const override
    {
        if (!isActive(session, civ, id))
            return QStringLiteral("(empty)");
        return labelOrUnnamed(unitName(session, civ, id));
    }

    QString internalName(const Session &session, int civ, int id) const override
    {
        if (!isActive(session, civ, id))
            return {};
        return QString::fromLatin1(session.dat()->Civs[civ].Units[id].Name);
    }

    QList<FieldValue> fields(const Session &session, int civ, int id) const override
    {
        if (!isActive(session, civ, id))
            return {};
        return snapshot(unitFields(), session.dat()->Civs[civ].Units[id], session, civ);
    }

    SetResult set(Session &session, int civ, int id, const QString &key, const QVariant &value, bool commit) override
    {
        if (!openCiv(session, civ) || id < 0 || id >= count(session, civ))
            return rejected(QStringLiteral("unknown_entity"), QStringLiteral("no such entity"));
        if (!isActive(session, civ, id))
            return rejected(QStringLiteral("inactive_entity"), QStringLiteral("inactive"));
        return store(session, session.dat()->Civs[civ].Units[id], unitFields(), key, value, commit);
    }
};

class TechKind : public EntityKind
{
public:
    QString key() const override { return QStringLiteral("tech"); }
    bool perCiv() const override { return false; }

    int count(const Session &session, int) const override
    {
        return session.isOpen() ? static_cast<int>(session.dat()->Techs.size()) : 0;
    }

    bool isActive(const Session &session, int civ, int id) const override
    {
        if (!session.isOpen() || !openCiv(session, civ) || id < 0 || id >= count(session, civ))
            return false;
        return classify(session.dat()->Techs[id].Civ, civ, techsDisabledByTree(*session.dat(), civ).contains(id))
               == TechAvailability::Available;
    }

    QString name(const Session &session, int civ, int id) const override
    {
        if (id < 0 || id >= count(session, civ))
            return {};
        const genie::Tech &tech = session.dat()->Techs[id];
        if (const QString text = session.names().text(tech.LanguageDLLName); !text.isEmpty())
            return text;
        return labelOrUnnamed(QString::fromLatin1(tech.Name));
    }

    QString internalName(const Session &session, int civ, int id) const override
    {
        if (id < 0 || id >= count(session, civ))
            return {};
        return QString::fromLatin1(session.dat()->Techs[id].Name);
    }

    QList<FieldValue> fields(const Session &session, int civ, int id) const override
    {
        if (id < 0 || id >= count(session, civ))
            return {};
        TechRef ref{id, session.dat()->Techs[id]};
        return snapshot(techFields(), ref, session, civ);
    }

    SetResult set(Session &session, int civ, int id, const QString &key, const QVariant &value, bool commit) override
    {
        if (id < 0 || id >= count(session, civ))
            return rejected(QStringLiteral("unknown_entity"), QStringLiteral("no such entity"));
        TechRef ref{id, session.dat()->Techs[id]};
        return store(session, ref, techFields(), key, value, commit);
    }
};

class EffectKind : public EntityKind
{
public:
    QString key() const override { return QStringLiteral("effect"); }
    bool perCiv() const override { return false; }

    int count(const Session &session, int) const override
    {
        return session.isOpen() ? static_cast<int>(session.dat()->Effects.size()) : 0;
    }

    bool isActive(const Session &session, int, int id) const override
    {
        return session.isOpen() && id >= 0 && id < count(session, 0);
    }

    QString name(const Session &session, int civ, int id) const override
    {
        if (!isActive(session, civ, id))
            return {};
        return labelOrUnnamed(internalName(session, civ, id));
    }

    QString internalName(const Session &session, int, int id) const override
    {
        if (!isActive(session, 0, id))
            return {};
        return QString::fromLatin1(session.dat()->Effects[id].Name);
    }

    QList<FieldValue> fields(const Session &session, int civ, int id) const override
    {
        if (!isActive(session, civ, id))
            return {};
        const genie::Effect &effect = session.dat()->Effects[id];
        EffectRef ref{id, session.dat()->Effects[id]};
        return snapshot(effectFields(effect, session.gameVersion()), ref, session, civ);
    }

    SetResult set(Session &session, int civ, int id, const QString &key, const QVariant &value, bool commit) override
    {
        if (!isActive(session, civ, id))
            return rejected(QStringLiteral("unknown_entity"), QStringLiteral("no such entity"));
        EffectRef ref{id, session.dat()->Effects[id]};
        return store(session, ref, effectFields(session.dat()->Effects[id], session.gameVersion()), key, value, commit);
    }
};

CivKind &civInstance()
{
    static CivKind kind;
    return kind;
}

UnitKind &unitInstance()
{
    static UnitKind kind;
    return kind;
}

TechKind &techInstance()
{
    static TechKind kind;
    return kind;
}

EffectKind &effectInstance()
{
    static EffectKind kind;
    return kind;
}

} // namespace

QList<TechAvailability> techAvailability(const Session &session, int civ)
{
    if (!openCiv(session, civ))
        return {};
    const genie::DatFile &dat = *session.dat();
    const QSet<int> disabled = techsDisabledByTree(dat, civ);
    QList<TechAvailability> table;
    table.reserve(static_cast<qsizetype>(dat.Techs.size()));
    for (qsizetype id = 0; id < static_cast<qsizetype>(dat.Techs.size()); ++id)
        table.append(classify(dat.Techs[id].Civ, civ, disabled.contains(static_cast<int>(id))));
    return table;
}

QList<EntityKind *> entityKinds()
{
    return {&civKind(), &unitKind(), &techKind(), &effectKind()};
}

EntityKind *findEntityKind(const QString &key)
{
    for (EntityKind *kind : entityKinds())
    {
        if (kind->key() == key)
            return kind;
    }
    return nullptr;
}

EntityKind &civKind()
{
    return civInstance();
}

EntityKind &unitKind()
{
    return unitInstance();
}

EntityKind &techKind()
{
    return techInstance();
}

EntityKind &effectKind()
{
    return effectInstance();
}

} // namespace newage
