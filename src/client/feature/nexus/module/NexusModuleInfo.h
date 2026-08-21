#pragma once

#include <string>

namespace Nexus {

    enum class NexusCategory {
        World,
        HUD,
        Building,
        Inventory,
        Entities,
        Utility,
        Gameplay,
        Administration
    };

    enum class NexusModulePolicy {
        ClientOnly,
        ServerGoverned,
        ServerRequired
    };

    struct NexusModuleInfo {
        std::string id;

        std::wstring name;
        std::wstring description;

        NexusCategory category;
        NexusModulePolicy policy;

        bool canFavorite = true;
    };

} // namespace Nexus
