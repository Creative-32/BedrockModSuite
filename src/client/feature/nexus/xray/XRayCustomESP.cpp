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
#include <string>
#include <tuple>
#include <utility>

namespace Nexus {

    namespace {

        //
        // ============================================================
        // FACE IDS
        // ============================================================
        //

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

        //
        // ============================================================
        // OUTLINE POINT
        // ============================================================
        //
        // Uses the same small inward offset as the fill.
        //

        Vec3 getOutlinePoint(std::uint8_t face, int plane, int u, int v) {
            float p = static_cast<float>(plane);

            float uf = static_cast<float>(u);
            float vf = static_cast<float>(v);

            //
            // Down / Up
            //
            // U = X
            // V = Z
            //

            if (face == FaceDown || face == FaceUp) {
                float y = face == FaceDown ? p + FaceInset : p - FaceInset;

                return { uf, y, vf };
            }

            //
            // North / South
            //
            // U = X
            // V = Y
            //

            if (face == FaceNorth || face == FaceSouth) {
                float z = face == FaceNorth ? p + FaceInset : p - FaceInset;

                return { uf, vf, z };
            }

            //
            // West / East
            //
            // U = Z
            // V = Y
            //

            float x = face == FaceWest ? p + FaceInset : p - FaceInset;

            return { x, vf, uf };
        }

        //
        // ============================================================
        // GREEDY-MERGED FACE DRAW
        // ============================================================
        //

        void drawMeshQuad(MCDrawUtil3D& dc, std::uint8_t face, int plane, int u, int v, int width, int height,
                          const d2d::Color& color) {
            float p = static_cast<float>(plane);

            float u0 = static_cast<float>(u);
            float v0 = static_cast<float>(v);

            float u1 = static_cast<float>(u + width);
            float v1 = static_cast<float>(v + height);

            //
            // Down / Up
            //
            // U = X
            // V = Z
            //

            if (face == FaceDown || face == FaceUp) {
                float y = face == FaceDown ? p + FaceInset : p - FaceInset;

                dc.fillQuad({ u0, y, v0 }, { u1, y, v0 }, { u1, y, v1 }, { u0, y, v1 }, color);

                return;
            }

            //
            // North / South
            //
            // U = X
            // V = Y
            //

            if (face == FaceNorth || face == FaceSouth) {
                float z = face == FaceNorth ? p + FaceInset : p - FaceInset;

                dc.fillQuad({ u0, v0, z }, { u0, v1, z }, { u1, v1, z }, { u1, v0, z }, color);

                return;
            }

            //
            // West / East
            //
            // U = Z
            // V = Y
            //

            if (face == FaceWest || face == FaceEast) {
                float x = face == FaceWest ? p + FaceInset : p - FaceInset;

                dc.fillQuad({ x, v0, u0 }, { x, v1, u0 }, { x, v1, u1 }, { x, v0, u1 }, color);
            }
        }

        //
        // ============================================================
        // BRIGHTNESS
        // ============================================================
        //
        // Matches the built-in Ore ESP range.
        //

        XRayColor applyBrightness(XRayColor color, int brightness) {
            float factor = std::clamp(static_cast<float>(brightness) / 100.0f, 0.10f, 1.50f);

            color.r = std::clamp(static_cast<int>(std::lround(static_cast<float>(color.r) * factor)), 0, 255);

            color.g = std::clamp(static_cast<int>(std::lround(static_cast<float>(color.g) * factor)), 0, 255);

            color.b = std::clamp(static_cast<int>(std::lround(static_cast<float>(color.b) * factor)), 0, 255);

            return color;
        }

        //
        // ============================================================
        // DISTANCE SHADING
        // ============================================================
        //
        // Near custom blocks keep their full target color.
        //
        // As they approach the edge of Ore Range they gradually darken.
        // This changes RGB rather than only opacity, so there is an
        // actual visible distance-shading variation.
        //

        float getDistanceShade(const Vec3& point, const Vec3& playerPos, int range) {
            float dx = point.x - playerPos.x;

            float dy = point.y - playerPos.y;

            float dz = point.z - playerPos.z;

            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            float rangeF = static_cast<float>(std::max(range, 1));

            //
            // Keep nearby targets at full brightness.
            //
            // Start fading after roughly 20% of the selected range.
            //

            float fadeStart = std::max(4.0f, rangeF * 0.20f);

            if (distance <= fadeStart) {
                return 1.0f;
            }

            float denominator = std::max(rangeF - fadeStart, 1.0f);

            float t = std::clamp((distance - fadeStart) / denominator, 0.0f, 1.0f);

            //
            // Smoothstep prevents visible brightness bands.
            //

            t = t * t * (3.0f - 2.0f * t);

            //
            // Near = 1.00
            // Far  = 0.45
            //

            return 1.0f - 0.55f * t;
        }

