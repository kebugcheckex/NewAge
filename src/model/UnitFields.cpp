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
    const QString general = QStringLiteral("General");
    const QString stats = QStringLiteral("Stats");
    const QString size = QStringLiteral("Size");
    const QString graphics = QStringLiteral("Graphics");
    const QString flags = QStringLiteral("Flags");

    static const QList<FieldDesc<Unit>> fields = {
        {"ID", general, {}, [](const Unit &u) { return intValue(u.ID); }},
        {"Type", general, {}, [](const Unit &u) { return QVariant(unitTypeName(u.Type)); }},
        {"Class", general, {}, [](const Unit &u) { return intValue(u.Class); }},
        {"Internal name", general, {}, [](const Unit &u) { return QVariant(QString::fromLatin1(u.Name)); }},
        // genieutils widens the 16-bit variants into these on load, so the
        // 32-bit members are valid for every game version.
        {"Language name", general, {}, [](const Unit &u) { return intValue(u.LanguageDLLName); }, true},
        {"Language creation", general, {}, [](const Unit &u) { return intValue(u.LanguageDLLCreation); }, true},

        {"Hit points", stats, {}, [](const Unit &u) { return intValue(u.HitPoints); }},
        {"Line of sight", stats, {}, [](const Unit &u) { return QVariant(u.LineOfSight); }},
        {"Speed", stats, hasSpeed, [](const Unit &u) { return QVariant(u.Speed); }},
        {"Garrison capacity", stats, {}, [](const Unit &u) { return intValue(u.GarrisonCapacity); }},
        {"Resource capacity", stats, {}, [](const Unit &u) { return intValue(u.ResourceCapacity); }},

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
    return fields;
}

} // namespace newage
