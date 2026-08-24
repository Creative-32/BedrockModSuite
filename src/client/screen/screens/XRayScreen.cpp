#include "pch.h"

#include "XRayScreen.h"
#include "NexusScreen.h"

#include "client/feature/nexus/ui/NexusControls.h"
#include "client/feature/nexus/NexusConfig.h"
#include "client/feature/nexus/xray/XRaySettings.h"
#include "client/feature/nexus/navigation/NexusNavigation.h"

#include "client/event/Eventing.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/RenderOverlayEvent.h"

#include "client/Latite.h"
#include "client/screen/ScreenManager.h"

#include "util/DrawContext.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

XRayScreen::XRayScreen() {
    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&XRayScreen::onRender, 1, true);

    Eventing::get().listen<ClickEvent>(this, (EventListenerFunc)&XRayScreen::onClick, 1);
}

void XRayScreen::onEnable(bool) {
    targetScroll = 0.0f;
    targetLerpScroll = 0.0f;
    targetScrollMax = 0.0f;

    targetDragPending = false;
    draggingTarget = false;
    draggingTargetWasExpanded = false;

    draggingTargetId.clear();

    draggingTargetOriginalIndex = 0;
    dragTargetIndex = 0;

    targetPressTime = {};

    dragPressX = 0.0f;
    dragPressY = 0.0f;

    dragOffsetX = 0.0f;
    dragOffsetY = 0.0f;

    expandedTargetId.clear();

    targetHoverAnim.clear();

    resetInputState();
}

void XRayScreen::onDisable() {
    Nexus::NexusConfig::save();

    targetDragPending = false;
    draggingTarget = false;
    draggingTargetWasExpanded = false;

    draggingTargetId.clear();

    targetHoverAnim.clear();

    resetInputState();
}

void XRayScreen::onClick(Event& event) {
    if (!isActive()) {
        return;
    }

    auto& clickEvent = reinterpret_cast<ClickEvent&>(event);

    if (clickEvent.getClickType() != ClickEvent::ClickType::Wheel) {
        return;
    }

    auto clientInstance = SDK::ClientInstance::get();

    if (!clientInstance) {
        return;
    }

    auto& cursorPos = clientInstance->cursorPos;

    bool overTargetViewport = cursorPos.x >= targetViewportLeft && cursorPos.x <= targetViewportRight &&
                              cursorPos.y >= targetViewportTop && cursorPos.y <= targetViewportBottom;

    if (!overTargetViewport) {
        return;
    }

    targetScroll =
        std::clamp(targetScroll - static_cast<float>(clickEvent.getWheelDelta()) / 3.0f, 0.0f, targetScrollMax);

    clickEvent.setCancelled(true);
}

