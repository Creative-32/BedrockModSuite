#pragma once

#include "../Screen.h"
#include "../TextBox.h"

#include <string>
#include <unordered_set>

class XRayBlockListScreen final : public Screen {
public:
    XRayBlockListScreen();

    std::string getName() override { return "XRayBlockList"; }

protected:
    void onEnable(bool ignoreAnimations) override;
    void onDisable() override;

private:
    void onRender(Event& event);
    void onClick(Event& event);
    void onChar(Event& event);
    void onKey(Event& event);

    float scroll = 0.0f;
    float lerpScroll = 0.0f;
    float scrollMax = 0.0f;

    float listLeft = 0.0f;
    float listTop = 0.0f;
    float listRight = 0.0f;
    float listBottom = 0.0f;

    bool selectedOnly = false;
    bool groupedMode = true;
    bool showTechnicalBlocks = false;

    bool layoutDropdownOpen = false;

    //
    // Draggable scrollbar.
    //
    bool scrollbarDragging = false;
    float scrollbarDragOffset = 0.0f;

    std::unordered_set<std::string> expandedFamilies {};

    TextBox searchBox {};
};
