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
#include <string>

namespace Nexus {

    namespace {

        //
        // ============================================================
        // ORE RENDERING
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
            // Vertical edges
            //
            dc.drawLine({ x, y, z }, { x, y2, z }, color);
            dc.drawLine({ x2, y, z }, { x2, y2, z }, color);
            dc.drawLine({ x2, y, z2 }, { x2, y2, z2 }, color);
            dc.drawLine({ x, y, z2 }, { x, y2, z2 }, color);
        }

        void drawFilledBlock(MCDrawUtil3D& dc, BlockPos const& pos, d2d::Color const& color) {
            float x = static_cast<float>(pos.x);
            float y = static_cast<float>(pos.y);
            float z = static_cast<float>(pos.z);

            //
            // Bottom
            //
            dc.fillQuad({ x, y, z }, { x + 1.f, y, z }, { x + 1.f, y, z + 1.f }, { x, y, z + 1.f }, color);

            //
            // Top
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
        // CAVE / PATH RENDERING
        // ============================================================
        //
        // CaveHit::pos represents the AIR block occupied by the
        // player's feet.
        //
        // Therefore pos.y is also the top surface of the solid block
        // directly beneath that air block.
        //

        void drawCaveTileOutline(MCDrawUtil3D& dc, BlockPos const& pos, d2d::Color const& color) {
            float x = static_cast<float>(pos.x);

            //
            // Slight offset above the floor to help prevent z-fighting.
            //
            float y = static_cast<float>(pos.y) + 0.025f;

            float z = static_cast<float>(pos.z);

            float x2 = x + 1.0f;
            float z2 = z + 1.0f;

            dc.drawLine({ x, y, z }, { x2, y, z }, color);

            dc.drawLine({ x2, y, z }, { x2, y, z2 }, color);

            dc.drawLine({ x2, y, z2 }, { x, y, z2 }, color);

            dc.drawLine({ x, y, z2 }, { x, y, z }, color);
        }

        void drawCaveTileFill(MCDrawUtil3D& dc, BlockPos const& pos, d2d::Color const& color) {
            float x = static_cast<float>(pos.x);
            float y = static_cast<float>(pos.y) + 0.02f;
            float z = static_cast<float>(pos.z);

            dc.fillQuad({ x, y, z }, { x + 1.f, y, z }, { x + 1.f, y, z + 1.f }, { x, y, z + 1.f }, color);
        }

    } // namespace

    //
    // ================================================================
    // INSTANCE / INITIALIZATION
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
    // SCAN ORDER
    // ================================================================
    //

    int XRayScanner::centeredOffset(int index) {
        if (index == 0) {
            return 0;
        }

        int distance = (index + 1) / 2;

        //
        // Produces:
        //
        // 0, -1, +1, -2, +2, -3, +3 ...
        //
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

        //
        // Diamond
        //
        if (id == "minecraft:diamond_ore" || id == "minecraft:deepslate_diamond_ore") {
            return OreType::Diamond;
        }

        //
        // Emerald
        //
        if (id == "minecraft:emerald_ore" || id == "minecraft:deepslate_emerald_ore") {
            return OreType::Emerald;
        }

        //
        // Gold
        //
        if (id == "minecraft:gold_ore" || id == "minecraft:deepslate_gold_ore" || id == "minecraft:nether_gold_ore") {
            return OreType::Gold;
        }

        //
        // Iron
        //
        if (id == "minecraft:iron_ore" || id == "minecraft:deepslate_iron_ore") {
            return OreType::Iron;
        }

        //
        // Redstone
        //
        if (id == "minecraft:redstone_ore" || id == "minecraft:deepslate_redstone_ore" ||
            id == "minecraft:lit_redstone_ore") {
            return OreType::Redstone;
        }

        //
        // Lapis
        //
        if (id == "minecraft:lapis_ore" || id == "minecraft:deepslate_lapis_ore") {
            return OreType::Lapis;
        }

        //
        // Coal
        //
        if (id == "minecraft:coal_ore" || id == "minecraft:deepslate_coal_ore") {
            return OreType::Coal;
        }

        //
        // Copper
        //
        if (id == "minecraft:copper_ore" || id == "minecraft:deepslate_copper_ore") {
            return OreType::Copper;
        }

        //
        // Ancient Debris
        //
        if (id == "minecraft:ancient_debris") {
            return OreType::AncientDebris;
        }

        return std::nullopt;
    }

