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
            // View-dependent subset of structural faces currently
            // hidden behind terrain.
            //
            std::uint8_t occludedFaces = 0;

            //
            // True when this cell belongs to a cave region large
            // enough to be useful.
            //
            bool regionVisible = false;
        };

        //
        // One greedy-meshed rectangular cave surface.
        //
        struct CaveMeshQuad {
            std::uint8_t face = 0;

            int plane = 0;

            int u = 0;
            int v = 0;

            int width = 1;
            int height = 1;
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
        // ============================================================
        // MAIN SCANNER
        // ============================================================
        //

        void resetScan(BlockPos const& center, int range);

        void scanBlocks(SDK::BlockSource* region);

        static int centeredOffset(int index);

        //
        // ============================================================
        // ORE ESP
        // ============================================================
        //

        std::optional<OreType> classifyOre(SDK::Block* block) const;

        bool isOreEnabled(OreType type) const;

        bool containsOre(BlockPos const& pos) const;

        void addOre(BlockPos const& pos, OreType type);

        void validateCachedOres(SDK::BlockSource* region);

        void pruneCachedOres(BlockPos const& center, int range);

        //
        // ============================================================
        // CAVE BLOCK CLASSIFICATION
        // ============================================================
        //

        bool isAirBlock(SDK::Block* block) const;

        bool isCaveSpaceBlock(SDK::Block* block) const;

        bool passesAirCheck(SDK::BlockSource* region, BlockPos const& pos) const;

        bool hasRoofAbove(SDK::BlockSource* region, BlockPos const& pos) const;

        bool isCaveAirCandidate(SDK::BlockSource* region, BlockPos const& pos) const;

        std::uint8_t getExposedCaveFaces(SDK::BlockSource* region, BlockPos const& pos) const;

        //
        // ============================================================
        // CAVE CACHE
        // ============================================================
        //

        void addOrUpdateCave(BlockPos const& pos, std::uint8_t faces);

        void eraseCaveAt(std::size_t index);

        void validateCachedCaves(SDK::BlockSource* region);

        void pruneCachedCaves(BlockPos const& center, int range);

        //
        // ============================================================
        // CAVE REGION FILTER
        // ============================================================
        //

        void rebuildCaveRegions();

        //
        // ============================================================
        // CAVE VISIBILITY / OCCLUSION
        // ============================================================
        //

        static Vec3 getCaveFaceCenter(BlockPos const& pos, std::uint8_t face);

        bool isLineOccluded(SDK::BlockSource* region, Vec3 const& start, Vec3 const& end) const;

        void updateCaveOcclusion(SDK::BlockSource* region, Vec3 const& viewOrigin);

        //
        // ============================================================
        // GREEDY CAVE MESH
        // ============================================================
        //

        void rebuildCaveMesh();

        static BlockKey makeBlockKey(BlockPos const& pos);

        //
        // ============================================================
        // CACHED WORLD DATA
        // ============================================================
        //

        std::vector<OreHit> ores {};

        std::vector<CaveHit> caves {};

        std::unordered_map<BlockKey, std::size_t, BlockKeyHash> caveIndex {};

        std::vector<CaveMeshQuad> caveMesh {};

        //
        // Structural cave topology changed.
        //
        bool caveRegionsDirty = true;

        //
        // Renderable cave faces changed.
        //
        bool caveMeshDirty = true;

        //
        // Avoid rebuilding all connected components every tick while
        // the incremental scanner is still discovering the world.
        //
        int caveRegionRebuildTimer = 0;

        //
        // ============================================================
        // SCANNER STATE
        // ============================================================
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
        // Cave filter setting state.
        //
        bool lastAirCheck3x3x3 = true;
        bool lastIgnoreSurface = true;

        //
        // ============================================================
        // PERFORMANCE
        // ============================================================
        //

        static constexpr int BlocksPerTickOreOnly = 4096;
        static constexpr int BlocksPerTickWithCaves = 2048;

        static constexpr int OreValidationPerTick = 32;
        static constexpr int CaveValidationPerTick = 64;
        static constexpr int CaveOcclusionCellsPerTick = 256;

        static constexpr int CaveRegionRebuildIntervalTicks = 5;

        static constexpr int RecenterDistance = 8;

        //
        // ============================================================
        // CAVE DETECTION
        // ============================================================
        //

        static constexpr int RoofCheckDistance = 12;
        static constexpr int MinimumAirNeighbors = 6;

        //
        // ============================================================
        // CAVE REGION FILTER
        // ============================================================
        //
        // Keep a region if:
        //
        // connected cells >= 8
        //
        // OR:
        //
        // it stretches at least 6 blocks along any axis.
        //
        // The span rule protects long narrow tunnels.
        //

        static constexpr int MinimumCaveRegionSize = 8;
        static constexpr int MinimumCaveRegionSpan = 6;
    };

} // namespace Nexus
