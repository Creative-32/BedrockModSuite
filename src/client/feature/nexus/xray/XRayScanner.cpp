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

    bool XRayScanner::isDiamondOre(SDK::Block* block) const {
        if (!block || !block->legacyBlock) {
            return false;
        }

        std::string id = block->legacyBlock->namespacedId.getString();

        return id == "minecraft:diamond_ore" || id == "minecraft:deepslate_diamond_ore";
    }

    bool XRayScanner::containsOre(BlockPos const& pos) const {
        for (auto const& ore : diamondOres) {
            if (ore.x == pos.x && ore.y == pos.y && ore.z == pos.z) {
                return true;
            }
        }

        return false;
    }

    void XRayScanner::addOre(BlockPos const& pos) {
        if (containsOre(pos)) {
            return;
        }

        diamondOres.push_back(pos);
    }

    void XRayScanner::pruneCachedOres(BlockPos const& center, int range) {
        //
        // Keep a small margin so boxes do not constantly disappear
        // when the player walks a couple blocks.
        //
        int keepRange = range + RecenterDistance;

        long long keepRangeSq = static_cast<long long>(keepRange) * static_cast<long long>(keepRange);

        std::erase_if(diamondOres, [&](BlockPos const& pos) {
            long long dx = static_cast<long long>(pos.x) - center.x;

            long long dy = static_cast<long long>(pos.y) - center.y;

            long long dz = static_cast<long long>(pos.z) - center.z;

            long long distanceSq = dx * dx + dy * dy + dz * dz;

            return distanceSq > keepRangeSq;
        });

        if (validationIndex >= diamondOres.size()) {
            validationIndex = 0;
        }
    }

    void XRayScanner::validateCachedOres(SDK::BlockSource* region) {
        if (!region || diamondOres.empty()) {
            validationIndex = 0;
            return;
        }

        int checked = 0;

        while (checked < ValidationPerTick && !diamondOres.empty()) {
            if (validationIndex >= diamondOres.size()) {
                validationIndex = 0;
            }

            BlockPos const pos = diamondOres[validationIndex];

            SDK::Block* block = region->getBlock(pos);

            if (!isDiamondOre(block)) {
                diamondOres.erase(diamondOres.begin() + static_cast<std::ptrdiff_t>(validationIndex));

                if (validationIndex >= diamondOres.size()) {
                    validationIndex = 0;
                }
            } else {
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

            if (isDiamondOre(block)) {
                addOre(pos);
            }
        }
    }

    void XRayScanner::onTick(Event& event) {
        auto& tick = static_cast<TickEvent&>(event);

        //
        // No active world.
        //
        if (!tick.getLevel()) {
            diamondOres.clear();

            scanInitialized = false;
            validationIndex = 0;

            return;
        }

        //
        // First proof-of-concept:
        //
        // Master X-Ray + Ore ESP + Diamond must all be enabled.
        //
        if (!xRaySettings.enabled || !xRaySettings.oreESP || !xRaySettings.diamond) {
            diamondOres.clear();

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
        if (!xRaySettings.enabled || !xRaySettings.oreESP || !xRaySettings.diamond || diamondOres.empty()) {
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

        d2d::Color outlineColor = d2d::Color::RGB(0x42, 0xE6, 0xD5).asAlpha(0.95f);

        d2d::Color fillColor = d2d::Color::RGB(0x42, 0xE6, 0xD5).asAlpha(0.18f);

        if (xRaySettings.outline) {
            for (auto const& pos : diamondOres) {
                drawOutlineBlock(dc, pos, outlineColor);
            }

            dc.flush();
        }

        if (xRaySettings.fill) {
            for (auto const& pos : diamondOres) {
                drawFilledBlock(dc, pos, fillColor);
            }

            dc.flush();
        }
    }

} // namespace Nexus
