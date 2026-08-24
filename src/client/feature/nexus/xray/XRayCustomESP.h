#pragma once

#include "client/event/Event.h"
#include "client/event/Listener.h"
#include "util/LMath.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SDK {
    class Block;
    class BlockSource;
}

namespace Nexus {

    class XRayCustomESP final : public Listener {
    public:
        static void initialize();

        static void observeBlock(const BlockPos& pos, SDK::Block* block);

        [[nodiscard]]
        bool shouldListen() override {
            return true;
        }

    private:
        struct BlockKey {
            int x = 0;
            int y = 0;
            int z = 0;

            bool operator==(const BlockKey& other) const noexcept {
                return x == other.x && y == other.y && z == other.z;
            }
        };

        struct BlockKeyHash {
            std::size_t operator()(const BlockKey& key) const noexcept {
                std::size_t h = std::hash<int> {}(key.x);

                h ^= std::hash<int> {}(key.y) + 0x9e3779b9 + (h << 6) + (h >> 2);

                h ^= std::hash<int> {}(key.z) + 0x9e3779b9 + (h << 6) + (h >> 2);

                return h;
            }
        };

        struct Hit {
            BlockPos pos { 0, 0, 0 };
            std::string blockId;
        };

        struct MeshQuad {
            std::string blockId;

            std::uint8_t face = 0;

            int plane = 0;

            int u = 0;
            int v = 0;

            int width = 1;
            int height = 1;
        };

        struct OutlineLine {
            Vec3 from { 0.0f, 0.0f, 0.0f };
            Vec3 to { 0.0f, 0.0f, 0.0f };

            std::string blockId;
        };

        XRayCustomESP();

        static XRayCustomESP& instance();

        void observeBlockImpl(const BlockPos& pos, SDK::Block* block);

        void onTick(Event& event);
        void onRender(Event& event);

        void syncTargetRevision();

        void pruneAndValidate(SDK::BlockSource* region, const Vec3& playerPos);

        void rebuildGeometry();

        void eraseHit(std::size_t index);

        static BlockKey makeKey(const BlockPos& pos);

        std::vector<Hit> hits {};

        std::unordered_map<BlockKey, std::size_t, BlockKeyHash> hitIndex {};

        std::vector<MeshQuad> mesh {};

        std::vector<OutlineLine> outlineLines {};

        bool geometryDirty = true;

        std::size_t validationCursor = 0;

        std::uint64_t targetRevision = 0;
    };

} // namespace Nexus
