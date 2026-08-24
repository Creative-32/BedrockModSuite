#include "pch.h"
#include "XRayTargets.h"

#include "client/feature/nexus/NexusConfig.h"

#include <algorithm>
#include <atomic>
#include <cctype>

namespace Nexus {

    namespace {

        std::atomic<std::uint64_t> gRevision { 1 };

        std::size_t colorIndex(XRayBuiltInTarget target) {
            return static_cast<std::size_t>(target);
        }

        XRayColor clampColor(XRayColor color) {
            color.r = std::clamp(color.r, 0, 255);
            color.g = std::clamp(color.g, 0, 255);
            color.b = std::clamp(color.b, 0, 255);

            return color;
        }

    } // namespace

    std::optional<XRayBuiltInTarget> XRayTargets::getBuiltInTarget(std::string_view id) {
        if (id == "minecraft:diamond_ore" || id == "minecraft:deepslate_diamond_ore") {
            return XRayBuiltInTarget::Diamond;
        }

        if (id == "minecraft:emerald_ore" || id == "minecraft:deepslate_emerald_ore") {
            return XRayBuiltInTarget::Emerald;
        }

        if (id == "minecraft:ancient_debris") {
            return XRayBuiltInTarget::AncientDebris;
        }

        if (id == "minecraft:gold_ore" || id == "minecraft:deepslate_gold_ore" || id == "minecraft:nether_gold_ore") {
            return XRayBuiltInTarget::Gold;
        }

        if (id == "minecraft:iron_ore" || id == "minecraft:deepslate_iron_ore") {
            return XRayBuiltInTarget::Iron;
        }

        if (id == "minecraft:copper_ore" || id == "minecraft:deepslate_copper_ore") {
            return XRayBuiltInTarget::Copper;
        }

        if (id == "minecraft:redstone_ore" || id == "minecraft:deepslate_redstone_ore" ||
            id == "minecraft:lit_redstone_ore") {
            return XRayBuiltInTarget::Redstone;
        }

        if (id == "minecraft:lapis_ore" || id == "minecraft:deepslate_lapis_ore") {
            return XRayBuiltInTarget::Lapis;
        }

        if (id == "minecraft:coal_ore" || id == "minecraft:deepslate_coal_ore") {
            return XRayBuiltInTarget::Coal;
        }

        return std::nullopt;
    }

    bool XRayTargets::isBuiltInBlockId(std::string_view blockId) {
        return getBuiltInTarget(blockId).has_value();
    }

    bool* XRayTargets::getBuiltInEnabled(XRayBuiltInTarget target) {
        switch (target) {
        case XRayBuiltInTarget::Diamond:
            return &xRaySettings.diamond;

        case XRayBuiltInTarget::Emerald:
            return &xRaySettings.emerald;

        case XRayBuiltInTarget::AncientDebris:
            return &xRaySettings.ancientDebris;

        case XRayBuiltInTarget::Gold:
            return &xRaySettings.gold;

        case XRayBuiltInTarget::Iron:
            return &xRaySettings.iron;

        case XRayBuiltInTarget::Copper:
            return &xRaySettings.copper;

        case XRayBuiltInTarget::Redstone:
            return &xRaySettings.redstone;

        case XRayBuiltInTarget::Lapis:
            return &xRaySettings.lapis;

        case XRayBuiltInTarget::Coal:
            return &xRaySettings.coal;

        case XRayBuiltInTarget::Count:
        default:
            return nullptr;
        }
    }

    bool* XRayTargets::getBuiltInEnabled(std::string_view blockId) {
        auto target = getBuiltInTarget(blockId);

        if (!target.has_value()) {
            return nullptr;
        }

        return getBuiltInEnabled(*target);
    }

    XRayColor* XRayTargets::getBuiltInColor(XRayBuiltInTarget target) {
        std::size_t index = colorIndex(target);

        if (index >= xRaySettings.oreColors.size()) {
            return nullptr;
        }

        return &xRaySettings.oreColors[index];
    }

    const XRayColor* XRayTargets::getBuiltInColorConst(XRayBuiltInTarget target) {
        std::size_t index = colorIndex(target);

        if (index >= xRaySettings.oreColors.size()) {
            return nullptr;
        }

        return &xRaySettings.oreColors[index];
    }

    XRayCustomTarget* XRayTargets::findCustom(std::string_view blockId) {
        auto found = std::find_if(xRaySettings.customTargets.begin(), xRaySettings.customTargets.end(),
                                  [&](const XRayCustomTarget& target) {
                                      return target.blockId == blockId;
                                  });

        if (found == xRaySettings.customTargets.end()) {
            return nullptr;
        }

        return &(*found);
    }

    const XRayCustomTarget* XRayTargets::findCustomConst(std::string_view blockId) {
        auto found = std::find_if(xRaySettings.customTargets.cbegin(), xRaySettings.customTargets.cend(),
                                  [&](const XRayCustomTarget& target) {
                                      return target.blockId == blockId;
                                  });

        if (found == xRaySettings.customTargets.cend()) {
            return nullptr;
        }

        return &(*found);
    }

