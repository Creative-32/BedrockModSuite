#include "pch.h"
#include "NexusConfig.h"

#include "xray/XRaySettings.h"

#include "module/NexusModuleRegistry.h"
#include "xray/XRayTargets.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Nexus {

    namespace {

        std::vector<std::string> defaultXRayTargetOrder() {
            return { "diamond", "emerald", "ancient_debris", "gold", "iron", "copper", "redstone", "lapis", "coal" };
        }

        const char* builtInTargetKey(XRayBuiltInTarget target) {
            switch (target) {
            case XRayBuiltInTarget::Diamond:
                return "diamond";

            case XRayBuiltInTarget::Emerald:
                return "emerald";

            case XRayBuiltInTarget::AncientDebris:
                return "ancient_debris";

            case XRayBuiltInTarget::Gold:
                return "gold";

            case XRayBuiltInTarget::Iron:
                return "iron";

            case XRayBuiltInTarget::Copper:
                return "copper";

            case XRayBuiltInTarget::Redstone:
                return "redstone";

            case XRayBuiltInTarget::Lapis:
                return "lapis";

            case XRayBuiltInTarget::Coal:
                return "coal";

            case XRayBuiltInTarget::Count:
            default:
                return "";
            }
        }

    } // namespace

    std::filesystem::path NexusConfig::getConfigPath() {
        const char* localAppData = std::getenv("LOCALAPPDATA");

        if (localAppData == nullptr) {
            return std::filesystem::current_path() / "Nexus" / "config.json";
        }

        return std::filesystem::path(localAppData) / "Nexus" / "config.json";
    }

    void NexusConfig::load() {
        if (loaded) {
            return;
        }

        loaded = true;

        xRayTargetOrder = defaultXRayTargetOrder();

        const auto path = getConfigPath();

        std::error_code ec;

        std::filesystem::create_directories(path.parent_path(), ec);

        std::ifstream file(path);

        if (!file.is_open()) {
            save();
            return;
        }

        try {
            nlohmann::json json;

            file >> json;

            //
            // ====================================================
            // GENERAL
            // ====================================================
            //

            menuKey = json.value("menuKey", static_cast<int>('N'));

            std::string viewModeValue = json.value("viewMode", std::string("list"));

            if (viewModeValue == "medium") {
                viewMode = NexusViewMode::Medium;
            }

            else if (viewModeValue == "compact") {
                viewMode = NexusViewMode::Compact;
            }

            else {
                viewMode = NexusViewMode::List;
            }

            blockListColumns = std::clamp(json.value("blockListColumns", 1), 1, 3);

            //
            // ====================================================
            // FAVORITES
            // ====================================================
            //

            favoriteOrder.clear();

            if (json.contains("favoriteOrder") && json["favoriteOrder"].is_array()) {
                for (const auto& entry : json["favoriteOrder"]) {
                    if (entry.is_string()) {
                        favoriteOrder.push_back(entry.get<std::string>());
                    }
                }
            }

            sanitizeFavoriteOrder();

            //
            // ====================================================
            // X-RAY
            // ====================================================
            //

            if (json.contains("xray") && json["xray"].is_object()) {
                const auto& xray = json["xray"];

                //
                // TARGET ORDER
                //

                if (xray.contains("targetOrder") && xray["targetOrder"].is_array()) {
                    xRayTargetOrder.clear();

                    for (const auto& entry : xray["targetOrder"]) {
                        if (entry.is_string()) {
                            xRayTargetOrder.push_back(entry.get<std::string>());
                        }
                    }
                }

                sanitizeXRayTargetOrder();

                //
                // MASTER
                //

                xRaySettings.enabled = xray.value("enabled", xRaySettings.enabled);

                //
                // ORE
                //

                xRaySettings.oreESP = xray.value("oreESP", xRaySettings.oreESP);

                xRaySettings.diamond = xray.value("diamond", xRaySettings.diamond);

                xRaySettings.ancientDebris = xray.value("ancientDebris", xRaySettings.ancientDebris);

                xRaySettings.emerald = xray.value("emerald", xRaySettings.emerald);

                xRaySettings.gold = xray.value("gold", xRaySettings.gold);

                xRaySettings.iron = xray.value("iron", xRaySettings.iron);

                xRaySettings.copper = xray.value("copper", xRaySettings.copper);

                xRaySettings.coal = xray.value("coal", xRaySettings.coal);

                xRaySettings.lapis = xray.value("lapis", xRaySettings.lapis);

                xRaySettings.redstone = xray.value("redstone", xRaySettings.redstone);

                //
                // ORE APPEARANCE
                //

                xRaySettings.oreRange = xray.value("oreRange", xRaySettings.oreRange);

                xRaySettings.oreOpacity = xray.value("oreOpacity", xRaySettings.oreOpacity);

                xRaySettings.oreBrightness = xray.value("oreBrightness", xRaySettings.oreBrightness);

                bool legacyOutline = xray.value("outline", true);

                bool legacyFill = xray.value("fill", true);

                xRaySettings.oreOutline = xray.value("oreOutline", legacyOutline);

                xRaySettings.oreFill = xray.value("oreFill", legacyFill);

                //
                // INDIVIDUAL ORE COLORS
                //

                if (xray.contains("oreColors") && xray["oreColors"].is_object()) {
                    const auto& colors = xray["oreColors"];

                    for (std::size_t index = 0; index < XRayBuiltInTargetCount; ++index) {
                        auto target = static_cast<XRayBuiltInTarget>(index);

                        const char* key = builtInTargetKey(target);

                        if (key[0] == '\0' || !colors.contains(key) || !colors[key].is_object()) {
                            continue;
                        }

                        auto& color = xRaySettings.oreColors[index];

                        const auto& colorJson = colors[key];

                        color.r = colorJson.value("r", color.r);

                        color.g = colorJson.value("g", color.g);

                        color.b = colorJson.value("b", color.b);
                    }
                }

                //
                // ====================================================
                // CUSTOM TARGETS
                // ====================================================
                //

                xRaySettings.customTargets.clear();

                    if (xray.contains("customTargets") && xray["customTargets"].is_array()) {
                        for (const auto& item : xray["customTargets"]) {
                            if (!item.is_object()) {
                                continue;
                            }

                            std::string blockId = item.value("id", std::string {});

                            if (blockId.empty() || blockId.find(':') == std::string::npos) {
                                continue;
                            }

                            //
                            // Built-in ore IDs continue to use the grouped
                            // Diamond/Emerald/etc. settings.
                            //
                            if (XRayTargets::isBuiltInBlockId(blockId)) {
                                continue;
                            }

                            bool duplicate =
                                std::any_of(xRaySettings.customTargets.begin(), xRaySettings.customTargets.end(),
                                            [&](const XRayCustomTarget& target) {
                                                return target.blockId == blockId;
                                            });

                            if (duplicate) {
                                continue;
                            }

                            XRayCustomTarget target;

                            target.blockId = blockId;
                            target.enabled = item.value("enabled", true);

                            if (item.contains("color") && item["color"].is_object()) {
                                const auto& color = item["color"];

                                target.color.r = std::clamp(color.value("r", target.color.r), 0, 255);

                                target.color.g = std::clamp(color.value("g", target.color.g), 0, 255);

                                target.color.b = std::clamp(color.value("b", target.color.b), 0, 255);
                            }

                            xRaySettings.customTargets.push_back(std::move(target));
                        }
                    }

                    //
                    // Make sure every custom target exists in the visual order.
                    //

                    for (const auto& target : xRaySettings.customTargets) {
                        std::string key = XRayTargets::makeOrderKey(target.blockId);

                        if (std::find(xRayTargetOrder.begin(), xRayTargetOrder.end(), key) == xRayTargetOrder.end()) {
                            xRayTargetOrder.push_back(std::move(key));
                        }
                    }

                    XRayTargets::markChanged();

                    sanitizeXRayTargetOrder();

                //
                // CAVE
                //

                xRaySettings.caveESP = xray.value("caveESP", xRaySettings.caveESP);

                xRaySettings.airCheck3x3x3 = xray.value("airCheck3x3x3", xRaySettings.airCheck3x3x3);

                xRaySettings.ignoreSurface = xray.value("ignoreSurface", xRaySettings.ignoreSurface);

                xRaySettings.scanRange = xray.value("scanRange", xRaySettings.scanRange);

                xRaySettings.caveOpacity = xray.value("caveOpacity", xRaySettings.caveOpacity);

                xRaySettings.caveBrightness = xray.value("caveBrightness", xRaySettings.caveBrightness);

                xRaySettings.caveOutlineOpacity = xray.value("caveOutlineOpacity", xRaySettings.caveOutlineOpacity);

                xRaySettings.caveColorR = xray.value("caveColorR", xRaySettings.caveColorR);

                xRaySettings.caveColorG = xray.value("caveColorG", xRaySettings.caveColorG);

                xRaySettings.caveColorB = xray.value("caveColorB", xRaySettings.caveColorB);

                xRaySettings.caveOutline = xray.value("caveOutline", legacyOutline);

                xRaySettings.caveFill = xray.value("caveFill", legacyFill);

                //
                // CLAMP
                //

                xRaySettings.oreRange = std::clamp(xRaySettings.oreRange, 16, 128);

                xRaySettings.oreOpacity = std::clamp(xRaySettings.oreOpacity, 5, 100);

                xRaySettings.oreBrightness = std::clamp(xRaySettings.oreBrightness, 10, 150);

                for (auto& color : xRaySettings.oreColors) {
                    color.r = std::clamp(color.r, 0, 255);

                    color.g = std::clamp(color.g, 0, 255);

                    color.b = std::clamp(color.b, 0, 255);
                }

                xRaySettings.scanRange = std::clamp(xRaySettings.scanRange, 16, 128);

                xRaySettings.caveOpacity = std::clamp(xRaySettings.caveOpacity, 5, 100);

                xRaySettings.caveBrightness = std::clamp(xRaySettings.caveBrightness, 10, 150);

                xRaySettings.caveOutlineOpacity = std::clamp(xRaySettings.caveOutlineOpacity, 5, 100);

                xRaySettings.caveColorR = std::clamp(xRaySettings.caveColorR, 0, 255);

                xRaySettings.caveColorG = std::clamp(xRaySettings.caveColorG, 0, 255);

                xRaySettings.caveColorB = std::clamp(xRaySettings.caveColorB, 0, 255);
            }

                // Ensure every persisted custom target has a stable target-order key.
                for (const auto& target : xRaySettings.customTargets) {
                    const std::string key = XRayTargets::makeOrderKey(target.blockId);

                    if (std::find(xRayTargetOrder.begin(), xRayTargetOrder.end(), key) == xRayTargetOrder.end()) {
                        xRayTargetOrder.push_back(key);
                    }
                }

                XRayTargets::markChanged();

                sanitizeXRayTargetOrder();
            }

            catch (...) {
            menuKey = 'N';

            viewMode = NexusViewMode::List;

            blockListColumns = 1;

            favoriteOrder.clear();

            xRayTargetOrder = defaultXRayTargetOrder();

            xRaySettings = XRaySettings {};

            XRayTargets::markChanged();

            save();
        }
    }

    void NexusConfig::save() {
        loaded = true;

        const auto path = getConfigPath();

        std::error_code ec;

        std::filesystem::create_directories(path.parent_path(), ec);

        nlohmann::json json;

        json["version"] = 5;

        json["menuKey"] = menuKey;

        json["blockListColumns"] = std::clamp(blockListColumns, 1, 3);

        switch (viewMode) {
        case NexusViewMode::Medium:
            json["viewMode"] = "medium";
            break;

        case NexusViewMode::Compact:
            json["viewMode"] = "compact";
            break;

        case NexusViewMode::List:
        default:
            json["viewMode"] = "list";
            break;
        }

        sanitizeFavoriteOrder();

        sanitizeXRayTargetOrder();

        json["favoriteOrder"] = favoriteOrder;

        json["xray"] = { { "enabled", xRaySettings.enabled },

                         { "oreESP", xRaySettings.oreESP },

                         { "diamond", xRaySettings.diamond },

                         { "ancientDebris", xRaySettings.ancientDebris },

                         { "emerald", xRaySettings.emerald },

                         { "gold", xRaySettings.gold },

                         { "iron", xRaySettings.iron },

                         { "copper", xRaySettings.copper },

                         { "coal", xRaySettings.coal },

                         { "lapis", xRaySettings.lapis },

                         { "redstone", xRaySettings.redstone },

                         { "oreRange", xRaySettings.oreRange },

                         { "oreOpacity", xRaySettings.oreOpacity },

                         { "oreBrightness", xRaySettings.oreBrightness },

                         { "oreOutline", xRaySettings.oreOutline },

                         { "oreFill", xRaySettings.oreFill },

                         { "targetOrder", xRayTargetOrder },

                         { "caveESP", xRaySettings.caveESP },

                         { "airCheck3x3x3", xRaySettings.airCheck3x3x3 },

                         { "ignoreSurface", xRaySettings.ignoreSurface },

                         { "scanRange", xRaySettings.scanRange },

                         { "caveOpacity", xRaySettings.caveOpacity },

                         { "caveBrightness", xRaySettings.caveBrightness },

                         { "caveOutlineOpacity", xRaySettings.caveOutlineOpacity },

                         { "caveColorR", xRaySettings.caveColorR },

                         { "caveColorG", xRaySettings.caveColorG },

                         { "caveColorB", xRaySettings.caveColorB },

                         { "caveOutline", xRaySettings.caveOutline },

                         { "caveFill", xRaySettings.caveFill } };

        json["xray"]["customTargets"] = nlohmann::json::array();

        for (const auto& target : xRaySettings.customTargets) {
            if (target.blockId.empty()) {
                continue;
            }

            json["xray"]["customTargets"].push_back({ { "id", target.blockId },
                                                      { "enabled", target.enabled },
                                                      { "color",
                                                        { { "r", std::clamp(target.color.r, 0, 255) },
                                                          { "g", std::clamp(target.color.g, 0, 255) },
                                                          { "b", std::clamp(target.color.b, 0, 255) } } } });
        }

        //
        // Individual target colors.
        //

        auto& colorJson = json["xray"]["oreColors"];

        for (std::size_t index = 0; index < XRayBuiltInTargetCount; ++index) {
            auto target = static_cast<XRayBuiltInTarget>(index);

            const char* key = builtInTargetKey(target);

            if (key[0] == '\0') {
                continue;
            }

            const auto& color = xRaySettings.oreColors[index];

            colorJson[key] = { { "r", color.r },

                               { "g", color.g },

                               { "b", color.b } };
        }

        std::ofstream file(path);

        if (!file.is_open()) {
            return;
        }

        file << json.dump(4);
    }

    //
    // ================================================================
    // FAVORITES
    // ================================================================
    //

    bool NexusConfig::isFavorite(const std::string& moduleId) {
        load();

        return std::find(favoriteOrder.begin(), favoriteOrder.end(), moduleId) != favoriteOrder.end();
    }

    void NexusConfig::setFavorite(const std::string& moduleId, bool favorite) {
        load();

        const auto* module = NexusModuleRegistry::find(moduleId);

        if (module == nullptr || !module->canFavorite) {
            return;
        }

        auto it = std::find(favoriteOrder.begin(), favoriteOrder.end(), moduleId);

        if (favorite) {
            if (it == favoriteOrder.end()) {
                favoriteOrder.push_back(moduleId);
            }
        }

        else if (it != favoriteOrder.end()) {
            favoriteOrder.erase(it);
        }

        save();
    }

    void NexusConfig::moveFavorite(const std::string& moduleId, std::size_t newIndex) {
        load();

        auto it = std::find(favoriteOrder.begin(), favoriteOrder.end(), moduleId);

        if (it == favoriteOrder.end()) {
            return;
        }

        std::string id = *it;

        favoriteOrder.erase(it);

        newIndex = std::min(newIndex, favoriteOrder.size());

        favoriteOrder.insert(favoriteOrder.begin() + static_cast<std::ptrdiff_t>(newIndex), id);

        save();
    }

    void NexusConfig::ensureXRayTargetOrderEntry(const std::string& targetId) {
        load();

        if (targetId.empty()) {
            return;
        }

        if (std::find(xRayTargetOrder.begin(), xRayTargetOrder.end(), targetId) == xRayTargetOrder.end()) {
            xRayTargetOrder.push_back(targetId);
        }
    }

    
