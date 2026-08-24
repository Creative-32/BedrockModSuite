#pragma once

#include <string>
#include <string_view>
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
        static void observe(SDK::Block* block);
        static void observeId(std::string_view namespacedId);
        static const std::vector<XRayCatalogEntry>& getEntries();
        static void clear();

    private:
        static void rebuild();
        static std::wstring makeDisplayName(std::string_view namespacedId);

        inline static std::vector<std::string> ids {};
        inline static std::vector<XRayCatalogEntry> entries {};
        inline static bool dirty = true;
    };

} // namespace Nexus