        XRayColor applyDistanceShade(XRayColor color, float shade) {
            shade = std::clamp(shade, 0.0f, 1.0f);

            color.r = std::clamp(static_cast<int>(std::lround(static_cast<float>(color.r) * shade)), 0, 255);

            color.g = std::clamp(static_cast<int>(std::lround(static_cast<float>(color.g) * shade)), 0, 255);

            color.b = std::clamp(static_cast<int>(std::lround(static_cast<float>(color.b) * shade)), 0, 255);

            return color;
        }

        Vec3 getMeshQuadCenter(std::uint8_t face, int plane, int u, int v, int width, int height) {
            float halfWidth = static_cast<float>(width) * 0.5f;

            float halfHeight = static_cast<float>(height) * 0.5f;

            //
            // Down / Up:
            // U = X
            // V = Z
            //

            if (face == FaceDown || face == FaceUp) {
                return { static_cast<float>(u) + halfWidth,

                         static_cast<float>(plane),

                         static_cast<float>(v) + halfHeight };
            }

            //
            // North / South:
            // U = X
            // V = Y
            //

            if (face == FaceNorth || face == FaceSouth) {
                return { static_cast<float>(u) + halfWidth,

                         static_cast<float>(v) + halfHeight,

                         static_cast<float>(plane) };
            }

            //
            // West / East:
            // U = Z
            // V = Y
            //

            return { static_cast<float>(plane),

                     static_cast<float>(v) + halfHeight,

                     static_cast<float>(u) + halfWidth };
        }

    } // namespace

    //
    // ================================================================
    // INSTANCE
    // ================================================================
    //

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

    //
    // ================================================================
    // KEY
    // ================================================================
    //

    XRayCustomESP::BlockKey XRayCustomESP::makeKey(const BlockPos& pos) {
        return { pos.x, pos.y, pos.z };
    }

    //
    // ================================================================
    // OBSERVE BLOCK
    // ================================================================
    //

    void XRayCustomESP::observeBlock(const BlockPos& pos, SDK::Block* block) {
        instance().observeBlockImpl(pos, block);
    }

    void XRayCustomESP::observeBlockImpl(const BlockPos& pos, SDK::Block* block) {
        BlockKey key = makeKey(pos);

        auto existing = hitIndex.find(key);

        //
        // If this position used to contain a selected custom block
        // but now contains nothing valid, remove it immediately.
        //

        if (!block || !block->legacyBlock) {
            if (existing != hitIndex.end()) {
                eraseHit(existing->second);
            }

            return;
        }

        std::string id = block->legacyBlock->namespacedId.getString();

        //
        // Built-in ores are handled by the normal Ore ESP.
        //
        // Never allow them into the custom renderer or they would
        // be drawn twice.
        //

        bool validCustom =
            !id.empty() && !XRayTargets::isBuiltInBlockId(id) && XRayTargets::findCustomConst(id) != nullptr;

        if (!validCustom) {
            if (existing != hitIndex.end()) {
                eraseHit(existing->second);
            }

            return;
        }

        //
        // Existing cached position.
        //

        if (existing != hitIndex.end()) {
            Hit& hit = hits[existing->second];

            if (hit.blockId != id) {
                hit.blockId = std::move(id);

                geometryDirty = true;
            }

            return;
        }

        //
        // New custom block position.
        //

        std::size_t index = hits.size();

        hits.push_back({ pos, std::move(id) });

        hitIndex.emplace(key, index);

        geometryDirty = true;
    }

    //
    // ================================================================
    // ERASE
    // ================================================================
    //

    void XRayCustomESP::eraseHit(std::size_t index) {
        if (index >= hits.size()) {
            return;
        }

        hitIndex.erase(makeKey(hits[index].pos));

        std::size_t last = hits.size() - 1;

        if (index != last) {
            hits[index] = std::move(hits[last]);

            hitIndex[makeKey(hits[index].pos)] = index;
        }

        hits.pop_back();

        if (validationCursor >= hits.size()) {
            validationCursor = 0;
        }

        geometryDirty = true;
    }

