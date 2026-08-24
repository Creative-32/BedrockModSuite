#pragma once

#include "../Screen.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

class XRayScreen final : public Screen {
public:
    XRayScreen();

    std::string getName() override { return "XRay"; }

protected:
    void onEnable(bool ignoreAnimations) override;
    void onDisable() override;

private:
    void onRender(Event& event);
    void onClick(Event& event);

    //
    // ============================================================
    // TARGET SCROLLING
    // ============================================================
    //

    float targetScroll = 0.0f;
    float targetLerpScroll = 0.0f;
    float targetScrollMax = 0.0f;

    float targetViewportLeft = 0.0f;
    float targetViewportTop = 0.0f;
    float targetViewportRight = 0.0f;
    float targetViewportBottom = 0.0f;

    //
    // Draggable target scrollbar.
    //

    bool targetScrollbarDragging = false;
    float targetScrollbarDragOffset = 0.0f;

    //
    // ============================================================
    // SLIDER INTERACTION
    // ============================================================
    //

    std::string activeSliderId {};

    //
    // Slider changes are written once the drag finishes instead
    // of saving the config every rendered frame.
    //

    bool sliderDirty = false;

    //
    // ============================================================
    // TARGET EXPANSION
    // ============================================================
    //

    std::string expandedTargetId {};

    //
    // ============================================================
    // WHOLE-CARD CLICK / HOLD / DRAG
    // ============================================================
    //
    // Quick click:
    //     expand / collapse
    //
    // Hold / movement:
    //     begin dragging
    //
    // Switch is excluded.
    //

    bool targetDragPending = false;
    bool draggingTarget = false;

    bool draggingTargetWasExpanded = false;

    std::string draggingTargetId {};

    std::size_t draggingTargetOriginalIndex = 0;
    std::size_t dragTargetIndex = 0;

    std::chrono::steady_clock::time_point targetPressTime {};

    float dragPressX = 0.0f;
    float dragPressY = 0.0f;

    float dragOffsetX = 0.0f;
    float dragOffsetY = 0.0f;

    //
    // ============================================================
    // HOVER BLOOM
    // ============================================================
    //

    std::unordered_map<std::string, float> targetHoverAnim {};
};
