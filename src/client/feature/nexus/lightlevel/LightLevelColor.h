#pragma once

#include <algorithm>
#include <cmath>

namespace Nexus {

    struct LightLevelColor {
        float r = 1.0f;
        float g = 0.0f;
        float b = 0.0f;
    };

    //
    // ============================================================
    // RED -> ORANGE -> YELLOW -> LIME -> GREEN
    // ============================================================
    //
    // Level 0:
    //     red
    //
    // Level 7-8:
    //     yellow
    //
    // Level 15:
    //     green
    //
    // Every level between them receives its own intermediate shade.
    //

    inline LightLevelColor getLightLevelColor(int lightLevel) {
        lightLevel = std::clamp(lightLevel, 0, 15);

        float t = static_cast<float>(lightLevel) / 15.0f;

        //
        // Hue:
        //
        // 0 degrees   = red
        // 60 degrees  = yellow
        // 120 degrees = green
        //
        float hue = 120.0f * t;

        float section = hue / 60.0f;

        float x = 1.0f - std::abs(std::fmod(section, 2.0f) - 1.0f);

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        if (section < 1.0f) {
            //
            // Red -> Yellow
            //
            r = 1.0f;
            g = x;
        }

        else {
            //
            // Yellow -> Green
            //
            r = x;
            g = 1.0f;
        }

        //
        // Slightly soften the colors so the overlay isn't painfully
        // saturated over Minecraft textures.
        //
        constexpr float soften = 0.08f;

        r = r * (1.0f - soften) + soften;

        g = g * (1.0f - soften) + soften;

        b = soften;

        return { std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f) };
    }

}
