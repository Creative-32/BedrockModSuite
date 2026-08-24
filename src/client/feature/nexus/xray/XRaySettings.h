#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Nexus {

    enum class XRayBuiltInTarget {
        Diamond,
        Emerald,
        AncientDebris,
        Gold,
        Iron,
        Copper,
        Redstone,
        Lapis,
        Coal,
        Count
    };

    inline constexpr std::size_t XRayBuiltInTargetCount = static_cast<std::size_t>(XRayBuiltInTarget::Count);

    struct XRayColor {
        int r = 255;
        int g = 255;
        int b = 255;
    };

    struct XRayCustomTarget {
        // Exact runtime namespaced block ID, e.g. minecraft:clay or addon:my_ore.
        std::string blockId;

        // Block List membership and main target enable state are intentionally
        // separate concepts. Presence in customTargets means the block was added;
        // enabled controls whether it currently renders.
        bool enabled = true;

        XRayColor color { 255, 255, 255 };
    };

    struct XRaySettings {
        bool enabled = true;
        bool oreESP = true;

        bool diamond = true;
        bool ancientDebris = true;
        bool emerald = true;
        bool gold = true;
        bool iron = false;
        bool copper = false;
        bool coal = false;
        bool lapis = true;
        bool redstone = true;

        int oreRange = 64;
        int oreOpacity = 80;
        int oreBrightness = 100;
        bool oreOutline = true;
        bool oreFill = true;

        std::array<XRayColor, XRayBuiltInTargetCount> oreColors {
            XRayColor { 0x42, 0xE6, 0xD5 }, // Diamond
            XRayColor { 0x35, 0xD0, 0x63 }, // Emerald
            XRayColor { 0x9C, 0x64, 0x4B }, // Ancient Debris
            XRayColor { 0xF5, 0xD4, 0x42 }, // Gold
            XRayColor { 0xD8, 0xC5, 0xB0 }, // Iron
            XRayColor { 0xD7, 0x7A, 0x45 }, // Copper
            XRayColor { 0xE0, 0x35, 0x35 }, // Redstone
            XRayColor { 0x38, 0x68, 0xD8 }, // Lapis
            XRayColor { 0x70, 0x70, 0x70 }  // Coal
        };

        // Arbitrary exact block IDs selected through Block List.
        std::vector<XRayCustomTarget> customTargets {};

        bool caveESP = true;
        bool airCheck3x3x3 = true;
        bool ignoreSurface = true;
        int scanRange = 64;
        int caveOpacity = 25;
        int caveBrightness = 80;
        int caveOutlineOpacity = 70;
        int caveColorR = 0xA9;
        int caveColorG = 0x5C;
        int caveColorB = 0xFF;
        bool caveOutline = true;
        bool caveFill = true;
    };

    inline XRaySettings xRaySettings {};

} // namespace Nexus
