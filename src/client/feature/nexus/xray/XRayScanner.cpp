#include "pch.h"
#include "XRayScanner.h"

#include "XRaySettings.h"

#include "client/event/Eventing.h"
#include "client/event/events/RenderLevelEvent.h"
#include "client/event/events/TickEvent.h"

#include "mc/common/client/renderer/MaterialPtr.h"
#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/level/block/BlockLegacy.h"

#include "util/DrawUtil3D.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Nexus {

    namespace {

        //
        // ============================================================
        // CAVE FACE MASK
        // ============================================================
        //

        constexpr std::uint8_t CaveFaceDown = 1u << 0;

        constexpr std::uint8_t CaveFaceUp = 1u << 1;

        constexpr std::uint8_t CaveFaceNorth = 1u << 2;

        constexpr std::uint8_t CaveFaceSouth = 1u << 3;

        constexpr std::uint8_t CaveFaceWest = 1u << 4;

        constexpr std::uint8_t CaveFaceEast = 1u << 5;

        constexpr std::uint8_t CaveFaceList[] = { CaveFaceDown,  CaveFaceUp,   CaveFaceNorth,
                                                  CaveFaceSouth, CaveFaceWest, CaveFaceEast };

        constexpr float CaveFaceInset = 0.01f;

        //
        // ============================================================
        // CAVE FACE INDEX
        // ============================================================
        //

        constexpr std::size_t caveFaceIndex(std::uint8_t face) {
            if (face == CaveFaceDown) {
                return 0;
            }

            if (face == CaveFaceUp) {
                return 1;
            }

            if (face == CaveFaceNorth) {
                return 2;
            }

            if (face == CaveFaceSouth) {
                return 3;
            }

            if (face == CaveFaceWest) {
                return 4;
            }

            return 5;
        }

        //
        // ============================================================
        // SMOOTH CAVE DEPTH LEVEL
        // ============================================================
        //
        // Levels 0-7 handle normal depth fading.
        //
        // Level 8 is reserved for terrain extremely close to the
        // camera. This preserves the close-wall foreground protection.
        //

        std::uint8_t caveDepthLevel(std::uint8_t solidDepth, float hiddenDistance, float firstSolidDistance) {
            //
            // ========================================================
            // SPECIAL CLOSE-FOREGROUND PROTECTION
            // ========================================================
            //
            // Keep this separate from normal smoothing.
            //
            // This is the behavior that stopped nearby stone/ore from
            // being washed over by stacked Cave ESP surfaces.
            //
            if (firstSolidDistance <= 1.5f) {
                return 8;
            }

            //
            // ========================================================
            // TERRAIN THICKNESS LEVEL
            // ========================================================
            //
            // 1 solid block -> level 0
            // 2 solid blocks -> level 1
            // ...
            // 8+ solid blocks -> level 7
            //

            int thicknessLevel = std::clamp(static_cast<int>(solidDepth) - 1, 0, 7);

            //
            // ========================================================
            // HIDDEN-GAP LEVEL
            // ========================================================
            //
            // Cave wall <= 2 blocks behind the first obstruction:
            // level 0.
            //
            // Cave wall >= 10 blocks behind the first obstruction:
            // level 7.
            //
            // Everything between is gradual.
            //

            float gapSeverity = std::clamp((hiddenDistance - 2.0f) / 8.0f, 0.0f, 1.0f);

            int gapLevel = static_cast<int>(std::lround(gapSeverity * 7.0f));

            //
            // ========================================================
            // FOREGROUND-PROXIMITY LEVEL
            // ========================================================
            //
            // The special <= 1.5 block case was already handled above.
            //
            // Around 3 blocks from the camera:
            // strongest normal suppression, level 7.
            //
            // Around 8+ blocks:
            // little/no proximity suppression.
            //

            float foregroundSeverity = std::clamp((8.0f - firstSolidDistance) / 5.0f, 0.0f, 1.0f);

            int foregroundLevel = static_cast<int>(std::lround(foregroundSeverity * 7.0f));

            //
            // Whichever depth-separation effect is strongest wins.
            //

            int finalLevel = std::max(thicknessLevel, std::max(gapLevel, foregroundLevel));

            return static_cast<std::uint8_t>(std::clamp(finalLevel, 0, 7));
        }

        //
        // ============================================================
        // ORE BLOCK OUTLINE
        // ============================================================
        //

        void drawOutlineBlock(MCDrawUtil3D& dc, BlockPos const& pos, d2d::Color const& color) {
            float x = static_cast<float>(pos.x);
            float y = static_cast<float>(pos.y);
            float z = static_cast<float>(pos.z);

            float x2 = x + 1.0f;
            float y2 = y + 1.0f;
            float z2 = z + 1.0f;

            //
            // Bottom
            //
            dc.drawLine({ x, y, z }, { x2, y, z }, color);

            dc.drawLine({ x2, y, z }, { x2, y, z2 }, color);

            dc.drawLine({ x2, y, z2 }, { x, y, z2 }, color);

            dc.drawLine({ x, y, z2 }, { x, y, z }, color);

            //
            // Top
            //
            dc.drawLine({ x, y2, z }, { x2, y2, z }, color);

            dc.drawLine({ x2, y2, z }, { x2, y2, z2 }, color);

            dc.drawLine({ x2, y2, z2 }, { x, y2, z2 }, color);

            dc.drawLine({ x, y2, z2 }, { x, y2, z }, color);

            //
            // Vertical
            //
            dc.drawLine({ x, y, z }, { x, y2, z }, color);

            dc.drawLine({ x2, y, z }, { x2, y2, z }, color);

            dc.drawLine({ x2, y, z2 }, { x2, y2, z2 }, color);

            dc.drawLine({ x, y, z2 }, { x, y2, z2 }, color);
        }

        //
        // ============================================================
        // ORE BLOCK FILL
        // ============================================================
        //

        void drawFilledBlock(MCDrawUtil3D& dc, BlockPos const& pos, d2d::Color const& color) {
            float x = static_cast<float>(pos.x);
            float y = static_cast<float>(pos.y);
            float z = static_cast<float>(pos.z);

            dc.fillQuad({ x, y, z }, { x + 1.f, y, z }, { x + 1.f, y, z + 1.f }, { x, y, z + 1.f }, color);

            dc.fillQuad({ x, y + 1.f, z }, { x + 1.f, y + 1.f, z }, { x + 1.f, y + 1.f, z + 1.f },
                        { x, y + 1.f, z + 1.f }, color);

            dc.fillQuad({ x, y, z }, { x, y + 1.f, z }, { x + 1.f, y + 1.f, z }, { x + 1.f, y, z }, color);

            dc.fillQuad({ x, y, z + 1.f }, { x, y + 1.f, z + 1.f }, { x + 1.f, y + 1.f, z + 1.f },
                        { x + 1.f, y, z + 1.f }, color);

            dc.fillQuad({ x, y, z }, { x, y + 1.f, z }, { x, y + 1.f, z + 1.f }, { x, y, z + 1.f }, color);

            dc.fillQuad({ x + 1.f, y, z }, { x + 1.f, y + 1.f, z }, { x + 1.f, y + 1.f, z + 1.f },
                        { x + 1.f, y, z + 1.f }, color);
        }

        //
        // ============================================================
        // GREEDY CAVE MESH FILL
        // ============================================================
        //

        void drawCaveMeshFill(MCDrawUtil3D& dc, std::uint8_t face, int plane, int u, int v, int width, int height,
                              d2d::Color const& color) {
            float p = static_cast<float>(plane);

            float u0 = static_cast<float>(u);

            float v0 = static_cast<float>(v);

            float u1 = static_cast<float>(u + width);

            float v1 = static_cast<float>(v + height);

            //
            // Down / Up
            //
            // U = X
            // V = Z
            //
            if (face == CaveFaceDown || face == CaveFaceUp) {
                float y = face == CaveFaceDown ? p + CaveFaceInset : p - CaveFaceInset;

                dc.fillQuad({ u0, y, v0 }, { u1, y, v0 }, { u1, y, v1 }, { u0, y, v1 }, color);

                return;
            }

            //
            // North / South
            //
            // U = X
            // V = Y
            //
            if (face == CaveFaceNorth || face == CaveFaceSouth) {
                float z = face == CaveFaceNorth ? p + CaveFaceInset : p - CaveFaceInset;

                dc.fillQuad({ u0, v0, z }, { u0, v1, z }, { u1, v1, z }, { u1, v0, z }, color);

                return;
            }

            //
            // West / East
            //
            // U = Z
            // V = Y
            //
            if (face == CaveFaceWest || face == CaveFaceEast) {
                float x = face == CaveFaceWest ? p + CaveFaceInset : p - CaveFaceInset;

                dc.fillQuad({ x, v0, u0 }, { x, v1, u0 }, { x, v1, u1 }, { x, v0, u1 }, color);
            }
        }

        //
        // ============================================================
        // CAVE OUTLINE POINT
        // ============================================================
        //
        // Converts the 2D coordinates used by each cave plane back
        // into a 3D world-space point.
        //

        Vec3 getCaveOutlinePoint(std::uint8_t face, int plane, int u, int v) {
            float p = static_cast<float>(plane);

            float uf = static_cast<float>(u);
            float vf = static_cast<float>(v);

            //
            // Down / Up
            //
            // U = X
            // V = Z
            //
            if (face == CaveFaceDown || face == CaveFaceUp) {
                float y = face == CaveFaceDown ? p + CaveFaceInset : p - CaveFaceInset;

                return { uf, y, vf };
            }

            //
            // North / South
            //
            // U = X
            // V = Y
            //
            if (face == CaveFaceNorth || face == CaveFaceSouth) {
                float z = face == CaveFaceNorth ? p + CaveFaceInset : p - CaveFaceInset;

                return { uf, vf, z };
            }

            //
            // West / East
            //
            // U = Z
            // V = Y
            //
            float x = face == CaveFaceWest ? p + CaveFaceInset : p - CaveFaceInset;

            return { x, vf, uf };
        }

    } // namespace

    //
    // ================================================================
    // INITIALIZATION
    // ================================================================
    //

    XRayScanner& XRayScanner::instance() {
        static XRayScanner scanner;

        return scanner;
    }

    void XRayScanner::initialize() {
        instance();
    }

    XRayScanner::XRayScanner() {
        Eventing::get().listen<TickEvent>(this, (EventListenerFunc)&XRayScanner::onTick, 0, true);

        Eventing::get().listen<RenderLevelEvent>(this, (EventListenerFunc)&XRayScanner::onRender, 0, true);
    }

    //
    // ================================================================
    // BLOCK KEY
    // ================================================================
    //

    XRayScanner::BlockKey XRayScanner::makeBlockKey(BlockPos const& pos) {
        return { pos.x, pos.y, pos.z };
    }

    //
    // ================================================================
    // SCAN ORDER
    // ================================================================
    //

    int XRayScanner::centeredOffset(int index) {
        if (index == 0) {
            return 0;
        }

        int distance = (index + 1) / 2;

        return (index & 1) ? -distance : distance;
    }

    //
    // ================================================================
    // RESET
    // ================================================================
    //

    void XRayScanner::resetScan(BlockPos const& center, int range) {
        scanCenter = center;
        activeRange = range;

        scanXIndex = 0;
        scanYIndex = 0;
        scanZIndex = 0;

        scanInitialized = true;

        pruneCachedOres(center, range);

        pruneCachedCaves(center, range);
    }

    //
    // ================================================================
    // ORE CLASSIFICATION
    // ================================================================
    //

    std::optional<XRayScanner::OreType> XRayScanner::classifyOre(SDK::Block* block) const {
        if (!block || !block->legacyBlock) {
            return std::nullopt;
        }

        std::string id = block->legacyBlock->namespacedId.getString();

        if (id == "minecraft:diamond_ore" || id == "minecraft:deepslate_diamond_ore") {
            return OreType::Diamond;
        }

        if (id == "minecraft:emerald_ore" || id == "minecraft:deepslate_emerald_ore") {
            return OreType::Emerald;
        }

        if (id == "minecraft:gold_ore" || id == "minecraft:deepslate_gold_ore" || id == "minecraft:nether_gold_ore") {
            return OreType::Gold;
        }

        if (id == "minecraft:iron_ore" || id == "minecraft:deepslate_iron_ore") {
            return OreType::Iron;
        }

        if (id == "minecraft:redstone_ore" || id == "minecraft:deepslate_redstone_ore" ||
            id == "minecraft:lit_redstone_ore") {
            return OreType::Redstone;
        }

        if (id == "minecraft:lapis_ore" || id == "minecraft:deepslate_lapis_ore") {
            return OreType::Lapis;
        }

        if (id == "minecraft:coal_ore" || id == "minecraft:deepslate_coal_ore") {
            return OreType::Coal;
        }

        if (id == "minecraft:copper_ore" || id == "minecraft:deepslate_copper_ore") {
            return OreType::Copper;
        }

        if (id == "minecraft:ancient_debris") {
            return OreType::AncientDebris;
        }

        return std::nullopt;
    }

    bool XRayScanner::isOreEnabled(OreType type) const {
        switch (type) {
        case OreType::Diamond:
            return xRaySettings.diamond;

        case OreType::Emerald:
            return xRaySettings.emerald;

        case OreType::Gold:
            return xRaySettings.gold;

        case OreType::Iron:
            return xRaySettings.iron;

        case OreType::Redstone:
            return xRaySettings.redstone;

        case OreType::Lapis:
            return xRaySettings.lapis;

        case OreType::Coal:
            return xRaySettings.coal;

        case OreType::Copper:
            return xRaySettings.copper;

        case OreType::AncientDebris:
            return xRaySettings.ancientDebris;
        }

        return false;
    }

    //
    // ================================================================
    // ORE CACHE
    // ================================================================
    //

    bool XRayScanner::containsOre(BlockPos const& pos) const {
        for (auto const& ore : ores) {
            if (ore.pos.x == pos.x && ore.pos.y == pos.y && ore.pos.z == pos.z) {
                return true;
            }
        }

        return false;
    }

    void XRayScanner::addOre(BlockPos const& pos, OreType type) {
        for (auto& ore : ores) {
            if (ore.pos.x == pos.x && ore.pos.y == pos.y && ore.pos.z == pos.z) {
                ore.type = type;
                return;
            }
        }

        ores.push_back({ pos, type });
    }

    void XRayScanner::pruneCachedOres(BlockPos const& center, int range) {
        int keepRange = range + RecenterDistance;

        long long keepRangeSq = static_cast<long long>(keepRange) * static_cast<long long>(keepRange);

        std::erase_if(ores, [&](OreHit const& ore) {
            long long dx = static_cast<long long>(ore.pos.x) - center.x;

            long long dy = static_cast<long long>(ore.pos.y) - center.y;

            long long dz = static_cast<long long>(ore.pos.z) - center.z;

            long long distanceSq = dx * dx + dy * dy + dz * dz;

            return distanceSq > keepRangeSq;
        });

        if (oreValidationIndex >= ores.size()) {
            oreValidationIndex = 0;
        }
    }

    void XRayScanner::validateCachedOres(SDK::BlockSource* region) {
        if (!region || ores.empty()) {
            oreValidationIndex = 0;
            return;
        }

        int checked = 0;

        while (checked < OreValidationPerTick && !ores.empty()) {
            if (oreValidationIndex >= ores.size()) {
                oreValidationIndex = 0;
            }

            BlockPos const pos = ores[oreValidationIndex].pos;

            SDK::Block* block = region->getBlock(pos);

            auto type = classifyOre(block);

            if (!type.has_value()) {
                ores.erase(ores.begin() + static_cast<std::ptrdiff_t>(oreValidationIndex));

                if (oreValidationIndex >= ores.size()) {
                    oreValidationIndex = 0;
                }
            } else {
                ores[oreValidationIndex].type = *type;

                ++oreValidationIndex;
            }

            ++checked;
        }
    }

    //
    // ================================================================
    // LITERAL AIR
    // ================================================================
    //

    bool XRayScanner::isAirBlock(SDK::Block* block) const {
        return classifyCaveBlockCached(block) == CaveBlockClass::Open;
    }

    //
    // ================================================================
    // CAVE-SPACE CLASSIFICATION
    // ================================================================
    //

    XRayScanner::CaveBlockClass XRayScanner::classifyCaveBlock(SDK::Block* block) const {
        if (!block || !block->legacyBlock) {
            //
            // Unknown/unloaded blocks should be conservative.
            //
            return CaveBlockClass::Solid;
        }

        std::string id = block->legacyBlock->namespacedId.getString();

        //
        // ============================================================
        // OPEN
        // ============================================================
        //

        if (id == "minecraft:air" || id == "minecraft:cave_air" || id == "minecraft:void_air") {
            return CaveBlockClass::Open;
        }

        //
        // ============================================================
        // FLUID
        // ============================================================
        //
        // Fluids occupy cave volume, but they should not behave like
        // solid stone for cave topology or visibility rays.
        //

        if (id == "minecraft:water" || id == "minecraft:flowing_water" || id == "minecraft:lava" ||
            id == "minecraft:flowing_lava" || id == "minecraft:bubble_column") {
            return CaveBlockClass::Fluid;
        }

        //
        // ============================================================
        // PARTIAL / NON-SOLID CONTENT
        // ============================================================
        //
        // These blocks exist inside otherwise open cave volume.
        // They should not create fake cave walls.
        //

        //
        // Torches / lights
        //
        if (id.find("torch") != std::string::npos) {
            return CaveBlockClass::Partial;
        }

        //
        // Rails
        //
        if (id == "minecraft:rail" || id.ends_with("_rail")) {
            return CaveBlockClass::Partial;
        }

        //
        // Redstone / interactables
        //
        if (id == "minecraft:redstone_wire" || id == "minecraft:lever" || id == "minecraft:tripwire" ||
            id == "minecraft:tripwire_hook" || id.ends_with("_button") || id.ends_with("_pressure_plate") ||
            id.find("repeater") != std::string::npos || id.find("comparator") != std::string::npos) {
            return CaveBlockClass::Partial;
        }

        //
        // Climbable / thin structures
        //
        if (id == "minecraft:ladder" || id == "minecraft:chain" || id == "minecraft:scaffolding" ||
            id.find("vine") != std::string::npos) {
            return CaveBlockClass::Partial;
        }

        //
        // Signs / banners
        //
        if (id.find("sign") != std::string::npos || id.find("banner") != std::string::npos) {
            return CaveBlockClass::Partial;
        }

        //
        // Floor / wall overlays
        //
        if (id == "minecraft:carpet" || id.ends_with("_carpet") || id == "minecraft:snow_layer" ||
            id == "minecraft:glow_lichen" || id == "minecraft:sculk_vein" || id == "minecraft:pink_petals" ||
            id == "minecraft:leaf_litter") {
            return CaveBlockClass::Partial;
        }

        //
        // Fire / portal-like blocks
        //
        if (id == "minecraft:fire" || id == "minecraft:soul_fire" || id == "minecraft:portal") {
            return CaveBlockClass::Partial;
        }

        //
        // Decorative thin blocks
        //
        if (id == "minecraft:cobweb" || id == "minecraft:flower_pot" || id == "minecraft:end_rod" ||
            id == "minecraft:lightning_rod" || id.find("candle") != std::string::npos ||
            id.find("coral_fan") != std::string::npos) {
            return CaveBlockClass::Partial;
        }

        //
        // Ordinary plants
        //
        if (id == "minecraft:short_grass" || id == "minecraft:tallgrass" || id == "minecraft:fern" ||
            id == "minecraft:large_fern" || id == "minecraft:deadbush" ||

            id == "minecraft:dandelion" || id == "minecraft:poppy" || id == "minecraft:blue_orchid" ||
            id == "minecraft:allium" || id == "minecraft:azure_bluet" || id == "minecraft:red_tulip" ||
            id == "minecraft:orange_tulip" || id == "minecraft:white_tulip" || id == "minecraft:pink_tulip" ||
            id == "minecraft:oxeye_daisy" || id == "minecraft:cornflower" || id == "minecraft:lily_of_the_valley" ||
            id == "minecraft:wither_rose" ||

            id == "minecraft:sunflower" || id == "minecraft:lilac" || id == "minecraft:rose_bush" ||
            id == "minecraft:peony" ||

            id == "minecraft:brown_mushroom" || id == "minecraft:red_mushroom" ||

            id == "minecraft:crimson_fungus" || id == "minecraft:warped_fungus" || id == "minecraft:crimson_roots" ||
            id == "minecraft:warped_roots" || id == "minecraft:nether_sprouts" || id == "minecraft:hanging_roots" ||

            id == "minecraft:spore_blossom") {
            return CaveBlockClass::Partial;
        }

        //
        // Underwater plants
        //
        if (id == "minecraft:seagrass" || id == "minecraft:sea_pickle" || id.find("kelp") != std::string::npos) {
            return CaveBlockClass::Partial;
        }

        //
        // Cave-specific small features
        //
        if (id == "minecraft:pointed_dripstone" ||

            id == "minecraft:small_amethyst_bud" || id == "minecraft:medium_amethyst_bud" ||
            id == "minecraft:large_amethyst_bud" || id == "minecraft:amethyst_cluster") {
            return CaveBlockClass::Partial;
        }

        //
        // Everything else is considered structural terrain.
        //
        return CaveBlockClass::Solid;
    }

    //
    // ================================================================
    // CACHED CAVE BLOCK CLASSIFICATION
    // ================================================================
    //

    XRayScanner::CaveBlockClass XRayScanner::classifyCaveBlockCached(SDK::Block* block) const {
        //
        // Unknown/unloaded blocks stay conservative.
        //
        if (!block || !block->legacyBlock) {
            return CaveBlockClass::Solid;
        }

        SDK::BlockLegacy* legacy = block->legacyBlock;

        auto found = caveBlockClassCache.find(legacy);

        if (found != caveBlockClassCache.end()) {
            return found->second;
        }

        //
        // First time this BlockLegacy has been encountered in this
        // cache. Run the full string-based classification once.
        //
        CaveBlockClass result = classifyCaveBlock(block);

        caveBlockClassCache.emplace(legacy, result);

        return result;
    }

    bool XRayScanner::isCaveSpaceBlock(SDK::Block* block) const {
        CaveBlockClass blockClass = classifyCaveBlockCached(block);

        return blockClass == CaveBlockClass::Open || blockClass == CaveBlockClass::Partial ||
               blockClass == CaveBlockClass::Fluid;
    }

    //
    // ================================================================
    // 3x3x3 OPEN-SPACE CHECK
    // ================================================================
    //

    bool XRayScanner::passesAirCheck(SDK::BlockSource* region, BlockPos const& pos) const {
        if (!region) {
            return false;
        }

        if (!xRaySettings.airCheck3x3x3) {
            return true;
        }

        int openCount = 0;

        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                for (int z = -1; z <= 1; ++z) {
                    BlockPos checkPos { pos.x + x, pos.y + y, pos.z + z };

                    SDK::Block* block = region->getBlock(checkPos);

                    if (isCaveSpaceBlock(block)) {
                        ++openCount;

                        if (openCount >= MinimumAirNeighbors) {
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    }

    //
    // ================================================================
    // SURFACE FILTER
    // ================================================================
    //

    bool XRayScanner::hasRoofAbove(SDK::BlockSource* region, BlockPos const& pos) const {
        if (!region) {
            return false;
        }

        if (!xRaySettings.ignoreSurface) {
            return true;
        }

        for (int offset = 1; offset <= RoofCheckDistance; ++offset) {
            BlockPos checkPos { pos.x, pos.y + offset, pos.z };

            SDK::Block* block = region->getBlock(checkPos);

            if (!block) {
                continue;
            }

            if (!isCaveSpaceBlock(block)) {
                return true;
            }
        }

        return false;
    }

    //
    // ================================================================
    // CAVE CANDIDATE
    // ================================================================
    //

    bool XRayScanner::isCaveAirCandidate(SDK::BlockSource* region, BlockPos const& pos) const {
        if (!region) {
            return false;
        }

        SDK::Block* block = region->getBlock(pos);

        if (!isCaveSpaceBlock(block)) {
            return false;
        }

        if (!passesAirCheck(region, pos)) {
            return false;
        }

        if (!hasRoofAbove(region, pos)) {
            return false;
        }

        return true;
    }

    //
    // ================================================================
    // STRUCTURAL CAVE FACES
    // ================================================================
    //

    std::uint8_t XRayScanner::getExposedCaveFaces(SDK::BlockSource* region, BlockPos const& pos) const {
        if (!region) {
            return 0;
        }

        std::uint8_t faces = 0;

        auto isStructural = [&](BlockPos const& checkPos) -> bool {
            SDK::Block* block = region->getBlock(checkPos);

            if (!block) {
                return false;
            }

            return !isCaveSpaceBlock(block);
        };

        if (isStructural({ pos.x, pos.y - 1, pos.z })) {
            faces |= CaveFaceDown;
        }

        if (isStructural({ pos.x, pos.y + 1, pos.z })) {
            faces |= CaveFaceUp;
        }

        if (isStructural({ pos.x, pos.y, pos.z - 1 })) {
            faces |= CaveFaceNorth;
        }

        if (isStructural({ pos.x, pos.y, pos.z + 1 })) {
            faces |= CaveFaceSouth;
        }

        if (isStructural({ pos.x - 1, pos.y, pos.z })) {
            faces |= CaveFaceWest;
        }

        if (isStructural({ pos.x + 1, pos.y, pos.z })) {
            faces |= CaveFaceEast;
        }

        return faces;
    }

    //
    // ================================================================
    // CAVE CACHE
    // ================================================================
    //

    void XRayScanner::addOrUpdateCave(BlockPos const& pos, std::uint8_t faces) {
        if (faces == 0) {
            return;
        }

        BlockKey key = makeBlockKey(pos);

        auto existing = caveIndex.find(key);

        if (existing != caveIndex.end()) {
            CaveHit& cave = caves[existing->second];

            std::uint8_t oldFaces = cave.faces;

            std::uint8_t oldOccluded = cave.occludedFaces;

            cave.faces = faces;

            //
            // Remove stale face bits from both accepted and pending
            // occlusion state.
            //
            cave.occludedFaces &= faces;
            cave.pendingOccludedFaces &= faces;

            //
            // Clear depth for structural faces that no longer exist.
            //
            for (std::uint8_t face : CaveFaceList) {
                if ((faces & face) == 0) {
                    cave.depthBuckets[caveFaceIndex(face)] = 0;
                }
            }

            //
            // Structural topology changed, so an old pending
            // transition is no longer trustworthy.
            //
            if (oldFaces != cave.faces) {
                cave.pendingOccludedFaces = cave.occludedFaces;
                cave.occlusionConfirmations = 0;
            }

            if (oldFaces != cave.faces || oldOccluded != cave.occludedFaces) {
                //
                // The fragment graph only cares whether this cave cell
                // has ANY hidden face, not which particular faces are
                // hidden.
                //
                bool oldRenderable = oldOccluded != 0;

                bool newRenderable = cave.occludedFaces != 0;

                if (oldRenderable != newRenderable) {
                    caveRenderableDirty = true;
                }

                caveMeshDirty = true;
            }

            return;
        }

        std::size_t index = caves.size();

        //
        // New cave cells stay hidden until region classification
        // and occlusion testing have both run.
        //
        caves.push_back({ pos, faces, 0, 0, 0, false });

        caveIndex.emplace(key, index);

        caveRegionsDirty = true;
        caveRenderableDirty = true;
        caveMeshDirty = true;
    }

    void XRayScanner::eraseCaveAt(std::size_t index) {
        if (index >= caves.size()) {
            return;
        }

        BlockKey removedKey = makeBlockKey(caves[index].pos);

        std::size_t lastIndex = caves.size() - 1;

        if (index != lastIndex) {
            caves[index] = caves[lastIndex];

            BlockKey movedKey = makeBlockKey(caves[index].pos);

            auto moved = caveIndex.find(movedKey);

            if (moved != caveIndex.end()) {
                moved->second = index;
            }
        }

        caves.pop_back();

        caveIndex.erase(removedKey);

        //
        // Removing one cave-space cell can split a region.
        //
        caveRegionsDirty = true;

        //
        // caves[] indices changed, so the cached mask is invalid.
        //
        caveRenderableDirty = true;

        caveMeshDirty = true;

        if (caveValidationIndex >= caves.size()) {
            caveValidationIndex = 0;
        }

        if (caveNearOcclusionIndex >= caves.size()) {
            caveNearOcclusionIndex = 0;
        }

        if (caveFarOcclusionIndex >= caves.size()) {
            caveFarOcclusionIndex = 0;
        }
    }

    void XRayScanner::pruneCachedCaves(BlockPos const& center, int range) {
        int keepRange = range + RecenterDistance;

        long long keepRangeSq = static_cast<long long>(keepRange) * static_cast<long long>(keepRange);

        std::size_t index = 0;

        while (index < caves.size()) {
            CaveHit const& cave = caves[index];

            long long dx = static_cast<long long>(cave.pos.x) - center.x;

            long long dy = static_cast<long long>(cave.pos.y) - center.y;

            long long dz = static_cast<long long>(cave.pos.z) - center.z;

            long long distanceSq = dx * dx + dy * dy + dz * dz;

            if (distanceSq > keepRangeSq) {
                eraseCaveAt(index);
                continue;
            }

            ++index;
        }

        if (caveValidationIndex >= caves.size()) {
            caveValidationIndex = 0;
        }

        if (caveNearOcclusionIndex >= caves.size()) {
            caveNearOcclusionIndex = 0;
        }

        if (caveFarOcclusionIndex >= caves.size()) {
            caveFarOcclusionIndex = 0;
        }
    }

    void XRayScanner::validateCachedCaves(SDK::BlockSource* region) {
        if (!region || caves.empty()) {
            caveValidationIndex = 0;
            caveNearOcclusionIndex = 0;
            caveFarOcclusionIndex = 0;
            return;
        }

        int checked = 0;

        while (checked < CaveValidationPerTick && !caves.empty()) {
            if (caveValidationIndex >= caves.size()) {
                caveValidationIndex = 0;
            }

            BlockPos const pos = caves[caveValidationIndex].pos;

            if (!isCaveAirCandidate(region, pos)) {
                eraseCaveAt(caveValidationIndex);

                ++checked;
                continue;
            }

            std::uint8_t faces = getExposedCaveFaces(region, pos);

            if (faces == 0) {
                eraseCaveAt(caveValidationIndex);

                ++checked;
                continue;
            }

            CaveHit& cave = caves[caveValidationIndex];

            std::uint8_t oldFaces = cave.faces;

            std::uint8_t oldOccluded = cave.occludedFaces;

            cave.faces = faces;

            cave.occludedFaces &= faces;
            cave.pendingOccludedFaces &= faces;

            for (std::uint8_t face : CaveFaceList) {
                if ((faces & face) == 0) {
                    cave.depthBuckets[caveFaceIndex(face)] = 0;
                }
            }

            if (oldFaces != cave.faces) {
                cave.pendingOccludedFaces = cave.occludedFaces;
                cave.occlusionConfirmations = 0;
            }

            if (oldFaces != cave.faces || oldOccluded != cave.occludedFaces) {
                bool oldRenderable = oldOccluded != 0;

                bool newRenderable = cave.occludedFaces != 0;

                if (oldRenderable != newRenderable) {
                    caveRenderableDirty = true;
                }

                caveMeshDirty = true;
            }

            ++caveValidationIndex;
            ++checked;
        }
    }

    //
    // ================================================================
    // CAVE FACE CENTER
    // ================================================================
    //

    Vec3 XRayScanner::getCaveFaceCenter(BlockPos const& pos, std::uint8_t face) {
        constexpr float inset = 0.06f;

        Vec3 center { static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f,
                      static_cast<float>(pos.z) + 0.5f };

        if (face == CaveFaceDown) {
            center.y = static_cast<float>(pos.y) + inset;
        } else if (face == CaveFaceUp) {
            center.y = static_cast<float>(pos.y) + 1.0f - inset;
        } else if (face == CaveFaceNorth) {
            center.z = static_cast<float>(pos.z) + inset;
        } else if (face == CaveFaceSouth) {
            center.z = static_cast<float>(pos.z) + 1.0f - inset;
        } else if (face == CaveFaceWest) {
            center.x = static_cast<float>(pos.x) + inset;
        } else if (face == CaveFaceEast) {
            center.x = static_cast<float>(pos.x) + 1.0f - inset;
        }

        return center;
    }

    //
    // ================================================================
    // VOXEL OCCLUSION RAY
    // ================================================================
    //

    XRayScanner::CaveRayInfo XRayScanner::getCaveRayInfo(SDK::BlockSource* region, Vec3 const& start,
                                                         Vec3 const& end) const {
        CaveRayInfo info {};

        if (!region) {
            return info;
        }

        float dx = end.x - start.x;
        float dy = end.y - start.y;
        float dz = end.z - start.z;

        float lengthSq = dx * dx + dy * dy + dz * dz;

        if (lengthSq <= 0.000001f) {
            return info;
        }

        info.targetDistance = std::sqrt(lengthSq);

        int x = static_cast<int>(std::floor(start.x));
        int y = static_cast<int>(std::floor(start.y));
        int z = static_cast<int>(std::floor(start.z));

        int endX = static_cast<int>(std::floor(end.x));
        int endY = static_cast<int>(std::floor(end.y));
        int endZ = static_cast<int>(std::floor(end.z));

        int stepX = dx > 0.0f ? 1 : (dx < 0.0f ? -1 : 0);
        int stepY = dy > 0.0f ? 1 : (dy < 0.0f ? -1 : 0);
        int stepZ = dz > 0.0f ? 1 : (dz < 0.0f ? -1 : 0);

        constexpr float infinity = std::numeric_limits<float>::infinity();

        float tDeltaX = stepX == 0 ? infinity : std::abs(1.0f / dx);

        float tDeltaY = stepY == 0 ? infinity : std::abs(1.0f / dy);

        float tDeltaZ = stepZ == 0 ? infinity : std::abs(1.0f / dz);

        auto calculateInitialT = [](float coordinate, float delta, int step) -> float {
            if (step > 0) {
                float nextBoundary = std::floor(coordinate) + 1.0f;

                return (nextBoundary - coordinate) / delta;
            }

            if (step < 0) {
                float previousBoundary = std::floor(coordinate);

                return (coordinate - previousBoundary) / (-delta);
            }

            return std::numeric_limits<float>::infinity();
        };

        float tMaxX = calculateInitialT(start.x, dx, stepX);

        float tMaxY = calculateInitialT(start.y, dy, stepY);

        float tMaxZ = calculateInitialT(start.z, dz, stepZ);

        bool foundFirstSolid = false;

        int safety = 0;

        while ((x != endX || y != endY || z != endZ) && safety < 512) {
            ++safety;

            float nextT = std::min(tMaxX, std::min(tMaxY, tMaxZ));

            if (!std::isfinite(nextT) || nextT > 1.0f) {
                break;
            }

            constexpr float epsilon = 0.00001f;

            if (tMaxX <= nextT + epsilon) {
                x += stepX;
                tMaxX += tDeltaX;
            }

            if (tMaxY <= nextT + epsilon) {
                y += stepY;
                tMaxY += tDeltaY;
            }

            if (tMaxZ <= nextT + epsilon) {
                z += stepZ;
                tMaxZ += tDeltaZ;
            }

            //
            // Do not count the destination cave-space cell.
            //
            if (x == endX && y == endY && z == endZ) {
                break;
            }

            if (y < -64 || y > 320) {
                continue;
            }

            SDK::Block* block = region->getBlock(BlockPos { x, y, z });

            if (!block) {
                continue;
            }

            CaveBlockClass blockClass = classifyCaveBlockCached(block);

            if (blockClass != CaveBlockClass::Solid) {
                continue;
            }

            //
            // Record the distance to the FIRST solid block.
            //
            if (!foundFirstSolid) {
                float normalizedT = std::clamp(nextT, 0.0f, 1.0f);

                info.firstSolidDistance = normalizedT * info.targetDistance;

                foundFirstSolid = true;
            }

            //
            // Count solid terrain crossed.
            //
            // Cave depth rendering only has useful thickness information
            // through 8 solid voxels. At 8+ blocks, the thickness component
            // is already at its maximum level.
            //
            if (info.solidDepth < 8) {
                ++info.solidDepth;
            }

            //
            // Once 8 solid blocks have been crossed, continuing this DDA
            // cannot change any value used by Cave ESP:
            //
            // - the ray is already known to be occluded
            // - firstSolidDistance is already known
            // - targetDistance was calculated before traversal
            // - solidDepth has reached the maximum useful depth level
            //
            // Stop tracing through additional terrain.
            //
            if (info.solidDepth >= 8) {
                break;
            }
        }

        return info;
    }

    std::uint8_t XRayScanner::getLineSolidDepth(SDK::BlockSource* region, Vec3 const& start, Vec3 const& end) const {
        return getCaveRayInfo(region, start, end).solidDepth;
    }

    bool XRayScanner::isLineOccluded(SDK::BlockSource* region, Vec3 const& start, Vec3 const& end) const {
        return getLineSolidDepth(region, start, end) > 0;
    }

    //
    // ================================================================
    // VIEW-DEPENDENT CAVE OCCLUSION
    // ================================================================
    //

    void XRayScanner::updateCaveOcclusion(SDK::BlockSource* region, Vec3 const& viewOrigin) {
        if (!region || caves.empty()) {
            caveNearOcclusionIndex = 0;
            caveFarOcclusionIndex = 0;
            return;
        }

        //
        // ============================================================
        // DISTANCE-PRIORITIZED RAY BUDGET
        // ============================================================
        //
        // Pass 1:
        //     nearby caves receive priority.
        //
        // Pass 2:
        //     distant caves receive the remaining total budget.
        //
        // The caves[] vector is NOT sorted or reordered.
        //

        int raysUsed = 0;

        float nearDistanceSq = CaveNearOcclusionDistance * CaveNearOcclusionDistance;

        //
        // ============================================================
        // DISTANCE TEST
        // ============================================================
        //

        auto isNearCave = [&](CaveHit const& cave) -> bool {
            float x = static_cast<float>(cave.pos.x) + 0.5f;

            float y = static_cast<float>(cave.pos.y) + 0.5f;

            float z = static_cast<float>(cave.pos.z) + 0.5f;

            float dx = x - viewOrigin.x;

            float dy = y - viewOrigin.y;

            float dz = z - viewOrigin.z;

            float distanceSq = dx * dx + dy * dy + dz * dz;

            return distanceSq <= nearDistanceSq;
        };

        //
        // ============================================================
        // PROCESS ONE COMPLETE CAVE CELL
        // ============================================================
        //
        // Returns false when the cell would exceed this pass's ray
        // budget. In that case the caller leaves its cursor on the
        // current cell so it gets first chance next tick.
        //

        auto processCave = [&](std::size_t caveIndexValue, int rayLimit) -> bool {
            if (caveIndexValue >= caves.size()) {
                return true;
            }

            CaveHit& cave = caves[caveIndexValue];

            //
            // ====================================================
            // REJECTED REGION
            // ====================================================
            //
            // Costs no rays.
            //

            if (!cave.regionVisible) {
                bool changed = cave.occludedFaces != 0;

                cave.occludedFaces = 0;
                cave.pendingOccludedFaces = 0;
                cave.occlusionConfirmations = 0;

                for (std::uint8_t& bucket : cave.depthBuckets) {
                    bucket = 0;
                }

                if (changed) {
                    caveRenderableDirty = true;
                    caveMeshDirty = true;
                }

                return true;
            }

            //
            // ====================================================
            // CELL RAY COST
            // ====================================================
            //

            int faceRayCost = 0;

            for (std::uint8_t face : CaveFaceList) {
                if ((cave.faces & face) != 0) {
                    ++faceRayCost;
                }
            }

            //
            // Defensive zero-face case.
            //
            if (faceRayCost == 0) {
                bool changed = cave.occludedFaces != 0;

                cave.occludedFaces = 0;
                cave.pendingOccludedFaces = 0;
                cave.occlusionConfirmations = 0;

                for (std::uint8_t& bucket : cave.depthBuckets) {
                    bucket = 0;
                }

                if (changed) {
                    caveRenderableDirty = true;
                    caveMeshDirty = true;
                }

                return true;
            }

            //
            // Keep the cave-cell update atomic.
            //
            // Never trace only some faces.
            //
            if (raysUsed + faceRayCost > rayLimit) {
                return false;
            }

            std::uint8_t desiredOccludedFaces = 0;

            std::array<std::uint8_t, 6> desiredDepthBuckets {};

            //
            // ====================================================
            // PER-FACE DDA
            // ====================================================
            //

            for (std::uint8_t face : CaveFaceList) {
                if ((cave.faces & face) == 0) {
                    continue;
                }

                Vec3 target = getCaveFaceCenter(cave.pos, face);

                CaveRayInfo rayInfo = getCaveRayInfo(region, viewOrigin, target);

                ++raysUsed;

                if (rayInfo.solidDepth == 0) {
                    continue;
                }

                desiredOccludedFaces |= face;

                float hiddenDistance = std::max(0.0f, rayInfo.targetDistance - rayInfo.firstSolidDistance);

                std::uint8_t depthLevel =
                    caveDepthLevel(rayInfo.solidDepth, hiddenDistance, rayInfo.firstSolidDistance);

                desiredDepthBuckets[caveFaceIndex(face)] = depthLevel;
            }

            desiredOccludedFaces &= cave.faces;

            bool depthChanged = false;

            //
            // ====================================================
            // OCCLUSION HYSTERESIS
            // ====================================================
            //

            if (desiredOccludedFaces == cave.occludedFaces) {
                cave.pendingOccludedFaces = desiredOccludedFaces;

                cave.occlusionConfirmations = 0;

                for (std::uint8_t face : CaveFaceList) {
                    std::size_t index = caveFaceIndex(face);

                    std::uint8_t newBucket = 0;

                    if ((desiredOccludedFaces & face) != 0) {
                        newBucket = desiredDepthBuckets[index];
                    }

                    if (cave.depthBuckets[index] != newBucket) {
                        cave.depthBuckets[index] = newBucket;

                        depthChanged = true;
                    }
                }
            }

            else if (desiredOccludedFaces == cave.pendingOccludedFaces) {
                if (cave.occlusionConfirmations < 255) {
                    ++cave.occlusionConfirmations;
                }

                if (cave.occlusionConfirmations >= 2) {
                    bool oldRenderable = cave.occludedFaces != 0;

                    cave.occludedFaces = desiredOccludedFaces;

                    cave.pendingOccludedFaces = desiredOccludedFaces;

                    cave.occlusionConfirmations = 0;

                    bool newRenderable = cave.occludedFaces != 0;

                    if (oldRenderable != newRenderable) {
                        caveRenderableDirty = true;
                    }

                    for (std::uint8_t face : CaveFaceList) {
                        std::size_t index = caveFaceIndex(face);

                        if ((cave.occludedFaces & face) != 0) {
                            cave.depthBuckets[index] = desiredDepthBuckets[index];
                        } else {
                            cave.depthBuckets[index] = 0;
                        }
                    }

                    caveMeshDirty = true;
                }

                else {
                    for (std::uint8_t face : CaveFaceList) {
                        if ((cave.occludedFaces & face) == 0) {
                            continue;
                        }

                        if ((desiredOccludedFaces & face) == 0) {
                            continue;
                        }

                        std::size_t index = caveFaceIndex(face);

                        if (cave.depthBuckets[index] != desiredDepthBuckets[index]) {
                            cave.depthBuckets[index] = desiredDepthBuckets[index];

                            depthChanged = true;
                        }
                    }
                }
            }

            else {
                cave.pendingOccludedFaces = desiredOccludedFaces;

                cave.occlusionConfirmations = 1;

                for (std::uint8_t face : CaveFaceList) {
                    if ((cave.occludedFaces & face) == 0) {
                        continue;
                    }

                    if ((desiredOccludedFaces & face) == 0) {
                        continue;
                    }

                    std::size_t index = caveFaceIndex(face);

                    if (cave.depthBuckets[index] != desiredDepthBuckets[index]) {
                        cave.depthBuckets[index] = desiredDepthBuckets[index];

                        depthChanged = true;
                    }
                }
            }

            if (depthChanged) {
                caveMeshDirty = true;
            }

            return true;
        };

        //
        // ============================================================
        // GENERIC PRIORITY PASS
        // ============================================================
        //

        auto runPass = [&](std::size_t& cursor, bool wantNear, int rayLimit) {
            std::size_t examined = 0;

            while (examined < caves.size() && raysUsed < rayLimit) {
                if (cursor >= caves.size()) {
                    cursor = 0;
                }

                std::size_t currentIndex = cursor;

                bool currentIsNear = isNearCave(caves[currentIndex]);

                //
                // This cell belongs to the other priority band.
                //
                if (currentIsNear != wantNear) {
                    ++cursor;
                    ++examined;
                    continue;
                }

                //
                // The current cell would exceed the remaining
                // budget.
                //
                // Leave the cursor here so it is first next tick.
                //
                if (!processCave(currentIndex, rayLimit)) {
                    break;
                }

                ++cursor;
                ++examined;
            }
        };

        //
        // ============================================================
        // PASS 1 — NEAR CAVES
        // ============================================================
        //

        runPass(caveNearOcclusionIndex, true, CaveNearOcclusionRaysPerTick);

        //
        // ============================================================
        // PASS 2 — DISTANT CAVES
        // ============================================================
        //
        // raysUsed already contains the near-pass cost.
        //
        // Therefore unused nearby budget automatically becomes
        // available here.
        //

        runPass(caveFarOcclusionIndex, false, CaveOcclusionRaysPerTick);
    }

    //
    // ================================================================
    // CONNECTED CAVE REGION FILTER
    // ================================================================
    //

    void XRayScanner::rebuildCaveRegions() {
        if (caves.empty()) {
            caveRegionsDirty = false;
            caveRenderableDirty = true;
            caveMeshDirty = true;

            return;
        }

        std::vector<std::uint8_t> visited(caves.size(), 0);

        constexpr int neighborOffsets[6][3] = { { -1, 0, 0 }, { 1, 0, 0 },  { 0, -1, 0 },
                                                { 0, 1, 0 },  { 0, 0, -1 }, { 0, 0, 1 } };

        std::vector<std::size_t> stack;

        std::vector<std::size_t> component;

        stack.reserve(256);
        component.reserve(256);

        for (std::size_t startIndex = 0; startIndex < caves.size(); ++startIndex) {
            if (visited[startIndex]) {
                continue;
            }

            stack.clear();
            component.clear();

            stack.push_back(startIndex);

            visited[startIndex] = 1;

            BlockPos const& startPos = caves[startIndex].pos;

            int minX = startPos.x;
            int maxX = startPos.x;

            int minY = startPos.y;
            int maxY = startPos.y;

            int minZ = startPos.z;
            int maxZ = startPos.z;

            //
            // Flood fill.
            //
            while (!stack.empty()) {
                std::size_t currentIndex = stack.back();

                stack.pop_back();

                component.push_back(currentIndex);

                BlockPos const& pos = caves[currentIndex].pos;

                minX = std::min(minX, pos.x);

                maxX = std::max(maxX, pos.x);

                minY = std::min(minY, pos.y);

                maxY = std::max(maxY, pos.y);

                minZ = std::min(minZ, pos.z);

                maxZ = std::max(maxZ, pos.z);

                for (auto const& offset : neighborOffsets) {
                    BlockKey neighborKey { pos.x + offset[0], pos.y + offset[1], pos.z + offset[2] };

                    auto found = caveIndex.find(neighborKey);

                    if (found == caveIndex.end()) {
                        continue;
                    }

                    std::size_t neighborIndex = found->second;

                    if (neighborIndex >= caves.size() || visited[neighborIndex]) {
                        continue;
                    }

                    visited[neighborIndex] = 1;

                    stack.push_back(neighborIndex);
                }
            }

            int spanX = maxX - minX + 1;

            int spanY = maxY - minY + 1;

            int spanZ = maxZ - minZ + 1;

            int longestSpan = std::max(spanX, std::max(spanY, spanZ));

            bool keepRegion = component.size() >= static_cast<std::size_t>(MinimumCaveRegionSize) ||
                              longestSpan >= MinimumCaveRegionSpan;

            for (std::size_t caveIndexValue : component) {
                CaveHit& cave = caves[caveIndexValue];

                if (cave.regionVisible != keepRegion) {
                    cave.regionVisible = keepRegion;

                    //
                    // Region classification changed.
                    //
                    // Completely reset its view-dependent state.
                    //
                    cave.occludedFaces = 0;
                    cave.pendingOccludedFaces = 0;
                    cave.occlusionConfirmations = 0;

                    for (std::uint8_t& bucket : cave.depthBuckets) {
                        bucket = 0;
                    }

                    //
                    // Structural region acceptance changed.
                    //
                    // The cached fragment graph may therefore be stale.
                    //
                    caveRenderableDirty = true;
                    caveMeshDirty = true;

                } else if (!keepRegion) {
                    bool changed = cave.occludedFaces != 0;

                    cave.occludedFaces = 0;
                    cave.pendingOccludedFaces = 0;
                    cave.occlusionConfirmations = 0;

                    for (std::uint8_t& bucket : cave.depthBuckets) {
                        bucket = 0;
                    }

                    if (changed) {
                        caveRenderableDirty = true;
                        caveMeshDirty = true;
                    }
                }
            }
        }

        caveRegionsDirty = false;
        caveMeshDirty = true;
    }

        //
    // ================================================================
    // RENDERED CAVE FRAGMENT MASK
    // ================================================================
    //

    void XRayScanner::rebuildCaveRenderableMask() {
        caveRenderableMask.assign(caves.size(), 0);

        if (caves.empty()) {
            caveRenderableDirty = false;
            caveMeshDirty = true;

            return;
        }

        std::vector<std::uint8_t> visited(caves.size(), 0);

        constexpr int neighborOffsets[6][3] = { { -1, 0, 0 }, { 1, 0, 0 },  { 0, -1, 0 },
                                                { 0, 1, 0 },  { 0, 0, -1 }, { 0, 0, 1 } };

        std::vector<std::size_t> stack;
        std::vector<std::size_t> component;

        stack.reserve(256);
        component.reserve(256);

        auto isRenderedCandidate = [&](std::size_t index) -> bool {
            if (index >= caves.size()) {
                return false;
            }

            CaveHit const& cave = caves[index];

            return cave.regionVisible && cave.occludedFaces != 0;
        };

        //
        // ============================================================
        // FIND CONNECTED RENDERED FRAGMENTS
        // ============================================================
        //

        for (std::size_t startIndex = 0; startIndex < caves.size(); ++startIndex) {
            if (visited[startIndex]) {
                continue;
            }

            visited[startIndex] = 1;

            if (!isRenderedCandidate(startIndex)) {
                continue;
            }

            stack.clear();
            component.clear();

            stack.push_back(startIndex);

            BlockPos const& startPos = caves[startIndex].pos;

            int minX = startPos.x;
            int maxX = startPos.x;

            int minY = startPos.y;
            int maxY = startPos.y;

            int minZ = startPos.z;
            int maxZ = startPos.z;

            while (!stack.empty()) {
                std::size_t currentIndex = stack.back();

                stack.pop_back();

                component.push_back(currentIndex);

                BlockPos const& pos = caves[currentIndex].pos;

                minX = std::min(minX, pos.x);
                maxX = std::max(maxX, pos.x);

                minY = std::min(minY, pos.y);
                maxY = std::max(maxY, pos.y);

                minZ = std::min(minZ, pos.z);
                maxZ = std::max(maxZ, pos.z);

                for (auto const& offset : neighborOffsets) {
                    BlockKey neighborKey { pos.x + offset[0], pos.y + offset[1], pos.z + offset[2] };

                    auto found = caveIndex.find(neighborKey);

                    if (found == caveIndex.end()) {
                        continue;
                    }

                    std::size_t neighborIndex = found->second;

                    if (neighborIndex >= caves.size()) {
                        continue;
                    }

                    if (visited[neighborIndex]) {
                        continue;
                    }

                    visited[neighborIndex] = 1;

                    if (!isRenderedCandidate(neighborIndex)) {
                        continue;
                    }

                    stack.push_back(neighborIndex);
                }
            }

            int spanX = maxX - minX + 1;

            int spanY = maxY - minY + 1;

            int spanZ = maxZ - minZ + 1;

            int longestSpan = std::max(spanX, std::max(spanY, spanZ));

            bool keepFragment = component.size() >= static_cast<std::size_t>(MinimumRenderedFragmentCells) ||
                                longestSpan >= MinimumRenderedFragmentSpan;

            if (!keepFragment) {
                continue;
            }

            for (std::size_t index : component) {
                caveRenderableMask[index] = 1;
            }
        }

        caveRenderableDirty = false;

        //
        // A different accepted-fragment mask means the mesh needs to
        // reflect it.
        //
        caveMeshDirty = true;
    }

    //
    // ================================================================
    // GREEDY CAVE MESH
    // ================================================================
    //

    void XRayScanner::rebuildCaveMesh() {
        caveMesh.clear();
        caveOutlineLines.clear();

        if (caves.empty()) {
            caveMeshDirty = false;
            return;
        }

        //
        // Safety: the cached fragment mask must correspond to caves[].
        //
        if (caveRenderableMask.size() != caves.size()) {
            caveRenderableDirty = true;
            return;
        }

        //
        // ============================================================
        // BUILD GREEDY-MESH INPUT PLANES
        // ============================================================
        //

        //
        // Fill mesh:
        //
        // Keep depth level in the key so differently faded surfaces
        // remain separate.
        //
        using PlaneKey = std::tuple<std::uint8_t, int, std::uint8_t>;

        //
        // Outline mesh:
        //
        // Ignore normal depth levels when deciding whether neighboring
        // cells can merge.
        //
        using OutlinePlaneKey = std::pair<std::uint8_t, int>;

        using Cell = std::pair<int, int>;

        std::map<PlaneKey, std::set<Cell>> planes;

        //
        // Each outline cell still remembers its depth level. That lets
        // the resulting larger outline rectangle retain appropriate
        // depth fading without splitting solely because its cells had
        // slightly different levels.
        //
        std::map<OutlinePlaneKey, std::map<Cell, std::uint8_t>> outlinePlanes;

        for (std::size_t index = 0; index < caves.size(); ++index) {
            //
            // NEW FILTER.
            //
            // Old code used every region-visible cave with occluded
            // faces. Now only accepted rendered fragments reach the
            // mesher.
            //
            if (!caveRenderableMask[index]) {
                continue;
            }

            CaveHit const& cave = caves[index];

            std::uint8_t faces = cave.occludedFaces;

            if (faces == 0) {
                continue;
            }

            for (std::uint8_t face : CaveFaceList) {
                if ((faces & face) == 0) {
                    continue;
                }

                std::uint8_t depthBucket = cave.depthBuckets[caveFaceIndex(face)];

                int plane = 0;
                int u = 0;
                int v = 0;

                //
                // Down
                //
                // Plane Y
                // U X
                // V Z
                //
                if (face == CaveFaceDown) {
                    plane = cave.pos.y;

                    u = cave.pos.x;
                    v = cave.pos.z;
                }

                //
                // Up
                //
                else if (face == CaveFaceUp) {
                    plane = cave.pos.y + 1;

                    u = cave.pos.x;
                    v = cave.pos.z;
                }

                //
                // North
                //
                // Plane Z
                // U X
                // V Y
                //
                else if (face == CaveFaceNorth) {
                    plane = cave.pos.z;

                    u = cave.pos.x;
                    v = cave.pos.y;
                }

                //
                // South
                //
                else if (face == CaveFaceSouth) {
                    plane = cave.pos.z + 1;

                    u = cave.pos.x;
                    v = cave.pos.y;
                }

                //
                // West
                //
                // Plane X
                // U Z
                // V Y
                //
                else if (face == CaveFaceWest) {
                    plane = cave.pos.x;

                    u = cave.pos.z;
                    v = cave.pos.y;
                }

                //
                // East
                //
                else if (face == CaveFaceEast) {
                    plane = cave.pos.x + 1;

                    u = cave.pos.z;
                    v = cave.pos.y;
                }

                //
                // Depth-aware fill input.
                //
                planes[{ face, plane, depthBucket }].insert({ u, v });

                //
                // Simplified outline input.
                //
                // Depth does NOT participate in the plane key.
                //
                auto& outlineCells = outlinePlanes[{ face, plane }];

                auto [outlineIt, inserted] = outlineCells.emplace(Cell { u, v }, depthBucket);

                //
                // There normally cannot be duplicate cells here, but
                // keep the more strongly suppressed level if one ever
                // occurs.
                //
                if (!inserted) {
                    outlineIt->second = std::max(outlineIt->second, depthBucket);
                }
            }
        }

        //
        // ============================================================
        // GREEDY RECTANGLE MERGING
        // ============================================================
        //

        for (auto& [planeKey, cells] : planes) {
            auto [face, plane, depthBucket] = planeKey;

            while (!cells.empty()) {
                Cell start = *cells.begin();

                int startU = start.first;
                int startV = start.second;

                //
                // Grow width.
                //
                int width = 1;

                while (cells.contains({ startU + width, startV })) {
                    ++width;
                }

                //
                // Grow height.
                //
                int height = 1;

                while (true) {
                    bool fullRow = true;

                    for (int offsetU = 0; offsetU < width; ++offsetU) {
                        if (!cells.contains({ startU + offsetU, startV + height })) {
                            fullRow = false;
                            break;
                        }
                    }

                    if (!fullRow) {
                        break;
                    }

                    ++height;
                }

                //
                // Remove merged cells.
                //
                for (int offsetV = 0; offsetV < height; ++offsetV) {
                    for (int offsetU = 0; offsetU < width; ++offsetU) {
                        cells.erase({ startU + offsetU, startV + offsetV });
                    }
                }

                caveMesh.push_back({ face, plane, startU, startV, width, height, depthBucket });
            }
        }

                //
        // ============================================================
        // BOUNDARY-ONLY CAVE OUTLINE
        // ============================================================
        //
        // A line is emitted only when a cave-face cell has no
        // neighboring cave-face cell on that side.
        //
        // Internal greedy-mesh seams are therefore eliminated.
        //

        for (auto const& [planeKey, cells] : outlinePlanes) {
            auto [face, plane] = planeKey;

            //
            // Horizontal edge:
            //
            // fixed V coordinate
            // changing U coordinate
            //
            using HorizontalKey = std::pair<int, std::uint8_t>;

            //
            // Vertical edge:
            //
            // fixed U coordinate
            // changing V coordinate
            //
            using VerticalKey = std::pair<int, std::uint8_t>;

            std::map<HorizontalKey, std::set<int>> horizontalEdges;

            std::map<VerticalKey, std::set<int>> verticalEdges;

            //
            // --------------------------------------------------------
            // EXTRACT TRUE PERIMETER EDGES
            // --------------------------------------------------------
            //

            for (auto const& [cell, depthLevel] : cells) {
                int u = cell.first;
                int v = cell.second;

                //
                // Bottom edge.
                //
                if (!cells.contains({ u, v - 1 })) {
                    horizontalEdges[{ v, depthLevel }].insert(u);
                }

                //
                // Top edge.
                //
                if (!cells.contains({ u, v + 1 })) {
                    horizontalEdges[{ v + 1, depthLevel }].insert(u);
                }

                //
                // Left edge.
                //
                if (!cells.contains({ u - 1, v })) {
                    verticalEdges[{ u, depthLevel }].insert(v);
                }

                //
                // Right edge.
                //
                if (!cells.contains({ u + 1, v })) {
                    verticalEdges[{ u + 1, depthLevel }].insert(v);
                }
            }

            //
            // --------------------------------------------------------
            // MERGE HORIZONTAL EDGES
            // --------------------------------------------------------
            //

            for (auto& [edgeKey, starts] : horizontalEdges) {
                auto [fixedV, depthLevel] = edgeKey;

                while (!starts.empty()) {
                    int startU = *starts.begin();

                    int length = 1;

                    while (starts.contains(startU + length)) {
                        ++length;
                    }

                    for (int offset = 0; offset < length; ++offset) {
                        starts.erase(startU + offset);
                    }

                    Vec3 start = getCaveOutlinePoint(face, plane, startU, fixedV);

                    Vec3 end = getCaveOutlinePoint(face, plane, startU + length, fixedV);

                    caveOutlineLines.push_back({ start, end, depthLevel });
                }
            }

            //
            // --------------------------------------------------------
            // MERGE VERTICAL EDGES
            // --------------------------------------------------------
            //

            for (auto& [edgeKey, starts] : verticalEdges) {
                auto [fixedU, depthLevel] = edgeKey;

                while (!starts.empty()) {
                    int startV = *starts.begin();

                    int length = 1;

                    while (starts.contains(startV + length)) {
                        ++length;
                    }

                    for (int offset = 0; offset < length; ++offset) {
                        starts.erase(startV + offset);
                    }

                    Vec3 start = getCaveOutlinePoint(face, plane, fixedU, startV);

                    Vec3 end = getCaveOutlinePoint(face, plane, fixedU, startV + length);

                    caveOutlineLines.push_back({ start, end, depthLevel });
                }
            }
        }

        caveMeshDirty = false;

    }

    //
    // ================================================================
    // MAIN WORLD SCANNER
    // ================================================================
    //

    void XRayScanner::scanBlocks(SDK::BlockSource* region) {
        if (!region || !scanInitialized) {
            return;
        }

        int range = activeRange;

        int side = range * 2 + 1;

        long long rangeSq = static_cast<long long>(range) * static_cast<long long>(range);

        int blockBudget = xRaySettings.caveESP ? BlocksPerTickWithCaves : BlocksPerTickOreOnly;

        int scanned = 0;

        while (scanned < blockBudget) {
            int dx = centeredOffset(scanXIndex);

            int dy = centeredOffset(scanYIndex);

            int dz = centeredOffset(scanZIndex);

            BlockPos pos { scanCenter.x + dx, scanCenter.y + dy, scanCenter.z + dz };

            ++scanYIndex;

            if (scanYIndex >= side) {
                scanYIndex = 0;
                ++scanXIndex;

                if (scanXIndex >= side) {
                    scanXIndex = 0;
                    ++scanZIndex;

                    if (scanZIndex >= side) {
                        scanZIndex = 0;
                    }
                }
            }

            ++scanned;

            long long distanceSq =
                static_cast<long long>(dx) * dx + static_cast<long long>(dy) * dy + static_cast<long long>(dz) * dz;

            if (distanceSq > rangeSq) {
                continue;
            }

            if (pos.y < -64 || pos.y > 320) {
                continue;
            }

            SDK::Block* block = region->getBlock(pos);

            if (!block) {
                continue;
            }

            //
            // Ore ESP
            //
            if (xRaySettings.oreESP) {
                auto oreType = classifyOre(block);

                if (oreType.has_value()) {
                    addOre(pos, *oreType);
                }
            }

            //
            // Cave ESP
            //
            if (xRaySettings.caveESP && isCaveSpaceBlock(block)) {
                if (!isCaveAirCandidate(region, pos)) {
                    continue;
                }

                std::uint8_t faces = getExposedCaveFaces(region, pos);

                if (faces != 0) {
                    addOrUpdateCave(pos, faces);
                }
            }
        }
    }

    //
    // ================================================================
    // TICK
    // ================================================================
    //

    void XRayScanner::onTick(Event& event) {
        auto& tick = static_cast<TickEvent&>(event);

        //
        // Cave classification settings changed.
        //
        if (lastAirCheck3x3x3 != xRaySettings.airCheck3x3x3 || lastIgnoreSurface != xRaySettings.ignoreSurface) {
            caves.clear();
            caveIndex.clear();
            caveBlockClassCache.clear();
            caveRenderableMask.clear();
            caveMesh.clear();
            caveOutlineLines.clear();

            caveValidationIndex = 0;
            caveNearOcclusionIndex = 0;
            caveFarOcclusionIndex = 0;

            caveRegionsDirty = true;
            caveRenderableDirty = true;
            caveMeshDirty = true;

            caveRegionRebuildTimer = 0;

            scanInitialized = false;

            lastAirCheck3x3x3 = xRaySettings.airCheck3x3x3;

            lastIgnoreSurface = xRaySettings.ignoreSurface;
        }

        //
        // No world.
        //
        if (!tick.getLevel()) {
            ores.clear();

            caves.clear();
            caveIndex.clear();
            caveBlockClassCache.clear();
            caveRenderableMask.clear();
            caveMesh.clear();
            caveOutlineLines.clear();

            scanInitialized = false;

            oreValidationIndex = 0;
            caveValidationIndex = 0;
            caveNearOcclusionIndex = 0;
            caveFarOcclusionIndex = 0;

            caveRegionsDirty = true;
            caveRenderableDirty = true;
            caveMeshDirty = true;

            caveRegionRebuildTimer = 0;

            return;
        }

        //
        // X-Ray disabled.
        //
        if (!xRaySettings.enabled) {
            ores.clear();

            caves.clear();
            caveIndex.clear();
            caveBlockClassCache.clear();
            caveRenderableMask.clear();
            caveMesh.clear();
            caveOutlineLines.clear();

            scanInitialized = false;

            oreValidationIndex = 0;
            caveValidationIndex = 0;
            caveNearOcclusionIndex = 0;
            caveFarOcclusionIndex = 0;

            caveRegionsDirty = true;
            caveRenderableDirty = true;
            caveMeshDirty = true;

            caveRegionRebuildTimer = 0;

            return;
        }

        //
        // Nothing enabled.
        //
        if (!xRaySettings.oreESP && !xRaySettings.caveESP) {
            ores.clear();

            caves.clear();
            caveIndex.clear();
            caveBlockClassCache.clear();
            caveRenderableMask.clear();
            caveMesh.clear();
            caveOutlineLines.clear();

            scanInitialized = false;

            oreValidationIndex = 0;
            caveValidationIndex = 0;
            caveNearOcclusionIndex = 0;
            caveFarOcclusionIndex = 0;

            caveRegionsDirty = true;
            caveRenderableDirty = true;
            caveMeshDirty = true;

            caveRegionRebuildTimer = 0;

            return;
        }

        //
        // Ore ESP disabled.
        //
        if (!xRaySettings.oreESP) {
            ores.clear();
            oreValidationIndex = 0;
        }

        //
        // Cave ESP disabled.
        //
        if (!xRaySettings.caveESP) {
            caves.clear();
            caveIndex.clear();
            caveBlockClassCache.clear();
            caveRenderableMask.clear();
            caveMesh.clear();
            caveOutlineLines.clear();

            caveValidationIndex = 0;
            caveNearOcclusionIndex = 0;
            caveFarOcclusionIndex = 0;

            caveRegionsDirty = true;
            caveRenderableDirty = true;
            caveMeshDirty = true;

            caveRegionRebuildTimer = 0;
        }

        auto clientInstance = SDK::ClientInstance::get();

        if (!clientInstance) {
            return;
        }

        auto player = clientInstance->getLocalPlayer();

        auto region = clientInstance->getRegion();

        if (!player || !region) {
            return;
        }

        Vec3 playerPos = player->getPos();

        //
        // Approximate first-person eye position.
        //
        Vec3 viewOrigin = playerPos;

        auto const& playerBox = player->getBoundingBox();

        float playerHeight = playerBox.higher.y - playerBox.lower.y;

        viewOrigin.y = playerBox.lower.y + playerHeight * 0.90f;

        BlockPos center { static_cast<int>(std::floor(playerPos.x)),

                          static_cast<int>(std::floor(playerPos.y)),

                          static_cast<int>(std::floor(playerPos.z)) };

        int range = std::clamp(xRaySettings.scanRange, 16, 128);

        bool needsReset = !scanInitialized || activeRange != range;

        if (!needsReset) {
            int dx = std::abs(center.x - scanCenter.x);

            int dy = std::abs(center.y - scanCenter.y);

            int dz = std::abs(center.z - scanCenter.z);

            needsReset = dx >= RecenterDistance || dy >= RecenterDistance || dz >= RecenterDistance;
        }

        if (needsReset) {
            resetScan(center, range);
        }

        if (xRaySettings.oreESP) {
            validateCachedOres(region);
        }

        if (xRaySettings.caveESP) {
            validateCachedCaves(region);
        }

        //
        // Shared incremental scan.
        //
        scanBlocks(region);

        //
        // Cave post-processing.
        //
        if (xRaySettings.caveESP) {
            //
            // Connected region filtering.
            //
            if (caveRegionRebuildTimer > 0) {
                --caveRegionRebuildTimer;
            }

            if (caveRegionsDirty && caveRegionRebuildTimer <= 0) {
                rebuildCaveRegions();

                caveRegionRebuildTimer = CaveRegionRebuildIntervalTicks;
            }

            //
            // Occlusion.
            //
            updateCaveOcclusion(region, viewOrigin);

            //
            // ========================================================
            // RENDERED FRAGMENT CONNECTIVITY
            // ========================================================
            //
            // Only rebuilt when cave topology or visibility changes.
            //
            if (caveRenderableDirty) {
                rebuildCaveRenderableMask();
            }

            //
            // ========================================================
            // RENDER MESH
            // ========================================================
            //
            // Depth-only changes can rebuild the mesh without running
            // the fragment flood-fill again.
            //
            if (caveMeshDirty) {
                rebuildCaveMesh();
            }
        }
    }

    //
    // ================================================================
    // RENDER
    // ================================================================
    //

    void XRayScanner::onRender(Event& event) {
        if (!xRaySettings.enabled) {
            return;
        }

        bool renderOres = xRaySettings.oreESP && !ores.empty();

        bool renderCaves = xRaySettings.caveESP && ((xRaySettings.fill && !caveMesh.empty()) || (xRaySettings.outline && !caveOutlineLines.empty()));

        if (!renderOres && !renderCaves) {
            return;
        }

        auto clientInstance = SDK::ClientInstance::get();

        if (!clientInstance || !clientInstance->levelRenderer) {
            return;
        }

        auto& renderEvent = static_cast<RenderLevelEvent&>(event);

        auto screenContext = renderEvent.getScreenContext();

        if (!screenContext) {
            return;
        }

        MCDrawUtil3D dc { clientInstance->levelRenderer, screenContext, SDK::MaterialPtr::getUIColor() };

        //
        // ============================================================
        // ORE COLORS
        // ============================================================
        //

        auto getOreColor = [](OreType type) -> d2d::Color {
            switch (type) {
            case OreType::Diamond:
                return d2d::Color::RGB(0x42, 0xE6, 0xD5);

            case OreType::Emerald:
                return d2d::Color::RGB(0x35, 0xD0, 0x63);

            case OreType::Gold:
                return d2d::Color::RGB(0xF5, 0xD4, 0x42);

            case OreType::Iron:
                return d2d::Color::RGB(0xD8, 0xC5, 0xB0);

            case OreType::Redstone:
                return d2d::Color::RGB(0xE0, 0x35, 0x35);

            case OreType::Lapis:
                return d2d::Color::RGB(0x38, 0x68, 0xD8);

            case OreType::Coal:
                return d2d::Color::RGB(0x70, 0x70, 0x70);

            case OreType::Copper:
                return d2d::Color::RGB(0xD7, 0x7A, 0x45);

            case OreType::AncientDebris:
                return d2d::Color::RGB(0x9C, 0x64, 0x4B);
            }

            return d2d::Color::RGB(0xFF, 0xFF, 0xFF);
        };

        //
        // ============================================================
        // ORE FILLS
        // ============================================================
        //

        if (renderOres && xRaySettings.fill) {
            for (auto const& ore : ores) {
                if (!isOreEnabled(ore.type)) {
                    continue;
                }

                d2d::Color color = getOreColor(ore.type).asAlpha(0.18f);

                drawFilledBlock(dc, ore.pos, color);
            }

            dc.flush();
        }

        //
        // ============================================================
        // ORE OUTLINES
        // ============================================================
        //

        if (renderOres && xRaySettings.outline) {
            for (auto const& ore : ores) {
                if (!isOreEnabled(ore.type)) {
                    continue;
                }

                d2d::Color color = getOreColor(ore.type).asAlpha(0.95f);

                drawOutlineBlock(dc, ore.pos, color);
            }

            dc.flush();
        }

        //
        // ============================================================
        // GREEDY-MESHED CAVE SHELL
        // ============================================================
        //

        if (renderCaves) {
            d2d::Color caveColor = d2d::Color::RGB(0xA9, 0x5C, 0xFF);

            float requestedOpacity = std::clamp(static_cast<float>(xRaySettings.caveOpacity) / 100.0f,

                                                0.05f, 1.0f);

            float fillAlpha = std::clamp(requestedOpacity * 0.45f,

                                         0.03f, 0.45f);

            float outlineAlpha = std::clamp(0.35f + requestedOpacity * 0.50f,

                                            0.40f, 0.85f);

            //
            // ========================================================
            // DEPTH OPACITY
            // ========================================================
            //

            auto depthFillMultiplier = [](std::uint8_t level) -> float {
                //
                // Level 8 is the special close-wall protection.
                //
                if (level >= 8) {
                    return 0.05f;
                }

                //
                // Normal levels 0-7 smoothly fade:
                //
                // 0 -> 1.00
                // 7 -> 0.20
                //
                float t = std::clamp(static_cast<float>(level) / 7.0f, 0.0f, 1.0f);

                return 1.00f - (0.80f * t);
            };

            auto depthOutlineMultiplier = [](std::uint8_t level) -> float {
                //
                // Level 8 keeps the successful close-wall outline
                // suppression.
                //
                if (level >= 8) {
                    return 0.08f;
                }

                //
                // Normal levels 0-7 smoothly fade:
                //
                // 0 -> 1.00
                // 7 -> 0.25
                //
                float t = std::clamp(static_cast<float>(level) / 7.0f, 0.0f, 1.0f);

                return 1.00f - (0.75f * t);
            };

            //
            // Cave fill.
            //
            if (xRaySettings.fill) {
                for (CaveMeshQuad const& quad : caveMesh) {
                    float quadAlpha = fillAlpha * depthFillMultiplier(quad.depthBucket);

                    d2d::Color fillColor = caveColor.asAlpha(quadAlpha);

                    drawCaveMeshFill(dc, quad.face, quad.plane, quad.u, quad.v, quad.width, quad.height, fillColor);
                }

                dc.flush();
            }

            //
            // Cave boundary outline.
            //
            if (xRaySettings.outline) {
                for (CaveOutlineLine const& line : caveOutlineLines) {
                    float lineAlpha = outlineAlpha * depthOutlineMultiplier(line.depthLevel);

                    d2d::Color outlineColor = caveColor.asAlpha(lineAlpha);

                    dc.drawLine(line.start, line.end, outlineColor);
                }

                dc.flush();
            }
        }
    }

} // namespace Nexus
