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

namespace Nexus {

    namespace {

        void drawOutlineBlock(MCDrawUtil3D& dc, BlockPos const& pos, d2d::Color const& color) {
            float x = static_cast<float>(pos.x);
            float y = static_cast<float>(pos.y);
            float z = static_cast<float>(pos.z);

            float x2 = x + 1.0f;
            float y2 = y + 1.0f;
            float z2 = z + 1.0f;

            dc.drawLine({ x, y, z }, { x2, y, z }, color);
            dc.drawLine({ x2, y, z }, { x2, y, z2 }, color);
            dc.drawLine({ x2, y, z2 }, { x, y, z2 }, color);
            dc.drawLine({ x, y, z2 }, { x, y, z }, color);

            dc.drawLine({ x, y2, z }, { x2, y2, z }, color);
            dc.drawLine({ x2, y2, z }, { x2, y2, z2 }, color);
            dc.drawLine({ x2, y2, z2 }, { x, y2, z2 }, color);
            dc.drawLine({ x, y2, z2 }, { x, y2, z }, color);

            dc.drawLine({ x, y, z }, { x, y2, z }, color);
            dc.drawLine({ x2, y, z }, { x2, y2, z }, color);
            dc.drawLine({ x2, y, z2 }, { x2, y2, z2 }, color);
            dc.drawLine({ x, y, z2 }, { x, y2, z2 }, color);
        }

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

    }

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

    int XRayScanner::centeredOffset(int index) {
        if (index == 0) {
            return 0;
        }

        int distance = (index + 1) / 2;

        //
        // 0, -1, +1, -2, +2, -3, +3 ...
        //
        return (index & 1) ? -distance : distance;
    }

    void XRayScanner::resetScan(BlockPos const& center, int range) {
        scanCenter = center;
        activeRange = range;

        scanXIndex = 0;
        scanYIndex = 0;
        scanZIndex = 0;

        scanInitialized = true;

        pruneCachedOres(center, range);
    }

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

        if (validationIndex >= ores.size()) {
            validationIndex = 0;
        }
    }

    void XRayScanner::validateCachedOres(SDK::BlockSource* region) {
        if (!region || ores.empty()) {
            validationIndex = 0;
            return;
        }

        int checked = 0;

        while (checked < ValidationPerTick && !ores.empty()) {
            if (validationIndex >= ores.size()) {
                validationIndex = 0;
            }

            BlockPos const pos = ores[validationIndex].pos;

            SDK::Block* block = region->getBlock(pos);

            auto type = classifyOre(block);

            if (!type.has_value()) {
                ores.erase(ores.begin() + static_cast<std::ptrdiff_t>(validationIndex));

                if (validationIndex >= ores.size()) {
                    validationIndex = 0;
                }
            } else {
                ores[validationIndex].type = *type;

                ++validationIndex;
            }

            ++checked;
        }
    }

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
            // This means nearby X/Z columns get scanned through
            // their vertical range before we move farther away
            // horizontally.
            //
            int dx = centeredOffset(scanXIndex);

            int dy = centeredOffset(scanYIndex);

            int dz = centeredOffset(scanZIndex);

            BlockPos pos { scanCenter.x + dx, scanCenter.y + dy, scanCenter.z + dz };

            //
            // Advance cursor.
            //
            ++scanYIndex;

            if (scanYIndex >= side) {
                scanYIndex = 0;
                ++scanXIndex;

                if (scanXIndex >= side) {
                    scanXIndex = 0;
                    ++scanZIndex;

                    //
                    // Full scan completed.
                    //
                    // Start another pass so changed blocks will
                    // eventually be rediscovered.
                    //
                    if (scanZIndex >= side) {
                        scanZIndex = 0;
                    }
                }
            }

            ++scanned;

            //
            // Ignore cube corners outside the requested spherical
            // scan radius.
            //
            long long distanceSq =
                static_cast<long long>(dx) * dx + static_cast<long long>(dy) * dy + static_cast<long long>(dz) * dz;

            if (distanceSq > rangeSq) {
                continue;
            }

            //
            // Normal Bedrock world vertical limits.
            //
            if (pos.y < -64 || pos.y > 320) {
                continue;
            }

            SDK::Block* block = region->getBlock(pos);

            if (!block) {
                continue;
            }

            auto oreType = classifyOre(block);

            if (oreType.has_value()) {
                addOre(pos, *oreType);
            }
        }
    }

    void XRayScanner::onTick(Event& event) {
        auto& tick = static_cast<TickEvent&>(event);

        //
        // No active world.
        //
        if (!tick.getLevel()) {
            ores.clear();

            scanInitialized = false;
            validationIndex = 0;

            return;
        }

        if (!xRaySettings.enabled || !xRaySettings.oreESP) {
            ores.clear();

            scanInitialized = false;
            validationIndex = 0;

            return;
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

        BlockPos center { static_cast<int>(std::floor(playerPos.x)), static_cast<int>(std::floor(playerPos.y)),
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

        validateCachedOres(region);
        scanBlocks(region);
    }

    void XRayScanner::onRender(Event& event) {
        if (!xRaySettings.enabled || !xRaySettings.oreESP || ores.empty()) {
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
        // Same material route Latite's BlockOutline uses when
        // drawing through terrain.
        //
        MCDrawUtil3D dc { clientInstance->levelRenderer, screenContext, SDK::MaterialPtr::getUIColor() };

        auto getColor = [](OreType type) -> d2d::Color {
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

        if (xRaySettings.outline) {
            for (auto const& ore : ores) {
                if (!isOreEnabled(ore.type)) {
                    continue;
                }

                d2d::Color color = getColor(ore.type).asAlpha(0.95f);

                drawOutlineBlock(dc, ore.pos, color);
            }

            dc.flush();
        }

        if (xRaySettings.fill) {
            for (auto const& ore : ores) {
                if (!isOreEnabled(ore.type)) {
                    continue;
                }

                d2d::Color color = getColor(ore.type).asAlpha(0.18f);

                drawFilledBlock(dc, ore.pos, color);
            }

            dc.flush();
        }
    }

} // namespace Nexus