    //
    // ================================================================
    // ORE SETTINGS
    // ================================================================
    //

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
        //
        // If this position already exists,
        // simply update its type.
        //
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

            } else {
                ores[oreValidationIndex].type = *type;

                ++oreValidationIndex;
            }

            ++checked;
        }
    }

    //
    // ================================================================
    // AIR DETECTION
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
    // 3x3x3 AIR CHECK
    // ================================================================
    //

    bool XRayScanner::passesAirCheck(SDK::BlockSource* region, BlockPos const& pos) const {
        if (!region) {
            return false;
        }

        //
        // User disabled the 3x3x3 filtering.
        //
        if (!xRaySettings.airCheck3x3x3) {
            return true;
        }

        int airCount = 0;

        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                for (int z = -1; z <= 1; ++z) {
                    BlockPos checkPos { pos.x + x, pos.y + y, pos.z + z };

                    SDK::Block* block = region->getBlock(checkPos);

                    if (isAirBlock(block)) {
                        ++airCount;

                        //
                        // We already know this is open enough.
                        // Stop doing unnecessary BlockSource lookups.
                        //
                        if (airCount >= MinimumAirNeighbors) {
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
    // SURFACE REJECTION
    // ================================================================
    //

    bool XRayScanner::hasRoofAbove(SDK::BlockSource* region, BlockPos const& pos) const {
        if (!region) {
            return false;
        }

        //
        // Surface filtering is disabled.
        //
        if (!xRaySettings.ignoreSurface) {
            return true;
        }

        //
        // Start two blocks above because:
        //
        // pos       = feet air
        // pos + 1   = head air
        //
        // We want to know whether there is actual terrain above
        // the walkable space.
        //
        for (int offset = 2; offset <= RoofCheckDistance; ++offset) {
            BlockPos checkPos { pos.x, pos.y + offset, pos.z };

            SDK::Block* block = region->getBlock(checkPos);

            if (!isAirBlock(block)) {
                return true;
            }
        }

        //
        // No roof found nearby.
        //
        // Most likely open surface terrain.
        //
        return false;
    }

    //
    // ================================================================
    // CAVE CANDIDATE
    // ================================================================
    //

    bool XRayScanner::isCaveCandidate(SDK::BlockSource* region, BlockPos const& pos) const {
        if (!region) {
            return false;
        }

        //
        // Feet position must be air.
        //
        SDK::Block* current = region->getBlock(pos);

        if (!isAirBlock(current)) {
            return false;
        }

        //
        // Need at least two blocks of vertical room.
        //
        BlockPos abovePos { pos.x, pos.y + 1, pos.z };

        SDK::Block* above = region->getBlock(abovePos);

        if (!isAirBlock(above)) {
            return false;
        }

        //
        // There must be something below the air.
        //
        // That gives us a walkable cave floor rather than filling
        // the entire cave volume with boxes.
        //
        BlockPos belowPos { pos.x, pos.y - 1, pos.z };

        SDK::Block* below = region->getBlock(belowPos);

        if (!below || isAirBlock(below)) {
            return false;
        }

        //
        // Optional 3x3x3 open-space validation.
        //
        if (!passesAirCheck(region, pos)) {
            return false;
        }

        //
        // Optional surface rejection.
        //
        if (!hasRoofAbove(region, pos)) {
            return false;
        }

        return true;
    }

    //
    // ================================================================
    // CAVE CACHE
    // ================================================================
    //

    bool XRayScanner::containsCave(BlockPos const& pos) const {
        for (auto const& cave : caves) {
            if (cave.pos.x == pos.x && cave.pos.y == pos.y && cave.pos.z == pos.z) {
                return true;
            }
        }

        return false;
    }

    void XRayScanner::addCave(BlockPos const& pos) {
        if (containsCave(pos)) {
            return;
        }

        caves.push_back({ pos });
    }

    void XRayScanner::pruneCachedCaves(BlockPos const& center, int range) {
        int keepRange = range + RecenterDistance;

        long long keepRangeSq = static_cast<long long>(keepRange) * static_cast<long long>(keepRange);

        std::erase_if(caves,

                      [&](CaveHit const& cave) {
                          long long dx = static_cast<long long>(cave.pos.x) - center.x;

                          long long dy = static_cast<long long>(cave.pos.y) - center.y;

                          long long dz = static_cast<long long>(cave.pos.z) - center.z;

                          long long distanceSq = dx * dx + dy * dy + dz * dz;

                          return distanceSq > keepRangeSq;
                      });

        if (caveValidationIndex >= caves.size()) {
            caveValidationIndex = 0;
        }
    }

    void XRayScanner::validateCachedCaves(SDK::BlockSource* region) {
        if (!region || caves.empty()) {
            caveValidationIndex = 0;

            return;
        }

        int checked = 0;

        while (checked < CaveValidationPerTick && !caves.empty()) {
            if (caveValidationIndex >= caves.size()) {
                caveValidationIndex = 0;
            }

            BlockPos const pos = caves[caveValidationIndex].pos;

            if (!isCaveCandidate(region, pos)) {
                caves.erase(caves.begin() + static_cast<std::ptrdiff_t>(caveValidationIndex));

                if (caveValidationIndex >= caves.size()) {
                    caveValidationIndex = 0;
                }

            } else {
                ++caveValidationIndex;
            }

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

        int scanned = 0;

        while (scanned < BlocksPerTick) {
            //
            // Y is the fastest-moving axis.
            //
            // Nearby X/Z columns are therefore fully scanned
            // vertically before moving farther horizontally.
            //
            int dx = centeredOffset(scanXIndex);

            int dy = centeredOffset(scanYIndex);

            int dz = centeredOffset(scanZIndex);

            BlockPos pos { scanCenter.x + dx, scanCenter.y + dy, scanCenter.z + dz };

            //
            // Advance scan cursor.
            //
            ++scanYIndex;

            if (scanYIndex >= side) {
                scanYIndex = 0;

                ++scanXIndex;

                if (scanXIndex >= side) {
                    scanXIndex = 0;

                    ++scanZIndex;

                    //
                    // Full pass complete.
                    //
                    // Begin another pass so:
                    //
                    // - newly exposed caves are discovered
                    // - newly placed/destroyed ores are found
                    //
                    if (scanZIndex >= side) {
                        scanZIndex = 0;
                    }
                }
            }

            ++scanned;

            //
            // Ignore cube corners outside the requested
            // spherical radius.
            //
            long long distanceSq =
                static_cast<long long>(dx) * dx + static_cast<long long>(dy) * dy + static_cast<long long>(dz) * dz;

            if (distanceSq > rangeSq) {
                continue;
            }

            //
            // Bedrock world height limits.
            //
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
            // CAVE / PATH ESP
            // ========================================================
            //
            if (xRaySettings.caveESP) {
                //
                // Cheap first filter.
                //
                // Do not perform all of the cave checks unless the
                // current block is already known to be air.
                //
                if (isAirBlock(block) && isCaveCandidate(region, pos)) {
                    addCave(pos);
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
        // No world loaded.
        //
        if (!tick.getLevel()) {
            ores.clear();
            caves.clear();

            scanInitialized = false;

            oreValidationIndex = 0;
            caveValidationIndex = 0;

            return;
        }

        //
        // Master X-Ray disabled.
        //
        if (!xRaySettings.enabled) {
            ores.clear();
            caves.clear();

            scanInitialized = false;

            oreValidationIndex = 0;
            caveValidationIndex = 0;

            return;
        }

        //
        // Neither scanner feature needs world data.
        //
        if (!xRaySettings.oreESP && !xRaySettings.caveESP) {
            ores.clear();
            caves.clear();

            scanInitialized = false;

            oreValidationIndex = 0;
            caveValidationIndex = 0;

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

            caveValidationIndex = 0;
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
        // Validate only features currently enabled.
        //
        if (xRaySettings.oreESP) {
            validateCachedOres(region);
        }

        if (xRaySettings.caveESP) {
            validateCachedCaves(region);
        }

        //
        // One shared scan performs both Ore ESP and Cave ESP.
        //
        scanBlocks(region);
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

        //
        // Same rendering material currently used by the working
        // Ore ESP and Latite BlockOutline.
        //
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
        // CAVE / PATH ESP
        // ============================================================
        //

        if (renderCaves) {
            //
            // Purple path color.
            //
            d2d::Color baseCaveColor = d2d::Color::RGB(0xA9, 0x5C, 0xFF);

            //
            // Cave Opacity is stored as 5 - 100.
            //
            float caveAlpha = std::clamp(static_cast<float>(xRaySettings.caveOpacity) / 100.0f,

                                         0.05f, 1.0f);

            //
            // --------------------------------------------------------
            // PATH OUTLINE
            // --------------------------------------------------------
            //

            if (xRaySettings.outline) {
                float outlineAlpha = std::max(0.60f, caveAlpha);

                d2d::Color outlineColor = baseCaveColor.asAlpha(outlineAlpha);

                for (auto const& cave : caves) {
                    drawCaveTileOutline(dc, cave.pos, outlineColor);
                }

                dc.flush();
            }

            //
            // --------------------------------------------------------
            // PATH FILL
            // --------------------------------------------------------
            //

            if (xRaySettings.fill) {
                d2d::Color fillColor = baseCaveColor.asAlpha(caveAlpha);

                for (auto const& cave : caves) {
                    drawCaveTileFill(dc, cave.pos, fillColor);
                }

                dc.flush();
            }
        }
    }

} // namespace Nexus
