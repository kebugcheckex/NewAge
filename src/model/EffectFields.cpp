#include "model/EffectFields.h"

#include <cmath>

#include "genie/dat/Techage.h"

namespace newage {

namespace {

template <typename Int>
QVariant intValue(Int value)
{
    return QVariant(static_cast<int>(value));
}

// GV_SWGB sits after GV_LatestDE2 in the enum, so a plain >= comparison would
// treat SWGB as Definitive Edition. Bounds follow AGE's effect type lists.
bool isAoE2DE(genie::GameVersion version)
{
    return version >= genie::GV_C2 && version <= genie::GV_LatestDE2;
}

bool isHdOrDe(genie::GameVersion version)
{
    return version >= genie::GV_Cysion && version <= genie::GV_LatestDE2;
}

// Type 6 and the tech modifiers arrive with AoK, and with AoE1 DE (Tapsa).
bool hasAoKEffects(genie::GameVersion version)
{
    return version >= genie::GV_AoKA || (version >= genie::GV_Tapsa && version <= genie::GV_LatestTap);
}

bool effectTypeKnown(genie::GameVersion version, int type)
{
    if (type >= 0 && type <= 5)
        return true;
    if (type == 102)
        return true;
    if (type == 6 || type == 101 || type == 103)
        return hasAoKEffects(version);
    if (type >= 10 && type <= 16)
        return isHdOrDe(version);
    if (type == 7 || type == 8 || (type >= 17 && type <= 18) || (type >= 20 && type <= 28) || (type >= 30 && type <= 38)
        || (type >= 40 && type <= 48))
        return isAoE2DE(version);
    return false;
}

int commandBase(int type)
{
    if (type >= 10 && type <= 48)
        return type % 10;
    return type;
}

QString scopePrefix(int type)
{
    if (type < 10 || type > 48)
        return {};
    switch (type / 10)
    {
    case 1: return QStringLiteral("Team ");
    case 2: return QStringLiteral("Enemy ");
    case 3: return QStringLiteral("Neutral ");
    case 4: return QStringLiteral("Gaia ");
    default: return {};
    }
}

QString baseName(int base)
{
    switch (base)
    {
    case 0: return QStringLiteral("Attribute Modifier (Set)");
    case 1: return QStringLiteral("Resource Modifier (Set/+/-)");
    case 2: return QStringLiteral("Enable/Disable Unit");
    case 3: return QStringLiteral("Upgrade Unit");
    case 4: return QStringLiteral("Attribute Modifier (+/-)");
    case 5: return QStringLiteral("Attribute Modifier (Multiply)");
    case 6: return QStringLiteral("Resource Modifier (Multiply)");
    case 7: return QStringLiteral("Spawn Unit");
    case 8: return QStringLiteral("Modify Tech");
    case 101: return QStringLiteral("Tech Cost Modifier (Set/+/-)");
    case 102: return QStringLiteral("Disable Tech");
    case 103: return QStringLiteral("Tech Time Modifier (Set/+/-)");
    default: return {};
    }
}

enum class Slot { A, B, C, D };

QVariant slotValue(const EffectRef &effect, int command, Slot slot)
{
    const genie::EffectCommand &c = effect.effect.EffectCommands.at(command);
    switch (slot)
    {
    case Slot::A: return intValue(c.A);
    case Slot::B: return intValue(c.B);
    case Slot::C: return intValue(c.C);
    case Slot::D: return QVariant(c.D);
    }
    return {};
}

// `omitUnused` drops the row when the slot is still the unused value -1.
// Amount and Modify Tech's Action pass false: -1 is a real value there.
void appendSlot(QList<FieldDesc<EffectRef>> &list, const QString &group, int command, const QString &name, Slot slot,
                RefKind ref = RefKind::None, bool omitUnused = true)
{
    FieldDesc<EffectRef> field{name, group, {}, [command, slot](const EffectRef &effect) {
                                  return slotValue(effect, command, slot);
                              }};
    if (omitUnused)
    {
        field.applies = [command, slot](const EffectRef &effect) {
            return slotValue(effect, command, slot).toInt() >= 0;
        };
    }
    field.ref = ref;
    list.append(std::move(field));
}

// Disable-tech stores the tech ID in D, a float. The ID is what the field means.
void appendTechId(QList<FieldDesc<EffectRef>> &list, const QString &group, int command)
{
    FieldDesc<EffectRef> field{QStringLiteral("Tech"), group, {}, [command](const EffectRef &effect) {
                                  const float id = effect.effect.EffectCommands.at(command).D;
                                  return QVariant(static_cast<int>(std::lround(id)));
                              }};
    field.applies = [command](const EffectRef &effect) {
        return std::lround(effect.effect.EffectCommands.at(command).D) >= 0;
    };
    field.ref = RefKind::Tech;
    list.append(std::move(field));
}

enum class ModeKind { SetOrAdd, Enable, OnMap };

void appendMode(QList<FieldDesc<EffectRef>> &list, const QString &group, int command, Slot slot, ModeKind kind)
{
    list.append({QStringLiteral("Mode"), group, {}, [command, slot, kind](const EffectRef &effect) {
                     const int mode = slotValue(effect, command, slot).toInt();
                     switch (kind)
                     {
                     case ModeKind::Enable:
                         return QVariant(mode == 0 ? QStringLiteral("0 - Disable")
                                                   : QStringLiteral("%1 - Enable").arg(mode));
                     case ModeKind::OnMap:
                         return QVariant(mode == -1 ? QStringLiteral("-1 - All")
                                                    : QStringLiteral("%1 - On map").arg(mode));
                     case ModeKind::SetOrAdd:
                         return QVariant(mode == 0 ? QStringLiteral("0 - Set") : QStringLiteral("%1 - +/-").arg(mode));
                     }
                     return QVariant();
                 }});
}

void appendCommand(QList<FieldDesc<EffectRef>> &list, int command, int type, genie::GameVersion version)
{
    const QString group = QStringLiteral("Command %1").arg(command + 1);
    list.append({QStringLiteral("Type"), group, {}, [command, version](const EffectRef &effect) {
                     return QVariant(effectTypeName(version, effect.effect.EffectCommands.at(command).Type));
                 }});

    if (!effectTypeKnown(version, type))
    {
        appendSlot(list, group, command, QStringLiteral("A"), Slot::A);
        appendSlot(list, group, command, QStringLiteral("B"), Slot::B);
        appendSlot(list, group, command, QStringLiteral("C"), Slot::C);
        appendSlot(list, group, command, QStringLiteral("D"), Slot::D, RefKind::None, false);
        return;
    }

    switch (commandBase(type))
    {
    case 0:
    case 4:
    case 5:
        appendSlot(list, group, command, QStringLiteral("Unit"), Slot::A, RefKind::Unit);
        appendSlot(list, group, command, QStringLiteral("Class"), Slot::B, RefKind::UnitClass);
        appendSlot(list, group, command, QStringLiteral("Attribute"), Slot::C, RefKind::Attribute);
        appendSlot(list, group, command, QStringLiteral("Amount"), Slot::D, RefKind::None, false);
        break;
    case 1:
        appendSlot(list, group, command, QStringLiteral("Resource"), Slot::A, RefKind::Resource);
        appendMode(list, group, command, Slot::B, ModeKind::SetOrAdd);
        if (isAoE2DE(version))
            appendSlot(list, group, command, QStringLiteral("Multiply resource"), Slot::C, RefKind::Resource);
        appendSlot(list, group, command, QStringLiteral("Amount"), Slot::D, RefKind::None, false);
        break;
    case 2:
        appendSlot(list, group, command, QStringLiteral("Unit"), Slot::A, RefKind::Unit);
        appendMode(list, group, command, Slot::B, ModeKind::Enable);
        break;
    case 3:
        appendSlot(list, group, command, QStringLiteral("Unit"), Slot::A, RefKind::Unit);
        appendSlot(list, group, command, QStringLiteral("To unit"), Slot::B, RefKind::Unit);
        if (isAoE2DE(version))
            appendMode(list, group, command, Slot::C, ModeKind::OnMap);
        break;
    case 6:
        appendSlot(list, group, command, QStringLiteral("Resource"), Slot::A, RefKind::Resource);
        appendSlot(list, group, command, QStringLiteral("Amount"), Slot::D, RefKind::None, false);
        break;
    case 7:
        appendSlot(list, group, command, QStringLiteral("Unit"), Slot::A, RefKind::Unit);
        appendSlot(list, group, command, QStringLiteral("From building"), Slot::B, RefKind::Unit);
        appendSlot(list, group, command, QStringLiteral("Amount"), Slot::C, RefKind::None, false);
        break;
    case 8:
        appendSlot(list, group, command, QStringLiteral("Tech"), Slot::A, RefKind::Tech);
        appendSlot(list, group, command, QStringLiteral("Action"), Slot::B, RefKind::None, false);
        appendSlot(list, group, command, QStringLiteral("Amount"), Slot::D, RefKind::None, false);
        break;
    case 101:
        appendSlot(list, group, command, QStringLiteral("Tech"), Slot::A, RefKind::Tech);
        appendSlot(list, group, command, QStringLiteral("Resource"), Slot::B, RefKind::Resource);
        appendMode(list, group, command, Slot::C, ModeKind::SetOrAdd);
        appendSlot(list, group, command, QStringLiteral("Amount"), Slot::D, RefKind::None, false);
        break;
    case 102:
        appendTechId(list, group, command);
        break;
    case 103:
        appendSlot(list, group, command, QStringLiteral("Tech"), Slot::A, RefKind::Tech);
        appendMode(list, group, command, Slot::C, ModeKind::SetOrAdd);
        appendSlot(list, group, command, QStringLiteral("Amount"), Slot::D, RefKind::None, false);
        break;
    default:
        appendSlot(list, group, command, QStringLiteral("A"), Slot::A);
        appendSlot(list, group, command, QStringLiteral("B"), Slot::B);
        appendSlot(list, group, command, QStringLiteral("C"), Slot::C);
        appendSlot(list, group, command, QStringLiteral("D"), Slot::D, RefKind::None, false);
        break;
    }
}

} // namespace

QString effectTypeName(genie::GameVersion version, int type)
{
    if (!effectTypeKnown(version, type))
        return QStringLiteral("%1 - Unknown").arg(type);
    return QStringLiteral("%1 - %2%3").arg(type).arg(scopePrefix(type), baseName(commandBase(type)));
}

QList<FieldDesc<EffectRef>> effectFields(const genie::Effect &effect, genie::GameVersion version)
{
    const QString general = QStringLiteral("General");
    QList<FieldDesc<EffectRef>> list = {
        {"ID", general, {}, [](const EffectRef &e) { return QVariant(e.id); }},
        {"Internal name", general, {},
         [](const EffectRef &e) { return QVariant(QString::fromLatin1(e.effect.Name)); }},
        {"Command count", general, {},
         [](const EffectRef &e) { return QVariant(static_cast<int>(e.effect.EffectCommands.size())); }},
    };
    for (int i = 0; i < static_cast<int>(effect.EffectCommands.size()); ++i)
        appendCommand(list, i, effect.EffectCommands[i].Type, version);
    return list;
}

} // namespace newage
