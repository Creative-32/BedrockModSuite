#pragma once

#include "client/event/Event.h"
#include "client/event/Listener.h"
#include "util/LMath.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace SDK {
    class Block;
    class BlockSource;
}

namespace Nexus {

    class XRayScanner final : public Listener {
    public:
        static void initialize();

        [[nodiscard]]
        bool shouldListen() override {
            return true;
        }

    private:
        enum class OreType {
            Diamond,
            Emerald,
            Gold,
            Iron,
            Redstone,
            Lapis,
            Coal,
            Copper,
            AncientDebris
        };

        struct OreHit {
            BlockPos pos;
            OreType type;
        };

        struct CaveHit {
            BlockPos pos;

            //
            // Structural cave shell.
            //
            std::uint8_t faces = 0;

            //
            // View-dependent subset of structural faces that are
            // currently hidden behind terrain.
            //
            std::uint8_t occludedFaces = 0;
        };

        struct BlockKey {
            int x;
            int y;
            int z;

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

        XRayScanner();

        static XRayScanner& instance();

        void onTick(Event& event);
        void onRender(Event& event);

        //
        // Main scanner
        //
        void resetScan(BlockPos const& center, int range);

        void scanBlocks(SDK::BlockSource* region);

        static int centeredOffset(int index);

        //
        // Ore ESP
        //
        std::optional<OreType> classifyOre(SDK::Block* block) const;

        bool isOreEnabled(OreType type) const;

        bool containsOre(BlockPos const& pos) const;

        void addOre(BlockPos const& pos, OreType type);

        void validateCachedOres(SDK::BlockSource* region);

        void pruneCachedOres(BlockPos const& center, int range);

        //
        // Cave ESP block classification
        //
        bool isAirBlock(SDK::Block* block) const;

        //
        // True when a voxel should behave as open cave volume rather
        // than a full 1x1x1 wall.
        //
        // Includes literal air plus thin/non-volume blocks such as
        // torches, rails, ladders, signs, buttons, etc.
        //
        bool isCaveSpaceBlock(SDK::Block* block) const;

        bool passesAirCheck(SDK::BlockSource* region, BlockPos const& pos) const;

        bool hasRoofAbove(SDK::BlockSource* region, BlockPos const& pos) const;

        bool isCaveAirCandidate(SDK::BlockSource* region, BlockPos const& pos) const;

        std::uint8_t getExposedCaveFaces(SDK::BlockSource* region, BlockPos const& pos) const;

        //
        // Cave cache
        //
        void addOrUpdateCave(BlockPos const& pos, std::uint8_t faces);

        void eraseCaveAt(std::size_t index);

        void validateCachedCaves(SDK::BlockSource* region);

        void pruneCachedCaves(BlockPos const& center, int range);

        //
        // Cave visibility / occlusion
        //
        static Vec3 getCaveFaceCenter(BlockPos const& pos, std::uint8_t face);

        bool isLineOccluded(SDK::BlockSource* region, Vec3 const& start, Vec3 const& end) const;

        void updateCaveOcclusion(SDK::BlockSource* region, Vec3 const& viewOrigin);

        static BlockKey makeBlockKey(BlockPos const& pos);

        //
        // Cached world data
        //
        std::vector<OreHit> ores {};

        std::vector<CaveHit> caves {};

        std::unordered_map<BlockKey, std::size_t, BlockKeyHash> caveIndex {};

        //
        // Scanner state
        //
        BlockPos scanCenter {};

        int activeRange = 0;

        int scanXIndex = 0;
        int scanYIndex = 0;
        int scanZIndex = 0;

        std::size_t oreValidationIndex = 0;
        std::size_t caveValidationIndex = 0;
        std::size_t caveOcclusionIndex = 0;

        bool scanInitialized = false;

        //
        // Cave filter setting state
        //
        bool lastAirCheck3x3x3 = true;
        bool lastIgnoreSurface = true;

        //
        // Performance
        //
        static constexpr int BlocksPerTickOreOnly = 4096;
        static constexpr int BlocksPerTickWithCaves = 2048;

        static constexpr int OreValidationPerTick = 32;
        static constexpr int CaveValidationPerTick = 64;
        static constexpr int CaveOcclusionCellsPerTick = 256;

        static constexpr int RecenterDistance = 8;

        //
        // Cave detection
        //
        static constexpr int RoofCheckDistance = 12;
        static constexpr int MinimumAirNeighbors = 6;
    };

} // namespace Nexus
