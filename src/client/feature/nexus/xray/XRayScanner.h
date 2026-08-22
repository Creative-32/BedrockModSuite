#pragma once

#include "client/event/Event.h"
#include "client/event/Listener.h"
#include "util/LMath.h"

#include <cstddef>
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
        XRayScanner();

        static XRayScanner& instance();

        void onTick(Event& event);
        void onRender(Event& event);

        void resetScan(BlockPos const& center, int range);
        void scanBlocks(SDK::BlockSource* region);
        void validateCachedOres(SDK::BlockSource* region);
        void pruneCachedOres(BlockPos const& center, int range);

        bool isDiamondOre(SDK::Block* block) const;

        bool containsOre(BlockPos const& pos) const;
        void addOre(BlockPos const& pos);

        static int centeredOffset(int index);

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

        std::vector<OreHit> ores {};

        BlockPos scanCenter {};

        int activeRange = 0;

        int scanXIndex = 0;
        int scanYIndex = 0;
        int scanZIndex = 0;

        std::size_t validationIndex = 0;

        bool scanInitialized = false;

        static constexpr int BlocksPerTick = 4096;
        static constexpr int ValidationPerTick = 32;
        static constexpr int RecenterDistance = 8;
    };

} // namespace Nexus
