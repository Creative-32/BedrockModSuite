#pragma once

namespace Nexus {

    struct LightLevelSettings {
        //
        // Master feature.
        //
        bool enabled = false;

        //
        // Scan/render range around the player.
        //
        int range = 32;

        //
        // Overlay appearance.
        //
        int opacity = 45;
        int brightness = 100;

        bool outline = true;
        bool fill = true;

        //
        // Optional number above each square.
        //
        bool showNumbers = false;

        //
        // Distance fade keeps the overlay from becoming visually
        // overwhelming at the edge of the scan range.
        //
        bool distanceFade = true;
    };

    inline LightLevelSettings lightLevelSettings {};

}