void XRayScreen::onRender(Event&) {
    if (!isActive()) {
        return;
    }

    using namespace Nexus;

    auto clientInstance = SDK::ClientInstance::get();

    if (!clientInstance) {
        return;
    }

    D2DUtil dc;

    auto screenSize = Latite::getRenderer().getScreenSize();

    auto& cursorPos = clientInstance->cursorPos;

    cursor = Cursor::Arrow;

//
    // ============================================================
    // CLICK / HOLD / DRAG BEHAVIOR
    // ============================================================
    //

    float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);

    //
    // A normal click opens/closes settings.
    //
    // Holding for this long turns the same interaction into a drag.
    //
    // 230 ms is long enough that normal clicking still feels immediate,
    // while dragging is intentional.
    //

    constexpr auto TargetDragHoldDelay = std::chrono::milliseconds(230);

    //
    // ------------------------------------------------------------
    // PENDING PRESS -> DRAG
    // ------------------------------------------------------------
    //

    if (targetDragPending && !draggingTarget && mouseButtons[0]) {
        auto heldFor = std::chrono::steady_clock::now() - targetPressTime;

        if (heldFor >= TargetDragHoldDelay) {
            draggingTarget = true;

            //
            // Temporarily collapse an expanded card while it is being
            // moved. We restore it after the drop.
            //

            draggingTargetWasExpanded = expandedTargetId == draggingTargetId;

            if (draggingTargetWasExpanded) {
                expandedTargetId.clear();
            }
        }
    }

    //
    // ------------------------------------------------------------
    // RELEASE
    // ------------------------------------------------------------
    //

    if ((targetDragPending || draggingTarget) && !mouseButtons[0]) {
        std::string releasedTargetId = draggingTargetId;

        bool wasDragging = draggingTarget;

        bool restoreExpanded = draggingTargetWasExpanded;

        //
        // --------------------------------------------------------
        // FINISH DRAG
        // --------------------------------------------------------
        //

        if (wasDragging && !releasedTargetId.empty()) {
            if (draggingTargetOriginalIndex != dragTargetIndex) {
                NexusConfig::moveXRayTarget(releasedTargetId, dragTargetIndex);

                playClickSound();
            }

            //
            // If its settings were open before the drag,
            // reopen them at the target's new location.
            //

            if (restoreExpanded) {
                expandedTargetId = releasedTargetId;
            }
        }

        //
        // --------------------------------------------------------
        // NORMAL SHORT CLICK
        // --------------------------------------------------------
        //

        else if (targetDragPending && !releasedTargetId.empty()) {
            float dx = cursorPos.x - dragPressX;

            float dy = cursorPos.y - dragPressY;

            //
            // A little cursor movement is fine.
            //
            // A large movement before the hold timer expires cancels
            // the click rather than accidentally opening settings.
            //

            float maxClickMovement = 8.0f * scale;

            bool stayedNearPress = dx * dx + dy * dy <= maxClickMovement * maxClickMovement;

            if (stayedNearPress) {
                if (expandedTargetId == releasedTargetId) {
                    expandedTargetId.clear();
                }

                else {
                    expandedTargetId = releasedTargetId;
                }

                playClickSound();
            }
        }

        targetDragPending = false;
        draggingTarget = false;
        draggingTargetWasExpanded = false;

        draggingTargetId.clear();

        draggingTargetOriginalIndex = 0;
        dragTargetIndex = 0;
    }

    //
    // ============================================================
    // MAIN PANEL
    // ============================================================
    //

    float panelWidth = std::min(screenSize.width * 0.92f, 1180.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.94f, 900.0f * scale);

    d2d::Rect panelRect = { (screenSize.width - panelWidth) * 0.5f, (screenSize.height - panelHeight) * 0.5f,
                            (screenSize.width + panelWidth) * 0.5f, (screenSize.height + panelHeight) * 0.5f };

    float padding = 28.0f * scale;

    dc.fillRoundedRectangle(panelRect, d2d::Color::RGB(0x0B, 0x0B, 0x0B).asAlpha(0.94f), 18.0f * scale);

    dc.drawRoundedRectangle(panelRect, d2d::Color::RGB(0x45, 0x45, 0x45).asAlpha(0.75f), 18.0f * scale, 1.5f * scale);

    //
    // ============================================================
    // TITLE
    // ============================================================
    //

    d2d::Rect titleRect = { panelRect.left + padding, panelRect.top + 10.0f * scale, panelRect.right - padding,
                            panelRect.top + 50.0f * scale };

    dc.drawText(titleRect, L"X-Ray", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryLight, 28.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect subtitleRect = { panelRect.left + padding, panelRect.top + 43.0f * scale, panelRect.right - padding,
                               panelRect.top + 67.0f * scale };

    dc.drawText(subtitleRect, L"Block ESP and Cave ESP", d2d::Color::RGB(0xA0, 0xA0, 0xA0).asAlpha(0.80f),
                Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // CONTROL HELPERS
    // ============================================================
    //

    auto drawDisabledOverlay = [&](const d2d::Rect& rect) {
        dc.fillRoundedRectangle(rect, d2d::Color::RGB(0x08, 0x08, 0x08).asAlpha(0.38f), 8.0f * scale);
    };

    auto drawToggle = [&](const d2d::Rect& rowRect, const std::wstring& label, bool& value, bool enabled = true) {
        bool hovering = enabled && shouldSelect(rowRect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        bool changed = Nexus::UI::drawToggle(dc, rowRect, label, value, hovering, enabled && justClicked[0], scale);

        if (!enabled) {
            drawDisabledOverlay(rowRect);
        }

        if (changed) {
            playClickSound();
        }

        return changed;
    };

    auto drawSlider = [&](const d2d::Rect& rowRect, const std::wstring& label, int& value, int minimum, int maximum,
                          int step, bool percent, bool enabled = true) {
        d2d::Rect interactionRect = { rowRect.left + 150.0f * scale,

                                      rowRect.top,

                                      rowRect.right - 72.0f * scale,

                                      rowRect.bottom };

        bool hovering = enabled && shouldSelect(interactionRect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        Nexus::UI::drawSlider(dc, rowRect, label, value, minimum, maximum, step, percent, hovering,
                              enabled && justClicked[0], enabled && mouseButtons[0], cursorPos.x, scale);

        if (!enabled) {
            drawDisabledOverlay(rowRect);
        }
    };

    auto drawSectionLabel = [&](const d2d::Rect& rect, const std::wstring& label, bool enabled) {
        dc.drawText(rect, label, enabled ? d2d::Color::RGB(0xA0, 0xA0, 0xA0) : d2d::Color::RGB(0x58, 0x58, 0x58),
                    Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    auto drawSmallButton = [&](const d2d::Rect& rect, const std::wstring& text, bool enabled = true) -> bool {
        bool hovering = enabled && shouldSelect(rect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        d2d::Color background;

        if (!enabled) {
            background = d2d::Color::RGB(0x13, 0x13, 0x13);
        }

        else if (hovering) {
            background = d2d::Color::RGB(0x2A, 0x2A, 0x2A);
        }

        else {
            background = d2d::Color::RGB(0x1C, 0x1C, 0x1C);
        }

        dc.fillRoundedRectangle(rect, background, 7.0f * scale);

        dc.drawRoundedRectangle(rect, enabled ? d2d::Color::RGB(0x45, 0x45, 0x45) : d2d::Color::RGB(0x28, 0x28, 0x28),
                                7.0f * scale, 1.0f * scale);

        dc.drawText(rect, text, enabled ? d2d::Color::RGB(0xD8, 0xD8, 0xD8) : d2d::Color::RGB(0x60, 0x60, 0x60),
                    Renderer::FontSelection::PrimaryRegular, 10.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        return hovering && justClicked[0];
    };

    //
    // Compact slider for expanded target settings.
    //

    auto drawMiniSlider = [&](const d2d::Rect& rect, const std::wstring& label, int& value, int minimum, int maximum,
                              bool enabled) {
        d2d::Rect labelRect = { rect.left + 8.0f * scale, rect.top, rect.left + 42.0f * scale, rect.bottom };

        d2d::Rect valueRect = { rect.right - 34.0f * scale, rect.top, rect.right - 5.0f * scale, rect.bottom };

        d2d::Rect trackRect = { rect.left + 48.0f * scale,

                                rect.center().y - 2.0f * scale,

                                rect.right - 42.0f * scale,

                                rect.center().y + 2.0f * scale };

        dc.drawText(labelRect, label, enabled ? d2d::Color::RGB(0xC0, 0xC0, 0xC0) : d2d::Color::RGB(0x60, 0x60, 0x60),
                    Renderer::FontSelection::PrimaryRegular, 10.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        dc.drawText(valueRect, std::to_wstring(value), enabled ? d2d::Colors::WHITE : d2d::Color::RGB(0x60, 0x60, 0x60),
                    Renderer::FontSelection::PrimaryRegular, 9.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        dc.fillRoundedRectangle(trackRect, d2d::Color::RGB(0x38, 0x38, 0x38), 2.0f * scale);

        float normalized = static_cast<float>(value - minimum) / static_cast<float>(maximum - minimum);

        normalized = std::clamp(normalized, 0.0f, 1.0f);

        d2d::Rect progressRect = { trackRect.left, trackRect.top, trackRect.left + trackRect.getWidth() * normalized,
                                   trackRect.bottom };

        dc.fillRoundedRectangle(progressRect, d2d::Color::RGB(0x42, 0x78, 0xA8), 2.0f * scale);

        float knobX = trackRect.left + trackRect.getWidth() * normalized;

        float knobRadius = 4.0f * scale;

        d2d::Rect knobRect = { knobX - knobRadius, rect.center().y - knobRadius, knobX + knobRadius,
                               rect.center().y + knobRadius };

        dc.fillRoundedRectangle(knobRect, d2d::Colors::WHITE, knobRadius);

        bool hovering = enabled && shouldSelect(trackRect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        if (!hovering || (!justClicked[0] && !mouseButtons[0])) {
            return;
        }

        float mouseNormalized = (cursorPos.x - trackRect.left) / trackRect.getWidth();

        mouseNormalized = std::clamp(mouseNormalized, 0.0f, 1.0f);

        int newValue = static_cast<int>(
            std::lround(static_cast<float>(minimum) + mouseNormalized * static_cast<float>(maximum - minimum)));

        value = std::clamp(newValue, minimum, maximum);
    };

    //
    // ============================================================
    // MASTER X-RAY
    // ============================================================
    //

    float rowHeight = 32.0f * scale;

    float rowGap = 5.0f * scale;

    float masterTop = panelRect.top + 76.0f * scale;

    d2d::Rect masterRect = { panelRect.left + padding, masterTop, panelRect.right - padding, masterTop + rowHeight };

    drawToggle(masterRect, L"Master X-Ray", xRaySettings.enabled);

    bool xrayAvailable = xRaySettings.enabled;

    bool oresAvailable = xrayAvailable && xRaySettings.oreESP;

    bool cavesAvailable = xrayAvailable && xRaySettings.caveESP;

    //
    // ============================================================
    // MAIN CONTENT CARDS
    // ============================================================
    //

    float contentTop = masterRect.bottom + 18.0f * scale;

    float contentBottom = panelRect.bottom - 72.0f * scale;

    float cardGap = 18.0f * scale;

    float contentWidth = panelRect.getWidth() - padding * 2.0f;

    float leftWidth = (contentWidth - cardGap) * 0.61f;

    float rightWidth = contentWidth - cardGap - leftWidth;

    float leftX = panelRect.left + padding;

    float rightX = leftX + leftWidth + cardGap;

    d2d::Rect targetCard = { leftX, contentTop, leftX + leftWidth, contentBottom };

    d2d::Rect caveCard = { rightX, contentTop, rightX + rightWidth, contentBottom };

    auto drawCard = [&](const d2d::Rect& rect) {
        dc.fillRoundedRectangle(rect, d2d::Color::RGB(0x0F, 0x0F, 0x0F).asAlpha(0.72f), 12.0f * scale);

        dc.drawRoundedRectangle(rect, d2d::Color::RGB(0x3C, 0x3C, 0x3C).asAlpha(0.72f), 12.0f * scale, 1.0f * scale);
    };

    drawCard(targetCard);
    drawCard(caveCard);

    float cardPadding = 12.0f * scale;

    //
    // ============================================================
    // BUILT-IN TARGET BINDINGS
    // ============================================================
    //

    struct TargetBinding {
        std::string id;
        std::wstring name;

        bool* enabled = nullptr;

        XRayBuiltInTarget colorTarget = XRayBuiltInTarget::Diamond;
    };

    std::vector<TargetBinding> bindings {
        { "diamond", L"Diamond", &xRaySettings.diamond, XRayBuiltInTarget::Diamond },

        { "emerald", L"Emerald", &xRaySettings.emerald, XRayBuiltInTarget::Emerald },

        { "ancient_debris", L"Ancient Debris", &xRaySettings.ancientDebris, XRayBuiltInTarget::AncientDebris },

        { "gold", L"Gold", &xRaySettings.gold, XRayBuiltInTarget::Gold },

        { "iron", L"Iron", &xRaySettings.iron, XRayBuiltInTarget::Iron },

        { "copper", L"Copper", &xRaySettings.copper, XRayBuiltInTarget::Copper },

        { "redstone", L"Redstone", &xRaySettings.redstone, XRayBuiltInTarget::Redstone },

        { "lapis", L"Lapis", &xRaySettings.lapis, XRayBuiltInTarget::Lapis },

        { "coal", L"Coal", &xRaySettings.coal, XRayBuiltInTarget::Coal }
    };

    auto findBinding = [&](const std::string& id) -> TargetBinding* {
        auto found = std::find_if(bindings.begin(), bindings.end(), [&](const TargetBinding& binding) {
            return binding.id == id;
        });

        if (found == bindings.end()) {
            return nullptr;
        }

        return &(*found);
    };

    std::vector<TargetBinding*> orderedTargets;

    for (const auto& id : NexusConfig::getXRayTargetOrder()) {
        TargetBinding* binding = findBinding(id);

        if (binding != nullptr) {
            orderedTargets.push_back(binding);
        }
    }

    //
    // ============================================================
    // TARGET CARD HEADER
    // ============================================================
    //

    float leftInner = targetCard.left + cardPadding;

    float leftInnerRight = targetCard.right - cardPadding;

    d2d::Rect targetTitleRect = { leftInner, targetCard.top + 7.0f * scale, leftInnerRight,
                                  targetCard.top + 29.0f * scale };

    int enabledTargetCount = 0;

    for (TargetBinding* target : orderedTargets) {
        if (target != nullptr && target->enabled != nullptr && *target->enabled) {
            ++enabledTargetCount;
        }
    }

    dc.drawText(targetTitleRect, L"BLOCK & ORE TARGETS",
                xrayAvailable ? d2d::Color::RGB(0xB8, 0xB8, 0xB8) : d2d::Color::RGB(0x60, 0x60, 0x60),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect targetCountRect = { targetTitleRect.left, targetTitleRect.top, targetTitleRect.right,
                                  targetTitleRect.bottom };

    std::wstring targetCountText =
        std::to_wstring(enabledTargetCount) + L" / " + std::to_wstring(orderedTargets.size()) + L" enabled";

    dc.drawText(targetCountRect, targetCountText, d2d::Color::RGB(0x78, 0x78, 0x78),
                Renderer::FontSelection::PrimaryRegular, 10.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // Ore ESP parent row.
    //

    float oreParentTop = targetTitleRect.bottom + 4.0f * scale;

    d2d::Rect oreParentRect = { leftInner, oreParentTop, leftInnerRight, oreParentTop + rowHeight };

    drawToggle(oreParentRect, L"Ore ESP", xRaySettings.oreESP, xrayAvailable);

    //
    // ============================================================
    // HELPER BUTTONS
    // ============================================================
    //

    float helperTop = oreParentRect.bottom + 6.0f * scale;

    float helperHeight = 24.0f * scale;

    float helperGap = 5.0f * scale;

    float helperWidth = (leftInnerRight - leftInner - helperGap * 3.0f) / 4.0f;

    d2d::Rect allOnRect = { leftInner, helperTop, leftInner + helperWidth, helperTop + helperHeight };

    d2d::Rect allOffRect = { allOnRect.right + helperGap, helperTop, allOnRect.right + helperGap + helperWidth,
                             helperTop + helperHeight };

    d2d::Rect collapseRect = { allOffRect.right + helperGap, helperTop, allOffRect.right + helperGap + helperWidth,
                               helperTop + helperHeight };

    d2d::Rect resetOrderRect = { collapseRect.right + helperGap, helperTop, leftInnerRight, helperTop + helperHeight };

    if (drawSmallButton(allOnRect, L"All On", oresAvailable)) {
        for (TargetBinding* target : orderedTargets) {
            if (target != nullptr && target->enabled != nullptr) {
                *target->enabled = true;
            }
        }

        playClickSound();

        NexusConfig::save();
    }

    if (drawSmallButton(allOffRect, L"All Off", oresAvailable)) {
        for (TargetBinding* target : orderedTargets) {
            if (target != nullptr && target->enabled != nullptr) {
                *target->enabled = false;
            }
        }

        playClickSound();

        NexusConfig::save();
    }

    if (drawSmallButton(collapseRect, L"Collapse", !expandedTargetId.empty())) {
        expandedTargetId.clear();

        playClickSound();
    }

    if (drawSmallButton(resetOrderRect, L"Reset Order", true)) {
        NexusConfig::resetXRayTargetOrder();

        expandedTargetId.clear();

        targetScroll = 0.0f;
        targetLerpScroll = 0.0f;

        playClickSound();
    }

    //
    // ============================================================
    // SCROLLABLE TARGET VIEWPORT
    // ============================================================
    //

    float viewportTop = helperTop + helperHeight + 8.0f * scale;

    float targetViewportHeight = 235.0f * scale;

    d2d::Rect targetViewport = { leftInner, viewportTop, leftInnerRight, viewportTop + targetViewportHeight };

    targetViewportLeft = targetViewport.left;

    targetViewportTop = targetViewport.top;

    targetViewportRight = targetViewport.right;

    targetViewportBottom = targetViewport.bottom;

    dc.fillRoundedRectangle(targetViewport, d2d::Color::RGB(0x0A, 0x0A, 0x0A).asAlpha(0.50f), 9.0f * scale);

    dc.drawRoundedRectangle(targetViewport, d2d::Color::RGB(0x34, 0x34, 0x34), 9.0f * scale, 1.0f * scale);

    float tileGap = 6.0f * scale;

    float tileHeight = 34.0f * scale;

    float expansionGap = 4.0f * scale;

    float expansionHeight = 106.0f * scale;

    float scrollbarSpace = 10.0f * scale;

    //
    // Consistent inner spacing around the target grid.
    //
    // This gives the top of the first card the same breathing room
    // it already has on the left side.
    //

    float targetInnerPadding = 5.0f * scale;

    float usableTargetWidth = targetViewport.getWidth() - scrollbarSpace - targetInnerPadding * 2.0f;

    float targetColumnWidth = (usableTargetWidth - tileGap) * 0.5f;

 //
    // ============================================================
    // INDEPENDENT TWO-COLUMN TARGET LAYOUT
    // ============================================================
    //
    // Even-index targets belong to the left column.
    //
    // Odd-index targets belong to the right column.
    //
    // Each column has its OWN vertical cursor.
    //
    // Therefore expanding Diamond does not push Emerald/Gold/etc.
    // downward.
    //
    // This preserves the side-by-side layout while allowing each
    // target to independently expand.
    //

    float columnContentHeight[2] = { 0.0f, 0.0f };

    for (std::size_t index = 0; index < orderedTargets.size(); ++index) {
        int column = static_cast<int>(index % 2);

        TargetBinding* target = orderedTargets[index];

        columnContentHeight[column] += tileHeight;

        if (target != nullptr && target->id == expandedTargetId) {
            columnContentHeight[column] += expansionGap + expansionHeight;
        }

        columnContentHeight[column] += tileGap;
    }

    //
    // Remove the trailing gap from each populated column.
    //

    if (!orderedTargets.empty()) {
        columnContentHeight[0] = std::max(0.0f, columnContentHeight[0] - tileGap);
    }

    if (orderedTargets.size() >= 2) {
        columnContentHeight[1] = std::max(0.0f, columnContentHeight[1] - tileGap);
    }

    //
    // Shared scroll range only needs to contain the taller column.
    //

    float contentHeight = std::max(columnContentHeight[0], columnContentHeight[1]) + targetInnerPadding * 2.0f;

    targetScrollMax = std::max(0.0f, contentHeight - targetViewport.getHeight());

    targetScroll = std::clamp(targetScroll, 0.0f, targetScrollMax);

    targetLerpScroll =
        std::lerp(targetLerpScroll, targetScroll, std::clamp(Latite::getRenderer().getDeltaTime() / 5.0f, 0.0f, 1.0f));

    targetLerpScroll = std::clamp(targetLerpScroll, 0.0f, targetScrollMax);

    //
    // ============================================================
    // CALCULATE TARGET RECTS
    // ============================================================
    //

    std::vector<d2d::Rect> targetRects(orderedTargets.size());

    std::vector<d2d::Rect> expansionRects(orderedTargets.size());

    float columnCursor[2] = { 0.0f, 0.0f };

    for (std::size_t index = 0; index < orderedTargets.size(); ++index) {
        TargetBinding* target = orderedTargets[index];

        int column = static_cast<int>(index % 2);

        float columnLeft =
            targetViewport.left + targetInnerPadding + static_cast<float>(column) * (targetColumnWidth + tileGap);

        float tileTop = targetViewport.top + targetInnerPadding + columnCursor[column] - targetLerpScroll;

        d2d::Rect tileRect = { columnLeft, tileTop, columnLeft + targetColumnWidth, tileTop + tileHeight };

        targetRects[index] = tileRect;

        columnCursor[column] += tileHeight;

        //
        // Only THIS column gains the expansion height.
        //

        if (target != nullptr && target->id == expandedTargetId) {
            columnCursor[column] += expansionGap;

            float expandedTop = targetViewport.top + targetInnerPadding + columnCursor[column] - targetLerpScroll;

            expansionRects[index] = { columnLeft, expandedTop, columnLeft + targetColumnWidth,
                                      expandedTop + expansionHeight };

            columnCursor[column] += expansionHeight;
        }

        columnCursor[column] += tileGap;
    }

    //
    // ============================================================
    // DRAG DESTINATION
    // ============================================================
    //

    if (draggingTarget && mouseButtons[0] && targetViewport.contains(cursorPos) && !targetRects.empty()) {
        float bestDistance = std::numeric_limits<float>::max();

        std::size_t bestIndex = dragTargetIndex;

        for (std::size_t index = 0; index < targetRects.size(); ++index) {
            const auto& rect = targetRects[index];

            float dx = cursorPos.x - rect.center().x;

            float dy = cursorPos.y - rect.center().y;

            float distance = dx * dx + dy * dy;

            if (distance < bestDistance) {
                bestDistance = distance;

                bestIndex = index;
            }
        }

        dragTargetIndex = bestIndex;
    }

    //
    // ============================================================
    // TARGET ROW DRAWING
    // ============================================================
    //

    dc.ctx->PushAxisAlignedClip(targetViewport.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    for (std::size_t index = 0; index < orderedTargets.size(); ++index) {
        TargetBinding* target = orderedTargets[index];

        if (target == nullptr || target->enabled == nullptr) {
            continue;
        }

        d2d::Rect tileRect = targetRects[index];

        bool isDraggingThis = draggingTarget && draggingTargetId == target->id;

        if (tileRect.bottom < targetViewport.top || tileRect.top > targetViewport.bottom) {
            continue;
        }

        if (isDraggingThis) {
            dc.fillRoundedRectangle(tileRect, d2d::Color::RGB(0x12, 0x12, 0x12), 8.0f * scale);

            dc.drawRoundedRectangle(tileRect, d2d::Color::RGB(0x35, 0x35, 0x35), 8.0f * scale, 1.0f * scale);

            continue;
        }

        bool tileHovered = isActive() && targetViewport.contains(cursorPos) && shouldSelect(tileRect, cursorPos);

        float& hoverAnim = targetHoverAnim[target->id];

        float hoverTarget = tileHovered ? 1.0f : 0.0f;

        float hoverBlend = std::clamp(Latite::getRenderer().getDeltaTime() * 12.0f, 0.0f, 1.0f);

        hoverAnim = std::lerp(hoverAnim, hoverTarget, hoverBlend);

        int baseShade = static_cast<int>(std::lround(0x17 + hoverAnim * 0x10));

        dc.fillRoundedRectangle(tileRect, d2d::Color::RGB(baseShade, baseShade, baseShade), 8.0f * scale);

        //
        // Hover bloom.
        //
        // Two soft borders give the card the little lifted/bloomed
        // appearance without turning it into a bright neon outline.
        //

        if (hoverAnim > 0.01f) {
            d2d::Rect outerBloomRect = { tileRect.left - 1.0f * scale, tileRect.top - 1.0f * scale,
                                         tileRect.right + 1.0f * scale, tileRect.bottom + 1.0f * scale };

            dc.drawRoundedRectangle(outerBloomRect, d2d::Color::RGB(0x4B, 0x86, 0xBA).asAlpha(hoverAnim * 0.16f),
                                    9.0f * scale, 2.5f * scale);

            dc.drawRoundedRectangle(tileRect, d2d::Color::RGB(0x62, 0x92, 0xBC).asAlpha(hoverAnim * 0.42f),
                                    8.0f * scale, 1.5f * scale);
        }

        dc.drawRoundedRectangle(tileRect, d2d::Color::RGB(0x45, 0x45, 0x45).asAlpha(0.70f), 8.0f * scale, 1.0f * scale);

        //
        // Destination highlight.
        //

        if (draggingTarget && index == dragTargetIndex) {
            dc.drawRoundedRectangle(tileRect, d2d::Color::RGB(0x62, 0x92, 0xBC), 8.0f * scale, 2.0f * scale);
        }

        //
        // --------------------------------------------------------
        // CHEVRON
        // --------------------------------------------------------
        //

        d2d::Rect chevronRect = { tileRect.left + 3.0f * scale,

                                  tileRect.top,

                                  tileRect.left + 27.0f * scale,

                                  tileRect.bottom };

        bool chevronHovered =
            oresAvailable && targetViewport.contains(cursorPos) && shouldSelect(chevronRect, cursorPos);

        if (chevronHovered) {
            cursor = Cursor::Hand;
        }

        bool expanded = expandedTargetId == target->id;

        dc.drawText(chevronRect, expanded ? L"\u25BE" : L"\u25B8",
                    chevronHovered ? d2d::Colors::WHITE : d2d::Color::RGB(0xA0, 0xA0, 0xA0),
                    Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        //
        // --------------------------------------------------------
        // SWITCH
        // --------------------------------------------------------
        //

        float switchWidth = 44.0f * scale;

        float switchHeight = 20.0f * scale;

        d2d::Rect switchRect = { tileRect.right - switchWidth - 7.0f * scale,

                                 tileRect.center().y - switchHeight * 0.5f,

                                 tileRect.right - 7.0f * scale,

                                 tileRect.center().y + switchHeight * 0.5f };

        bool switchHovered = oresAvailable && targetViewport.contains(cursorPos) && shouldSelect(switchRect, cursorPos);

        if (switchHovered) {
            cursor = Cursor::Hand;
        }

        if (Nexus::UI::drawSwitch(dc, switchRect, *target->enabled, switchHovered, oresAvailable && justClicked[0],
                                  scale)) {
            playClickSound();

            NexusConfig::save();
        }

        //
        // --------------------------------------------------------
        // NAME
        // --------------------------------------------------------
        //

        d2d::Rect nameRect = { chevronRect.right + 2.0f * scale,

                               tileRect.top,

                               switchRect.left - 5.0f * scale,

                               tileRect.bottom };

        dc.drawText(nameRect, target->name,
                    oresAvailable ? d2d::Color::RGB(0xE0, 0xE0, 0xE0) : d2d::Color::RGB(0x68, 0x68, 0x68),
                    Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

//
        // --------------------------------------------------------
        // WHOLE CARD INTERACTION
        // --------------------------------------------------------
        //
        // Switch:
        //     toggles target
        //
        // Anywhere else on the card:
        //
        //     quick click
        //         -> expand / collapse
        //
        //     click + hold
        //         -> drag
        //

        bool cardBodyHovered = oresAvailable && tileHovered && !switchHovered;

        if (cardBodyHovered) {
            cursor = Cursor::Hand;
        }

        if (cardBodyHovered && justClicked[0] && !targetDragPending && !draggingTarget) {
            targetDragPending = true;

            draggingTargetId = target->id;

            draggingTargetOriginalIndex = index;

            dragTargetIndex = index;

            targetPressTime = std::chrono::steady_clock::now();

            dragPressX = cursorPos.x;

            dragPressY = cursorPos.y;

            dragOffsetX = cursorPos.x - tileRect.left;

            dragOffsetY = cursorPos.y - tileRect.top;
        }

        //
        // --------------------------------------------------------
        // EXPANDED TARGET SETTINGS
        // --------------------------------------------------------
        //

        if (expanded && index < expansionRects.size()) {
            d2d::Rect expandedRect = expansionRects[index];

            if (expandedRect.bottom >= targetViewport.top && expandedRect.top <= targetViewport.bottom) {
                dc.fillRoundedRectangle(expandedRect, d2d::Color::RGB(0x13, 0x13, 0x13), 8.0f * scale);

                dc.drawRoundedRectangle(expandedRect, d2d::Color::RGB(0x42, 0x78, 0xA8).asAlpha(0.65f), 8.0f * scale,
                                        1.5f * scale);

                std::size_t colorIndex = static_cast<std::size_t>(target->colorTarget);

                XRayColor& color = xRaySettings.oreColors[colorIndex];

                //
                // Color preview.
                //

                d2d::Rect colorLabelRect = { expandedRect.left + 8.0f * scale,

                                             expandedRect.top + 5.0f * scale,

                                             expandedRect.right - 48.0f * scale,

                                             expandedRect.top + 27.0f * scale };

                dc.drawText(colorLabelRect, L"Color",
                            oresAvailable ? d2d::Color::RGB(0xC8, 0xC8, 0xC8) : d2d::Color::RGB(0x60, 0x60, 0x60),
                            Renderer::FontSelection::PrimaryRegular, 10.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                d2d::Rect swatchRect = { expandedRect.right - 36.0f * scale,

                                         expandedRect.top + 7.0f * scale,

                                         expandedRect.right - 8.0f * scale,

                                         expandedRect.top + 25.0f * scale };

                dc.fillRoundedRectangle(swatchRect,
                                        d2d::Color::RGB(std::clamp(color.r, 0, 255), std::clamp(color.g, 0, 255),
                                                        std::clamp(color.b, 0, 255)),
                                        5.0f * scale);

                dc.drawRoundedRectangle(swatchRect, d2d::Color::RGB(0x9A, 0x9A, 0x9A), 5.0f * scale, 1.0f * scale);

                float miniTop = expandedRect.top + 30.0f * scale;

                float miniHeight = 22.0f * scale;

                d2d::Rect redRect = { expandedRect.left + 5.0f * scale, miniTop, expandedRect.right - 5.0f * scale,
                                      miniTop + miniHeight };

                d2d::Rect greenRect = { redRect.left, redRect.bottom, redRect.right, redRect.bottom + miniHeight };

                d2d::Rect blueRect = { greenRect.left, greenRect.bottom, greenRect.right,
                                       greenRect.bottom + miniHeight };

                drawMiniSlider(redRect, L"R", color.r, 0, 255, oresAvailable);

                drawMiniSlider(greenRect, L"G", color.g, 0, 255, oresAvailable);

                drawMiniSlider(blueRect, L"B", color.b, 0, 255, oresAvailable);
            }
        }
    }

    //
    // ============================================================
    // FLOATING DRAGGED TARGET
    // ============================================================
    //

    if (draggingTarget && !draggingTargetId.empty()) {
        TargetBinding* dragged = findBinding(draggingTargetId);

        if (dragged != nullptr) {
            float floatingLeft = cursorPos.x - dragOffsetX;

            float floatingTop = cursorPos.y - dragOffsetY;

            floatingLeft = std::clamp(floatingLeft, targetViewport.left + 4.0f * scale,
                                      targetViewport.right - scrollbarSpace - targetColumnWidth);

            floatingTop = std::clamp(floatingTop, targetViewport.top, targetViewport.bottom - tileHeight);

            d2d::Rect floatingRect = { floatingLeft, floatingTop, floatingLeft + targetColumnWidth,
                                       floatingTop + tileHeight };

            dc.fillRoundedRectangle(floatingRect, d2d::Color::RGB(0x2C, 0x2C, 0x2C), 9.0f * scale);

            dc.drawRoundedRectangle(floatingRect, d2d::Color::RGB(0x62, 0x92, 0xBC), 9.0f * scale, 2.0f * scale);

            d2d::Rect floatingChevron = { floatingRect.left + 3.0f * scale, floatingRect.top,
                                          floatingRect.left + 27.0f * scale, floatingRect.bottom };

            dc.drawText(floatingChevron, L"\u25B8", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                        12.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            float floatingSwitchWidth = 44.0f * scale;

            float floatingSwitchHeight = 20.0f * scale;

            d2d::Rect floatingSwitch = { floatingRect.right - floatingSwitchWidth - 7.0f * scale,

                                         floatingRect.center().y - floatingSwitchHeight * 0.5f,

                                         floatingRect.right - 7.0f * scale,

                                         floatingRect.center().y + floatingSwitchHeight * 0.5f };

            d2d::Rect floatingName = { floatingChevron.right + 2.0f * scale,

                                       floatingRect.top,

                                       floatingSwitch.left - 5.0f * scale,

                                       floatingRect.bottom };

            dc.drawText(floatingName, dragged->name, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                        12.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            bool drawOnlyValue = *dragged->enabled;

            Nexus::UI::drawSwitch(dc, floatingSwitch, drawOnlyValue, false, false, scale);
        }
    }

    dc.ctx->PopAxisAlignedClip();

    //
    // ============================================================
    // TARGET SCROLLBAR
    // ============================================================
    //

    if (targetScrollMax > 0.0f) {
        float trackWidth = 4.0f * scale;

        d2d::Rect trackRect = { targetViewport.right - 7.0f * scale,

                                targetViewport.top + 5.0f * scale,

                                targetViewport.right - 3.0f * scale,

                                targetViewport.bottom - 5.0f * scale };

        dc.fillRoundedRectangle(trackRect, d2d::Color::RGB(0x2D, 0x2D, 0x2D), trackWidth * 0.5f);

        float visibleRatio = targetViewport.getHeight() / contentHeight;

        float thumbHeight = std::max(28.0f * scale, trackRect.getHeight() * visibleRatio);

        thumbHeight = std::min(thumbHeight, trackRect.getHeight());

        float availableTrack = trackRect.getHeight() - thumbHeight;

        float scrollPercent = targetScrollMax > 0.0f ? targetLerpScroll / targetScrollMax : 0.0f;

        float thumbTop = trackRect.top + availableTrack * scrollPercent;

        d2d::Rect thumbRect = { trackRect.left, thumbTop, trackRect.right, thumbTop + thumbHeight };

        dc.fillRoundedRectangle(thumbRect, d2d::Color::RGB(0x72, 0x72, 0x72), trackWidth * 0.5f);
    }

    //
    // ============================================================
    // GLOBAL ORE APPEARANCE
    // ============================================================
    //

    float oreAppearanceTop = targetViewport.bottom + 9.0f * scale;

    d2d::Rect oreAppearanceHeader = { leftInner, oreAppearanceTop, leftInnerRight, oreAppearanceTop + 20.0f * scale };

    drawSectionLabel(oreAppearanceHeader, L"GLOBAL APPEARANCE", oresAvailable);

    float oreAppearanceY = oreAppearanceHeader.bottom + 3.0f * scale;

    auto nextOreAppearanceRow = [&]() {
        d2d::Rect rect = { leftInner, oreAppearanceY, leftInnerRight, oreAppearanceY + rowHeight };

        oreAppearanceY += rowHeight + rowGap;

        return rect;
    };

    drawSlider(nextOreAppearanceRow(), L"Range", xRaySettings.oreRange, 16, 128, 8, false, oresAvailable);

    drawSlider(nextOreAppearanceRow(), L"Opacity", xRaySettings.oreOpacity, 5, 100, 5, true, oresAvailable);

    drawSlider(nextOreAppearanceRow(), L"Brightness", xRaySettings.oreBrightness, 10, 150, 5, true, oresAvailable);

    drawToggle(nextOreAppearanceRow(), L"Outline", xRaySettings.oreOutline, oresAvailable);

    drawToggle(nextOreAppearanceRow(), L"Transparent Fill", xRaySettings.oreFill, oresAvailable);

    //
    // ============================================================
    // CAVE CARD
    // ============================================================
    //

    float rightInner = caveCard.left + cardPadding;

    float rightInnerRight = caveCard.right - cardPadding;

    d2d::Rect caveTitleRect = { rightInner, caveCard.top + 7.0f * scale, rightInnerRight,
                                caveCard.top + 29.0f * scale };

    dc.drawText(caveTitleRect, L"CAVE ESP",
                xrayAvailable ? d2d::Color::RGB(0xB8, 0xB8, 0xB8) : d2d::Color::RGB(0x60, 0x60, 0x60),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float rightY = caveTitleRect.bottom + 4.0f * scale;

    auto nextRightRow = [&]() {
        d2d::Rect rect = { rightInner, rightY, rightInnerRight, rightY + rowHeight };

        rightY += rowHeight + rowGap;

        return rect;
    };

    drawToggle(nextRightRow(), L"Cave ESP", xRaySettings.caveESP, xrayAvailable);

    rightY += 6.0f * scale;

    d2d::Rect detectionHeader = { rightInner, rightY, rightInnerRight, rightY + 19.0f * scale };

    drawSectionLabel(detectionHeader, L"DETECTION", cavesAvailable);

    rightY = detectionHeader.bottom + 3.0f * scale;

    drawToggle(nextRightRow(), L"3x3x3 Open-Space Check", xRaySettings.airCheck3x3x3, cavesAvailable);

    drawToggle(nextRightRow(), L"Ignore Surface Openings", xRaySettings.ignoreSurface, cavesAvailable);

    rightY += 6.0f * scale;

    d2d::Rect appearanceHeader = { rightInner, rightY, rightInnerRight, rightY + 19.0f * scale };

    drawSectionLabel(appearanceHeader, L"APPEARANCE", cavesAvailable);

    rightY = appearanceHeader.bottom + 3.0f * scale;

    drawSlider(nextRightRow(), L"Range", xRaySettings.scanRange, 16, 128, 8, false, cavesAvailable);

    drawSlider(nextRightRow(), L"Opacity", xRaySettings.caveOpacity, 5, 100, 5, true, cavesAvailable);

    drawSlider(nextRightRow(), L"Brightness", xRaySettings.caveBrightness, 10, 150, 5, true, cavesAvailable);

    drawSlider(nextRightRow(), L"Outline Opacity", xRaySettings.caveOutlineOpacity, 5, 100, 5, true, cavesAvailable);

    rightY += 6.0f * scale;

    d2d::Rect colorHeader = { rightInner, rightY, rightInnerRight, rightY + 19.0f * scale };

    drawSectionLabel(colorHeader, L"COLOR", cavesAvailable);

    rightY = colorHeader.bottom + 3.0f * scale;

    drawSlider(nextRightRow(), L"Red", xRaySettings.caveColorR, 0, 255, 1, false, cavesAvailable);

    drawSlider(nextRightRow(), L"Green", xRaySettings.caveColorG, 0, 255, 1, false, cavesAvailable);

    drawSlider(nextRightRow(), L"Blue", xRaySettings.caveColorB, 0, 255, 1, false, cavesAvailable);

    rightY += 6.0f * scale;

    d2d::Rect displayHeader = { rightInner, rightY, rightInnerRight, rightY + 19.0f * scale };

    drawSectionLabel(displayHeader, L"DISPLAY", cavesAvailable);

    rightY = displayHeader.bottom + 3.0f * scale;

    drawToggle(nextRightRow(), L"Outline", xRaySettings.caveOutline, cavesAvailable);

    drawToggle(nextRightRow(), L"Transparent Fill", xRaySettings.caveFill, cavesAvailable);

    //
    // ============================================================
    // BACK
    // ============================================================
    //

    d2d::Rect backRect = { panelRect.left + padding,

                           panelRect.bottom - 58.0f * scale,

                           panelRect.left + padding + 130.0f * scale,

                           panelRect.bottom - 18.0f * scale };

    bool hoveringBack = shouldSelect(backRect, cursorPos);

    if (hoveringBack) {
        cursor = Cursor::Hand;
    }

    dc.fillRoundedRectangle(
        backRect, hoveringBack ? d2d::Color::RGB(0x35, 0x35, 0x35) : d2d::Color::RGB(0x20, 0x20, 0x20), 10.0f * scale);

    dc.drawRoundedRectangle(backRect, d2d::Color::RGB(0x55, 0x55, 0x55), 10.0f * scale, 1.0f * scale);

    dc.drawText(backRect, L"< Back", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 16.0f * scale,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // RESET DEFAULTS
    // ============================================================
    //

    d2d::Rect resetRect = { backRect.right + 12.0f * scale,

                            backRect.top,

                            backRect.right + 182.0f * scale,

                            backRect.bottom };

    bool hoveringReset = shouldSelect(resetRect, cursorPos);

    if (hoveringReset) {
        cursor = Cursor::Hand;
    }

    dc.fillRoundedRectangle(resetRect,
                            hoveringReset ? d2d::Color::RGB(0x42, 0x2A, 0x2A) : d2d::Color::RGB(0x28, 0x20, 0x20),
                            10.0f * scale);

    dc.drawRoundedRectangle(resetRect, d2d::Color::RGB(0x70, 0x4A, 0x4A), 10.0f * scale, 1.0f * scale);

    dc.drawText(resetRect, L"Reset Defaults", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                15.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // VERSION
    // ============================================================
    //

    d2d::Rect versionRect = { panelRect.right - 250.0f * scale,

                              panelRect.bottom - 48.0f * scale,

                              panelRect.right - padding,

                              panelRect.bottom - 18.0f * scale };

    dc.drawText(versionRect, L"X-Ray v0.1", d2d::Color::RGB(0x90, 0x90, 0x90), Renderer::FontSelection::PrimaryRegular,
                12.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // BUTTON ACTIONS
    // ============================================================
    //

    if (hoveringReset && justClicked[0]) {
        playClickSound();

        Nexus::xRaySettings = Nexus::XRaySettings {};

        Nexus::NexusConfig::resetXRayTargetOrder();

        expandedTargetId.clear();

        targetScroll = 0.0f;
        targetLerpScroll = 0.0f;

        return;
    }

    if (hoveringBack && justClicked[0]) {
        playClickSound();

        Nexus::NexusNavigation::backToHub();

        return;
    }
}
