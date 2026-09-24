#include "model/UnitFields.h"

#include "genie/dat/Unit.h"

namespace newage {

namespace {

using Unit = genie::Unit;

// uint8_t and friends would otherwise land in QVariant as char types.
template <typename Int>
QVariant intValue(Int value)
{
    return QVariant(static_cast<int>(value));
}

bool hasSpeed(const Unit &unit)
{
    // Speed is only stored for Type >= 20 (see genie::Unit::serializeObject).
    return unit.Type >= genie::UT_Flag;
}

bool isCreatable(const Unit &unit)
{
    // The Creatable part (costs, train location) is only stored for Type >= 70.
    return unit.Type >= genie::UT_Creatable;
}

bool hasTrainLocation(const Unit &unit)
{
    return isCreatable(unit) && !unit.Creatable.TrainLocations.empty();
}

} // namespace

QString unitTypeName(int type)
{
    switch (type)
    {
    case genie::UT_EyeCandy: return QStringLiteral("10 - Eye Candy");
    case genie::UT_Trees: return QStringLiteral("15 - Tree (AoK)");
    case genie::UT_Flag: return QStringLiteral("20 - Animated");
    case genie::UT_25: return QStringLiteral("25 - Doppelganger");
    case genie::UT_Dead_Fish: return QStringLiteral("30 - Moving");
    case genie::UT_Bird: return QStringLiteral("40 - Actor");
    case genie::UT_Combatant: return QStringLiteral("50 - Superclass");
    case genie::UT_Projectile: return QStringLiteral("60 - Projectile");
    case genie::UT_Creatable: return QStringLiteral("70 - Combatant");
    case genie::UT_Building: return QStringLiteral("80 - Building");
    case genie::UT_AoeTrees: return QStringLiteral("90 - Tree (AoE)");
    default: return QStringLiteral("%1 - Unknown").arg(type);
    }
}

const QList<FieldDesc<Unit>> &unitFields()
{
    static const QList<FieldDesc<Unit>> fields = [] {
        const QString general = QStringLiteral("General");
        const QString stats = QStringLiteral("Stats");
        const QString costs = QStringLiteral("Costs");
        const QString training = QStringLiteral("Training");
        const QString size = QStringLiteral("Size");
        const QString graphics = QStringLiteral("Graphics");
        const QString flags = QStringLiteral("Flags");

        QList<FieldDesc<Unit>> list = {
            {"ID", general, {}, [](const Unit &u) { return intValue(u.ID); }},
            {"Type", general, {}, [](const Unit &u) { return QVariant(unitTypeName(u.Type)); }},
            {"Class", general, {}, [](const Unit &u) { return intValue(u.Class); }},
            {"Internal name", general, {}, [](const Unit &u) { return QVariant(QString::fromLatin1(u.Name)); }},
            // genieutils widens the 16-bit variants into these on load, so the
            // 32-bit members are valid for every game version.
            {"Language name", general, {}, [](const Unit &u) { return intValue(u.LanguageDLLName); }, true},
            {"Language creation", general, {}, [](const Unit &u) { return intValue(u.LanguageDLLCreation); }, true},

            numberField<Unit>("Hit points", stats, {}, [](auto &u) -> auto & { return u.HitPoints; }),
            numberField<Unit>("Line of sight", stats, {}, [](auto &u) -> auto & { return u.LineOfSight; }),
            numberField<Unit>("Speed", stats, hasSpeed, [](auto &u) -> auto & { return u.Speed; }),
            {"Garrison capacity", stats, {}, [](const Unit &u) { return intValue(u.GarrisonCapacity); }},
            {"Resource capacity", stats, {}, [](const Unit &u) { return intValue(u.ResourceCapacity); }},
        };

        // Always 3 slots (Creatable::getResourceCostsSize).
        for (int i = 0; i < 3; ++i)
        {
            const auto applies = [i](const Unit &u) {
                return isCreatable(u) && i < static_cast<int>(u.Creatable.ResourceCosts.size());
            };
            list.append(numberField<Unit>(QStringLiteral("Cost %1 resource").arg(i + 1), costs, applies,
                                          [i](auto &u) -> auto & { return u.Creatable.ResourceCosts.at(i).Type; }));
            list.append(numberField<Unit>(QStringLiteral("Cost %1 amount").arg(i + 1), costs, applies,
                                          [i](auto &u) -> auto & { return u.Creatable.ResourceCosts.at(i).Amount; }));
            // 1: the amount is paid; 0: it only has to be available.
            list.append(numberField<Unit>(QStringLiteral("Cost %1 paid").arg(i + 1), costs, applies,
                                          [i](auto &u) -> auto & { return u.Creatable.ResourceCosts.at(i).Flag; }));
        }

        // DE can list several train locations; only the first is shown for now.
        list.append({"Train location", training, hasTrainLocation,
                     [](const Unit &u) { return intValue(u.Creatable.TrainLocations.front().LocationID); }});
        list.append(numberField<Unit>("Train time", training, hasTrainLocation,
                                      [](auto &u) -> auto & { return u.Creatable.TrainLocations.front().QueueTime; }));

        list += QList<FieldDesc<Unit>>{
            {"Collision size X", size, {}, [](const Unit &u) { return QVariant(u.CollisionSize.x); }},
            {"Collision size Y", size, {}, [](const Unit &u) { return QVariant(u.CollisionSize.y); }},
            {"Collision size Z", size, {}, [](const Unit &u) { return QVariant(u.CollisionSize.z); }},

            {"Standing graphic 1", graphics, {}, [](const Unit &u) { return intValue(u.StandingGraphic.first); }},
            {"Standing graphic 2", graphics, {}, [](const Unit &u) { return intValue(u.StandingGraphic.second); }},
            {"Dying graphic", graphics, {}, [](const Unit &u) { return intValue(u.DyingGraphic); }},
            {"Icon", graphics, {}, [](const Unit &u) { return intValue(u.IconID); }},

            {"Enabled", flags, {}, [](const Unit &u) { return intValue(u.Enabled); }},
            {"Hide in editor", flags, {}, [](const Unit &u) { return intValue(u.HideInEditor); }},
        };
        return list;
    }();
    return fields;
}

} // namespace newage
