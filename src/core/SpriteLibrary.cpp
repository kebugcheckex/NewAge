#include "core/SpriteLibrary.h"

#include <algorithm>
#include <exception>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include "genie/dat/Unit.h"
#include "genie/resource/Color.h"
#include "genie/resource/DrsFile.h"
#include "genie/resource/PalFile.h"
#include "genie/resource/SlpFile.h"
#include "genie/resource/SlpFrame.h"

namespace newage {

namespace {

constexpr int kPaletteId = 50500;
constexpr int kMaxFrameEdge = 4096;

// genieutils opens files through std::fstream with narrow paths, so on
// Windows this is the ANSI code page. Paths outside it fail to open.
QByteArray nativePath(const QString &path)
{
    return QFile::encodeName(path);
}

// Resolves `relative` ('/'-separated) under `dir`, matching each part
// case-insensitively. Empty if it doesn't exist. Same walk as GameInstall.
QString findPath(const QString &dir, const QString &relative)
{
    QString current = QDir(dir).absolutePath();
    for (const QString &part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts))
    {
        const QString exact = QDir(current).filePath(part);
        if (QFileInfo::exists(exact))
        {
            current = exact;
            continue;
        }
        const QStringList entries = QDir(current).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        auto it = std::find_if(entries.begin(), entries.end(), [&](const QString &entry) {
            return entry.compare(part, Qt::CaseInsensitive) == 0;
        });
        if (it == entries.end())
            return {};
        current = QDir(current).filePath(*it);
    }
    return QDir::cleanPath(current);
}

QString parentDir(const QString &dir)
{
    const QString parent = QFileInfo(dir).absolutePath();
    return parent == dir ? QString() : parent;
}

void appendUnique(QStringList *folders, const QString &folder)
{
    if (folder.isEmpty())
        return;
    const QString clean = QDir::cleanPath(folder);
    for (const QString &have : *folders)
    {
        if (have.compare(clean, Qt::CaseInsensitive) == 0)
            return;
    }
    folders->append(clean);
}

bool isDir(const QString &path)
{
    return !path.isEmpty() && QFileInfo(path).isDir();
}

// Interface archives, first hit wins. The plain file is last so a missing
// expansion archive still falls through.
QStringList interfaceArchives(genie::GameVersion version)
{
    QStringList names;
    if (version >= genie::GV_CCV)
    {
        names << QStringLiteral("interfac_x2.drs") << QStringLiteral("interfac_x1_p1.drs")
              << QStringLiteral("interfac_p1.drs");
    }
    else if (version == genie::GV_CC)
    {
        names << QStringLiteral("interfac_x1.drs");
    }
    names << QStringLiteral("interfac.drs");
    return names;
}

QStringList paletteArchives(genie::GameVersion version)
{
    QStringList names = interfaceArchives(version);
    names << QStringLiteral("graphics.drs") << QStringLiteral("graphics_x1.drs")
          << QStringLiteral("graphics_x1_p1.drs") << QStringLiteral("graphics_p1.drs");
    return names;
}

bool hasLooseSprites(const QString &drsFolder)
{
    for (const char *name : {"gamedata_x2", "gamedata_x1", "interface", "graphics", "terrain"})
    {
        if (isDir(findPath(drsFolder, QString::fromLatin1(name))))
            return true;
    }
    return false;
}

SpriteSource probe(const QString &dir, genie::GameVersion version)
{
    const QString loose = findPath(dir, QStringLiteral("resources/_common/drs"));
    if (isDir(loose))
    {
        SpriteSource source;
        if (hasLooseSprites(loose))
            source.looseFolder = loose;
        if (!findPath(loose, QStringLiteral("interfac.drs")).isEmpty())
            source.drsFolders.append(loose);
        if (!source.isEmpty())
            return source;
    }

    for (const char *relative : {"DRS/interfac.drs", "data/DRS/interfac.drs"})
    {
        const QString file = findPath(dir, QString::fromLatin1(relative));
        if (!file.isEmpty())
        {
            SpriteSource source;
            source.drsFolders.append(QFileInfo(file).absolutePath());
            return source;
        }
    }

    SpriteSource source;
    const bool ror = version >= genie::GV_RoR && version < genie::GV_AoKE3;
    const auto addArchive = [&](const QString &relative) {
        const QString file = findPath(dir, relative);
        if (!file.isEmpty())
            appendUnique(&source.drsFolders, QFileInfo(file).absolutePath());
    };
    if (ror)
        addArchive(QStringLiteral("data2/interfac.drs"));
    addArchive(QStringLiteral("data/interfac.drs"));
    addArchive(QStringLiteral("interfac.drs"));
    // data2/empires.dat sits beside data/, and AGE reads data2's archive first.
    if (ror && QFileInfo(dir).fileName().compare(QStringLiteral("data2"), Qt::CaseInsensitive) == 0)
    {
        const QString sibling = findPath(parentDir(dir), QStringLiteral("data/interfac.drs"));
        if (!sibling.isEmpty())
            appendUnique(&source.drsFolders, QFileInfo(sibling).absolutePath());
    }
    return source;
}

QStringList looseSlpDirs(const QString &root)
{
    QStringList dirs;
    for (const char *name : {"gamedata_x2", "gamedata_x1", "interface", "graphics", "terrain"})
    {
        const QString path = findPath(root, QString::fromLatin1(name));
        if (isDir(path))
            dirs.append(path);
    }
    const QFileInfo info(root);
    if (info.fileName().compare(QStringLiteral("drs"), Qt::CaseInsensitive) == 0)
    {
        const QString slp = findPath(info.absolutePath(), QStringLiteral("slp"));
        if (isDir(slp))
            dirs.append(slp);
    }
    return dirs;
}

void putPixel(uchar *pixels, int width, int height, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0 || y < 0 || x >= width || y >= height)
        return;
    uchar *pixel = pixels + (y * width + x) * 4;
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
    pixel[3] = a;
}