void NexusConfig::removeXRayTargetOrderEntry(const std::string& targetId) {
        load();

        std::erase(xRayTargetOrder, targetId);
    }

    const std::vector<std::string>& NexusConfig::getFavoriteOrder() {
        load();

        return favoriteOrder;
    }

    void NexusConfig::sanitizeFavoriteOrder() {
        std::vector<std::string> cleaned;

        for (const auto& id : favoriteOrder) {
            const auto* module = NexusModuleRegistry::find(id);

            if (module == nullptr || !module->canFavorite) {
                continue;
            }

            if (std::find(cleaned.begin(), cleaned.end(), id) != cleaned.end()) {
                continue;
            }

            cleaned.push_back(id);
        }

        favoriteOrder = std::move(cleaned);
    }

    //
    // ================================================================
    // X-RAY TARGET ORDER
    // ================================================================
    //

    const std::vector<std::string>& NexusConfig::getXRayTargetOrder() {
        load();

        sanitizeXRayTargetOrder();

        return xRayTargetOrder;
    }

    void NexusConfig::moveXRayTarget(const std::string& targetId, std::size_t newIndex) {
        load();

        auto it = std::find(xRayTargetOrder.begin(), xRayTargetOrder.end(), targetId);

        if (it == xRayTargetOrder.end()) {
            return;
        }

        std::string id = *it;

        xRayTargetOrder.erase(it);

        newIndex = std::min(newIndex, xRayTargetOrder.size());

        xRayTargetOrder.insert(xRayTargetOrder.begin() + static_cast<std::ptrdiff_t>(newIndex), id);

        save();
    }

    void NexusConfig::resetXRayTargetOrder() {
        load();

        xRayTargetOrder = defaultXRayTargetOrder();

        for (const auto& target : xRaySettings.customTargets) {
            xRayTargetOrder.push_back(XRayTargets::makeOrderKey(target.blockId));
        }

        save();
    }

    void NexusConfig::sanitizeXRayTargetOrder() {
        std::vector<std::string> cleaned;

        cleaned.reserve(xRayTargetOrder.size() + defaultXRayTargetOrder().size() + xRaySettings.customTargets.size());

        //
        // Preserve valid existing order.
        //

        for (const auto& id : xRayTargetOrder) {
            if (id.empty()) {
                continue;
            }

            //
            // Custom target keys are only valid while the corresponding
            // exact block target still exists.
            //

            if (XRayTargets::isCustomOrderKey(id)) {
                std::string blockId = XRayTargets::blockIdFromOrderKey(id);

                if (blockId.empty() || XRayTargets::findCustomConst(blockId) == nullptr) {
                    continue;
                }
            }

            //
            // Remove duplicates.
            //

            if (std::find(cleaned.begin(), cleaned.end(), id) != cleaned.end()) {
                continue;
            }

            cleaned.push_back(id);
        }

        //
        // Restore any missing built-in targets.
        //

        for (const auto& id : defaultXRayTargetOrder()) {
            if (std::find(cleaned.begin(), cleaned.end(), id) == cleaned.end()) {
                cleaned.push_back(id);
            }
        }

        //
        // Restore any missing custom targets.
        //

        for (const auto& target : xRaySettings.customTargets) {
            if (target.blockId.empty()) {
                continue;
            }

            std::string key = XRayTargets::makeOrderKey(target.blockId);

            if (std::find(cleaned.begin(), cleaned.end(), key) == cleaned.end()) {
                cleaned.push_back(std::move(key));
            }
        }

        xRayTargetOrder = std::move(cleaned);
    }

} // namespace Nexus
