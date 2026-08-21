#pragma once

#include "../Screen.h"
#include "../TextBox.h"
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

    bool draggingFavorite = false;
    std::string draggingFavoriteId {};
    std::size_t dragTargetIndex = 0;

    float dragOffsetX = 0.0f;
    float dragOffsetY = 0.0f;

    std::unordered_map<std::string, float> favoriteAnimX {};
    std::unordered_map<std::string, float> favoriteAnimY {};

    TextBox searchBox {};
};