SpriteImage decodeFrame(const genie::SlpFrame &frame, const std::vector<genie::Color> &palette)
{
    const int width = static_cast<int>(frame.getWidth());
    const int height = static_cast<int>(frame.getHeight());
    if (width <= 0 || height <= 0 || width > kMaxFrameEdge || height > kMaxFrameEdge)
        return {};

    SpriteImage image;
    image.width = width;
    image.height = height;
    image.rgba = QByteArray(width * height * 4, '\0');
    auto *pixels = reinterpret_cast<uchar *>(image.rgba.data());
    const genie::SlpFrameData &img = frame.img_data;

    if (frame.is32bit())
    {
        const int area = width * height;
        if (static_cast<int>(img.bgra_channels.size()) < area)
            return {};
        for (int i = 0; i < area; ++i)
        {
            const uint32_t bgra = img.bgra_channels[static_cast<size_t>(i)];
            uchar *pixel = pixels + i * 4;
            pixel[0] = static_cast<uint8_t>(bgra >> 16);
            pixel[1] = static_cast<uint8_t>(bgra >> 8);
            pixel[2] = static_cast<uint8_t>(bgra);
            pixel[3] = static_cast<uint8_t>(bgra >> 24);
        }
        for (const genie::XY16 &xy : img.transparency_mask)
        {
            if (xy.x >= width || xy.y >= height)
                continue;
            uchar *alpha = pixels + (xy.y * width + xy.x) * 4 + 3;
            *alpha = static_cast<uint8_t>(255 - *alpha);
        }
        return image;
    }

    const std::vector<genie::Color> *pal = &palette;
    if (!img.palette.empty())
        pal = &img.palette;
    if (pal->empty())
        return {};

    const int mainWidth = static_cast<int>(frame.getMainLayerWidth());
    const int mainOffX = frame.getMainLayerOffsetX();
    const int mainOffY = frame.getMainLayerOffsetY();
    if (mainWidth > 0)
    {
        for (size_t i = 0; i < img.pixel_indexes.size(); ++i)
        {
            const int x = static_cast<int>(i % static_cast<size_t>(mainWidth)) + mainOffX;
            const int y = static_cast<int>(i / static_cast<size_t>(mainWidth)) + mainOffY;
            const uint16_t colorId = img.pixel_indexes[i];
            const genie::Color color = colorId < pal->size() ? (*pal)[colorId] : genie::Color(0, 0, 0, 0);
            const uint8_t alpha = img.alpha_channel.empty() ? 255
                                   : i < img.alpha_channel.size() ? img.alpha_channel[i]
                                                                  : 0;
            putPixel(pixels, width, height, x, y, color.r, color.g, color.b, alpha);
        }
    }

    // Shadows are part of the graphic (AGE draws them by default). Outlines
    // and player colour are editor overlays, so they stay out.
    if (img.special_shadow_mask.empty())
    {
        for (const genie::XY16 &xy : img.shadow_mask)
            putPixel(pixels, width, height, xy.x + mainOffX, xy.y + mainOffY, 0, 0, 0, 127);
    }
    else
    {
        const int offX = frame.getShadowLayerOffsetX();
        const int offY = frame.getShadowLayerOffsetY();
        for (const genie::Color8XY16 &xy : img.special_shadow_mask)
            putPixel(pixels, width, height, xy.x + offX, xy.y + offY, 0, 0, 0, xy.index);
    }
    return image;
}

} // namespace

