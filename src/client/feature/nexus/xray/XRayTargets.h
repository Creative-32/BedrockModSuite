#pragma once

#include "XRaySettings.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Nexus {

    class XRayTargets final {
    public:
        static std::optional<XRayBuiltInTarget> getBuiltInTarget(std::string_view blockId);
        static bool isBuiltInBlockId(std::string_view blockId);

        static bool* getBuiltInEnabled(XRayBuiltInTarget target);
        static bool* getBuiltInEnabled(std::string_view blockId);

        static XRayColor* getBuiltInColor(XRayBuiltInTarget target);
        static const XRayColor* getBuiltInColorConst(XRayBuiltInTarget target);

        static XRayCustomTarget* findCustom(std::string_view blockId);
        static const XRayCustomTarget* findCustomConst(std::string_view blockId);

        // Block List semantics:
        // built-ins -> existing grouped enable switch
        // custom    -> membership in customTargets
        static bool isSelected(std::string_view blockId);
        static bool setSelected(std::string_view blockId, bool selected);

        // Main X-Ray target card semantics for a custom block.
        static bool isCustomEnabled(std::string_view blockId);
        static bool setCustomEnabled(std::string_view blockId, bool enabled);

        static XRayColor getColorForBlock(std::string_view blockId);
        static bool setColorForBlock(std::string_view blockId, XRayColor color);

        static std::string makeOrderKey(std::string_view blockId);
        static bool isCustomOrderKey(std::string_view key);
        static std::string blockIdFromOrderKey(std::string_view key);

        static std::wstring makeDisplayName(std::string_view blockId);

        // Lightweight change counter for render/scanner caches.
        static std::uint64_t getRevision();
        static void markChanged();
    };

} // namespace Nexus
