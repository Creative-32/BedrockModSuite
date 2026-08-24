#include "pch.h"
#include "XRayCustomESP.h"

#include "XRaySettings.h"
#include "XRayTargets.h"

#include "client/event/Eventing.h"
#include "client/event/events/RenderLevelEvent.h"
#include "client/event/events/TickEvent.h"

#include "mc/common/client/renderer/MaterialPtr.h"
#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/level/block/BlockLegacy.h"

#include "util/DrawUtil3D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace Nexus {
    namespace {

        constexpr std::uint8_t FaceDown = 0;
        constexpr std::uint8_t FaceUp = 1;
        constexpr std::uint8_t FaceNorth = 2;
        constexpr std::uint8_t FaceSouth = 3;
        constexpr std::uint8_t FaceWest = 4;
        constexpr std::uint8_t FaceEast = 5;

        constexpr float FaceInset = 0.01f;
        constexpr std::size_t ValidationBudgetPerTick = 128;

        constexpr std::array<std::array<int, 3>, 6> NeighborOffsets {
            { { { 0, -1, 0 } }, { { 0, 1, 0 } }, { { 0, 0, -1 } }, { { 0, 0, 1 } }, { { -1, 0, 0 } }, { { 1, 0, 0 } } }
        };

        struct IntPoint {
            int x = 0;
            int y = 0;
            int z = 0;

            auto operator<=>(const IntPoint&) const = default;
        };

        struct SurfaceEdgeKey {
            std::string blockId;
            std::uint8_t face = 0;
            int plane = 0;
            IntPoint a {};
            IntPoint b {};

            auto operator<=>(const SurfaceEdgeKey&) const = default;
        };

        struct FinalEdgeKey {
            std::string blockId;
            IntPoint a {};
            IntPoint b {};

            auto operator<=>(const FinalEdgeKey&) const = default;
        };

        std::pair<IntPoint, IntPoint> orderedEdge(IntPoint a, IntPoint b) {
            if (b < a) std::swap(a, b);
            return { a, b };
        }

        int facePlane(const BlockPos& pos, std::uint8_t face) {
            switch (face) {
            case FaceDown:
                return pos.y;
            case FaceUp:
                return pos.y + 1;
            case FaceNorth:
                return pos.z;
            case FaceSouth:
                return pos.z + 1;
            case FaceWest:
                return pos.x;
            case FaceEast:
                return pos.x + 1;
            default:
                return 0;
            }
        }

        std::array<std::pair<IntPoint, IntPoint>, 4> faceEdges(const BlockPos& p, std::uint8_t face) {
            const int x0 = p.x;
            const int y0 = p.y;
            const int z0 = p.z;
            const int x1 = p.x + 1;
            const int y1 = p.y + 1;
            const int z1 = p.z + 1;

            std::array<std::pair<IntPoint, IntPoint>, 4> edges {};

            switch (face) {
            case FaceDown:
            case FaceUp: {
                int y = face == FaceDown ? y0 : y1;
                edges = { { { { x0, y, z0 }, { x1, y, z0 } },
                            { { x1, y, z0 }, { x1, y, z1 } },
                            { { x1, y, z1 }, { x0, y, z1 } },
                            { { x0, y, z1 }, { x0, y, z0 } } } };
                break;
            }
            case FaceNorth:
            case FaceSouth: {
                int z = face == FaceNorth ? z0 : z1;
                edges = { { { { x0, y0, z }, { x1, y0, z } },
                            { { x1, y0, z }, { x1, y1, z } },
                            { { x1, y1, z }, { x0, y1, z } },
                            { { x0, y1, z }, { x0, y0, z } } } };
                break;
            }
            case FaceWest:
            case FaceEast: {
                int x = face == FaceWest ? x0 : x1;
                edges = { { { { x, y0, z0 }, { x, y0, z1 } },
                            { { x, y0, z1 }, { x, y1, z1 } },
                            { { x, y1, z1 }, { x, y1, z0 } },
                            { { x, y1, z0 }, { x, y0, z0 } } } };
                break;
            }
            }

            for (auto& [a, b] : edges) {
                auto ordered = orderedEdge(a, b);
                a = ordered.first;
                b = ordered.second;
            }

            return edges;
        }

        Vec3 toVec3(const IntPoint& p) {
            return { static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z) };
        }

        XRayColor brighten(XRayColor color, int brightness) {
            float factor = std::clamp(brightness, 0, 100) / 100.0f;
            color.r = std::clamp(static_cast<int>(std::lround(color.r * factor)), 0, 255);
            color.g = std::clamp(static_cast<int>(std::lround(color.g * factor)), 0, 255);
            color.b = std::clamp(static_cast<int>(std::lround(color.b * factor)), 0, 255);
            return color;
        }

        void drawFace(MCDrawUtil3D& dc, const BlockPos& p, std::uint8_t face, const d2d::Color& color) {
            float x0 = static_cast<float>(p.x);
            float y0 = static_cast<float>(p.y);
            float z0 = static_cast<float>(p.z);
            float x1 = x0 + 1.0f;
            float y1 = y0 + 1.0f;
            float z1 = z0 + 1.0f;

            switch (face) {
            case FaceDown: {
                float y = y0 + FaceInset;
                dc.fillQuad({ x0, y, z0 }, { x0, y, z1 }, { x1, y, z1 }, { x1, y, z0 }, color);
                break;
            }
            case FaceUp: {
                float y = y1 - FaceInset;
                dc.fillQuad({ x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 }, color);
                break;
            }
            case FaceNorth: {
                float z = z0 + FaceInset;
                dc.fillQuad({ x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z }, color);
                break;
            }
            case FaceSouth: {
                float z = z1 - FaceInset;
                dc.fillQuad({ x0, y0, z }, { x0, y1, z }, { x1, y1, z }, { x1, y0, z }, color);
                break;
            }
            case FaceWest: {
                float x = x0 + FaceInset;
                dc.fillQuad({ x, y0, z0 }, { x, y1, z0 }, { x, y1, z1 }, { x, y0, z1 }, color);
                break;
            }
            case FaceEast: {
                float x = x1 - FaceInset;
                dc.fillQuad({ x, y0, z0 }, { x, y0, z1 }, { x, y1, z1 }, { x, y1, z0 }, color);
                break;
            }
            }
        }

    } // namespace

    XRayCustomESP& XRayCustomESP::instance() {
        static XRayCustomESP esp;
        return esp;
    }

    void XRayCustomESP::initialize() {
        instance();
    }

    XRayCustomESP::XRayCustomESP() {
        targetRevision = XRayTargets::getRevision();
        Eventing::get().listen<TickEvent>(this, (EventListenerFunc)&XRayCustomESP::onTick, 0, true);
        Eventing::get().listen<RenderLevelEvent>(this, (EventListenerFunc)&XRayCustomESP::onRender, 0, true);
    }

    void XRayCustomESP::observeBlock(const BlockPos& pos, SDK::Block* block) {
        instance().observeBlockImpl(pos, block);
    }

    XRayCustomESP::BlockKey XRayCustomESP::makeKey(const BlockPos& pos) {
        return { pos.x, pos.y, pos.z };
    }

    void XRayCustomESP::observeBlockImpl(const BlockPos& pos, SDK::Block* block) {
        if (!block || !block->legacyBlock) return;

        std::string id = block->legacyBlock->namespacedId.getString();
        if (id.empty() || XRayTargets::isBuiltInBlockId(id)) return;

        // Presence in customTargets means the exact block ID was selected in Block List.
        if (XRayTargets::findCustomConst(id) == nullptr) return;

        BlockKey key = makeKey(pos);
        auto found = hitIndex.find(key);

        if (found != hitIndex.end()) {
            Hit& hit = hits[found->second];
            if (hit.blockId != id) {
                hit.blockId = std::move(id);
                geometryDirty = true;
            }
            return;
        }

        std::size_t index = hits.size();
        hits.push_back({ pos, std::move(id) });
        hitIndex.emplace(key, index);
        geometryDirty = true;
    }

    void XRayCustomESP::eraseHit(std::size_t index) {
        if (index >= hits.size()) return;

        hitIndex.erase(makeKey(hits[index].pos));

        std::size_t last = hits.size() - 1;
        if (index != last) {
            hits[index] = std::move(hits[last]);
            hitIndex[makeKey(hits[index].pos)] = index;
        }

        hits.pop_back();
        if (validationCursor > hits.size()) validationCursor = 0;
        geometryDirty = true;
    }

    void XRayCustomESP::syncTargetRevision() {
        std::uint64_t revision = XRayTargets::getRevision();
        if (revision == targetRevision) return;

        targetRevision = revision;

        // Remove cached positions whose exact custom target no longer exists.
        for (std::size_t i = hits.size(); i > 0; --i) {
            const Hit& hit = hits[i - 1];
            if (XRayTargets::findCustomConst(hit.blockId) == nullptr) eraseHit(i - 1);
        }

        geometryDirty = true;
    }

    void XRayCustomESP::pruneAndValidate(SDK::BlockSource* region, const Vec3& playerPos) {
        if (!region) return;

        int range = std::clamp(xRaySettings.oreRange, 16, 128);
        float rangeSq = static_cast<float>(range * range);

        for (std::size_t i = hits.size(); i > 0; --i) {
            const auto& p = hits[i - 1].pos;
            float dx = (static_cast<float>(p.x) + 0.5f) - playerPos.x;
            float dy = (static_cast<float>(p.y) + 0.5f) - playerPos.y;
            float dz = (static_cast<float>(p.z) + 0.5f) - playerPos.z;
            if (dx * dx + dy * dy + dz * dz > rangeSq) eraseHit(i - 1);
        }

        if (hits.empty()) {
            validationCursor = 0;
            return;
        }

        std::size_t budget = std::min(ValidationBudgetPerTick, hits.size());
        for (std::size_t n = 0; n < budget && !hits.empty(); ++n) {
            if (validationCursor >= hits.size()) validationCursor = 0;

            std::size_t index = validationCursor;
            const Hit& hit = hits[index];

            SDK::Block* block = region->getBlock(hit.pos);
            bool valid = block && block->legacyBlock && block->legacyBlock->namespacedId.getString() == hit.blockId &&
                         XRayTargets::findCustomConst(hit.blockId) != nullptr;

            if (!valid) {
                eraseHit(index);
                continue;
            }

            ++validationCursor;
        }
    }

    void XRayCustomESP::rebuildGeometry() {
        faces.clear();
        lines.clear();

        if (hits.empty()) {
            geometryDirty = false;
            return;
        }

        // First generate only exposed faces. A face is internal only when the
        // adjacent cached block is the exact same custom target ID.
        for (const Hit& hit : hits) {
            for (std::uint8_t face = 0; face < 6; ++face) {
                const auto& o = NeighborOffsets[face];
                BlockKey neighborKey { hit.pos.x + o[0], hit.pos.y + o[1], hit.pos.z + o[2] };

                bool sameTargetNeighbor = false;
                auto found = hitIndex.find(neighborKey);
                if (found != hitIndex.end() && found->second < hits.size())
                    sameTargetNeighbor = hits[found->second].blockId == hit.blockId;

                if (!sameTargetNeighbor) faces.push_back({ hit.pos, hit.blockId, face });
            }
        }

        // Boundary-only outline: cancel edges shared by two adjacent cells on the
        // same coplanar surface, then dedupe identical world-space cube edges.
        std::map<SurfaceEdgeKey, int> surfaceEdgeCounts;

        for (const Face& face : faces) {
            int plane = facePlane(face.pos, face.face);
            for (const auto& edge : faceEdges(face.pos, face.face)) {
                SurfaceEdgeKey key;
                key.blockId = face.blockId;
                key.face = face.face;
                key.plane = plane;
                key.a = edge.first;
                key.b = edge.second;
                ++surfaceEdgeCounts[key];
            }
        }

        std::set<FinalEdgeKey> finalEdges;
        for (const auto& [edge, count] : surfaceEdgeCounts) {
            if ((count & 1) == 0) continue;
            finalEdges.insert({ edge.blockId, edge.a, edge.b });
        }

        lines.reserve(finalEdges.size());
        for (const auto& edge : finalEdges)
            lines.push_back({ toVec3(edge.a), toVec3(edge.b), edge.blockId });

        geometryDirty = false;
    }

    void XRayCustomESP::onTick(Event& event) {
        auto& tick = static_cast<TickEvent&>(event);

        if (!tick.getLevel()) {
            hits.clear();
            hitIndex.clear();
            faces.clear();
            lines.clear();
            validationCursor = 0;
            geometryDirty = true;
            return;
        }

        syncTargetRevision();

        auto client = SDK::ClientInstance::get();
        if (!client) return;

        auto player = client->getLocalPlayer();
        auto region = client->getRegion();
        if (!player || !region) return;

        pruneAndValidate(region, player->getPos());

        if (geometryDirty) rebuildGeometry();
    }

    void XRayCustomESP::onRender(Event& event) {
        if (!xRaySettings.enabled || !xRaySettings.oreESP || hits.empty()) return;

        if (!xRaySettings.oreFill && !xRaySettings.oreOutline) return;

        if (geometryDirty) rebuildGeometry();

        auto client = SDK::ClientInstance::get();
        if (!client || !client->levelRenderer) return;

        auto& renderEvent = static_cast<RenderLevelEvent&>(event);
        auto screenContext = renderEvent.getScreenContext();
        if (!screenContext) return;

        MCDrawUtil3D dc { client->levelRenderer, screenContext, SDK::MaterialPtr::getUIColor() };

        int opacity = std::clamp(xRaySettings.oreOpacity, 0, 100);
        float fillAlpha = opacity / 100.0f;
        float outlineAlpha = std::clamp(fillAlpha + 0.20f, 0.0f, 1.0f);

        if (xRaySettings.oreFill) {
            for (const Face& face : faces) {
                const XRayCustomTarget* target = XRayTargets::findCustomConst(face.blockId);
                if (!target || !target->enabled) continue;

                XRayColor c = brighten(target->color, xRaySettings.oreBrightness);
                d2d::Color color = d2d::Color::RGB(c.r, c.g, c.b).asAlpha(fillAlpha);
                drawFace(dc, face.pos, face.face, color);
            }
        }

        if (xRaySettings.oreOutline) {
            for (const Line& line : lines) {
                const XRayCustomTarget* target = XRayTargets::findCustomConst(line.blockId);
                if (!target || !target->enabled) continue;

                XRayColor c = brighten(target->color, xRaySettings.oreBrightness);
                d2d::Color color = d2d::Color::RGB(c.r, c.g, c.b).asAlpha(outlineAlpha);
                dc.drawLine(line.from, line.to, color);
            }
        }

        dc.flush();
    }

} // namespace Nexus
