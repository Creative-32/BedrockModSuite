#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Nexus {

    enum class NexusViewMode {
        List,
        Medium,
        Compact
    };

    class NexusConfig {
    public:
        static void load();
        static void save();

        //
        // ============================================================
        // NEXUS FAVORITES
        // ============================================================
        //

        static bool isFavorite(const std::string& moduleId);

        static void setFavorite(const std::string& moduleId, bool favorite);

        static void moveFavorite(const std::string& moduleId, std::size_t newIndex);

        static const std::vector<std::string>& getFavoriteOrder();

        //
        // ============================================================
        // X-RAY TARGET ORDER
        // ============================================================
        //

        static const std::vector<std::string>& getXRayTargetOrder();

        static void moveXRayTarget(const std::string& targetId, std::size_t newIndex);

        static void resetXRayTargetOrder();

        //
        // ============================================================
        // GENERAL
        // ============================================================
        //

        inline static int menuKey = 'N';

        inline static NexusViewMode viewMode = NexusViewMode::List;

    private:
        static std::filesystem::path getConfigPath();

        static void sanitizeFavoriteOrder();

        static void sanitizeXRayTargetOrder();

        inline static bool loaded = false;

        inline static std::vector<std::string> favoriteOrder {};

        //
        // Default visual order:
        //
        // Diamond       Emerald
        // AncientDebris Gold
        // Iron          Copper
        // Redstone      Lapis
        // Coal
        //

        inline static std::vector<std::string> xRayTargetOrder { "diamond",  "emerald", "ancient_debris",
                                                                 "gold",     "iron",    "copper",
                                                                 "redstone", "lapis",   "coal" };
    };

} // namespace Nexus
