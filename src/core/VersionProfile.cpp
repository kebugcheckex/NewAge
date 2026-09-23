#include "core/VersionProfile.h"

namespace newage {

// Ported from EditorVersionAsGameVersion() in AGE (AGE_Frame/Other.cpp).
// For the DE releases genieutils refines the version from the file header
// during load, so GV_Tapsa / GV_C2 are only the starting point.
const QList<VersionProfile> &versionProfiles()
{
    static const QList<VersionProfile> profiles = {
        {"aoe",      "Age of Empires (7.2)",                          genie::GV_AoE},
        {"ror",      "Rise of Rome (7.24)",                           genie::GV_RoR},
        {"aoede",    "Age of Empires: Definitive Edition",            genie::GV_Tapsa},
        {"aok",      "Age of Kings (11.5)",                           genie::GV_AoK},
        {"tc",       "The Conquerors (11.76)",                        genie::GV_TC},
        {"tcv",      "The Conquerors (11.76) + Terrain patch",        genie::GV_TCV},
        {"aokhd",    "Age of Kings HD (Forgotten / AK / Rajas)",      genie::GV_Cysion},
        {"aoe2de",   "Age of Empires II: Definitive Edition",         genie::GV_C2},
        {"swgb",     "Star Wars: Galactic Battlegrounds (1.0)",       genie::GV_SWGB},
        {"cc",       "Clone Campaigns (1.1)",                         genie::GV_CC},
        {"ef",       "Mod: Expanding Fronts (1.3 - 1.4)",             genie::GV_CCV},
        {"ef2",      "Mod: Expanding Fronts (>= 1.4.1)",              genie::GV_CCV2},
    };
    return profiles;
}

const VersionProfile *findVersionProfile(const QString &key)
{
    for (const VersionProfile &profile : versionProfiles())
    {
        if (profile.key == key)
            return &profile;
    }
    return nullptr;
}

} // namespace newage
