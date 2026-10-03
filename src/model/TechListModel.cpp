#include "model/TechListModel.h"

#include <cmath>

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/FieldTreeModel.h"
#include "model/ResourceNames.h"
#include "model/TechFields.h"

namespace newage {

namespace {

// Effect command type "disable tech"; D is the tech ID.
constexpr int kDisableTech = 102;

// Language name, else internal name, of unit `id` in `civ`. Empty when the slot
// is unused or unnamed, so the view keeps the plain ID.
QString unitName(const Session &session, int civ, int id)
{
    if (civ < 0 || id < 0)
        return {};
    const auto &civs = session.dat()->Civs;
    if (civ >= static_cast<int>(civs.size()))
        return {};
    const genie::Civ &selected = civs[civ];
    if (id >= static_cast<int>(selected.Units.size()) || id >= static_cast<int>(selected.UnitPointers.size())
        || selected.UnitPointers[id] == 0)
        return {};
    const genie::Unit &unit = selected.Units[id];
    if (const QString name = session.names().text(unit.LanguageDLLName); !name.isEmpty())
        return name;
    if (!unit.Name.empty())
        return QString::fromLatin1(unit.Name);
    return {};
}

} // namespace

TechListModel::TechListModel(Session *session, QObject *parent)
    : EntityListModel(session, parent)
{
}

void TechListModel::civChanged()
{
    availability_.clear();
    if (!hasCiv())
        return;

    const genie::DatFile &dat = *session()->dat();
    availability_.fill(Availability::Available, static_cast<qsizetype>(dat.Techs.size()));
    for (qsizetype id = 0; id < availability_.size(); ++id)
    {
        const int techCiv = dat.Techs[id].Civ;
        if (techCiv >= 0 && techCiv != civ())
            availability_[id] = Availability::OtherCiv;
    }

    const int techTree = dat.Civs.at(civ()).TechTreeID;
    if (techTree < 0 || techTree >= static_cast<int>(dat.Effects.size()))
        return;
    for (const genie::EffectCommand &command : dat.Effects[techTree].EffectCommands)
    {
        if (command.Type != kDisableTech)
            continue;
        const auto id = static_cast<qsizetype>(std::lround(command.D));
        // A tech of another civ is reported as that, the more telling reason.
        if (id >= 0 && id < availability_.size() && availability_[id] == Availability::Available)
            availability_[id] = Availability::DisabledByTechTree;
    }
}

const genie::Tech *TechListModel::tech(int row) const
{
    if (row < 0 || row >= rowCount())
        return nullptr;
    return &session()->dat()->Techs.at(row);
}

TechListModel::Availability TechListModel::availability(int row) const
{
    return row >= 0 && row < availability_.size() ? availability_.at(row) : Availability::Available;
}

int TechListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !hasCiv())
        return 0;
    return static_cast<int>(availability_.size());
}

QString TechListModel::name(int row) const
{
    const genie::Tech *t = tech(row);
    if (!t)
        return {};
    if (const QString name = session()->names().text(t->LanguageDLLName); !name.isEmpty())
        return name;
    if (!t->Name.empty())
        return QString::fromLatin1(t->Name);
    return tr("(unnamed)");
}

void TechListModel::showFields(int row, FieldTreeModel &fields)
{
    if (!tech(row))
    {
        fields.clear();
        return;
    }

    // Techs are global, so the writer doesn't depend on the civ. It looks the
    // tech up again on every write, so it never holds a pointer into the data.
    const auto writer = [this, row](int field, const QVariant &value) -> QVariant {
        if (!tech(row))
            return {};
        TechRef target{row, session()->dat()->Techs.at(row)};
        const FieldDesc<TechRef> &desc = techFields().at(field);
        desc.set(target, value);
        entityEdited(row);
        return desc.get(target);
    };
    const auto refNamer = [this](RefKind kind, int id) {
        switch (kind)
        {
        case RefKind::Tech: return name(id);
        case RefKind::Resource: return resourceName(session()->gameVersion(), id);
        case RefKind::Civ:
        {
            const auto &civs = session()->dat()->Civs;
            if (id < 0 || id >= static_cast<int>(civs.size()) || civs[id].Name.empty())
                return QString();
            return QString::fromLatin1(civs[id].Name);
        }
        case RefKind::Effect:
        {
            const auto &effects = session()->dat()->Effects;
            if (id < 0 || id >= static_cast<int>(effects.size()) || effects[id].Name.empty())
                return QString();
            return QString::fromLatin1(effects[id].Name);
        }
        case RefKind::Unit: return unitName(*session(), civ(), id);
        default: return QString();
        }
    };
    fields.setObject(techFields(), TechRef{row, session()->dat()->Techs.at(row)}, &session()->names(), writer,
                     refNamer);
}

QVariant TechListModel::data(const QModelIndex &index, int role) const
{
    if (role != Qt::ToolTipRole || !index.isValid())
        return EntityListModel::data(index, role);

    QString tip = internalName(index.row());
    QString reason;
    switch (availability(index.row()))
    {
    case Availability::Available:
        break;
    case Availability::OtherCiv:
    {
        const int techCiv = tech(index.row())->Civ;
        const auto &civs = session()->dat()->Civs;
        reason = techCiv < static_cast<int>(civs.size())
                     ? tr("Only for civ %1 - %2").arg(techCiv).arg(QString::fromLatin1(civs[techCiv].Name))
                     : tr("Only for civ %1").arg(techCiv);
        break;
    }
    case Availability::DisabledByTechTree:
        reason = tr("Disabled by this civ's tech tree");
        break;
    }
    if (!reason.isEmpty())
        tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) + reason;
    return tip.isEmpty() ? QVariant() : QVariant(tip);
}

bool TechListModel::isActive(int row) const
{
    return tech(row) && availability(row) == Availability::Available;
}

QString TechListModel::internalName(int row) const
{
    const genie::Tech *t = tech(row);
    return t ? QString::fromLatin1(t->Name) : QString();
}

} // namespace newage
