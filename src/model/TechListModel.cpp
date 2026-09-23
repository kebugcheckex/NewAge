#include "model/TechListModel.h"

#include <cmath>

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/FieldTreeModel.h"
#include "model/TechFields.h"

namespace newage {

namespace {

// Effect command type "disable tech"; D is the tech ID.
constexpr int kDisableTech = 102;

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

void TechListModel::showFields(int row, FieldTreeModel &fields) const
{
    if (const genie::Tech *t = tech(row))
        fields.setObject(techFields(), TechRef{row, *t}, &session()->names());
    else
        fields.clear();
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
