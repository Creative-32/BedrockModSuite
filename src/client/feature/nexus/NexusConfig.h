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

        static bool isFavorite(const std::string& moduleId);
        static void setFavorite(const std::string& moduleId, bool favorite);
        static void moveFavorite(const std::string& moduleId, std::size_t newIndex);
        static const std::vector<std::string>& getFavoriteOrder();

        static const std::vector<std::string>& getXRayTargetOrder();
        static void moveXRayTarget(const std::string& targetId, std::size_t newIndex);
        static void resetXRayTargetOrder();

        // Used by XRayTargets when arbitrary exact block IDs are added/removed.
        // These only mutate the order vector; caller decides when to save.
        static void ensureXRayTargetOrderEntry(const std::string& targetId);
        static void removeXRayTargetOrderEntry(const std::string& targetId);

        inline static int menuKey = 'N';
        inline static NexusViewMode viewMode = NexusViewMode::List;
        inline static int blockListColumns = 1;

    private:
        static std::filesystem::path getConfigPath();
        static void sanitizeFavoriteOrder();
        static void sanitizeXRayTargetOrder();

        inline static bool loaded = false;
        inline static std::vector<std::string> favoriteOrder {};
        inline static std::vector<std::string> xRayTargetOrder {};
    };

} // namespace Nexus
