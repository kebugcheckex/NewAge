#include "model/TechListModel.h"

#include "core/Session.h"
#include "genie/dat/DatFile.h"
#include "model/EntityKind.h"
#include "model/FieldTreeModel.h"
#include "model/RefNames.h"
#include "model/TechFields.h"

namespace newage {

TechListModel::TechListModel(Session *session, QObject *parent)
    : EntityListModel(session, parent)
{
}

void TechListModel::civChanged()
{
    availability_.clear();
    if (!hasCiv())
        return;

    availability_ = techAvailability(*session(), civ());
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
    if (!hasCiv())
        return {};
    return techKind().name(*session(), civ(), row);
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
        const SetResult result = techKind().set(*session(), civ(), row, techFields().at(field).key, value);
        if (!result.ok)
            return {};
        if (result.changed)
            entityEdited(row);
        return result.newValue;
    };
    const auto refNamer = [this](RefKind kind, int id) { return refName(*session(), kind, id, civ()); };
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
    return hasCiv() && techKind().isActive(*session(), civ(), row);
}

QString TechListModel::internalName(int row) const
{
    if (!hasCiv())
        return {};
    return techKind().internalName(*session(), civ(), row);
}

} // namespace newage