struct SpriteLibrary::Cache
{
    SpriteSource source;
    bool located = false;
    bool paletteReady = false;
    std::vector<genie::Color> palette;
    QHash<QString, genie::DrsFile *> archives;
    std::vector<std::unique_ptr<genie::DrsFile>> ownedArchives;
    QHash<int, genie::SlpFilePtr> slps;
    QHash<qint64, SpriteImage> frames;

    genie::DrsFile *openArchive(const QString &path, genie::GameVersion version)
    {
        if (archives.contains(path))
            return archives.value(path);
        // A short file isn't a DRS. genieutils doesn't always throw on one,
        // and a garbage table count would spin.
        if (QFileInfo(path).size() < 64)
        {
            archives.insert(path, nullptr);
            return nullptr;
        }
        auto file = std::make_unique<genie::DrsFile>();
        file->setGameVersion(version);
        try
        {
            file->load(nativePath(path).constData());
        }
        catch (...)
        {
            archives.insert(path, nullptr);
            return nullptr;
        }
        genie::DrsFile *raw = file.get();
        ownedArchives.push_back(std::move(file));
        archives.insert(path, raw);
        return raw;
    }

    void loadPalette(genie::GameVersion version)
    {
        if (paletteReady)
            return;
        paletteReady = true;

        if (!source.looseFolder.isEmpty())
        {
            const QString path = findPath(source.looseFolder, QStringLiteral("interface/50500.bina"));
            if (!path.isEmpty())
            {
                try
                {
                    genie::PalFile pal;
                    pal.load(nativePath(path).constData());
                    if (pal.size() > 0)
                    {
                        palette = pal.getColors();
                        return;
                    }
                }
                catch (...)
                {
                }
            }
        }

        const QStringList names = paletteArchives(version);
        for (const QString &folder : source.drsFolders)
        {
            for (const QString &name : names)
            {
                const QString path = findPath(folder, name);
                if (path.isEmpty())
                    continue;
                genie::DrsFile *file = openArchive(path, version);
                if (!file)
                    continue;
                try
                {
                    const genie::PalFilePtr pal = file->getPalFile(kPaletteId);
                    if (pal && pal->size() > 0)
                    {
                        palette = pal->getColors();
                        return;
                    }
                }
                catch (...)
                {
                }
            }
        }
    }

    genie::SlpFilePtr loadLoose(int slpId) const
    {
        const QString fileName = QString::number(slpId) + QStringLiteral(".slp");
        for (const QString &dir : looseSlpDirs(source.looseFolder))
        {
            const QString path = findPath(dir, fileName);
            if (path.isEmpty() || QFileInfo(path).size() < 32)
                continue;
            try
            {
                auto slp = std::make_shared<genie::SlpFile>();
                slp->loadAndRelease(nativePath(path).constData());
                if (slp->getFrameCount() > 0)
                    return slp;
            }
            catch (...)
            {
            }
        }
        return {};
    }

    genie::SlpFilePtr loadFromDrs(int slpId, genie::GameVersion version)
    {
        for (const QString &folder : source.drsFolders)
        {
            for (const QString &name : interfaceArchives(version))
            {
                const QString path = findPath(folder, name);
                if (path.isEmpty())
                    continue;
                genie::DrsFile *file = openArchive(path, version);
                if (!file)
                    continue;
                try
                {
                    genie::SlpFilePtr slp = file->getSlpFile(static_cast<uint32_t>(slpId));
                    if (slp && slp->getFrameCount() > 0)
                        return slp;
                }
                catch (...)
                {
                }
            }
        }
        return {};
    }

