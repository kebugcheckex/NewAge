#include "model/TechFields.h"

#include <algorithm>

#include "genie/dat/Research.h"

namespace newage {

namespace {

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
        FieldDesc<TechRef> civ =
            numberField<TechRef>("civ", "Civ", general, {}, [](auto &t) -> auto & { return t.tech.Civ; });
        civ.ref = RefKind::Civ;
        FieldDesc<TechRef> effect =
            numberField<TechRef>("effect", "Effect", general, {}, [](auto &t) -> auto & { return t.tech.EffectID; });
        effect.ref = RefKind::Effect;
        FieldDesc<TechRef> type =
            numberField<TechRef>("type", "Type", general, {}, [](auto &t) -> auto & { return t.tech.Type; });
        type.ref = RefKind::TechType;
        FieldDesc<TechRef> icon =
            numberField<TechRef>("icon", "Icon", general, {}, [](auto &t) -> auto & { return t.tech.IconID; });
        icon.sprite = SpriteKind::TechIcon;

        // genieutils widens the 16-bit variants into these on load, and they
        // share memory, so the 32-bit members are valid for every game
        // version. A file before C18 stores only the low 16 bits.
        const auto stringId = [&](const char *key, const char *name, auto access) {
            FieldDesc<TechRef> field = numberField<TechRef>(key, name, general, {}, access);
            field.isStringId = true;
            return field;
        };

        QList<FieldDesc<TechRef>> list = {
            {"id", "ID", general, {}, [](const TechRef &t) { return QVariant(t.id); }},
            // Length-prefixed or a debug string, by version; 255 is a
            // conservative limit.
            textField<TechRef>("internal_name", "Internal name", general, {},
                               [](auto &t) -> auto & { return t.tech.Name; }, 255),
            stringId("language_name", "Language name", [](auto &t) -> auto & { return t.tech.LanguageDLLName; }),
            stringId("language_description", "Language description",
                     [](auto &t) -> auto & { return t.tech.LanguageDLLDescription; }),
            // Stored from AoE beta on: the help text (by convention 100000
            // plus the name ID) and the tech tree text (150000 plus it).
            stringId("language_help", "Language help", [](auto &t) -> auto & { return t.tech.LanguageDLLHelp; }),
            stringId("language_tech_tree", "Language tech tree",
                     [](auto &t) -> auto & { return t.tech.LanguageDLLTechTree; }),
            type,
            civ,
            effect,
            icon,
            numberField<TechRef>("full_tech_mode", "Full tech mode", general, {},
                                 [](auto &t) -> auto & { return t.tech.FullTechMode; }),
        };

        // 4 slots in AoE/RoR, 6 from AoK on (Tech::getRequiredTechsSize).
        // Unused slots hold -1 and are left out, but can be written.
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
            field.settable = [i](const TechRef &t) { return i < static_cast<int>(t.tech.RequiredTechs.size()); };
            list.append(field);
        }
        list.append(numberField<TechRef>("required_tech_count", "Required tech count", requirements, {},
                                         [](auto &t) -> auto & { return t.tech.RequiredTechCount; }));

        // Unused cost slots have resource -1 and are left out, but can be
        // written: set the resource first, then the amount.
        for (int i = 0; i < 3; ++i)
        {
            const auto applies = [i](const TechRef &t) {
                return i < static_cast<int>(t.tech.ResourceCosts.size()) && t.tech.ResourceCosts.at(i).Type >= 0;
            };
            const auto settable = [i](const TechRef &t) { return i < static_cast<int>(t.tech.ResourceCosts.size()); };
            const QString slot = QString::number(i + 1);
            FieldDesc<TechRef> resource = numberField<TechRef>(
                QStringLiteral("cost%1.resource").arg(slot), QStringLiteral("Cost %1 resource").arg(slot), costs,
                applies, [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Type; });
            resource.ref = RefKind::Resource;
            resource.settable = settable;
            list.append(resource);
            list.append(numberField<TechRef>(QStringLiteral("cost%1.amount").arg(slot),
                                             QStringLiteral("Cost %1 amount").arg(slot), costs, applies,
                                             [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Amount; }));
            list.last().settable = settable;
            // 1: the amount is paid; 0: it only has to be available.
            list.append(numberField<TechRef>(QStringLiteral("cost%1.paid").arg(slot),
                                             QStringLiteral("Cost %1 paid").arg(slot), costs, applies,
                                             [i](auto &t) -> auto & { return t.tech.ResourceCosts.at(i).Flag; }));
            list.last().settable = settable;
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
        list.append(numberField<TechRef>("button", "Button", location, hasLocation,
                                         [](auto &t) -> auto & { return t.tech.ResearchLocations.front().ButtonID; }));
        list.append(numberField<TechRef>("hotkey", "Hotkey", location, hasLocation,
                                         [](auto &t) -> auto & { return t.tech.ResearchLocations.front().HotKeyID; }));
        return list;
    }();
    return fields;
}

void maskTechFields(genie::Tech &tech)
{
    tech.Name.clear();
    tech.LanguageDLLName = 0;
    tech.LanguageDLLNameU16 = 0;
    tech.LanguageDLLDescription = 0;
    tech.LanguageDLLDescriptionU16 = 0;
    tech.LanguageDLLHelp = 0;
    tech.LanguageDLLTechTree = 0;
    tech.Type = 0;
    tech.Civ = 0;
    tech.EffectID = 0;
    tech.IconID = 0;
    tech.FullTechMode = 0;
    std::fill(tech.RequiredTechs.begin(), tech.RequiredTechs.end(), 0);
    tech.RequiredTechCount = 0;
    for (auto &cost : tech.ResourceCosts)
    {
        cost.Type = 0;
        cost.Amount = 0;
        cost.Flag = 0;
    }
    if (!tech.ResearchLocations.empty())
    {
        tech.ResearchLocations.front().LocationID = 0;
        tech.ResearchLocations.front().QueueTime = 0;
        tech.ResearchLocations.front().ButtonID = 0;
        tech.ResearchLocations.front().HotKeyID = 0;
    }
}

} // namespace newage
