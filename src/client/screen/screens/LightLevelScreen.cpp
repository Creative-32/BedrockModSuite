#include "pch.h"

#include "LightLevelScreen.h"
#include "NexusScreen.h"

#include "client/feature/nexus/NexusConfig.h"
#include "client/feature/nexus/lightlevel/LightLevelColor.h"
#include "client/feature/nexus/lightlevel/LightLevelSettings.h"
#include "client/feature/nexus/navigation/NexusNavigation.h"
#include "client/feature/nexus/ui/NexusControls.h"

#include "client/event/Eventing.h"
#include "client/event/events/RenderOverlayEvent.h"

#include "client/Latite.h"

#include "util/DrawContext.h"

#include <algorithm>
#include <cmath>
#include <string_view>

LightLevelScreen::LightLevelScreen() {
    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&LightLevelScreen::onRender, 1, true);
}

void LightLevelScreen::onEnable(bool) {
    activeSliderId.clear();
    sliderDirty = false;

    resetInputState();
}

void LightLevelScreen::onDisable() {
    Nexus::NexusConfig::save();

    activeSliderId.clear();
    sliderDirty = false;

    resetInputState();
}

void LightLevelScreen::onRender(Event&) {
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

    float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);

    //
    // ============================================================
    // FINISH SLIDER DRAG
    // ============================================================
    //

    if (!mouseButtons[0]) {
        if (sliderDirty) {
            NexusConfig::save();

            sliderDirty = false;
        }

        activeSliderId.clear();
    }

    //
    // ============================================================
    // PANEL
    // ============================================================
    //

    float panelWidth = std::min(screenSize.width * 0.72f, 780.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.82f, 690.0f * scale);

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

    dc.drawText(titleRect, L"Light Level", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryLight, 28.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect subtitleRect = { panelRect.left + padding, panelRect.top + 43.0f * scale, panelRect.right - padding,
                               panelRect.top + 67.0f * scale };

    dc.drawText(subtitleRect, L"Visualize nearby floor lighting", d2d::Color::RGB(0xA0, 0xA0, 0xA0).asAlpha(0.80f),
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
            NexusConfig::save();

            playClickSound();
        }

        return changed;
    };

    auto drawSlider = [&](std::string_view sliderId, const d2d::Rect& rowRect, const std::wstring& label, int& value,
                          int minimum, int maximum, int step, bool percent, bool enabled = true) {
        d2d::Rect interactionRect = { rowRect.left + 108.0f * scale,

                                      rowRect.top - 6.0f * scale,

                                      rowRect.right - 28.0f * scale,

                                      rowRect.bottom + 6.0f * scale };

        bool hovering = enabled && shouldSelect(interactionRect, cursorPos);

        if (enabled && hovering && justClicked[0]) {
            activeSliderId.assign(sliderId);
        }

        bool draggingThis = enabled && mouseButtons[0] && activeSliderId == sliderId;

        if (hovering || draggingThis) {
            cursor = Cursor::Hand;
        }

        int oldValue = value;

        Nexus::UI::drawSlider(dc, rowRect, label, value, minimum, maximum, step, percent,

                              hovering || draggingThis,

                              enabled && hovering && justClicked[0],

                              draggingThis,

                              cursorPos.x, scale);

        if (value != oldValue) {
            sliderDirty = true;
        }

        if (!enabled) {
            if (activeSliderId == sliderId) {
                activeSliderId.clear();
            }

            drawDisabledOverlay(rowRect);
        }
    };

    auto drawSectionLabel = [&](const d2d::Rect& rect, const std::wstring& label, bool enabled) {
        dc.drawText(rect, label,

                    enabled ? d2d::Color::RGB(0xA0, 0xA0, 0xA0) : d2d::Color::RGB(0x58, 0x58, 0x58),

                    Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    //
    // ============================================================
    // MASTER
    // ============================================================
    //

    float rowHeight = 32.0f * scale;

    float rowGap = 6.0f * scale;

    float contentLeft = panelRect.left + padding;

    float contentRight = panelRect.right - padding;

    float currentY = panelRect.top + 80.0f * scale;

    d2d::Rect masterRect = { contentLeft, currentY, contentRight, currentY + rowHeight };

    drawToggle(masterRect, L"Light Level Overlay", lightLevelSettings.enabled);

    bool controlsAvailable = lightLevelSettings.enabled;

    currentY = masterRect.bottom + 16.0f * scale;

    //
    // ============================================================
    // APPEARANCE
    // ============================================================
    //

    d2d::Rect appearanceHeader = { contentLeft, currentY, contentRight, currentY + 20.0f * scale };

    drawSectionLabel(appearanceHeader, L"APPEARANCE", controlsAvailable);

    currentY = appearanceHeader.bottom + 4.0f * scale;

    auto nextRow = [&]() {
        d2d::Rect rect = { contentLeft, currentY, contentRight, currentY + rowHeight };

        currentY += rowHeight + rowGap;

        return rect;
    };

    drawSlider("light.range", nextRow(), L"Range", lightLevelSettings.range, 8, 64, 4, false, controlsAvailable);

    drawSlider("light.opacity", nextRow(), L"Opacity", lightLevelSettings.opacity, 5, 100, 5, true, controlsAvailable);

    drawSlider("light.brightness", nextRow(), L"Brightness", lightLevelSettings.brightness, 10, 150, 5, true,
               controlsAvailable);

    drawToggle(nextRow(), L"Transparent Fill", lightLevelSettings.fill, controlsAvailable);

    drawToggle(nextRow(), L"Outline", lightLevelSettings.outline, controlsAvailable);

    drawToggle(nextRow(), L"Distance Fade", lightLevelSettings.distanceFade, controlsAvailable);

    //
    // ============================================================
    // GRADIENT PREVIEW
    // ============================================================
    //

    currentY += 8.0f * scale;

    d2d::Rect gradientHeader = { contentLeft, currentY, contentRight, currentY + 20.0f * scale };

    drawSectionLabel(gradientHeader, L"LIGHT GRADIENT", controlsAvailable);

    currentY = gradientHeader.bottom + 8.0f * scale;

    float gradientHeight = 28.0f * scale;

    float gradientWidth = contentRight - contentLeft;

    float segmentGap = 1.0f * scale;

    float segmentWidth = (gradientWidth - segmentGap * 15.0f) / 16.0f;

    for (int level = 0; level <= 15; ++level) {
        auto color = getLightLevelColor(level);

        int r = std::clamp(static_cast<int>(std::lround(color.r * 255.0f)), 0, 255);

        int g = std::clamp(static_cast<int>(std::lround(color.g * 255.0f)), 0, 255);

        int b = std::clamp(static_cast<int>(std::lround(color.b * 255.0f)), 0, 255);

        float left = contentLeft + static_cast<float>(level) * (segmentWidth + segmentGap);

        d2d::Rect segmentRect = { left, currentY, left + segmentWidth, currentY + gradientHeight };

        dc.fillRoundedRectangle(segmentRect, d2d::Color::RGB(r, g, b).asAlpha(controlsAvailable ? 0.90f : 0.30f),
                                3.0f * scale);
    }

    currentY += gradientHeight + 3.0f * scale;

    d2d::Rect gradientLabels = { contentLeft, currentY, contentRight, currentY + 24.0f * scale };

    dc.drawText(gradientLabels, L"0",
                controlsAvailable ? d2d::Color::RGB(0xD0, 0xD0, 0xD0) : d2d::Color::RGB(0x60, 0x60, 0x60),
                Renderer::FontSelection::PrimaryRegular, 10.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    dc.drawText(gradientLabels, L"15",
                controlsAvailable ? d2d::Color::RGB(0xD0, 0xD0, 0xD0) : d2d::Color::RGB(0x60, 0x60, 0x60),
                Renderer::FontSelection::PrimaryRegular, 10.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

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

    dc.fillRoundedRectangle(backRect,

                            hoveringBack ? d2d::Color::RGB(0x35, 0x35, 0x35) : d2d::Color::RGB(0x20, 0x20, 0x20),

                            10.0f * scale);

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

    dc.drawText(versionRect, L"Light Level v0.1", d2d::Color::RGB(0x90, 0x90, 0x90),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // BUTTON ACTIONS
    // ============================================================
    //

    if (hoveringReset && justClicked[0]) {
        playClickSound();

        lightLevelSettings = LightLevelSettings {};

        NexusConfig::save();

        activeSliderId.clear();
        sliderDirty = false;

        return;
    }

    if (hoveringBack && justClicked[0]) {
        playClickSound();

        NexusNavigation::backToHub();

        return;
    }
}
