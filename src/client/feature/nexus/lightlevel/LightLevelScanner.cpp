#include "pch.h"

#include "LightLevelScanner.h"

#include "LightLevelColor.h"
#include "LightLevelSettings.h"

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
#include <limits>

namespace Nexus {

    namespace {

        //
        // ============================================================
        // SCANNER CONFIGURATION
        // ============================================================
        //

        constexpr int VerticalScanRange = 16;

        constexpr int BlocksPerTick = 4096;

        constexpr std::size_t CachedLightRefreshPerTick = 4096;

        constexpr int RecenterDistance = 4;

        constexpr float SurfaceInset = 0.0125f;

        constexpr float TileInset = 0.035f;

        bool isAirBlock(SDK::Block* block) {
            if (!block || !block->legacyBlock) {
                return false;
            }

            const std::string id = block->legacyBlock->namespacedId.getString();

            return id == "minecraft:air" || id == "minecraft:cave_air" || id == "minecraft:void_air";
        }

        //
        // ============================================================
        // DRAW ONE LIGHT TILE
        // ============================================================
        //
        // selection_overlay gives us the depth behavior we need, but
        // this Minecraft build does not honor our supplied alpha.
        //
        // Because of that, "opacity" is represented geometrically:
        //
        //     100% -> full tile
        //      50% -> smaller centered tile
        //      10% -> much smaller centered tile
        //
        // Distance fade uses the exact same coverage value.
        //

        void drawLightTile(MCDrawUtil3D& dc, BlockPos const& floorPos, d2d::Color const& color, float coverage,
                           bool drawFill, bool drawOutline) {
            coverage = std::clamp(coverage, 0.0f, 1.0f);

            if (coverage <= 0.0f) {
                return;
            }

            //
            // Coverage represents AREA.
            //
            // If:
            //
            //     sideScale = sqrt(coverage)
            //
            // then the visible area changes approximately linearly
            // with the selected coverage value.
            //

            float sideScale = std::sqrt(coverage);

            float centerX = static_cast<float>(floorPos.x) + 0.5f;

            float centerZ = static_cast<float>(floorPos.z) + 0.5f;

            float y = static_cast<float>(floorPos.y + 1) + SurfaceInset;

            float fullHalfSize = 0.5f - TileInset;

            float halfSize = fullHalfSize * sideScale;

            float x0 = centerX - halfSize;

            float x1 = centerX + halfSize;

            float z0 = centerZ - halfSize;

            float z1 = centerZ + halfSize;

            if (drawFill) {
                dc.fillQuad({ x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 }, color);
            }

            if (drawOutline) {
                dc.drawLine({ x0, y, z0 }, { x1, y, z0 }, color);

                dc.drawLine({ x1, y, z0 }, { x1, y, z1 }, color);

                dc.drawLine({ x1, y, z1 }, { x0, y, z1 }, color);

                dc.drawLine({ x0, y, z1 }, { x0, y, z0 }, color);
            }
        }

    }

    //
    // ================================================================
    // INITIALIZATION
    // ================================================================
    //

    LightLevelScanner& LightLevelScanner::instance() {
        static LightLevelScanner scanner;

        return scanner;
    }

    void LightLevelScanner::initialize() {
        instance();
    }

    LightLevelScanner::LightLevelScanner() {
        Eventing::get().listen<TickEvent>(this, (EventListenerFunc)&LightLevelScanner::onTick, 0, true);

        Eventing::get().listen<RenderLevelEvent>(this, (EventListenerFunc)&LightLevelScanner::onRender, 0, true);
    }

    //
    // ================================================================
    // BLOCK KEY
    // ================================================================
    //

    LightLevelScanner::BlockKey LightLevelScanner::makeBlockKey(BlockPos const& pos) {
        return { pos.x, pos.y, pos.z };
    }

    //
    // ================================================================
    // CENTERED SCAN ORDER
    // ================================================================
    //

    int LightLevelScanner::centeredOffset(int index) {
        if (index == 0) {
            return 0;
        }

        int distance = (index + 1) / 2;

        return (index & 1) ? -distance : distance;
    }

    //
    // ================================================================
    // BRIGHTNESS -> 0-15
    // ================================================================
    //

