#pragma once

#include <memory>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "genie/Types.h"

namespace newage {

// Where the interface sprites for an open .dat live, if they can be found
// next to it. Empty when the file was opened on its own.
struct SpriteSource
{
    // HD/DE loose sprites: the resources/_common/drs folder. Empty otherwise.
    QString looseFolder;
    // Classic and AoE DE DRS folders, first hit wins. RoR lists data2 before data.
    QStringList drsFolders;

    bool isEmpty() const { return looseFolder.isEmpty() && drsFolders.isEmpty(); }
};

// Looks for AGE's interface-sprite layouts in `gameDir`, the install the data
// set was found in, then walks up from `datPath`. A mod's .dat lives outside
// the install, so only `gameDir` finds its sprites. Does not open the files. A
// loose .dat that isn't inside an install comes back empty.
SpriteSource locateSpriteSource(const QString &datPath, genie::GameVersion version, const QString &gameDir = {});

// One decoded sprite frame, straight RGBA, or a null image when the frame
// isn't available. Kept as bytes so this stays QtCore-only; the view wraps it
// in a QImage.
struct SpriteImage
{
    int width = 0;
    int height = 0;
    QByteArray rgba;

    bool isNull() const { return width <= 0 || height <= 0 || rgba.size() != width * height * 4; }
};

// Loads interface SLPs and palette 50500 for the open game, and caches frames.
// Shared by the tech- and unit-icon previews. A miss — no install, no palette,
// frame -1, frame past the end — is a null image, not an error.
class SpriteLibrary
{
public:
    SpriteLibrary();
    ~SpriteLibrary();

    SpriteLibrary(const SpriteLibrary &) = delete;
    SpriteLibrary &operator=(const SpriteLibrary &) = delete;

    // Forgets anything already loaded and remembers where to look. Files are
    // opened on the first frame() call. `version` is the loaded game version:
    // it picks the DRS header size and which archive names to try. `gameDir`
    // is the install, if known; see locateSpriteSource().
    void setSource(const QString &datPath, genie::GameVersion version, const QString &gameDir = {});
    void clear();

    // Frame `frameId` of SLP `slpId`, or a null image. Negative ids are null.
    SpriteImage frame(int slpId, int frameId) const;

    // SLP resource that holds tech icons. `iconSet` is the selected civ's
    // Civ::IconSet; AoE and AoE2 ignore it. The frame is the tech's IconID.
    static int techIconSlpId(genie::GameVersion version, int iconSet);

    // SLP resource that holds this unit's icon. `type` and `unitClass` are
    // Unit::Type and Unit::Class: buildings use a different SLP, except packed
    // and unpacked siege. `iconSet` is the selected civ's Civ::IconSet; AoE and
    // AoE2 ignore it for non-building icons. The frame is unitIconFrame().
    static int unitIconSlpId(genie::GameVersion version, int iconSet, int type, int unitClass);

    // Frame of that SLP for IconID `iconId`. Buildings add GraphicsAngle.
    static int unitIconFrame(int iconId, int type, int graphicsAngle);

private:
    struct Cache;

    QString datPath_;
    QString gameDir_;
    genie::GameVersion version_ = genie::GV_None;
    mutable std::unique_ptr<Cache> cache_;
};

} // namespace newage
