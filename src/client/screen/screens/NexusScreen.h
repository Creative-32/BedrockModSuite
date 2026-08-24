#pragma once

#include "../Screen.h"
#include "../TextBox.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

class NexusScreen final : public Screen {
public:
    NexusScreen();

    std::string getName() override { return "Nexus"; }

    void prepareForSubmenu() { skipCloseAnimation = true; }

protected:
    void onEnable(bool ignoreAnimations) override;
    void onDisable() override;

private:
    void onRender(Event& event);
    void onClick(Event& event);

    void onChar(Event& event);
    void onKey(Event& event);

    float openAnim = 0.0f;
    bool skipCloseAnimation = false;

    float scroll = 0.0f;
    float lerpScroll = 0.0f;
    float scrollMax = 0.0f;

    //
    // ============================================================
    // FAVORITE CLICK / HOLD / DRAG
    // ============================================================
    //

    bool favoriteDragPending = false;
    bool draggingFavorite = false;

    std::string draggingFavoriteId {};

    std::size_t draggingFavoriteOriginalIndex = 0;
    std::size_t dragTargetIndex = 0;

    std::chrono::steady_clock::time_point favoritePressTime {};

    float favoritePressX = 0.0f;
    float favoritePressY = 0.0f;

    float dragOffsetX = 0.0f;
    float dragOffsetY = 0.0f;

    //
    // Position animation during drag reorder.
    //

    std::unordered_map<std::string, float> favoriteAnimX {};
    std::unordered_map<std::string, float> favoriteAnimY {};


    //
    // Hover bloom.
    //

    std::unordered_map<std::string, float> moduleHoverAnim {};

    TextBox searchBox {};
};
