#pragma once

#include <array>
#include <cstddef>

namespace Nexus {

    //
    // ============================================================
    // BUILT-IN X-RAY TARGETS
    // ============================================================
    //

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

    struct XRaySettings {
        //
        // ========================================================
        // MASTER
        // ========================================================
        //

        bool enabled = true;

        //
        // ========================================================
        // ORE / BLOCK ESP
        // ========================================================
        //

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

        //
        // ========================================================
        // GLOBAL ORE APPEARANCE
        // ========================================================
        //

        int oreRange = 64;
        int oreOpacity = 80;
        int oreBrightness = 100;

        bool oreOutline = true;
        bool oreFill = true;

        //
        // ========================================================
        // INDIVIDUAL TARGET COLORS
        // ========================================================
        //
        // Order MUST match XRayBuiltInTarget.
        //

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

        //
        // ========================================================
        // CAVE ESP
        // ========================================================
        //

        bool caveESP = true;

        bool airCheck3x3x3 = true;
        bool ignoreSurface = true;

        //
        // Cave scan range.
        //

        int scanRange = 64;

        //
        // ========================================================
        // CAVE APPEARANCE
        // ========================================================
        //

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
