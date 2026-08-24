#include "pch.h"

#include "XRayBlockCatalog.h"

#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/level/block/BlockLegacy.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Nexus {

    void XRayBlockCatalog::observe(SDK::Block* block) {
        if (!block || !block->legacyBlock) {
            return;
        }

        std::string id = block->legacyBlock->namespacedId.getString();

        if (id.empty()) {
            return;
        }

        observeId(id);
    }

    void XRayBlockCatalog::observeId(std::string_view namespacedId) {
        if (namespacedId.empty()) {
            return;
        }

        //
        // Ignore invalid-looking IDs.
        //
        // We intentionally do NOT require minecraft: because addon
        // namespaces are one of the reasons this catalog exists.
        //

        if (namespacedId.find(':') == std::string_view::npos) {
            return;
        }

        std::string id(namespacedId);

        if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
            return;
        }

        ids.push_back(std::move(id));

        dirty = true;
    }

    const std::vector<XRayCatalogEntry>& XRayBlockCatalog::getEntries() {
        if (dirty) {
            rebuild();
        }

        return entries;
    }

    void XRayBlockCatalog::clear() {
        ids.clear();
        entries.clear();

        dirty = true;
    }

    void XRayBlockCatalog::rebuild() {
        entries.clear();

        entries.reserve(ids.size());

        //
        // Sort by namespace + identifier.
        //

        std::sort(ids.begin(), ids.end());

        for (const auto& id : ids) {
            entries.push_back({ id, makeDisplayName(id) });
        }

        dirty = false;
    }

    std::wstring XRayBlockCatalog::makeDisplayName(std::string_view namespacedId) {
        std::string_view identifier = namespacedId;

        std::size_t colon = namespacedId.find(':');

        if (colon != std::string_view::npos && colon + 1 < namespacedId.size()) {
            identifier = namespacedId.substr(colon + 1);
        }

        std::wstring result;

        result.reserve(identifier.size());

        bool newWord = true;

        for (char raw : identifier) {
            unsigned char value = static_cast<unsigned char>(raw);

            if (raw == '_' || raw == '-' || raw == '.') {
                if (!result.empty() && result.back() != L' ') {
                    result.push_back(L' ');
                }

                newWord = true;

                continue;
            }

            if (newWord) {
                result.push_back(static_cast<wchar_t>(std::toupper(value)));

                newWord = false;
            }

            else {
                result.push_back(static_cast<wchar_t>(std::tolower(value)));
            }
        }

        if (result.empty()) {
            return L"Unknown Block";
        }

        return result;
    }

} // namespace Nexus
