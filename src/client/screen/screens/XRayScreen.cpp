#include "pch.h"

#include "XRayScreen.h"
#include "NexusScreen.h"

#include "client/feature/nexus/ui/NexusControls.h"
#include "client/feature/nexus/NexusConfig.h"
#include "client/feature/nexus/xray/XRaySettings.h"
#include "client/feature/nexus/navigation/NexusNavigation.h"

#include "client/event/Eventing.h"
#include "client/event/events/RenderOverlayEvent.h"

#include "client/Latite.h"
#include "client/screen/ScreenManager.h"

#include "util/DrawContext.h"

#include <algorithm>
#include <string>

XRayScreen::XRayScreen() {
    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&XRayScreen::onRender, 1, true);
}

void XRayScreen::onEnable(bool) {
    resetInputState();
}

void XRayScreen::onDisable() {
    Nexus::NexusConfig::save();

    resetInputState();
}

void XRayScreen::onRender(Event&) {
    if (!isActive()) {
        return;
    }

    using namespace Nexus;

    D2DUtil dc;

    auto screenSize = Latite::getRenderer().getScreenSize();
    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;

    cursor = Cursor::Arrow;

    //
    // ============================================================
    // LAYOUT
    // ============================================================
    //

    float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);

    float panelWidth = std::min(screenSize.width * 0.78f, 900.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.90f, 820.0f * scale);

    d2d::Rect panelRect = { (screenSize.width - panelWidth) * 0.5f, (screenSize.height - panelHeight) * 0.5f,
                            (screenSize.width + panelWidth) * 0.5f, (screenSize.height + panelHeight) * 0.5f };

    float padding = 28.0f * scale;

    //
    // ============================================================
    // STABLE PANEL
    // ============================================================
    //
    // Keep this flat.
    //
    // No Gaussian blur.
    // No fullscreen tint.
    // No first-open white/black transition.
    //

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

    //
    // Subtitle
    //

    d2d::Rect subtitleRect = { panelRect.left + padding, panelRect.top + 43.0f * scale, panelRect.right - padding,
                               panelRect.top + 67.0f * scale };

    dc.drawText(subtitleRect, L"Ore ESP and Cave ESP", d2d::Color::RGB(0xA0, 0xA0, 0xA0).asAlpha(0.80f),
                Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // CONTROL HELPERS
    // ============================================================
    //

    auto drawDisabledOverlay = [&](const d2d::Rect& rowRect) {
        dc.fillRoundedRectangle(rowRect, d2d::Color::RGB(0x08, 0x08, 0x08).asAlpha(0.48f), 8.0f * scale);
    };

    //
    // Toggle helper.
    //
    // enabled=false:
    //
    //   - still renders current value
    //   - cannot be clicked
    //   - cannot claim mouse hover
    //   - gets visually dimmed
    //

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

    //
    // Integer slider helper.
    //

    auto drawSlider = [&](const d2d::Rect& rowRect, const std::wstring& label, int& value, int minimum, int maximum,
                          int step, bool percent, bool enabled = true) {
        d2d::Rect interactionRect = { rowRect.left + 150.0f * scale, rowRect.top, rowRect.right - 72.0f * scale,
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

    //
    // ============================================================
    // MASTER X-RAY
    // ============================================================
    //

    float rowHeight = 32.0f * scale;

    float masterTop = panelRect.top + 76.0f * scale;

    d2d::Rect masterRect = { panelRect.left + padding, masterTop, panelRect.right - padding, masterTop + rowHeight };

    drawToggle(masterRect, L"Master X-Ray", xRaySettings.enabled);

    //
    // Parent availability.
    //

    bool xrayAvailable = xRaySettings.enabled;

    bool oresAvailable = xrayAvailable && xRaySettings.oreESP;

    bool cavesAvailable = xrayAvailable && xRaySettings.caveESP;

    //
    // ============================================================
    // TWO-COLUMN LAYOUT
    // ============================================================
    //

    float columnGap = 22.0f * scale;

    float columnTop = masterRect.bottom + 40.0f * scale;

    float usableWidth = panelRect.getWidth() - padding * 2.0f;

    float columnWidth = (usableWidth - columnGap) * 0.5f;

    float leftX = panelRect.left + padding;

    float rightX = leftX + columnWidth + columnGap;

    float rowGap = 5.0f * scale;

    auto leftRow = [&](int index) {
        float top = columnTop + static_cast<float>(index) * (rowHeight + rowGap);

        return d2d::Rect { leftX, top, leftX + columnWidth, top + rowHeight };
    };

    auto rightRow = [&](int index) {
        float top = columnTop + static_cast<float>(index) * (rowHeight + rowGap);

        return d2d::Rect { rightX, top, rightX + columnWidth, top + rowHeight };
    };

    //
    // ============================================================
    // ORES
    // ============================================================
    //

    d2d::Rect oresHeader = { leftX, columnTop - 28.0f * scale, leftX + columnWidth, columnTop - 4.0f * scale };

    dc.drawText(oresHeader, L"ORES",
                xrayAvailable ? d2d::Color::RGB(0xB8, 0xB8, 0xB8) : d2d::Color::RGB(0x62, 0x62, 0x62),
                Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // Ore parent.
    //

    drawToggle(leftRow(0), L"Ore ESP", xRaySettings.oreESP, xrayAvailable);

    //
    // Individual ores.
    //
    // These retain their values while Ore ESP is off.
    // They simply become temporarily non-interactive.
    //

    drawToggle(leftRow(1), L"Diamond", xRaySettings.diamond, oresAvailable);

    drawToggle(leftRow(2), L"Ancient Debris", xRaySettings.ancientDebris, oresAvailable);

    drawToggle(leftRow(3), L"Emerald", xRaySettings.emerald, oresAvailable);

    drawToggle(leftRow(4), L"Gold", xRaySettings.gold, oresAvailable);

    drawToggle(leftRow(5), L"Iron", xRaySettings.iron, oresAvailable);

    drawToggle(leftRow(6), L"Copper", xRaySettings.copper, oresAvailable);

    drawToggle(leftRow(7), L"Coal", xRaySettings.coal, oresAvailable);

    drawToggle(leftRow(8), L"Lapis", xRaySettings.lapis, oresAvailable);

    drawToggle(leftRow(9), L"Redstone", xRaySettings.redstone, oresAvailable);

    //
    // ============================================================
    // ORE APPEARANCE
    // ============================================================
    //

    drawSlider(leftRow(10), L"Ore Range", xRaySettings.oreRange, 16, 128, 8, false, oresAvailable);

    drawSlider(leftRow(11), L"Ore Opacity", xRaySettings.oreOpacity, 5, 100, 5, true, oresAvailable);

    drawSlider(leftRow(12), L"Ore Brightness", xRaySettings.oreBrightness, 10, 150, 5, true, oresAvailable);

    drawToggle(leftRow(13), L"Ore Outline", xRaySettings.oreOutline, oresAvailable);

    drawToggle(leftRow(14), L"Ore Fill", xRaySettings.oreFill, oresAvailable);

    //
    // ============================================================
    // CAVE ESP
    // ============================================================
    //

    d2d::Rect caveHeader = { rightX, columnTop - 28.0f * scale, rightX + columnWidth, columnTop - 4.0f * scale };

    dc.drawText(caveHeader, L"CAVE ESP",
                xrayAvailable ? d2d::Color::RGB(0xB8, 0xB8, 0xB8) : d2d::Color::RGB(0x62, 0x62, 0x62),
                Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // Cave parent.
    //

    drawToggle(rightRow(0), L"Cave ESP", xRaySettings.caveESP, xrayAvailable);

    //
    // Cave classification.
    //

    drawToggle(rightRow(1), L"3x3x3 Open-Space Check", xRaySettings.airCheck3x3x3, cavesAvailable);

    drawToggle(rightRow(2), L"Ignore Surface Openings", xRaySettings.ignoreSurface, cavesAvailable);

    //
    // ============================================================
    // APPEARANCE EDITOR
    // ============================================================
    //

    drawSlider(rightRow(3), L"Cave Range", xRaySettings.scanRange, 16, 128, 8, false, cavesAvailable);

    drawSlider(rightRow(4), L"Cave Opacity", xRaySettings.caveOpacity, 5, 100, 5, true, cavesAvailable);

    drawSlider(rightRow(5), L"Brightness", xRaySettings.caveBrightness, 10, 150, 5, true, cavesAvailable);

    drawSlider(rightRow(6), L"Outline Opacity", xRaySettings.caveOutlineOpacity, 5, 100, 5, true, cavesAvailable);

    drawSlider(rightRow(7), L"Red", xRaySettings.caveColorR, 0, 255, 1, false, cavesAvailable);

    drawSlider(rightRow(8), L"Green", xRaySettings.caveColorG, 0, 255, 1, false, cavesAvailable);

    drawSlider(rightRow(9), L"Blue", xRaySettings.caveColorB, 0, 255, 1, false, cavesAvailable);

    //
    // ============================================================
    // DISPLAY
    // ============================================================
    //

    d2d::Rect displayHeader = { rightX, rightRow(10).top, rightX + columnWidth, rightRow(10).bottom };

    dc.drawText(displayHeader, L"DISPLAY",
                cavesAvailable ? d2d::Color::RGB(0xB8, 0xB8, 0xB8) : d2d::Color::RGB(0x62, 0x62, 0x62),
                Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    drawToggle(rightRow(11), L"Cave Outline", xRaySettings.caveOutline, cavesAvailable);

    drawToggle(rightRow(12), L"Cave Fill", xRaySettings.caveFill, cavesAvailable);

    //
    // ============================================================
    // BACK BUTTON
    // ============================================================
    //

    d2d::Rect backRect = { panelRect.left + padding, panelRect.bottom - 58.0f * scale,
                           panelRect.left + padding + 130.0f * scale, panelRect.bottom - 18.0f * scale };

    bool hoveringBack = shouldSelect(backRect, cursorPos);

    if (hoveringBack) {
        cursor = Cursor::Hand;
    }

    d2d::Color backColor = hoveringBack ? d2d::Color::RGB(0x35, 0x35, 0x35) : d2d::Color::RGB(0x20, 0x20, 0x20);

    dc.fillRoundedRectangle(backRect, backColor, 10.0f * scale);

    dc.drawRoundedRectangle(backRect, d2d::Color::RGB(0x55, 0x55, 0x55), 10.0f * scale, 1.0f * scale);

    dc.drawText(backRect, L"< Back", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 16.0f * scale,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // RESET DEFAULTS BUTTON
    // ============================================================
    //

    d2d::Rect resetRect = { backRect.right + 12.0f * scale, backRect.top, backRect.right + 182.0f * scale,
                            backRect.bottom };

    bool hoveringReset = shouldSelect(resetRect, cursorPos);

    if (hoveringReset) {
        cursor = Cursor::Hand;
    }

    d2d::Color resetColor = hoveringReset ? d2d::Color::RGB(0x42, 0x2A, 0x2A) : d2d::Color::RGB(0x28, 0x20, 0x20);

    dc.fillRoundedRectangle(resetRect, resetColor, 10.0f * scale);

    dc.drawRoundedRectangle(resetRect, d2d::Color::RGB(0x70, 0x4A, 0x4A), 10.0f * scale, 1.0f * scale);

    dc.drawText(resetRect, L"Reset Defaults", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                15.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // VERSION
    // ============================================================
    //

    d2d::Rect versionRect = { panelRect.right - 250.0f * scale, panelRect.bottom - 48.0f * scale,
                              panelRect.right - padding, panelRect.bottom - 18.0f * scale };

    dc.drawText(versionRect, L"X-Ray v0.1", d2d::Color::RGB(0x90, 0x90, 0x90), Renderer::FontSelection::PrimaryRegular,
                12.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // BUTTON ACTIONS
    // ============================================================
    //

    if (hoveringReset && justClicked[0]) {
        playClickSound();

        //
        // Reset the entire X-Ray configuration to the defaults
        // defined in XRaySettings.h.
        //
        Nexus::xRaySettings = Nexus::XRaySettings {};

        //
        // Save immediately so the reset persists even if the client
        // closes before leaving this screen.
        //
        Nexus::NexusConfig::save();

        return;
    }

    if (hoveringBack && justClicked[0]) {
        playClickSound();

        Nexus::NexusNavigation::backToHub();

        return;
    }
}
