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

// Walks up from `datPath` looking for AGE's interface-sprite layouts. Does not
// open the files. A loose .dat that isn't inside an install comes back empty.
SpriteSource locateSpriteSource(const QString &datPath, genie::GameVersion version);

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
// Shared so a later unit-icon preview can use the same files. A miss — no
// install, no palette, frame -1, frame past the end — is a null image, not
// an error.
class SpriteLibrary
{
public:
    SpriteLibrary();
    ~SpriteLibrary();

    SpriteLibrary(const SpriteLibrary &) = delete;
    SpriteLibrary &operator=(const SpriteLibrary &) = delete;

    // Forgets anything already loaded and remembers where to look. Files are
    // opened on the first frame() call. `version` is the loaded game version:
    // it picks the DRS header size and which archive names to try.
    void setSource(const QString &datPath, genie::GameVersion version);
    void clear();

    // Frame `frameId` of SLP `slpId`, or a null image. Negative ids are null.
    SpriteImage frame(int slpId, int frameId) const;

    // SLP resource that holds tech icons. `iconSet` is the selected civ's
    // Civ::IconSet; AoE and AoE2 ignore it. The frame is the tech's IconID.
    static int techIconSlpId(genie::GameVersion version, int iconSet);

private:
    struct Cache;

    QString datPath_;
    genie::GameVersion version_ = genie::GV_None;
    mutable std::unique_ptr<Cache> cache_;
};

} // namespace newage