    genie::SlpFilePtr slp(int slpId, genie::GameVersion version)
    {
        if (slps.contains(slpId))
            return slps.value(slpId);
        genie::SlpFilePtr found;
        if (!source.looseFolder.isEmpty())
            found = loadLoose(slpId);
        if (!found)
            found = loadFromDrs(slpId, version);
        slps.insert(slpId, found);
        return found;
    }
};

SpriteSource locateSpriteSource(const QString &datPath, genie::GameVersion version)
{
    QString dir = QFileInfo(datPath).absolutePath();
    for (int depth = 0; depth < 8 && !dir.isEmpty(); ++depth)
    {
        const SpriteSource source = probe(dir, version);
        if (!source.isEmpty())
            return source;
        dir = parentDir(dir);
    }
    return {};
}

SpriteLibrary::SpriteLibrary() = default;

SpriteLibrary::~SpriteLibrary() = default;

void SpriteLibrary::setSource(const QString &datPath, genie::GameVersion version)
{
    clear();
    datPath_ = datPath;
    version_ = version;
}

void SpriteLibrary::clear()
{
    datPath_.clear();
    version_ = genie::GV_None;
    cache_.reset();
}

SpriteImage SpriteLibrary::frame(int slpId, int frameId) const
{
    if (slpId < 0 || frameId < 0 || datPath_.isEmpty())
        return {};
    if (!cache_)
        cache_ = std::make_unique<Cache>();
    if (!cache_->located)
    {
        cache_->located = true;
        cache_->source = locateSpriteSource(datPath_, version_);
    }
    if (cache_->source.isEmpty())
        return {};

    const qint64 key = (qint64(slpId) << 32) | quint32(frameId);
    if (cache_->frames.contains(key))
        return cache_->frames.value(key);

    SpriteImage image;
    try
    {
        cache_->loadPalette(version_);
        const genie::SlpFilePtr slp = cache_->slp(slpId, version_);
        if (slp && frameId < slp->getFrameCount())
        {
            const genie::SlpFramePtr decoded = slp->getFrame(static_cast<uint16_t>(frameId));
            if (decoded)
                image = decodeFrame(*decoded, cache_->palette);
        }
    }
    catch (...)
    {
        image = {};
    }
    cache_->frames.insert(key, image);
    return image;
}

int SpriteLibrary::techIconSlpId(genie::GameVersion version, int iconSet)
{
    if (iconSet < 0)
        iconSet = 0;
    // AGE's tech-icon table: a fixed SLP for AoE and AoE2, plus the civ icon
    // set for Star Wars. CC/EF bases are the ini defaults (53260 / 53360).
    if (version >= genie::GV_CCV2)
        return 53360 + iconSet;
    if (version >= genie::GV_CC)
        return 53260 + iconSet;
    if (version == genie::GV_SWGB)
        return 50689 + iconSet;
    return 50729;
}

int SpriteLibrary::unitIconSlpId(genie::GameVersion version, int iconSet, int type, int unitClass)
{
    if (iconSet < 0)
        iconSet = 0;
    // AGE's unit-icon table (Units.cpp). Type 80 uses the building-icon SLP,
    // except packed and unpacked siege — AoE/AoE2 classes 51 and 54, SWGB
    // classes 34 and 36 — which share the unit-icon SLP. AoE and AoE2 ignore
    // the civ icon set for unit icons. CC/EF bases are the ini defaults
    // (building 53240 / 53300, unit 53250 / 53330).
    const bool swgb = version >= genie::GV_SWGB;
    const bool buildingIcons = type == genie::UT_Building
                               && (swgb ? unitClass != 34 && unitClass != 36 : unitClass != 51 && unitClass != 54);
    if (buildingIcons)
    {
        int base = 50704;
        if (version >= genie::GV_CCV2)
            base = 53300;
        else if (version >= genie::GV_CC)
            base = 53240;
        return base + iconSet;
    }
    if (version >= genie::GV_CCV2)
        return 53330 + iconSet;
    if (version >= genie::GV_CC)
        return 53250 + iconSet;
    if (version == genie::GV_SWGB)
        return 50733 + iconSet;
    return 50730;
}

int SpriteLibrary::unitIconFrame(int iconId, int type, int graphicsAngle)
{
    // Only buildings store GraphicsAngle; AGE adds it to the icon frame.
    if (type == genie::UT_Building)
        return iconId + graphicsAngle;
    return iconId;
}

} // namespace newage
