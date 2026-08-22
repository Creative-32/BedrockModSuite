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
#include <string>

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

        constexpr float CaveFaceInset = 0.01f;

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

            //
            // Down
            //
            dc.fillQuad({ x, y, z }, { x + 1.f, y, z }, { x + 1.f, y, z + 1.f }, { x, y, z + 1.f }, color);

            //
            // Up
            //
            dc.fillQuad({ x, y + 1.f, z }, { x + 1.f, y + 1.f, z }, { x + 1.f, y + 1.f, z + 1.f },
                        { x, y + 1.f, z + 1.f }, color);

            //
            // North
            //
            dc.fillQuad({ x, y, z }, { x, y + 1.f, z }, { x + 1.f, y + 1.f, z }, { x + 1.f, y, z }, color);

            //
            // South
            //
            dc.fillQuad({ x, y, z + 1.f }, { x, y + 1.f, z + 1.f }, { x + 1.f, y + 1.f, z + 1.f },
                        { x + 1.f, y, z + 1.f }, color);

            //
            // West
            //
            dc.fillQuad({ x, y, z }, { x, y + 1.f, z }, { x, y + 1.f, z + 1.f }, { x, y, z + 1.f }, color);

            //
            // East
            //
            dc.fillQuad({ x + 1.f, y, z }, { x + 1.f, y + 1.f, z }, { x + 1.f, y + 1.f, z + 1.f },
                        { x + 1.f, y, z + 1.f }, color);
        }

        //
        // ============================================================
        // CAVE FACE FILL
        // ============================================================
        //

        void drawCaveFaceFill(MCDrawUtil3D& dc, BlockPos const& pos, std::uint8_t face, d2d::Color const& color) {
            float x = static_cast<float>(pos.x);

            float y = static_cast<float>(pos.y);

            float z = static_cast<float>(pos.z);

            float x2 = x + 1.0f;
            float y2 = y + 1.0f;
            float z2 = z + 1.0f;

            if (face == CaveFaceDown) {
                float fy = y + CaveFaceInset;

                dc.fillQuad({ x, fy, z }, { x2, fy, z }, { x2, fy, z2 }, { x, fy, z2 }, color);

                return;
            }

            if (face == CaveFaceUp) {
                float fy = y2 - CaveFaceInset;

                dc.fillQuad({ x, fy, z }, { x2, fy, z }, { x2, fy, z2 }, { x, fy, z2 }, color);

                return;
            }

            if (face == CaveFaceNorth) {
                float fz = z + CaveFaceInset;

                dc.fillQuad({ x, y, fz }, { x, y2, fz }, { x2, y2, fz }, { x2, y, fz }, color);

                return;
            }

            if (face == CaveFaceSouth) {
                float fz = z2 - CaveFaceInset;

                dc.fillQuad({ x, y, fz }, { x, y2, fz }, { x2, y2, fz }, { x2, y, fz }, color);

                return;
            }

            if (face == CaveFaceWest) {
                float fx = x + CaveFaceInset;

                dc.fillQuad({ fx, y, z }, { fx, y2, z }, { fx, y2, z2 }, { fx, y, z2 }, color);

                return;
            }

            if (face == CaveFaceEast) {
                float fx = x2 - CaveFaceInset;

                dc.fillQuad({ fx, y, z }, { fx, y2, z }, { fx, y2, z2 }, { fx, y, z2 }, color);
            }
        }

        //
        // ============================================================
        // CAVE FACE OUTLINE
        // ============================================================
        //

        void drawCaveFaceOutline(MCDrawUtil3D& dc, BlockPos const& pos, std::uint8_t face, d2d::Color const& color) {
            float x = static_cast<float>(pos.x);

            float y = static_cast<float>(pos.y);

            float z = static_cast<float>(pos.z);

            float x2 = x + 1.0f;
            float y2 = y + 1.0f;
            float z2 = z + 1.0f;

            if (face == CaveFaceDown || face == CaveFaceUp) {
                float fy = face == CaveFaceDown ? y + CaveFaceInset : y2 - CaveFaceInset;

                dc.drawLine({ x, fy, z }, { x2, fy, z }, color);

                dc.drawLine({ x2, fy, z }, { x2, fy, z2 }, color);

                dc.drawLine({ x2, fy, z2 }, { x, fy, z2 }, color);

                dc.drawLine({ x, fy, z2 }, { x, fy, z }, color);

                return;
            }

            if (face == CaveFaceNorth || face == CaveFaceSouth) {
                float fz = face == CaveFaceNorth ? z + CaveFaceInset : z2 - CaveFaceInset;

                dc.drawLine({ x, y, fz }, { x2, y, fz }, color);

                dc.drawLine({ x2, y, fz }, { x2, y2, fz }, color);

                dc.drawLine({ x2, y2, fz }, { x, y2, fz }, color);

                dc.drawLine({ x, y2, fz }, { x, y, fz }, color);

                return;
            }

            if (face == CaveFaceWest || face == CaveFaceEast) {
                float fx = face == CaveFaceWest ? x + CaveFaceInset : x2 - CaveFaceInset;

                dc.drawLine({ fx, y, z }, { fx, y, z2 }, color);

                dc.drawLine({ fx, y, z2 }, { fx, y2, z2 }, color);

                dc.drawLine({ fx, y2, z2 }, { fx, y2, z }, color);

                dc.drawLine({ fx, y2, z }, { fx, y, z }, color);
            }
        }

        //
        // ============================================================
        // CAVE SHELL FILL
        // ============================================================
        //

        void drawCaveShellFill(MCDrawUtil3D& dc, BlockPos const& pos, std::uint8_t faces, d2d::Color const& color) {
            constexpr std::uint8_t faceList[] = { CaveFaceDown,  CaveFaceUp,   CaveFaceNorth,
                                                  CaveFaceSouth, CaveFaceWest, CaveFaceEast };

            for (std::uint8_t face : faceList) {
                if ((faces & face) == 0) {
                    continue;
                }

                drawCaveFaceFill(dc, pos, face, color);
            }
        }

        //
        // ============================================================
        // CAVE SHELL OUTLINE
        // ============================================================
        //

        void drawCaveShellOutline(MCDrawUtil3D& dc, BlockPos const& pos, std::uint8_t faces, d2d::Color const& color) {
            constexpr std::uint8_t faceList[] = { CaveFaceDown,  CaveFaceUp,   CaveFaceNorth,
                                                  CaveFaceSouth, CaveFaceWest, CaveFaceEast };

            for (std::uint8_t face : faceList) {
                if ((faces & face) == 0) {
                    continue;
                }

                drawCaveFaceOutline(dc, pos, face, color);
            }
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

        std::erase_if(ores,

                      [&](OreHit const& ore) {
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
            }

            else {
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
        if (!block || !block->legacyBlock) {
            return false;
        }

        std::string id = block->legacyBlock->namespacedId.getString();

        return id == "minecraft:air" || id == "minecraft:cave_air" || id == "minecraft:void_air";
    }

    //
    // ================================================================
    // CAVE-SPACE CLASSIFICATION
    // ================================================================
    //
    // Cave ESP needs to distinguish between:
    //
    // 1. A voxel occupied by real terrain.
    //
    // 2. A voxel that contains only a small/thin block and therefore
    //    should still behave as open cave volume.
    //
    // A torch, rail, button, sign, vine, etc. should NOT create a
    // fake one-block-wide wall in Cave ESP.
    //

    bool XRayScanner::isCaveSpaceBlock(SDK::Block* block) const {
        if (!block || !block->legacyBlock) {
            return false;
        }

        //
        // Literal air is always cave space.
        //
        if (isAirBlock(block)) {
            return true;
        }

        std::string id = block->legacyBlock->namespacedId.getString();

        //
        // ------------------------------------------------------------
        // TORCHES / LIGHT SOURCES
        // ------------------------------------------------------------
        //

        if (id.find("torch") != std::string::npos) {
            return true;
        }

        //
        // ------------------------------------------------------------
        // RAILS
        // ------------------------------------------------------------
        //

        if (id == "minecraft:rail" || id.ends_with("_rail")) {
            return true;
        }

        //
        // ------------------------------------------------------------
        // BASIC REDSTONE
        // ------------------------------------------------------------
        //

        if (id == "minecraft:redstone_wire" || id == "minecraft:lever" || id == "minecraft:tripwire" ||
            id == "minecraft:tripwire_hook" ||

            id.ends_with("_button") || id.ends_with("_pressure_plate") ||

            id.find("repeater") != std::string::npos ||

            id.find("comparator") != std::string::npos) {
            return true;
        }

        //
        // ------------------------------------------------------------
        // CLIMBABLE / THIN STRUCTURES
        // ------------------------------------------------------------
        //

        if (id == "minecraft:ladder" || id == "minecraft:chain" || id == "minecraft:scaffolding" ||

            id.find("vine") != std::string::npos) {
            return true;
        }

        //
        // ------------------------------------------------------------
        // SIGNS / BANNERS
        // ------------------------------------------------------------
        //

        if (id.find("sign") != std::string::npos ||

            id.find("banner") != std::string::npos) {
            return true;
        }

        //
        // ------------------------------------------------------------
        // FLOOR / WALL OVERLAYS
        // ------------------------------------------------------------
        //

        if (id == "minecraft:carpet" || id.ends_with("_carpet") ||

            id == "minecraft:snow_layer" || id == "minecraft:glow_lichen" || id == "minecraft:sculk_vein" ||

            id == "minecraft:pink_petals" || id == "minecraft:leaf_litter") {
            return true;
        }

        //
        // ------------------------------------------------------------
        // FIRE / PORTAL-LIKE THIN CONTENT
        // ------------------------------------------------------------
        //

        if (id == "minecraft:fire" || id == "minecraft:soul_fire" || id == "minecraft:portal") {
            return true;
        }

        //
        // ------------------------------------------------------------
        // DECORATIVE THIN BLOCKS
        // ------------------------------------------------------------
        //

        if (id == "minecraft:cobweb" || id == "minecraft:flower_pot" || id == "minecraft:end_rod" ||
            id == "minecraft:lightning_rod" ||

            id.find("candle") != std::string::npos ||

            id.find("coral_fan") != std::string::npos) {
            return true;
        }

        //
        // ------------------------------------------------------------
        // SMALL PLANTS
        // ------------------------------------------------------------
        //
        // These are intentionally explicit instead of doing something
        // dangerous like:
        //
        //     id.find("grass")
        //
        // because that would accidentally classify grass_block as open
        // cave space.
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
            return true;
        }

        //
        // ------------------------------------------------------------
        // CAVE-SPECIFIC SMALL FEATURES
        // ------------------------------------------------------------
        //

        if (id == "minecraft:pointed_dripstone" ||

            id == "minecraft:small_amethyst_bud" || id == "minecraft:medium_amethyst_bud" ||
            id == "minecraft:large_amethyst_bud" || id == "minecraft:amethyst_cluster") {
            return true;
        }

        //
        // Everything else is treated as structural terrain for now.
        //
        // Slabs, stairs, fences, walls, doors, trapdoors, glass,
        // fluids, leaves, etc. are intentionally NOT thrown into this
        // category yet because they need partial-volume handling rather
        // than simply pretending they are air.
        //
        return false;
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

            //
            // Unknown/unloaded data is not a roof.
            //
            if (!block) {
                continue;
            }

            //
            // Torches, signs, rails, etc. also aren't roofs.
            //
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

        //
        // Not restricted to literal air anymore.
        //
        // Thin blocks are considered part of the cave volume.
        //
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

            //
            // Unknown/unloaded data should not generate
            // fake geometry.
            //
            if (!block) {
                return false;
            }

            //
            // Thin blocks behave as open cave space.
            //
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

            cave.faces = faces;

            cave.occludedFaces &= faces;

            return;
        }

        std::size_t index = caves.size();

        //
        // New cave geometry stays invisible until the
        // occlusion system has actually tested it.
        //
        caves.push_back({ pos, faces, 0 });

        caveIndex.emplace(key, index);
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

        if (caveValidationIndex >= caves.size()) {
            caveValidationIndex = 0;
        }

        if (caveOcclusionIndex >= caves.size()) {
            caveOcclusionIndex = 0;
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

        if (caveOcclusionIndex >= caves.size()) {
            caveOcclusionIndex = 0;
        }
    }

    void XRayScanner::validateCachedCaves(SDK::BlockSource* region) {
        if (!region || caves.empty()) {
            caveValidationIndex = 0;
            caveOcclusionIndex = 0;

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

            //
            // Entirely surrounded by cave space.
            // Interior voxel does not belong in shell cache.
            //
            if (faces == 0) {
                eraseCaveAt(caveValidationIndex);

                ++checked;

                continue;
            }

            caves[caveValidationIndex].faces = faces;

            caves[caveValidationIndex].occludedFaces &= faces;

            ++caveValidationIndex;
            ++checked;
        }
    }

    //
    // ================================================================
    // CAVE FACE TARGET
    // ================================================================
    //

    Vec3 XRayScanner::getCaveFaceCenter(BlockPos const& pos, std::uint8_t face) {
        constexpr float inset = 0.06f;

        Vec3 center { static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f,
                      static_cast<float>(pos.z) + 0.5f };

        if (face == CaveFaceDown) {
            center.y = static_cast<float>(pos.y) + inset;
        }

        else if (face == CaveFaceUp) {
            center.y = static_cast<float>(pos.y) + 1.0f - inset;
        }

        else if (face == CaveFaceNorth) {
            center.z = static_cast<float>(pos.z) + inset;
        }

        else if (face == CaveFaceSouth) {
            center.z = static_cast<float>(pos.z) + 1.0f - inset;
        }

        else if (face == CaveFaceWest) {
            center.x = static_cast<float>(pos.x) + inset;
        }

        else if (face == CaveFaceEast) {
            center.x = static_cast<float>(pos.x) + 1.0f - inset;
        }

        return center;
    }

    //
    // ================================================================
    // VOXEL OCCLUSION RAY
    // ================================================================
    //

    bool XRayScanner::isLineOccluded(SDK::BlockSource* region, Vec3 const& start, Vec3 const& end) const {
        if (!region) {
            return false;
        }

        float dx = end.x - start.x;

        float dy = end.y - start.y;

        float dz = end.z - start.z;

        float lengthSq = dx * dx + dy * dy + dz * dz;

        if (lengthSq <= 0.000001f) {
            return false;
        }

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
            // Reached target cave-space voxel.
            //
            if (x == endX && y == endY && z == endZ) {
                break;
            }

            if (y < -64 || y > 320) {
                continue;
            }

            SDK::Block* block = region->getBlock(BlockPos { x, y, z });

            //
            // IMPORTANT:
            //
            // Thin blocks such as torches no longer occlude this ray.
            //
            if (block && !isCaveSpaceBlock(block)) {
                return true;
            }
        }

        return false;
    }

    //
    // ================================================================
    // VIEW-DEPENDENT CAVE OCCLUSION
    // ================================================================
    //

    void XRayScanner::updateCaveOcclusion(SDK::BlockSource* region, Vec3 const& viewOrigin) {
        if (!region || caves.empty()) {
            caveOcclusionIndex = 0;

            return;
        }

        constexpr std::uint8_t faceList[] = { CaveFaceDown,  CaveFaceUp,   CaveFaceNorth,
                                              CaveFaceSouth, CaveFaceWest, CaveFaceEast };

        int checked = 0;

        while (checked < CaveOcclusionCellsPerTick && !caves.empty()) {
            if (caveOcclusionIndex >= caves.size()) {
                caveOcclusionIndex = 0;
            }

            CaveHit& cave = caves[caveOcclusionIndex];

            Vec3 cellCenter { static_cast<float>(cave.pos.x) + 0.5f,

                              static_cast<float>(cave.pos.y) + 0.5f,

                              static_cast<float>(cave.pos.z) + 0.5f };

            //
            // If the cave cell is directly visible, no X-Ray
            // geometry is needed for it.
            //
            if (!isLineOccluded(region, viewOrigin, cellCenter)) {
                cave.occludedFaces = 0;

                ++caveOcclusionIndex;
                ++checked;

                continue;
            }

            //
            // Hidden cell:
            // refine visibility face-by-face.
            //
            std::uint8_t occluded = 0;

            for (std::uint8_t face : faceList) {
                if ((cave.faces & face) == 0) {
                    continue;
                }

                Vec3 target = getCaveFaceCenter(cave.pos, face);

                if (isLineOccluded(region, viewOrigin, target)) {
                    occluded |= face;
                }
            }

            cave.occludedFaces = occluded;

            ++caveOcclusionIndex;
            ++checked;
        }
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

        int side = (range * 2) + 1;

        long long rangeSq = static_cast<long long>(range) * static_cast<long long>(range);

        int blockBudget = xRaySettings.caveESP ? BlocksPerTickWithCaves : BlocksPerTickOreOnly;

        int scanned = 0;

        while (scanned < blockBudget) {
            int dx = centeredOffset(scanXIndex);

            int dy = centeredOffset(scanYIndex);

            int dz = centeredOffset(scanZIndex);

            BlockPos pos { scanCenter.x + dx, scanCenter.y + dy, scanCenter.z + dz };

            //
            // Advance scanner.
            //
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

            long long distanceSq = static_cast<long long>(dx) * dx +

                                   static_cast<long long>(dy) * dy +

                                   static_cast<long long>(dz) * dz;

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
            // ========================================================
            // ORE ESP
            // ========================================================
            //

            if (xRaySettings.oreESP) {
                auto oreType = classifyOre(block);

                if (oreType.has_value()) {
                    addOre(pos, *oreType);
                }
            }

            //
            // ========================================================
            // CAVE ESP
            // ========================================================
            //
            // Thin blocks are now valid cave-space cells too.
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
        if (lastAirCheck3x3x3 != xRaySettings.airCheck3x3x3 ||

            lastIgnoreSurface != xRaySettings.ignoreSurface) {
            caves.clear();
            caveIndex.clear();

            caveValidationIndex = 0;
            caveOcclusionIndex = 0;

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

            scanInitialized = false;

            oreValidationIndex = 0;
            caveValidationIndex = 0;
            caveOcclusionIndex = 0;

            return;
        }

        //
        // X-Ray master disabled.
        //
        if (!xRaySettings.enabled) {
            ores.clear();

            caves.clear();
            caveIndex.clear();

            scanInitialized = false;

            oreValidationIndex = 0;
            caveValidationIndex = 0;
            caveOcclusionIndex = 0;

            return;
        }

        //
        // Nothing needs scanning.
        //
        if (!xRaySettings.oreESP && !xRaySettings.caveESP) {
            ores.clear();

            caves.clear();
            caveIndex.clear();

            scanInitialized = false;

            oreValidationIndex = 0;
            caveValidationIndex = 0;
            caveOcclusionIndex = 0;

            return;
        }

        //
        // Individual feature disabled.
        //
        if (!xRaySettings.oreESP) {
            ores.clear();

            oreValidationIndex = 0;
        }

        if (!xRaySettings.caveESP) {
            caves.clear();
            caveIndex.clear();

            caveValidationIndex = 0;
            caveOcclusionIndex = 0;
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

        //
        // Validate old cache.
        //
        if (xRaySettings.oreESP) {
            validateCachedOres(region);
        }

        if (xRaySettings.caveESP) {
            validateCachedCaves(region);
        }

        //
        // One shared world scan.
        //
        scanBlocks(region);

        //
        // Refresh which cave faces are hidden by actual terrain.
        //
        if (xRaySettings.caveESP) {
            updateCaveOcclusion(region, viewOrigin);
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

        bool renderCaves = xRaySettings.caveESP && !caves.empty();

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

            return d2d::Colors::WHITE;
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
        // CAVE SHELL
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
            // --------------------------------------------------------
            // CAVE FILL
            // --------------------------------------------------------
            //

            if (xRaySettings.fill) {
                d2d::Color fillColor = caveColor.asAlpha(fillAlpha);

                for (auto const& cave : caves) {
                    if (cave.occludedFaces == 0) {
                        continue;
                    }

                    drawCaveShellFill(dc, cave.pos, cave.occludedFaces, fillColor);
                }

                dc.flush();
            }

            //
            // --------------------------------------------------------
            // CAVE OUTLINE
            // --------------------------------------------------------
            //

            if (xRaySettings.outline) {
                d2d::Color outlineColor = caveColor.asAlpha(outlineAlpha);

                for (auto const& cave : caves) {
                    if (cave.occludedFaces == 0) {
                        continue;
                    }

                    drawCaveShellOutline(dc, cave.pos, cave.occludedFaces, outlineColor);
                }

                dc.flush();
            }
        }
    }

} // namespace Nexus
