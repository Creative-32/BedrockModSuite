#include "pch.h"
#include "NexusConfig.h"

#include "xray/XRaySettings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include "module/NexusModuleRegistry.h"

namespace Nexus {

std::filesystem::path NexusConfig::getConfigPath() {
        const char* localAppData = std::getenv("LOCALAPPDATA");

        if (localAppData == nullptr) {
            return std::filesystem::current_path() / "Nexus" / "config.json";
        }

        return std::filesystem::path(localAppData) / "Nexus" / "config.json";
    }

    void NexusConfig::load() {
        if (loaded) return;

        loaded = true;

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

            menuKey = json.value("menuKey", static_cast<int>('N'));

            std::string viewModeValue = json.value("viewMode", std::string("list"));

            if (viewModeValue == "medium") {
                viewMode = NexusViewMode::Medium;
            } else if (viewModeValue == "compact") {
                viewMode = NexusViewMode::Compact;
            } else {
                viewMode = NexusViewMode::List;
            }

            favoriteOrder.clear();

            if (json.contains("favoriteOrder") && json["favoriteOrder"].is_array()) {
                for (const auto& entry : json["favoriteOrder"]) {
                    if (entry.is_string()) {
                        favoriteOrder.push_back(entry.get<std::string>());
                    }
                }
            }

            sanitizeFavoriteOrder();

            if (json.contains("xray") && json["xray"].is_object()) {
                const auto& xray = json["xray"];

                xRaySettings.enabled = xray.value("enabled", xRaySettings.enabled);

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

                xRaySettings.caveESP = xray.value("caveESP", xRaySettings.caveESP);

                xRaySettings.airCheck3x3x3 = xray.value("airCheck3x3x3", xRaySettings.airCheck3x3x3);

                xRaySettings.ignoreSurface = xray.value("ignoreSurface", xRaySettings.ignoreSurface);

                xRaySettings.scanRange = xray.value("scanRange", xRaySettings.scanRange);

                xRaySettings.caveOpacity = xray.value("caveOpacity", xRaySettings.caveOpacity);

                xRaySettings.outline = xray.value("outline", xRaySettings.outline);

                xRaySettings.fill = xray.value("fill", xRaySettings.fill);

                xRaySettings.scanRange = std::clamp(xRaySettings.scanRange, 16, 128);

                xRaySettings.caveOpacity = std::clamp(xRaySettings.caveOpacity, 5, 100);
            }
        } catch (...) {
            menuKey = 'N';
            viewMode = NexusViewMode::List;
            favoriteOrder.clear();
            xRaySettings = XRaySettings {};
            save();
        }
    }

    void NexusConfig::save() {
        loaded = true;

        const auto path = getConfigPath();

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        nlohmann::json json;

        json["version"] = 3;
        json["menuKey"] = menuKey;

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

                         { "caveESP", xRaySettings.caveESP },
                         { "airCheck3x3x3", xRaySettings.airCheck3x3x3 },
                         { "ignoreSurface", xRaySettings.ignoreSurface },

                         { "scanRange", xRaySettings.scanRange },
                         { "caveOpacity", xRaySettings.caveOpacity },

                         { "outline", xRaySettings.outline },
                         { "fill", xRaySettings.fill } };

        std::ofstream file(path);

        if (!file.is_open()) return;

        file << json.dump(4);
    }

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
    } else {
        if (it != favoriteOrder.end()) {
            favoriteOrder.erase(it);
        }
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

    favoriteOrder.insert(favoriteOrder.begin() + newIndex, id);

    save();
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

} // namespace Nexus
