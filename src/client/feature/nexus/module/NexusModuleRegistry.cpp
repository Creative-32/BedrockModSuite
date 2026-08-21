#include "pch.h"
#include "NexusModuleRegistry.h"

namespace Nexus {

    void NexusModuleRegistry::initialize() {
        if (initialized) return;
        initialized = true;

        modules = { { "xray", L"X-Ray", L"Highlight ores and underground paths.", NexusCategory::World,
                      NexusModulePolicy::ServerGoverned },

                    { "light_levels", L"Light Levels", L"Visualize block light levels.", NexusCategory::World,
                      NexusModulePolicy::ClientOnly },

                    { "build_guide", L"BuildGuide", L"Render shapes and construction guides.", NexusCategory::Building,
                      NexusModulePolicy::ClientOnly },

                    { "armor_hud", L"Armor HUD", L"Display equipped armor and durability.", NexusCategory::HUD,
                      NexusModulePolicy::ClientOnly },

                    { "block_break_hud", L"Block Break HUD", L"Show the block being mined and its break progress.",
                      NexusCategory::HUD, NexusModulePolicy::ClientOnly },

                    { "animal_feeder", L"Animal Feeder", L"Assist with nearby animal feeding and breeding.",
                      NexusCategory::Entities, NexusModulePolicy::ClientOnly },

                    { "animal_timers", L"Animal Timers", L"Show nearby breeding and growth timers.",
                      NexusCategory::Entities, NexusModulePolicy::ClientOnly },

                    { "chest_tools", L"Chest Tools", L"Sorting and utility tools for opened containers.",
                      NexusCategory::Inventory, NexusModulePolicy::ClientOnly } };
    }

    const std::vector<NexusModuleInfo>& NexusModuleRegistry::getModules() {
        initialize();
        return modules;
    }

    const NexusModuleInfo* NexusModuleRegistry::find(const std::string& id) {
        initialize();

        for (const auto& module : modules) {
            if (module.id == id) {
                return &module;
            }
        }

        return nullptr;
    }

} // namespace Nexus
