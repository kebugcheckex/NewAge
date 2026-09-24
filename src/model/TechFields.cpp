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

        QList<FieldDesc<TechRef>> list = {
            {"ID", general, {}, [](const TechRef &t) { return QVariant(t.id); }},
            {"Internal name", general, {}, [](const TechRef &t) { return QVariant(QString::fromLatin1(t.tech.Name)); }},
            // genieutils widens the 16-bit variants into these on load, so the
            // 32-bit members are valid for every game version.
            {"Language name", general, {}, [](const TechRef &t) { return intValue(t.tech.LanguageDLLName); }, true},
            {"Language description", general, {},
             [](const TechRef &t) { return intValue(t.tech.LanguageDLLDescription); }, true},
            {"Type", general, {}, [](const TechRef &t) { return QVariant(techTypeName(t.tech.Type)); }},
            // Civ and Full tech mode are only stored from AoK on; older files
            // read as the defaults (-1, 0).
            {"Civ", general, {}, [](const TechRef &t) { return intValue(t.tech.Civ); }},
            {"Effect", general, {}, [](const TechRef &t) { return intValue(t.tech.EffectID); }},
            {"Icon", general, {}, [](const TechRef &t) { return intValue(t.tech.IconID); }},
            {"Full tech mode", general, {}, [](const TechRef &t) { return intValue(t.tech.FullTechMode); }},
        };

        // 4 slots in AoE/RoR, 6 from AoK on (Tech::getRequiredTechsSize).
        for (int i = 0; i < 6; ++i)
        {
            list.append({QStringLiteral("Required tech %1").arg(i + 1), requirements,
                         [i](const TechRef &t) { return i < static_cast<int>(t.tech.RequiredTechs.size()); },
                         [i](const TechRef &t) { return intValue(t.tech.RequiredTechs.at(i)); }});
        }
        list.append({"Required tech count", requirements, {},
                     [](const TechRef &t) { return intValue(t.tech.RequiredTechCount); }});

        for (int i = 0; i < 3; ++i)
        {
            const auto applies = [i](const TechRef &t) {
                return i < static_cast<int>(t.tech.ResourceCosts.size());
            };
            list.append(numberField<TechRef>(QStringLiteral("Cost %1 resource").arg(i + 1), costs, applies,
                                             [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Type; }));
            list.append(numberField<TechRef>(QStringLiteral("Cost %1 amount").arg(i + 1), costs, applies,
                                             [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Amount; }));
            // 1: the amount is paid; 0: it only has to be available.
            list.append(numberField<TechRef>(QStringLiteral("Cost %1 paid").arg(i + 1), costs, applies,
                                             [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Flag; }));
        }

        // DE can list several locations; only the first is shown for now.
        list.append({"Location", location, hasLocation,
                     [](const TechRef &t) { return intValue(t.tech.ResearchLocations.front().LocationID); }});
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