    int LightLevelScanner::brightnessToLevel(float brightness) {
        if (!std::isfinite(brightness)) {
            return 0;
        }

        //
        // Some builds may expose an actual 0-15 value.
        //

        if (brightness > 1.001f) {
            return std::clamp(static_cast<int>(std::lround(brightness)), 0, 15);
        }

        brightness = std::clamp(brightness, 0.0f, 1.0f);

        //
        // Approximate Minecraft's nonlinear visual brightness ramp.
        //

        int bestLevel = 0;

        float bestDifference = std::numeric_limits<float>::max();

        for (int level = 0; level <= 15; ++level) {
            float normalizedLevel = static_cast<float>(level) / 15.0f;

            float inverse = 1.0f - normalizedLevel;

            float expectedBrightness = normalizedLevel / (inverse * 3.0f + 1.0f);

            float difference = std::abs(brightness - expectedBrightness);

            if (difference < bestDifference) {
                bestDifference = difference;

                bestLevel = level;
            }
        }

        return bestLevel;
    }

    //
    // ================================================================
    // RESET SCAN
    // ================================================================
    //

    void LightLevelScanner::resetScan(BlockPos const& center, int range) {
        scanCenter = center;

        activeRange = range;

        scanXIndex = 0;
        scanYIndex = 0;
        scanZIndex = 0;

        scanInitialized = true;

        lightRefreshCursor = 0;

        lightHits.clear();
    }

    //
    // ================================================================
    // WORLD SCANNER
    // ================================================================
    //

    void LightLevelScanner::scanBlocks(SDK::BlockSource* region) {
        if (!region || !scanInitialized) {
            return;
        }

        int range = activeRange;

        int horizontalSide = range * 2 + 1;

        int verticalSide = VerticalScanRange * 2 + 1;

        long long rangeSq = static_cast<long long>(range) * static_cast<long long>(range);

        int scanned = 0;

        while (scanned < BlocksPerTick) {
            int dx = centeredOffset(scanXIndex);

            int dy = centeredOffset(scanYIndex);

            int dz = centeredOffset(scanZIndex);

            BlockPos floorPos { scanCenter.x + dx, scanCenter.y + dy, scanCenter.z + dz };

            //
            // --------------------------------------------------------
            // ADVANCE SCAN CURSOR
            // --------------------------------------------------------
            //

            ++scanYIndex;

            if (scanYIndex >= verticalSide) {
                scanYIndex = 0;

                ++scanXIndex;

                if (scanXIndex >= horizontalSide) {
                    scanXIndex = 0;

                    ++scanZIndex;

                    if (scanZIndex >= horizontalSide) {
                        scanZIndex = 0;
                    }
                }
            }

            ++scanned;

            long long horizontalDistanceSq = static_cast<long long>(dx) * dx + static_cast<long long>(dz) * dz;

            if (horizontalDistanceSq > rangeSq) {
                continue;
            }

            if (floorPos.y < -64 || floorPos.y > 318) {
                continue;
            }

            BlockKey key = makeBlockKey(floorPos);

            SDK::Block* floorBlock = region->getBlock(floorPos);

            if (!floorBlock || !floorBlock->legacyBlock || isAirBlock(floorBlock)) {
                lightHits.erase(key);

                continue;
            }

            BlockPos feetPos { floorPos.x, floorPos.y + 1, floorPos.z };

            BlockPos headPos { floorPos.x, floorPos.y + 2, floorPos.z };

            SDK::Block* feetBlock = region->getBlock(feetPos);

            SDK::Block* headBlock = region->getBlock(headPos);

            if (!isAirBlock(feetBlock) || !isAirBlock(headBlock)) {
                lightHits.erase(key);

                continue;
            }

            float rawBrightness = region->getBrightnessValue(feetPos);

            int lightLevel = brightnessToLevel(rawBrightness);

            lightHits[key] = { floorPos, lightLevel, rawBrightness };
        }
    }

    //
    // ================================================================
    // REFRESH CACHED LIGHT
    // ================================================================
    //

    void LightLevelScanner::refreshCachedLight(SDK::BlockSource* region) {
        if (!region || lightHits.empty()) {
            lightRefreshCursor = 0;

            return;
        }

        std::size_t totalHits = lightHits.size();

        if (lightRefreshCursor >= totalHits) {
            lightRefreshCursor = 0;
        }

        std::size_t currentIndex = 0;

        std::size_t refreshed = 0;

        for (auto& [key, hit] : lightHits) {
            (void)key;

            if (currentIndex < lightRefreshCursor) {
                ++currentIndex;

                continue;
            }

            ++currentIndex;

            BlockPos feetPos { hit.floorPos.x, hit.floorPos.y + 1, hit.floorPos.z };

            float rawBrightness = region->getBrightnessValue(feetPos);

            hit.rawBrightness = rawBrightness;

            hit.lightLevel = brightnessToLevel(rawBrightness);

            ++refreshed;

            if (refreshed >= CachedLightRefreshPerTick) {
                break;
            }
        }

        lightRefreshCursor += refreshed;

        if (lightRefreshCursor >= totalHits || refreshed == 0) {
            lightRefreshCursor = 0;
        }
    }

