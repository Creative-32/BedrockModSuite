#pragma once

#include "client/event/Event.h"
#include "client/event/Listener.h"
#include "util/LMath.h"

#include <cstddef>
#include <optional>
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

        //
        // Ore ESP
        //
        void validateCachedOres(SDK::BlockSource* region);
        void pruneCachedOres(BlockPos const& center, int range);

        std::optional<OreType> classifyOre(SDK::Block* block) const;
        bool isOreEnabled(OreType type) const;

        bool containsOre(BlockPos const& pos) const;
        void addOre(BlockPos const& pos, OreType type);

        //
        // Cave / Path ESP
        //
        bool isAirBlock(SDK::Block* block) const;

        bool isCaveCandidate(SDK::BlockSource* region, BlockPos const& pos) const;

        bool passesAirCheck(SDK::BlockSource* region, BlockPos const& pos) const;

        bool hasRoofAbove(SDK::BlockSource* region, BlockPos const& pos) const;

        bool containsCave(BlockPos const& pos) const;
        void addCave(BlockPos const& pos);

        void validateCachedCaves(SDK::BlockSource* region);
        void pruneCachedCaves(BlockPos const& center, int range);

        //
        // Scan ordering
        //
        static int centeredOffset(int index);

        //
        // Cached results
        //
        std::vector<OreHit> ores {};
        std::vector<CaveHit> caves {};

        //
        // Scan state
        //
        BlockPos scanCenter {};

        int activeRange = 0;

        int scanXIndex = 0;
        int scanYIndex = 0;
        int scanZIndex = 0;

        std::size_t oreValidationIndex = 0;
        std::size_t caveValidationIndex = 0;

        bool scanInitialized = false;

        //
        // Performance
        //
        static constexpr int BlocksPerTick = 4096;

        static constexpr int OreValidationPerTick = 32;
        static constexpr int CaveValidationPerTick = 96;

        static constexpr int RecenterDistance = 8;

        //
        // Cave detection
        //
        static constexpr int RoofCheckDistance = 12;
        static constexpr int MinimumAirNeighbors = 6;
    };

} // namespace Nexus
