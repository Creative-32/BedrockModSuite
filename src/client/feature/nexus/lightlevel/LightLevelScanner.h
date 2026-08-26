#pragma once

#include "client/event/Event.h"
#include "client/event/Listener.h"
#include "util/LMath.h"

#include <cstddef>
#include <unordered_map>

namespace SDK {
    class BlockSource;
}

namespace Nexus {

    class LightLevelScanner final : public Listener {
    public:
        static void initialize();

        [[nodiscard]]
        bool shouldListen() override {
            return true;
        }

    private:
        struct BlockKey {
            int x = 0;
            int y = 0;
            int z = 0;

            bool operator==(BlockKey const& other) const noexcept {
                return x == other.x && y == other.y && z == other.z;
            }
        };

        struct BlockKeyHash {
            std::size_t operator()(BlockKey const& key) const noexcept {
                std::size_t h = std::hash<int> {}(key.x);

                h ^= std::hash<int> {}(key.y) + 0x9e3779b9 + (h << 6) + (h >> 2);

                h ^= std::hash<int> {}(key.z) + 0x9e3779b9 + (h << 6) + (h >> 2);

                return h;
            }
        };

        struct LightHit {
            //
            // Solid block whose top surface receives the overlay.
            //
            BlockPos floorPos {};

            //
            // Provisional Minecraft-style 0-15 light value.
            //
            int lightLevel = 0;

            //
            // Keep the native value too.
            //
            // This will be useful when we can get in-game and verify
            // exactly what BlockSource::getBrightness returns.
            //
            float rawBrightness = 0.0f;
        };

        LightLevelScanner();

        static LightLevelScanner& instance();

        void onTick(Event& event);
        void onRender(Event& event);

        //
        // ============================================================
        // SCANNING
        // ============================================================
        //

        void resetScan(BlockPos const& center, int range);

        void scanBlocks(SDK::BlockSource* region);

        void refreshCachedLight(SDK::BlockSource* region);

        static int centeredOffset(int index);

        //
        // ============================================================
        // LIGHT
        // ============================================================
        //

        static int brightnessToLevel(float brightness);

        //
        // ============================================================
        // CACHE
        // ============================================================
        //

        static BlockKey makeBlockKey(BlockPos const& pos);

        std::unordered_map<BlockKey, LightHit, BlockKeyHash> lightHits {};

        //
        // ============================================================
        // SCAN STATE
        // ============================================================
        //

        BlockPos scanCenter {};

        int activeRange = 0;

        int scanXIndex = 0;
        int scanYIndex = 0;
        int scanZIndex = 0;

        bool scanInitialized = false;

        //
        // Round-robin cursor used to keep already discovered floor
        // light values fresh without re-running the entire 3D scan.
        //
        std::size_t lightRefreshCursor = 0;
    };

}
