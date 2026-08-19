#pragma once

namespace Nexus {

    struct XRaySettings {
        // Master
        bool enabled = true;

        // Ore ESP
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

        // Cave / Path ESP
        bool caveESP = true;
        bool airCheck3x3x3 = true;
        bool ignoreSurface = true;

        // Scan / rendering
        int scanRange = 64;
        int caveOpacity = 25;

        bool outline = true;
        bool fill = true;
    };

    // One shared settings object for the whole client.
    // Later the scanner/renderer will read from this too.
    inline XRaySettings xRaySettings {};

} // namespace Nexus