    //
    // ================================================================
    // TARGET REVISION
    // ================================================================
    //

    void XRayCustomESP::syncTargetRevision() {
        std::uint64_t revision = XRayTargets::getRevision();

        if (revision == targetRevision) {
            return;
        }

        targetRevision = revision;

        //
        // Remove targets that were actually deleted.
        //
        // Disabled targets stay cached so enabling them again does
        // not require rediscovering every position first.
        //

        for (std::size_t i = hits.size(); i > 0; --i) {
            const Hit& hit = hits[i - 1];

            if (XRayTargets::findCustomConst(hit.blockId) == nullptr) {
                eraseHit(i - 1);
            }
        }
    }

    //
    // ================================================================
    // PRUNE + VALIDATE
    // ================================================================
    //

    void XRayCustomESP::pruneAndValidate(SDK::BlockSource* region, const Vec3& playerPos) {
        if (!region) {
            return;
        }

        int range = std::clamp(xRaySettings.oreRange, 16, 128);

        float rangeSq = static_cast<float>(range) * static_cast<float>(range);

        //
        // Remove positions outside the active Ore range.
        //

        for (std::size_t i = hits.size(); i > 0; --i) {
            const BlockPos& p = hits[i - 1].pos;

            float dx = (static_cast<float>(p.x) + 0.5f) - playerPos.x;

            float dy = (static_cast<float>(p.y) + 0.5f) - playerPos.y;

            float dz = (static_cast<float>(p.z) + 0.5f) - playerPos.z;

            float distanceSq = dx * dx + dy * dy + dz * dz;

            if (distanceSq > rangeSq) {
                eraseHit(i - 1);
            }
        }

        if (hits.empty()) {
            validationCursor = 0;

            return;
        }

        //
        // Incrementally verify cached blocks.
        //

        std::size_t budget = std::min(ValidationBudgetPerTick, hits.size());

        for (std::size_t n = 0; n < budget && !hits.empty(); ++n) {
            if (validationCursor >= hits.size()) {
                validationCursor = 0;
            }

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

    //
    // ================================================================
    // REBUILD MERGED GEOMETRY
    // ================================================================
    //

    void XRayCustomESP::rebuildGeometry() {
        mesh.clear();
        outlineLines.clear();

        if (hits.empty()) {
            geometryDirty = false;

            return;
        }

        using Cell = std::pair<int, int>;

        //
        // Exact block ID participates in the key.
        //
        // Therefore:
        //
        // minecraft:clay next to minecraft:stone
        //
        // remains two separate ESP objects.
        //

        using PlaneKey = std::tuple<std::string, std::uint8_t, int>;

        std::map<PlaneKey, std::set<Cell>> planes;

        //
        // ============================================================
        // EXPOSED FACES
        // ============================================================
        //

        for (const Hit& hit : hits) {
            for (std::uint8_t face = 0; face < 6; ++face) {
                const auto& offset = NeighborOffsets[face];

                BlockKey neighborKey { hit.pos.x + offset[0], hit.pos.y + offset[1], hit.pos.z + offset[2] };

                bool sameTargetNeighbor = false;

                auto found = hitIndex.find(neighborKey);

                if (found != hitIndex.end() && found->second < hits.size()) {
                    sameTargetNeighbor = hits[found->second].blockId == hit.blockId;
                }

                //
                // Cull internal faces between identical custom blocks.
                //

                if (sameTargetNeighbor) {
                    continue;
                }

                int plane = 0;
                int u = 0;
                int v = 0;

                switch (face) {
                case FaceDown:
                    plane = hit.pos.y;
                    u = hit.pos.x;
                    v = hit.pos.z;
                    break;

                case FaceUp:
                    plane = hit.pos.y + 1;
                    u = hit.pos.x;
                    v = hit.pos.z;
                    break;

                case FaceNorth:
                    plane = hit.pos.z;
                    u = hit.pos.x;
                    v = hit.pos.y;
                    break;

                case FaceSouth:
                    plane = hit.pos.z + 1;
                    u = hit.pos.x;
                    v = hit.pos.y;
                    break;

                case FaceWest:
                    plane = hit.pos.x;
                    u = hit.pos.z;
                    v = hit.pos.y;
                    break;

                case FaceEast:
                    plane = hit.pos.x + 1;
                    u = hit.pos.z;
                    v = hit.pos.y;
                    break;

                default:
                    continue;
                }

                planes[{ hit.blockId, face, plane }].insert({ u, v });
            }
        }

        //
        // ============================================================
        // GREEDY MERGED FILL QUADS
        // ============================================================
        //

        for (const auto& [planeKey, sourceCells] : planes) {
            auto [blockId, face, plane] = planeKey;

            std::set<Cell> cells = sourceCells;

            while (!cells.empty()) {
                Cell start = *cells.begin();

                int startU = start.first;

                int startV = start.second;

                //
                // Grow width.
                //

                int width = 1;

                while (cells.contains({ startU + width, startV })) {
                    ++width;
                }

                //
                // Grow height.
                //

                int height = 1;

                while (true) {
                    bool completeRow = true;

                    for (int offsetU = 0; offsetU < width; ++offsetU) {
                        if (!cells.contains({ startU + offsetU, startV + height })) {
                            completeRow = false;

                            break;
                        }
                    }

                    if (!completeRow) {
                        break;
                    }

                    ++height;
                }

                //
                // Remove merged cells.
                //

                for (int offsetV = 0; offsetV < height; ++offsetV) {
                    for (int offsetU = 0; offsetU < width; ++offsetU) {
                        cells.erase({ startU + offsetU, startV + offsetV });
                    }
                }

                mesh.push_back({ blockId, face, plane, startU, startV, width, height });
            }
        }

        //
        // ============================================================
        // BOUNDARY-ONLY OUTLINES
        // ============================================================
        //

        for (const auto& [planeKey, cells] : planes) {
            auto [blockId, face, plane] = planeKey;

            std::map<int, std::set<int>> horizontalEdges;

            std::map<int, std::set<int>> verticalEdges;

            //
            // Extract the true perimeter.
            //

            for (const Cell& cell : cells) {
                int u = cell.first;

                int v = cell.second;

                //
                // Bottom.
                //

                if (!cells.contains({ u, v - 1 })) {
                    horizontalEdges[v].insert(u);
                }

                //
                // Top.
                //

                if (!cells.contains({ u, v + 1 })) {
                    horizontalEdges[v + 1].insert(u);
                }

                //
                // Left.
                //

                if (!cells.contains({ u - 1, v })) {
                    verticalEdges[u].insert(v);
                }

                //
                // Right.
                //

                if (!cells.contains({ u + 1, v })) {
                    verticalEdges[u + 1].insert(v);
                }
            }

            //
            // Merge horizontal perimeter segments.
            //

            for (auto& [fixedV, starts] : horizontalEdges) {
                while (!starts.empty()) {
                    int startU = *starts.begin();

                    int length = 1;

                    while (starts.contains(startU + length)) {
                        ++length;
                    }

                    for (int offset = 0; offset < length; ++offset) {
                        starts.erase(startU + offset);
                    }

                    Vec3 from = getOutlinePoint(face, plane, startU, fixedV);

                    Vec3 to = getOutlinePoint(face, plane, startU + length, fixedV);

                    outlineLines.push_back({ from, to, blockId });
                }
            }

            //
            // Merge vertical perimeter segments.
            //

            for (auto& [fixedU, starts] : verticalEdges) {
                while (!starts.empty()) {
                    int startV = *starts.begin();

                    int length = 1;

                    while (starts.contains(startV + length)) {
                        ++length;
                    }

                    for (int offset = 0; offset < length; ++offset) {
                        starts.erase(startV + offset);
                    }

                    Vec3 from = getOutlinePoint(face, plane, fixedU, startV);

                    Vec3 to = getOutlinePoint(face, plane, fixedU, startV + length);

                    outlineLines.push_back({ from, to, blockId });
                }
            }
        }

        geometryDirty = false;
    }

    //
    // ================================================================
    // TICK
    // ================================================================
    //

    void XRayCustomESP::onTick(Event& event) {
        auto& tick = static_cast<TickEvent&>(event);

        //
        // No world.
        //

        if (!tick.getLevel()) {
            hits.clear();
            hitIndex.clear();

            mesh.clear();
            outlineLines.clear();

            validationCursor = 0;

            geometryDirty = true;

            return;
        }

        //
        // Master X-Ray or Ore ESP disabled.
        //
        // Drop the position cache and allow the scanner to repopulate it
        // when the feature is enabled again.
        //

        if (!xRaySettings.enabled || !xRaySettings.oreESP) {
            hits.clear();
            hitIndex.clear();

            mesh.clear();
            outlineLines.clear();

            validationCursor = 0;

            geometryDirty = true;

            return;
        }

        syncTargetRevision();

        auto client = SDK::ClientInstance::get();

        if (!client) {
            return;
        }

        auto player = client->getLocalPlayer();

        auto region = client->getRegion();

        if (!player || !region) {
            return;
        }

        pruneAndValidate(region, player->getPos());

        if (geometryDirty) {
            rebuildGeometry();
        }
    }

    //
    // ================================================================
    // RENDER
    // ================================================================
    //

    void XRayCustomESP::onRender(Event& event) {
        if (!xRaySettings.enabled || !xRaySettings.oreESP || hits.empty()) {
            return;
        }

        if (!xRaySettings.oreFill && !xRaySettings.oreOutline) {
            return;
        }

        if (geometryDirty) {
            rebuildGeometry();
        }

        auto client = SDK::ClientInstance::get();

        if (!client || !client->levelRenderer) {
            return;
        }

        auto player = client->getLocalPlayer();

        if (!player) {
            return;
        }

        Vec3 playerPos = player->getPos();

        int distanceShadeRange = std::clamp(xRaySettings.oreRange, 16, 128);

        auto& renderEvent = static_cast<RenderLevelEvent&>(event);

        auto screenContext = renderEvent.getScreenContext();

        if (!screenContext) {
            return;
        }

        MCDrawUtil3D dc { client->levelRenderer, screenContext, SDK::MaterialPtr::getUIColor() };

        //
        // Match built-in Ore ESP opacity behavior.
        //

        float requestedOpacity = std::clamp(static_cast<float>(xRaySettings.oreOpacity) / 100.0f, 0.05f, 1.0f);

        float fillAlpha = std::clamp(requestedOpacity * 0.35f, 0.02f, 0.35f);

        float outlineAlpha = std::clamp(requestedOpacity, 0.05f, 1.0f);

        //
        // ============================================================
        // FILL
        // ============================================================
        //

        if (xRaySettings.oreFill) {
            for (const MeshQuad& quad : mesh) {
                const XRayCustomTarget* target = XRayTargets::findCustomConst(quad.blockId);

                if (!target || !target->enabled) {
                    continue;
                }

                XRayColor base = applyBrightness(target->color, xRaySettings.oreBrightness);

                Vec3 quadCenter = getMeshQuadCenter(quad.face, quad.plane, quad.u, quad.v, quad.width, quad.height);

                float distanceShade = getDistanceShade(quadCenter, playerPos, distanceShadeRange);

                base = applyDistanceShade(base, distanceShade);

                //
                // Let distance affect opacity slightly too,
                // but don't make distant targets disappear.
                //

                float distanceFillAlpha = fillAlpha * (0.70f + 0.30f * distanceShade);

                d2d::Color color = d2d::Color::RGB(base.r, base.g, base.b).asAlpha(distanceFillAlpha);

                drawMeshQuad(dc, quad.face, quad.plane, quad.u, quad.v, quad.width, quad.height, color);
            }

            dc.flush();
        }

        //
        // ============================================================
        // OUTLINE
        // ============================================================
        //

        if (xRaySettings.oreOutline) {
            for (const OutlineLine& line : outlineLines) {
                const XRayCustomTarget* target = XRayTargets::findCustomConst(line.blockId);

                if (!target || !target->enabled) {
                    continue;
                }

                XRayColor base = applyBrightness(target->color, xRaySettings.oreBrightness);

                Vec3 lineCenter { (line.from.x + line.to.x) * 0.5f,

                                  (line.from.y + line.to.y) * 0.5f,

                                  (line.from.z + line.to.z) * 0.5f };

                float distanceShade = getDistanceShade(lineCenter, playerPos, distanceShadeRange);

                base = applyDistanceShade(base, distanceShade);

                //
                // Keep outlines easier to see than fills at long range.
                //

                float distanceOutlineAlpha = outlineAlpha * (0.82f + 0.18f * distanceShade);

                d2d::Color color = d2d::Color::RGB(base.r, base.g, base.b).asAlpha(distanceOutlineAlpha);

                dc.drawLine(line.from, line.to, color);
            }

            dc.flush();
        }
    }

} // namespace Nexus
