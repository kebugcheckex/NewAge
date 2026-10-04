#include "model/EffectNames.h"

#include <algorithm>

#include <QHash>
#include <QStringList>

namespace newage {

namespace {

// Ported from Advanced Genie Editor (AGE_Frame/Lists.cpp). Selection 0 in
// AGE's combos is the invalid/-1 entry and is not a name.

bool isAoE2DE(genie::GameVersion version)
{
    return version >= genie::GV_C2 && version <= genie::GV_LatestDE2;
}

QStringList buildUnitClassNames(genie::GameVersion version)
{
    QStringList names;
    if (version >= genie::GV_SWGB)
    {
        names.append(QStringLiteral("Unused"));
        names.append(QStringLiteral("Nerf/Bantha"));
        names.append(QStringLiteral("Fambaa"));
        names.append(QStringLiteral("Unused"));
        names.append(QStringLiteral("Wild Animal"));
        names.append(QStringLiteral("Monster/Trouble"));
        names.append(QStringLiteral("Wall"));
        names.append(QStringLiteral("Farm"));
        names.append(QStringLiteral("Gate"));
        names.append(QStringLiteral("Fortress/A-A Turret"));
        names.append(QStringLiteral("Turret"));
        names.append(QStringLiteral("Cruiser"));
        names.append(QStringLiteral("Unused"));
        names.append(QStringLiteral("Destroyer"));
        names.append(QStringLiteral("Utility Trawler"));
        names.append(QStringLiteral("Frigate 1"));
        names.append(QStringLiteral("A-A Destroyer 1"));
        names.append(QStringLiteral("Transport Ship"));
        names.append(QStringLiteral("Building"));
        names.append(QStringLiteral("Doppelganger"));
        names.append(QStringLiteral("Other/Dead/Projectile"));
        names.append(QStringLiteral("Command Base"));
        names.append(QStringLiteral("Cliff"));
        names.append(QStringLiteral("Fish"));
        names.append(QStringLiteral("Unused"));
        names.append(QStringLiteral("Shore Fish"));
        names.append(QStringLiteral("Game Engine Stuff"));
        names.append(QStringLiteral("Fruit Bush"));
        names.append(QStringLiteral("Holocron"));
        names.append(QStringLiteral("Nova"));
        names.append(QStringLiteral("Ore"));
        names.append(QStringLiteral("Tree/Carbon"));
        names.append(QStringLiteral("Artillery"));
        names.append(QStringLiteral("A-A Mobile"));
        names.append(QStringLiteral("Undeployed Cannon"));
        names.append(QStringLiteral("Pummel"));
        names.append(QStringLiteral("Cannon"));
        names.append(QStringLiteral("Unused"));
        names.append(QStringLiteral("Unused"));
        names.append(QStringLiteral("Frigate 2"));
        names.append(QStringLiteral("A-A Destroyer 2"));
        names.append(QStringLiteral("Unused"));
        names.append(QStringLiteral("Bridge/Eye Candy"));
        names.append(QStringLiteral("Bomber"));
        names.append(QStringLiteral("Bounty Hunter"));
        names.append(QStringLiteral("Cargo Trader"));
        names.append(QStringLiteral("Mixed 1"));
        names.append(QStringLiteral("Scout"));
        names.append(QStringLiteral("Fighter"));
        names.append(QStringLiteral("Grenade Trooper"));
        names.append(QStringLiteral("Jedi"));
        names.append(QStringLiteral("Jedi with Holocron"));
        names.append(QStringLiteral("Trooper"));
        names.append(QStringLiteral("War Machine"));
        names.append(QStringLiteral("Medic"));
        names.append(QStringLiteral("A-A Trooper"));
        names.append(QStringLiteral("Mounted Trooper"));
        names.append(QStringLiteral("Fambaa Shield Generator"));
        names.append(QStringLiteral("Workers"));
        names.append(QStringLiteral("Air Transport"));
        names.append(QStringLiteral("Domestic Animal"));
        names.append(QStringLiteral("Power Droid"));
        names.append(QStringLiteral("Air Cruiser"));
        names.append(QStringLiteral("Geonosian Warrior"));
        names.append(QStringLiteral("Jedi Starfighter"));
        return names;
    }

    const bool aoe2 = version >= genie::GV_AoKA;
    names.append(QStringLiteral("Archer"));
    names.append(QStringLiteral("Artifact"));
    names.append(QStringLiteral("Trade Boat"));
    names.append(QStringLiteral("Building"));
    names.append(QStringLiteral("Civilian"));
    names.append(QStringLiteral("Ocean Fish"));
    names.append(QStringLiteral("Infantry"));
    names.append(QStringLiteral("Berry Bush"));
    names.append(QStringLiteral("Stone Mine"));
    names.append(QStringLiteral("Prey Animal"));
    names.append(QStringLiteral("Predator Animal"));
    names.append(QStringLiteral("Miscellaneous"));
    names.append(QStringLiteral("Cavalry"));
    names.append(QStringLiteral("Siege Weapon"));
    names.append(QStringLiteral("Terrain"));
    names.append(QStringLiteral("Tree"));
    names.append(QStringLiteral("Tree Stump"));
    names.append(QStringLiteral("Healer"));
    names.append(aoe2 ? QStringLiteral("Monk") : QStringLiteral("Priest"));
    names.append(QStringLiteral("Trade Cart"));
    names.append(QStringLiteral("Transport Boat"));
    names.append(QStringLiteral("Fishing Boat"));
    names.append(QStringLiteral("Warship"));
    names.append(aoe2 ? QStringLiteral("Conquistador") : QStringLiteral("Chariot Archer"));
    names.append(QStringLiteral("War Elephant"));
    names.append(QStringLiteral("Hero"));
    names.append(QStringLiteral("Elephant Archer"));
    names.append(QStringLiteral("Wall"));
    names.append(QStringLiteral("Phalanx"));
    names.append(QStringLiteral("Domestic Animal"));
    names.append(QStringLiteral("Flag"));
    names.append(QStringLiteral("Deep Sea Fish"));
    names.append(QStringLiteral("Gold Mine"));
    names.append(QStringLiteral("Shore Fish"));
    names.append(QStringLiteral("Cliff"));
    names.append(aoe2 ? QStringLiteral("Petard") : QStringLiteral("Chariot"));
    names.append(QStringLiteral("Cavalry Archer"));
    names.append(QStringLiteral("Doppelganger"));
    names.append(QStringLiteral("Bird"));
    if (!aoe2)
    {
        names.append(QStringLiteral("Slinger"));
        return names;
    }
    names.append(QStringLiteral("Gate"));
    names.append(QStringLiteral("Salvage Pile"));
    names.append(QStringLiteral("Resource Pile"));
    names.append(QStringLiteral("Relic"));
    names.append(QStringLiteral("Monk with Relic"));
    names.append(QStringLiteral("Hand Cannoneer"));
    names.append(QStringLiteral("Two Handed Swordsman"));
    names.append(QStringLiteral("Pikeman"));
    names.append(QStringLiteral("Scout"));
    names.append(QStringLiteral("Ore Mine"));
    names.append(QStringLiteral("Farm"));
    names.append(QStringLiteral("Spearman"));
    names.append(QStringLiteral("Packed Unit"));
    names.append(QStringLiteral("Tower"));
    names.append(QStringLiteral("Boarding Boat"));
    names.append(QStringLiteral("Unpacked Siege Unit"));
    names.append(QStringLiteral("Ballista"));
    names.append(QStringLiteral("Raider"));
    names.append(QStringLiteral("Cavalry Raider"));
    names.append(QStringLiteral("Livestock"));
    names.append(QStringLiteral("King"));
    names.append(QStringLiteral("Misc Building"));
    names.append(QStringLiteral("Controlled Animal"));
    return names;
}

QHash<int, QString> buildEffectAttributeNames(genie::GameVersion version)
{
    QHash<int, QString> names;
    const auto add = [&](int id, QString name) { names.insert(id, std::move(name)); };
    const bool aok = version >= genie::GV_AoKA;

    add(0, QStringLiteral("Hit Points"));
    add(1, QStringLiteral("Line of Sight"));
    add(2, QStringLiteral("Garrison Capacity"));
    add(3, QStringLiteral("Unit Size X"));
    add(4, QStringLiteral("Unit Size Y"));
    add(5, QStringLiteral("Movement Speed"));
    add(6, QStringLiteral("Rotation Speed"));
    add(7, QStringLiteral("Unused"));
    add(8, aok ? QStringLiteral("Armor") : QStringLiteral("Armor (no multiply)"));
    add(9, aok ? QStringLiteral("Attack") : QStringLiteral("Attack (no multiply)"));
    add(10, QStringLiteral("Attack Reload Time"));
    add(11, QStringLiteral("Accuracy Percent"));
    add(12, QStringLiteral("Max Range"));
    add(13, QStringLiteral("Work Rate"));
    add(14, QStringLiteral("Carry Capacity"));
    add(15, QStringLiteral("Base Armor"));
    add(16, QStringLiteral("Projectile Unit"));
    add(17, QStringLiteral("Icon/Graphics Angle"));
    add(18, QStringLiteral("Terrain Defense Bonus"));
    add(19, version >= genie::GV_AoEB ? QStringLiteral("Enable Smart Projectiles") : QStringLiteral("Unused"));

    if (!aok)
    {
        add(100, QStringLiteral("Resource Costs"));
        if (version >= genie::GV_RoR)
            add(101, QStringLiteral("Population (set only)"));
        return names;
    }

    add(20, QStringLiteral("Min Range"));
    add(21, QStringLiteral("Amount of 1st resource storage"));
    add(22, QStringLiteral("Blast Width"));
    add(23, QStringLiteral("Search Radius"));
    if (isAoE2DE(version))
    {
        add(24, QStringLiteral("Hidden Damage Resistance"));
        add(25, QStringLiteral("Icon"));
        add(26, QStringLiteral("Amount of 2nd resource storage"));
        add(27, QStringLiteral("Amount of 3rd resource storage"));
        add(28, QStringLiteral("Fog Visibility"));
        add(29, QStringLiteral("Occlusion Mode"));
        add(30, QStringLiteral("Garrison Type"));
        add(31, QStringLiteral("Unknown"));
        add(32, QStringLiteral("Unit Size Z"));
        add(33, QStringLiteral("Can Be Built On"));
        add(34, QStringLiteral("Foundation Terrain"));
        add(40, QStringLiteral("Hero Status"));
        add(41, QStringLiteral("Frame Delay"));
        add(42, QStringLiteral("Train Location"));
        add(43, QStringLiteral("Train Button"));
        add(44, QStringLiteral("Blast Attack Level"));
        add(45, QStringLiteral("Blast Defense Level"));
        add(46, QStringLiteral("Displayed Attack"));
        add(47, QStringLiteral("Displayed Range"));
        add(48, QStringLiteral("Displayed Melee Armor"));
        add(49, QStringLiteral("Displayed Pierce Armor"));
        add(50, QStringLiteral("Unit Name String"));
        add(51, QStringLiteral("Unit Short Description String"));
        add(52, QStringLiteral("Unused"));
        add(53, QStringLiteral("Terrain Restriction"));
        add(54, QStringLiteral("Unit Trait"));
        add(55, QStringLiteral("Unit Civilization"));
        add(56, QStringLiteral("Unit Trait Piece"));
        add(57, QStringLiteral("Dead Unit"));
        add(58, QStringLiteral("Hotkey"));
        add(59, QStringLiteral("Maximum Charge"));
        add(60, QStringLiteral("Recharge Rate"));
        add(61, QStringLiteral("Charge Event"));
        add(62, QStringLiteral("Charge Type"));
        add(63, QStringLiteral("Combat Ability"));
        add(64, QStringLiteral("Attack Dispersion"));
        add(65, QStringLiteral("Secondary Projectile Unit"));
        add(66, QStringLiteral("Blood Unit"));
        add(67, QStringLiteral("Projectile Hit Mode"));
        add(68, QStringLiteral("Projectile Vanish Mode"));
        add(69, QStringLiteral("Projectile Arc"));
        add(70, QStringLiteral("Attack Graphic"));
        add(71, QStringLiteral("Standing Graphic"));
        add(72, QStringLiteral("Second Standing Graphic"));
        add(73, QStringLiteral("Dying Graphic"));
        add(74, QStringLiteral("Undead Graphic"));
        add(75, QStringLiteral("Walking Graphic"));
        add(76, QStringLiteral("Running Graphic"));
        add(77, QStringLiteral("Special Graphic"));
    }
    add(100, QStringLiteral("Resource Costs"));
    add(101, QStringLiteral("Train Time"));
    add(102, QStringLiteral("Total Missiles"));
    add(103, QStringLiteral("Food Costs"));
    if (version < genie::GV_SWGB)
    {
        add(104, QStringLiteral("Wood Costs"));
        add(105, QStringLiteral("Gold Costs"));
        add(106, QStringLiteral("Stone Costs"));
    }
    else
    {
        add(104, QStringLiteral("Carbon Costs"));
        add(105, QStringLiteral("Nova Costs"));
        add(106, QStringLiteral("Ore Costs"));
    }
    add(107, QStringLiteral("Max Total Missiles"));
    if (version >= genie::GV_AoKB)
        add(108, QStringLiteral("Garrison Heal Rate"));
    if (version >= genie::GV_Cysion && version <= genie::GV_LatestDE2)
        add(109, QStringLiteral("Regeneration Rate"));
    if (isAoE2DE(version))
    {
        add(110, QStringLiteral("Population"));
        add(111, QStringLiteral("Minimum Conversion Time Modifier"));
        add(112, QStringLiteral("Maximum Conversion Time Modifier"));
        add(113, QStringLiteral("Conversion Chance Modifier"));
        add(114, QStringLiteral("Formation Category"));
        add(115, QStringLiteral("Area Damage"));
    }
    return names;
}

const QStringList &unitClassNames(genie::GameVersion version)
{
    static QHash<int, QStringList> cache;
    auto it = cache.find(version);
    if (it == cache.end())
        it = cache.insert(version, buildUnitClassNames(version));
    return *it;
}

const QHash<int, QString> &effectAttributeNames(genie::GameVersion version)
{
    static QHash<int, QHash<int, QString>> cache;
    auto it = cache.find(version);
    if (it == cache.end())
        it = cache.insert(version, buildEffectAttributeNames(version));
    return *it;
}

} // namespace

QString unitClassName(genie::GameVersion version, int id)
{
    const QStringList &names = unitClassNames(version);
    return id >= 0 && id < names.size() ? names.at(id) : QString();
}

int unitClassCount(genie::GameVersion version)
{
    return static_cast<int>(unitClassNames(version).size());
}

QString effectAttributeName(genie::GameVersion version, int id)
{
    return effectAttributeNames(version).value(id);
}

QList<int> effectAttributeIds(genie::GameVersion version)
{
    QList<int> ids = effectAttributeNames(version).keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace newage
