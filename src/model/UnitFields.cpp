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
    case genie::UT_EyeCandy: return QStringLiteral("Eye Candy");
    case genie::UT_Trees: return QStringLiteral("Tree (AoK)");
    case genie::UT_Flag: return QStringLiteral("Animated");
    case genie::UT_25: return QStringLiteral("Doppelganger");
    case genie::UT_Dead_Fish: return QStringLiteral("Moving");
    case genie::UT_Bird: return QStringLiteral("Actor");
    case genie::UT_Combatant: return QStringLiteral("Superclass");
    case genie::UT_Projectile: return QStringLiteral("Projectile");
    case genie::UT_Creatable: return QStringLiteral("Combatant");
    case genie::UT_Building: return QStringLiteral("Building");
    case genie::UT_AoeTrees: return QStringLiteral("Tree (AoE)");
    default: return QStringLiteral("Unknown");
    }
}

QList<int> unitTypeIds()
{
    return {genie::UT_EyeCandy, genie::UT_Trees, genie::UT_Flag, genie::UT_25,
            genie::UT_Dead_Fish, genie::UT_Bird, genie::UT_Combatant, genie::UT_Projectile,
            genie::UT_Creatable, genie::UT_Building, genie::UT_AoeTrees};
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
            {"id", "ID", general, {}, [](const Unit &u) { return intValue(u.ID); }},
            {"type", "Type", general, {}, [](const Unit &u) { return intValue(u.Type); }},
            {"class", "Class", general, {}, [](const Unit &u) { return intValue(u.Class); }},
            {"internal_name", "Internal name", general, {},
             [](const Unit &u) { return QVariant(QString::fromLatin1(u.Name)); }},
            // genieutils widens the 16-bit variants into these on load, so the
            // 32-bit members are valid for every game version.
            {"language_name", "Language name", general, {},
             [](const Unit &u) { return intValue(u.LanguageDLLName); }, true},
            {"language_creation", "Language creation", general, {},
             [](const Unit &u) { return intValue(u.LanguageDLLCreation); }, true},

            numberField<Unit>("hit_points", "Hit points", stats, {}, [](auto &u) -> auto & { return u.HitPoints; }),
            numberField<Unit>("line_of_sight", "Line of sight", stats, {},
                              [](auto &u) -> auto & { return u.LineOfSight; }),
            numberField<Unit>("speed", "Speed", stats, hasSpeed, [](auto &u) -> auto & { return u.Speed; }),
            {"garrison_capacity", "Garrison capacity", stats, {},
             [](const Unit &u) { return intValue(u.GarrisonCapacity); }},
            {"resource_capacity", "Resource capacity", stats, {},
             [](const Unit &u) { return intValue(u.ResourceCapacity); }},
        };

        // Always 3 slots (Creatable::getResourceCostsSize). Unused ones have
        // resource -1 and are left out.
        for (int i = 0; i < 3; ++i)
        {
            const auto applies = [i](const Unit &u) {
                return isCreatable(u) && i < static_cast<int>(u.Creatable.ResourceCosts.size())
                       && u.Creatable.ResourceCosts.at(i).Type >= 0;
            };
            const QString slot = QString::number(i + 1);
            FieldDesc<Unit> resource = numberField<Unit>(
                QStringLiteral("cost%1.resource").arg(slot), QStringLiteral("Cost %1 resource").arg(slot), costs,
                applies, [i](auto &u) -> auto & { return u.Creatable.ResourceCosts.at(i).Type; });
            resource.ref = RefKind::Resource;
            list.append(resource);
            list.append(numberField<Unit>(QStringLiteral("cost%1.amount").arg(slot),
                                          QStringLiteral("Cost %1 amount").arg(slot), costs, applies,
                                          [i](auto &u) -> auto & { return u.Creatable.ResourceCosts.at(i).Amount; }));
            // 1: the amount is paid; 0: it only has to be available.
            list.append(numberField<Unit>(QStringLiteral("cost%1.paid").arg(slot),
                                          QStringLiteral("Cost %1 paid").arg(slot), costs, applies,
                                          [i](auto &u) -> auto & { return u.Creatable.ResourceCosts.at(i).Flag; }));
        }

        // DE can list several train locations; only the first is shown for now.
        // LocationID is the building, labelled like a research location.
        FieldDesc<Unit> trainAt{QStringLiteral("train_location"), QStringLiteral("Train location"), training,
                                hasTrainLocation, [](const Unit &u) {
                                    return intValue(u.Creatable.TrainLocations.front().LocationID);
                                }};
        trainAt.ref = RefKind::Unit;
        list.append(trainAt);
        list.append(numberField<Unit>("train_time", "Train time", training, hasTrainLocation,
                                      [](auto &u) -> auto & { return u.Creatable.TrainLocations.front().QueueTime; }));

        list += QList<FieldDesc<Unit>>{
            {"collision_size_x", "Collision size X", size, {},
             [](const Unit &u) { return QVariant(u.CollisionSize.x); }},
            {"collision_size_y", "Collision size Y", size, {},
             [](const Unit &u) { return QVariant(u.CollisionSize.y); }},
            {"collision_size_z", "Collision size Z", size, {},
             [](const Unit &u) { return QVariant(u.CollisionSize.z); }},

            {"standing_graphic1", "Standing graphic 1", graphics, {},
             [](const Unit &u) { return intValue(u.StandingGraphic.first); }},
            {"standing_graphic2", "Standing graphic 2", graphics, {},
             [](const Unit &u) { return intValue(u.StandingGraphic.second); }},
            {"dying_graphic", "Dying graphic", graphics, {}, [](const Unit &u) { return intValue(u.DyingGraphic); }},
            {"icon", "Icon", graphics, {}, [](const Unit &u) { return intValue(u.IconID); }},

            {"enabled", "Enabled", flags, {}, [](const Unit &u) { return intValue(u.Enabled); }},
            {"hide_in_editor", "Hide in editor", flags, {}, [](const Unit &u) { return intValue(u.HideInEditor); }},
        };
        for (FieldDesc<Unit> &field : list)
        {
            if (field.key == QStringLiteral("internal_name"))
                field.typeId = QMetaType::QString;
            if (field.key.startsWith(QStringLiteral("collision_size_")))
                field.typeId = QMetaType::Float;
            if (field.key == QStringLiteral("type"))
                field.ref = RefKind::UnitType;
            if (field.key == QStringLiteral("class"))
                field.ref = RefKind::UnitClass;
            if (field.key == QStringLiteral("icon"))
                field.sprite = SpriteKind::UnitIcon;
        }
        return list;
    }();
    return fields;
}

} // namespace newage
