#pragma once

#include "client/event/Event.h"
#include "client/event/Listener.h"
#include "util/LMath.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace SDK {
    class Block;
    class BlockLegacy;
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

        enum class CaveBlockClass {
            Open,
            Partial,
            Solid,
            Fluid
        };

        struct CaveRayInfo {
            //
            // Number of solid voxels crossed by the ray.
            //
            std::uint8_t solidDepth = 0;

            //
            // Distance from the camera to the first solid obstruction.
            //
            float firstSolidDistance = 0.0f;

            //
            // Full distance from the camera to the cave face.
            //
            float targetDistance = 0.0f;
        };

        struct OreHit {
            BlockPos pos;
            OreType type;
        };

        //
        // One cached greedy-meshed Ore ESP surface.
        //
        struct OreMeshQuad {
            OreType type;

            std::uint8_t face = 0;

            int plane = 0;

            int u = 0;
            int v = 0;

            int width = 1;
            int height = 1;
        };

        //
        // One cached outer boundary line for a merged ore vein.
        //
        struct OreOutlineLine {
            Vec3 start;
            Vec3 end;

            OreType type;
        };

        struct CaveHit {
            BlockPos pos;

            //
            // Structural cave shell.
            //
            std::uint8_t faces = 0;

            //
            // Currently accepted view-dependent hidden faces.
            //
            std::uint8_t occludedFaces = 0;

            //
            // Most recent candidate occlusion result.
            //
            std::uint8_t pendingOccludedFaces = 0;

            //
            // Number of consecutive matching candidate results.
            //
            std::uint8_t occlusionConfirmations = 0;

            //
            // True when this cell belongs to a useful cave region.
            //
            bool regionVisible = false;

            //
            // Terrain-depth bucket for each face.
            //
            // Index order:
            //
            // 0 = Down
            // 1 = Up
            // 2 = North
            // 3 = South
            // 4 = West
            // 5 = East
            //
            // Depth levels:
            //
            // 0 = strongest normal Cave ESP
            // 1-6 = progressively deeper / more separated
            // 7 = weakest normal Cave ESP
            // 8 = special extremely-close foreground protection
            //
            std::array<std::uint8_t, 6> depthBuckets {};
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

            //
            // Depth bucket shared by every face merged into this quad.
            //
            std::uint8_t depthBucket = 0;
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

        void addOre(BlockPos const& pos, OreType type);

        void validateCachedOres(SDK::BlockSource* region);

        void pruneCachedOres(BlockPos const& center, int range);

        void rebuildOreMesh();

        //
        // ============================================================
        // CAVE BLOCK CLASSIFICATION
        // ============================================================
        //

        //
        // Full block-ID classification.
        //
        CaveBlockClass classifyCaveBlock(SDK::Block* block) const;

        //
        // Cached classification used by hot scan/ray paths.
        //
        CaveBlockClass classifyCaveBlockCached(SDK::Block* block) const;

        bool isCaveSpaceBlock(SDK::Block* block) const;

        bool isUndergroundForCaveESP(SDK::BlockSource* region, BlockPos const& pos) const;

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

        CaveRayInfo getCaveRayInfo(SDK::BlockSource* region, Vec3 const& start, Vec3 const& end) const;

        void updateCaveOcclusion(SDK::BlockSource* region, Vec3 const& viewOrigin);

        //
        // ============================================================
        // GREEDY CAVE MESH
        // ============================================================
        //

        void rebuildCaveRenderableMask();

        void rebuildCaveMesh();

        static BlockKey makeBlockKey(BlockPos const& pos);

        //
        // ============================================================
        // CACHED WORLD DATA
        // ============================================================
        //

        std::vector<OreHit> ores {};

        //
        // Cached merged Ore ESP geometry.
        //

        std::vector<OreMeshQuad> oreMesh {};

        std::vector<OreOutlineLine> oreOutlineLines {};

        std::vector<CaveHit> caves {};

        std::unordered_map<BlockKey, std::size_t, BlockKeyHash> caveIndex {};

        //
        // ============================================================
        // CAVE BLOCK CLASSIFICATION CACHE
        // ============================================================
        //
        // Cave classification only depends on BlockLegacy ID.
        //
        // Many individual world blocks share the same BlockLegacy, so
        // this avoids repeatedly constructing/comparing block-ID
        // strings during scanner and DDA ray work.
        //
        mutable std::unordered_map<SDK::BlockLegacy*, CaveBlockClass> caveBlockClassCache {};

        //
        // Cached result of the post-occlusion fragment filter.
        //
        // Index corresponds directly to caves[].
        //
        // 0 = do not render
        // 1 = accepted rendered fragment
        //
        std::vector<std::uint8_t> caveRenderableMask {};

        //
        // Depth-aware mesh used for Cave ESP fill.
        //
        std::vector<CaveMeshQuad> caveMesh {};

        struct CaveOutlineLine {
            Vec3 start;
            Vec3 end;

            //
            // 0-7 = normal smoothed depth
            // 8   = close-foreground suppression
            //
            std::uint8_t depthLevel = 0;
        };

        //
        // Boundary-only Cave ESP outline.
        //
        std::vector<CaveOutlineLine> caveOutlineLines {};

        //
        // Ore blocks or ore-type visibility changed.
        // Rebuild the merged Ore ESP geometry on the next tick.
        //
        bool oreMeshDirty = true;

        //
        // Structural cave topology changed.
        //
        bool caveRegionsDirty = true;

        //
        // Rendered-fragment connectivity needs to be rebuilt.
        //
        // This is only necessary when cave visibility/topology changes.
        //
        bool caveRenderableDirty = true;

        //
        // Fill depth levels or visible cave geometry changed.
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

        //
        // Separate round-robin cursors for nearby and distant cave
        // occlusion updates.
        //

        std::size_t caveNearOcclusionIndex = 0;
        std::size_t caveFarOcclusionIndex = 0;

        //
        // Shared incremental scanner state.
        //

        bool scanInitialized = false;

        //
        // Cave ESP underground gate.
        //

        bool caveUndergroundActive = false;

        //
        // Cave filter setting state.
        //

        bool lastAirCheck3x3x3 = true;
        bool lastIgnoreSurface = true;

        int lastOreRange = 64;
        int lastCaveRange = 64;
        int lastOreEnabledMask = -1;

        //
        // ============================================================
        // PERFORMANCE
        // ============================================================
        //

        static constexpr int BlocksPerTickOreOnly = 4096;
        static constexpr int BlocksPerTickWithCaves = 2048;

        static constexpr int OreValidationPerTick = 32;
        static constexpr int CaveValidationPerTick = 64;

        //
        // Maximum number of per-face DDA occlusion rays performed each tick.
        //
        // Unlike the old cell budget, this directly limits the expensive work.
        // A cave cell with 1 structural face costs 1 ray.
        // A cave cell with 6 structural faces costs 6 rays.
        //
        //
        // Hard upper limit for all Cave ESP DDA rays in one tick.
        //
        static constexpr int CaveOcclusionRaysPerTick = 512;

        //
        // Reserve the first portion of the ray budget for caves close
        // enough that camera movement and foreground separation are
        // visually important.
        //
        // Any unused near budget automatically becomes available to
        // the distant pass.
        //
        static constexpr int CaveNearOcclusionRaysPerTick = 384;

        //
        // Caves within this distance are considered high priority.
        //
        static constexpr float CaveNearOcclusionDistance = 32.0f;
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

        //
        // ============================================================
        // RENDERED CAVE FRAGMENT FILTER
        // ============================================================
        //
        // After view-dependent occlusion, one valid cave can appear
        // as several disconnected rendered pieces.
        //
        // This filter removes tiny rendered fragments without
        // affecting the actual structural cave-region filter.
        //

        static constexpr int MinimumRenderedFragmentCells = 6;
        static constexpr int MinimumRenderedFragmentSpan = 4;
    };

} // namespace Nexus