    bool XRayTargets::isSelected(std::string_view blockId) {
        if (bool* builtIn = getBuiltInEnabled(blockId)) {
            return *builtIn;
        }

        return findCustomConst(blockId) != nullptr;
    }

    bool XRayTargets::setSelected(std::string_view blockId, bool selected) {
        if (blockId.empty() || blockId.find(':') == std::string_view::npos) {
            return false;
        }

        //
        // ========================================================
        // BUILT-IN TARGET
        // ========================================================
        //

        if (bool* builtIn = getBuiltInEnabled(blockId)) {
            if (*builtIn == selected) {
                return false;
            }

            *builtIn = selected;

            markChanged();

            return true;
        }

        //
        // ========================================================
        // CUSTOM EXACT-ID TARGET
        // ========================================================
        //

        XRayCustomTarget* existing = findCustom(blockId);

        if (selected) {
            if (existing != nullptr) {
                return false;
            }

            XRayCustomTarget target;

            target.blockId.assign(blockId.begin(), blockId.end());

            target.enabled = true;

            target.color = { 255, 255, 255 };

            xRaySettings.customTargets.push_back(std::move(target));

            NexusConfig::ensureXRayTargetOrderEntry(makeOrderKey(blockId));

            markChanged();

            return true;
        }

        if (existing == nullptr) {
            return false;
        }

        std::string id = existing->blockId;

        std::erase_if(xRaySettings.customTargets, [&](const XRayCustomTarget& target) {
            return target.blockId == id;
        });

        NexusConfig::removeXRayTargetOrderEntry(makeOrderKey(id));

        markChanged();

        return true;
    }

    bool XRayTargets::isCustomEnabled(std::string_view blockId) {
        const auto* target = findCustomConst(blockId);

        return target != nullptr && target->enabled;
    }

    bool XRayTargets::setCustomEnabled(std::string_view blockId, bool enabled) {
        auto* target = findCustom(blockId);

        if (target == nullptr || target->enabled == enabled) {
            return false;
        }

        target->enabled = enabled;

        markChanged();

        return true;
    }

    XRayColor XRayTargets::getColorForBlock(std::string_view blockId) {
        if (auto builtIn = getBuiltInTarget(blockId)) {
            if (const auto* color = getBuiltInColorConst(*builtIn)) {
                return *color;
            }
        }

        if (const auto* target = findCustomConst(blockId)) {
            return target->color;
        }

        return { 255, 255, 255 };
    }

    bool XRayTargets::setColorForBlock(std::string_view blockId, XRayColor color) {
        color = clampColor(color);

        //
        // Built-in target.
        //

        if (auto builtIn = getBuiltInTarget(blockId)) {
            auto* current = getBuiltInColor(*builtIn);

            if (current == nullptr) {
                return false;
            }

            if (current->r == color.r && current->g == color.g && current->b == color.b) {
                return false;
            }

            *current = color;

            markChanged();

            return true;
        }

        //
        // Custom target.
        //

        auto* target = findCustom(blockId);

        if (target == nullptr) {
            return false;
        }

        if (target->color.r == color.r && target->color.g == color.g && target->color.b == color.b) {
            return false;
        }

        target->color = color;

        markChanged();

        return true;
    }

    std::string XRayTargets::makeOrderKey(std::string_view blockId) {
        std::string key = "block:";

        key.append(blockId.begin(), blockId.end());

        return key;
    }

    bool XRayTargets::isCustomOrderKey(std::string_view key) {
        return key.starts_with("block:");
    }

    std::string XRayTargets::blockIdFromOrderKey(std::string_view key) {
        if (!isCustomOrderKey(key)) {
            return {};
        }

        return std::string(key.substr(6));
    }

    std::wstring XRayTargets::makeDisplayName(std::string_view blockId) {
        std::string_view path = blockId;

        if (auto colon = blockId.find(':'); colon != std::string_view::npos) {
            path = blockId.substr(colon + 1);
        }

        std::wstring result;

        result.reserve(path.size());

        bool capitalize = true;

        for (unsigned char c : path) {
            if (c == '_' || c == '-' || c == '.') {
                if (!result.empty() && result.back() != L' ') {
                    result.push_back(L' ');
                }

                capitalize = true;

                continue;
            }

            wchar_t wc = static_cast<wchar_t>(c);

            if (capitalize && wc >= L'a' && wc <= L'z') {
                wc = static_cast<wchar_t>(wc - L'a' + L'A');
            }

            result.push_back(wc);

            capitalize = false;
        }

        if (result.empty()) {
            return L"Block";
        }

        return result;
    }

    std::uint64_t XRayTargets::getRevision() {
        return gRevision.load(std::memory_order_relaxed);
    }

    void XRayTargets::markChanged() {
        gRevision.fetch_add(1, std::memory_order_relaxed);
    }

} // namespace Nexus
