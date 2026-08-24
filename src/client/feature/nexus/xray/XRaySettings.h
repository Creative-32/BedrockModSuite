#pragma once

#include <array>
#include <cstddef>

namespace Nexus {

    enum class XRayBuiltInTarget : std::size_t {
        Diamond = 0,
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

    struct XRayColor {
        int r = 255;
        int g = 255;
        int b = 255;
    };

    inline constexpr std::size_t XRayBuiltInTargetCount = static_cast<std::size_t>(XRayBuiltInTarget::Count);

    struct XRaySettings {
        //
        // ============================================================
        // MASTER
        // ============================================================
        //

        bool enabled = true;

        //
        // ============================================================
        // ORE ESP
        // ============================================================
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
        // ============================================================
        // ORE APPEARANCE / RANGE
        // ============================================================
        //

        int oreRange = 64;
        int oreOpacity = 80;
        int oreBrightness = 100;

        bool oreOutline = true;
        bool oreFill = true;

        //
        // Individual built-in ore colors.
        //
        // These are deliberately stored independently from OreType so
        // this data can later move into the generic XRayTarget system.
        //

        std::array<XRayColor, XRayBuiltInTargetCount> oreColors { { //
                                                                    // Diamond
                                                                    //
                                                                    { 0x42, 0xE6, 0xD5 },

                                                                    //
                                                                    // Emerald
                                                                    //
                                                                    { 0x35, 0xD0, 0x63 },

                                                                    //
                                                                    // Ancient Debris
                                                                    //
                                                                    { 0x9C, 0x64, 0x4B },

                                                                    //
                                                                    // Gold
                                                                    //
                                                                    { 0xF5, 0xD4, 0x42 },

                                                                    //
                                                                    // Iron
                                                                    //
                                                                    { 0xD8, 0xC5, 0xB0 },

                                                                    //
                                                                    // Copper
                                                                    //
                                                                    { 0xD7, 0x7A, 0x45 },

                                                                    //
                                                                    // Redstone
                                                                    //
                                                                    { 0xE0, 0x35, 0x35 },

                                                                    //
                                                                    // Lapis
                                                                    //
                                                                    { 0x38, 0x68, 0xD8 },

                                                                    //
                                                                    // Coal
                                                                    //
                                                                    { 0x70, 0x70, 0x70 } } };

        //
        // ============================================================
        // CAVE ESP
        // ============================================================
        //

        bool caveESP = true;

        bool airCheck3x3x3 = true;
        bool ignoreSurface = true;

        //
        // Cave range.
        //

        int scanRange = 64;

        //
        // Cave appearance.
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
