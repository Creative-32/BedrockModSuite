#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SDK {
    class Block;
}

namespace Nexus {

    struct XRayCatalogEntry {
        std::string namespacedId;
        std::wstring displayName;
    };

    class XRayBlockCatalog final {
    public:
        //
        // Loads the installed Bedrock vanilla resource-pack catalog.
        //
        // Safe to call more than once.
        //
        static void initialize();

        //
        // Runtime fallback.
        //
        // The existing X-Ray scanner can continue calling this so
        // addon/server/custom blocks that were not in the vanilla
        // resource files can still be discovered.
        //
        static void observe(SDK::Block* block);

        //
        // Adds an exact runtime namespaced block ID.
        //
        static void observeId(std::string_view namespacedId);

        //
        // Same as above but allows a proper localized display name.
        //
        static void observeId(std::string_view namespacedId, std::wstring_view displayName);

        static const std::vector<XRayCatalogEntry>& getEntries();

        //
        // Mostly useful for debugging/reloading.
        //
        static void clear();

        [[nodiscard]]
        static std::size_t size() {
            return idSet.size();
        }

        [[nodiscard]]
        static bool loadedVanillaResources() {
            return vanillaResourcesLoaded;
        }

    private:
        static bool loadInstalledVanillaResources();

        static void rebuild();

        static std::wstring makeDisplayName(std::string_view namespacedId);

        inline static std::unordered_set<std::string> idSet {};

        inline static std::vector<std::string> ids {};

        inline static std::unordered_map<std::string, std::wstring> displayNameOverrides {};

        inline static std::vector<XRayCatalogEntry> entries {};

        inline static bool initialized = false;
        inline static bool vanillaResourcesLoaded = false;
        inline static bool dirty = true;
    };

} // namespace Nexus
