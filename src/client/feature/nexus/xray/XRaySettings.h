#pragma once

namespace Nexus {

    struct XRaySettings {
        //
        // Master
        //
        bool enabled = true;

        //
        // Ore ESP
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
        // Ore appearance / range
        //
        int oreRange = 64;
        int oreOpacity = 80;
        int oreBrightness = 100;

        bool oreOutline = true;
        bool oreFill = true;

        //
        // Cave ESP
        //
        bool caveESP = true;
        bool airCheck3x3x3 = true;
        bool ignoreSurface = true;

        //
        // Cave scan range
        //
        int scanRange = 64;

        //
        // Cave appearance
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
