#include "model/TechFields.h"

#include "genie/dat/Research.h"

namespace newage {

namespace {

// uint8_t and friends would otherwise land in QVariant as char types.
template <typename Int>
QVariant intValue(Int value)
{
    return QVariant(static_cast<int>(value));
}

bool hasLocation(const TechRef &ref)
{
    return !ref.tech.ResearchLocations.empty();
}

} // namespace

QString techTypeName(int type)
{
    switch (type)
    {
    case 0: return QStringLiteral("0 - Regular");
    case 2: return QStringLiteral("2 - Age");
    default: return QStringLiteral("%1 - Unknown").arg(type);
    }
}

const QList<FieldDesc<TechRef>> &techFields()
{
    static const QList<FieldDesc<TechRef>> fields = [] {
        const QString general = QStringLiteral("General");
        const QString requirements = QStringLiteral("Requirements");
        const QString costs = QStringLiteral("Costs");
        const QString location = QStringLiteral("Research location");

        // Civ and Full tech mode are only stored from AoK on; older files
        // read as the defaults (-1, 0). -1 is not a civ, so the label stays empty.
        FieldDesc<TechRef> civ{QStringLiteral("Civ"), general, {},
                               [](const TechRef &t) { return intValue(t.tech.Civ); }};
        civ.ref = RefKind::Civ;
        FieldDesc<TechRef> effect{QStringLiteral("Effect"), general, {},
                                  [](const TechRef &t) { return intValue(t.tech.EffectID); }};
        effect.ref = RefKind::Effect;
        FieldDesc<TechRef> icon{QStringLiteral("Icon"), general, {},
                                [](const TechRef &t) { return intValue(t.tech.IconID); }};
        icon.sprite = SpriteKind::TechIcon;

        QList<FieldDesc<TechRef>> list = {
            {"ID", general, {}, [](const TechRef &t) { return QVariant(t.id); }},
            {"Internal name", general, {}, [](const TechRef &t) { return QVariant(QString::fromLatin1(t.tech.Name)); }},
            // genieutils widens the 16-bit variants into these on load, so the
            // 32-bit members are valid for every game version.
            {"Language name", general, {}, [](const TechRef &t) { return intValue(t.tech.LanguageDLLName); }, true},
            {"Language description", general, {},
             [](const TechRef &t) { return intValue(t.tech.LanguageDLLDescription); }, true},
            {"Type", general, {}, [](const TechRef &t) { return QVariant(techTypeName(t.tech.Type)); }},
            civ,
            effect,
            icon,
            {"Full tech mode", general, {}, [](const TechRef &t) { return intValue(t.tech.FullTechMode); }},
        };

        // 4 slots in AoE/RoR, 6 from AoK on (Tech::getRequiredTechsSize).
        // Unused slots hold -1 and are left out.
        for (int i = 0; i < 6; ++i)
        {
            FieldDesc<TechRef> field{QStringLiteral("Required tech %1").arg(i + 1), requirements,
                                     [i](const TechRef &t) {
                                         return i < static_cast<int>(t.tech.RequiredTechs.size())
                                                && t.tech.RequiredTechs.at(i) >= 0;
                                     },
                                     [i](const TechRef &t) { return intValue(t.tech.RequiredTechs.at(i)); }};
            field.ref = RefKind::Tech;
            list.append(field);
        }
        list.append({"Required tech count", requirements, {},
                     [](const TechRef &t) { return intValue(t.tech.RequiredTechCount); }});

        // Unused cost slots have resource -1 and are left out.
        for (int i = 0; i < 3; ++i)
        {
            const auto applies = [i](const TechRef &t) {
                return i < static_cast<int>(t.tech.ResourceCosts.size()) && t.tech.ResourceCosts.at(i).Type >= 0;
            };
            FieldDesc<TechRef> resource = numberField<TechRef>(
                QStringLiteral("Cost %1 resource").arg(i + 1), costs, applies,
                [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Type; });
            resource.ref = RefKind::Resource;
            list.append(resource);
            list.append(numberField<TechRef>(QStringLiteral("Cost %1 amount").arg(i + 1), costs, applies,
                                             [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Amount; }));
            // 1: the amount is paid; 0: it only has to be available.
            list.append(numberField<TechRef>(QStringLiteral("Cost %1 paid").arg(i + 1), costs, applies,
                                             [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Flag; }));
        }

        // DE can list several locations; only the first is shown for now.
        FieldDesc<TechRef> researchAt{QStringLiteral("Location"), location, hasLocation,
                                      [](const TechRef &t) {
                                          return intValue(t.tech.ResearchLocations.front().LocationID);
                                      }};
        researchAt.ref = RefKind::Unit;
        list.append(researchAt);
        list.append(numberField<TechRef>("Research time", location, hasLocation, [](auto &t) -> auto & {
            return t.tech.ResearchLocations.front().QueueTime;
        }));
        list.append({"Button", location, hasLocation,
                     [](const TechRef &t) { return intValue(t.tech.ResearchLocations.front().ButtonID); }});
        return list;
    }();
    return fields;
}

} // namespace newage
