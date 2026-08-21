#pragma once

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

        inline static int menuKey = 'N';

        inline static NexusViewMode viewMode = NexusViewMode::List;

    private:
        static std::filesystem::path getConfigPath();

        static void sanitizeFavoriteOrder();

        inline static bool loaded = false;

        inline static std::vector<std::string> favoriteOrder {};
    };

} // namespace Nexus
