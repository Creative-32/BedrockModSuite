#pragma once

#include <filesystem>

namespace Nexus {

    class NexusConfig {
    public:
        static void load();
        static void save();

        inline static int menuKey = 'N';

    private:
        static std::filesystem::path getConfigPath();

        inline static bool loaded = false;
    };

} // namespace Nexus
