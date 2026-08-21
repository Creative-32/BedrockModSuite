#pragma once

#include "NexusModuleInfo.h"

#include <vector>

namespace Nexus {

    class NexusModuleRegistry {
    public:
        static void initialize();

        static const std::vector<NexusModuleInfo>& getModules();

        static const NexusModuleInfo* find(const std::string& id);

    private:
        inline static bool initialized = false;

        inline static std::vector<NexusModuleInfo> modules {};
    };

} // namespace Nexus
