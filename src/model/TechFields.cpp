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
    case 0: return QStringLiteral("Regular");
    case 2: return QStringLiteral("Age");
    default: return QStringLiteral("Unknown");
    }
}

QList<int> techTypeIds()
{
    return {0, 2};
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
        FieldDesc<TechRef> civ{QStringLiteral("civ"), QStringLiteral("Civ"), general, {},
                               [](const TechRef &t) { return intValue(t.tech.Civ); }};
        civ.ref = RefKind::Civ;
        FieldDesc<TechRef> effect{QStringLiteral("effect"), QStringLiteral("Effect"), general, {},
                                  [](const TechRef &t) { return intValue(t.tech.EffectID); }};
        effect.ref = RefKind::Effect;
        FieldDesc<TechRef> type{QStringLiteral("type"), QStringLiteral("Type"), general, {},
                                 [](const TechRef &t) { return intValue(t.tech.Type); }};
        type.ref = RefKind::TechType;
        FieldDesc<TechRef> icon{QStringLiteral("icon"), QStringLiteral("Icon"), general, {},
                                [](const TechRef &t) { return intValue(t.tech.IconID); }};
        icon.sprite = SpriteKind::TechIcon;

        QList<FieldDesc<TechRef>> list = {
            {"id", "ID", general, {}, [](const TechRef &t) { return QVariant(t.id); }},
            {"internal_name", "Internal name", general, {},
             [](const TechRef &t) { return QVariant(QString::fromLatin1(t.tech.Name)); }},
            // genieutils widens the 16-bit variants into these on load, so the
            // 32-bit members are valid for every game version.
            {"language_name", "Language name", general, {},
             [](const TechRef &t) { return intValue(t.tech.LanguageDLLName); }, true},
            {"language_description", "Language description", general, {},
             [](const TechRef &t) { return intValue(t.tech.LanguageDLLDescription); }, true},
            type,
            civ,
            effect,
            icon,
            {"full_tech_mode", "Full tech mode", general, {},
             [](const TechRef &t) { return intValue(t.tech.FullTechMode); }},
        };

        // 4 slots in AoE/RoR, 6 from AoK on (Tech::getRequiredTechsSize).
        // Unused slots hold -1 and are left out.
        for (int i = 0; i < 6; ++i)
        {
            FieldDesc<TechRef> field = numberField<TechRef>(
                QStringLiteral("required_tech%1").arg(i + 1), QStringLiteral("Required tech %1").arg(i + 1),
                requirements,
                [i](const TechRef &t) {
                    return i < static_cast<int>(t.tech.RequiredTechs.size()) && t.tech.RequiredTechs.at(i) >= 0;
                },
                [i](auto &t) -> auto & { return t.tech.RequiredTechs.at(i); });
            field.ref = RefKind::Tech;
            list.append(field);
        }
        list.append({"required_tech_count", "Required tech count", requirements, {},
                     [](const TechRef &t) { return intValue(t.tech.RequiredTechCount); }});

        // Unused cost slots have resource -1 and are left out.
        for (int i = 0; i < 3; ++i)
        {
            const auto applies = [i](const TechRef &t) {
                return i < static_cast<int>(t.tech.ResourceCosts.size()) && t.tech.ResourceCosts.at(i).Type >= 0;
            };
            const QString slot = QString::number(i + 1);
            FieldDesc<TechRef> resource = numberField<TechRef>(
                QStringLiteral("cost%1.resource").arg(slot), QStringLiteral("Cost %1 resource").arg(slot), costs,
                applies, [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Type; });
            resource.ref = RefKind::Resource;
            list.append(resource);
            list.append(numberField<TechRef>(QStringLiteral("cost%1.amount").arg(slot),
                                             QStringLiteral("Cost %1 amount").arg(slot), costs, applies,
                                             [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Amount; }));
            // 1: the amount is paid; 0: it only has to be available.
            list.append(numberField<TechRef>(QStringLiteral("cost%1.paid").arg(slot),
                                             QStringLiteral("Cost %1 paid").arg(slot), costs, applies,
                                             [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Flag; }));
        }

        // DE can list several locations; only the first is shown for now.
        // LocationID is the building. The tech browser edits it as a combo of
        // the current civ's buildings.
        FieldDesc<TechRef> researchAt = numberField<TechRef>(
            QStringLiteral("research_location"), QStringLiteral("Location"), location, hasLocation,
            [](auto &t) -> auto & { return t.tech.ResearchLocations.front().LocationID; });
        researchAt.ref = RefKind::Unit;
        list.append(researchAt);
        list.append(numberField<TechRef>("research_time", "Research time", location, hasLocation,
                                         [](auto &t) -> auto & { return t.tech.ResearchLocations.front().QueueTime; }));
        list.append({"button", "Button", location, hasLocation,
                     [](const TechRef &t) { return intValue(t.tech.ResearchLocations.front().ButtonID); }});
        for (FieldDesc<TechRef> &field : list)
        {
            if (field.key == QStringLiteral("internal_name"))
                field.typeId = QMetaType::QString;
        }
        return list;
    }();
    return fields;
}

} // namespace newage