    //
    // ================================================================
    // TICK
    // ================================================================
    //

    void LightLevelScanner::onTick(Event& event) {
        auto& tick = static_cast<TickEvent&>(event);

        if (!tick.getLevel()) {
            lightHits.clear();

            lightRefreshCursor = 0;

            scanInitialized = false;

            return;
        }

        if (!lightLevelSettings.enabled) {
            lightHits.clear();

            lightRefreshCursor = 0;

            scanInitialized = false;

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

        BlockPos center { static_cast<int>(std::floor(playerPos.x)),

                          static_cast<int>(std::floor(playerPos.y)),

                          static_cast<int>(std::floor(playerPos.z)) };

        int range = std::clamp(lightLevelSettings.range, 8, 64);

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

        scanBlocks(region);

        refreshCachedLight(region);
    }

    //
    // ================================================================
    // RENDER
    // ================================================================
    //

    void LightLevelScanner::onRender(Event& event) {
        if (!lightLevelSettings.enabled || lightHits.empty()) {
            return;
        }

        auto clientInstance = SDK::ClientInstance::get();

        if (!clientInstance || !clientInstance->levelRenderer) {
            return;
        }

        auto player = clientInstance->getLocalPlayer();

        if (!player) {
            return;
        }

        auto& renderEvent = static_cast<RenderLevelEvent&>(event);

        auto screenContext = renderEvent.getScreenContext();

        if (!screenContext) {
            return;
        }

        //
        // ============================================================
        // DEPTH-SAFE MATERIAL
        // ============================================================
        //
        // This material respects terrain/entity depth correctly.
        //
        // Its supplied vertex alpha is not usable in this Minecraft
        // version, so geometric coverage handles opacity/fade instead.
        //

        MCDrawUtil3D dc { clientInstance->levelRenderer, screenContext,
                          SDK::MaterialPtr::getSelectionOverlayMaterial() };

        Vec3 playerPos = player->getPos();

        float range = static_cast<float>(std::clamp(lightLevelSettings.range, 8, 64));

        //
        // "Opacity" is converted into visible geometric coverage.
        //

        float baseCoverage = std::clamp(static_cast<float>(lightLevelSettings.opacity) / 100.0f, 0.0f, 1.0f);

        float brightnessScale = std::clamp(static_cast<float>(lightLevelSettings.brightness) / 100.0f, 0.1f, 1.5f);

        for (auto const& [key, hit] : lightHits) {
            (void)key;

            float centerX = static_cast<float>(hit.floorPos.x) + 0.5f;

            float centerZ = static_cast<float>(hit.floorPos.z) + 0.5f;

            float dx = centerX - playerPos.x;

            float dz = centerZ - playerPos.z;

            float horizontalDistance = std::sqrt(dx * dx + dz * dz);

            if (horizontalDistance > range) {
                continue;
            }

            //
            // ========================================================
            // DISTANCE COVERAGE
            // ========================================================
            //

            float distanceMultiplier = 1.0f;

            if (lightLevelSettings.distanceFade) {
                float distanceT = std::clamp(horizontalDistance / std::max(range, 1.0f), 0.0f, 1.0f);

                distanceMultiplier = 1.0f - distanceT;
            }

            float coverage = std::clamp(baseCoverage * distanceMultiplier, 0.0f, 1.0f);

            if (coverage <= 0.0f) {
                continue;
            }

            //
            // ========================================================
            // LIGHT COLOR
            // ========================================================
            //

            auto gradient = getLightLevelColor(hit.lightLevel);

            int r = std::clamp(static_cast<int>(std::lround(gradient.r * 255.0f * brightnessScale)), 0, 255);

            int g = std::clamp(static_cast<int>(std::lround(gradient.g * 255.0f * brightnessScale)), 0, 255);

            int b = std::clamp(static_cast<int>(std::lround(gradient.b * 255.0f * brightnessScale)), 0, 255);

            //
            // Actual alpha stays opaque.
            //
            // selection_overlay ignores the dynamic alpha anyway.
            //

            d2d::Color color = d2d::Color::RGB(r, g, b).asAlpha(1.0f);

            drawLightTile(dc, hit.floorPos, color, coverage, lightLevelSettings.fill, lightLevelSettings.outline);
        }

        dc.flush();
    }

} // namespace Nexus
