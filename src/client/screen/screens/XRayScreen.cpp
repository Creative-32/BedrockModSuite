#include "pch.h"

#include "XRayScreen.h"
#include "NexusScreen.h"
#include "client/feature/nexus/ui/NexusControls.h"

#include "client/event/Eventing.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/Latite.h"
#include "client/screen/ScreenManager.h"
#include "client/feature/nexus/NexusConfig.h"
#include "client/feature/nexus/xray/XRaySettings.h"
#include "client/feature/nexus/navigation/NexusNavigation.h"

#include "util/DrawContext.h"

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
    if (!isActive()) return;

    using namespace Nexus;

    D2DUtil dc;

    auto screenSize = Latite::getRenderer().getScreenSize();

    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;

    cursor = Cursor::Arrow;

    float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);

    float panelWidth = std::min(screenSize.width * 0.78f, 900.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.76f, 620.0f * scale);

    d2d::Rect panelRect = { (screenSize.width - panelWidth) * 0.5f, (screenSize.height - panelHeight) * 0.5f,
                            (screenSize.width + panelWidth) * 0.5f, (screenSize.height + panelHeight) * 0.5f };

    //
    // Stable background.
    // Deliberately no Gaussian blur or fullscreen tint.
    //

    dc.fillRoundedRectangle(panelRect, d2d::Color::RGB(0x0B, 0x0B, 0x0B).asAlpha(0.94f), 18.0f * scale);

    dc.drawRoundedRectangle(panelRect, d2d::Color::RGB(0x45, 0x45, 0x45).asAlpha(0.75f), 18.0f * scale, 1.5f * scale);

    float padding = 28.0f * scale;

    //
    // TITLE
    //

    d2d::Rect titleRect = { panelRect.left + padding, panelRect.top + 14.0f * scale, panelRect.right - padding,
                            panelRect.top + 58.0f * scale };

    dc.drawText(titleRect, L"X-Ray", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryLight, 28.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // Reusable toggle row
    //

    auto drawToggle = [&](const d2d::Rect& rowRect, const std::wstring& label, bool& value) {
        bool hovering = shouldSelect(rowRect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        if (Nexus::UI::drawToggle(dc, rowRect, label, value, hovering, justClicked[0], scale)) {
            playClickSound();
        }
    };

    //
    // Reusable integer slider
    //

    auto drawSlider = [&](const d2d::Rect& rowRect, const std::wstring& label, int& value, int minimum, int maximum,
                          int step, bool percent) {
        d2d::Rect interactionRect = { rowRect.left + 150.0f * scale, rowRect.top, rowRect.right - 72.0f * scale,
                                      rowRect.bottom };

        bool hovering = shouldSelect(interactionRect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        Nexus::UI::drawSlider(dc, rowRect, label, value, minimum, maximum, step, percent, hovering, justClicked[0],
                              mouseButtons[0], cursorPos.x, scale);
    };

    //
    // MASTER
    //

    float masterTop = panelRect.top + 70.0f * scale;

    float rowHeight = 32.0f * scale;

    d2d::Rect masterRect = { panelRect.left + padding, masterTop, panelRect.right - padding, masterTop + rowHeight };

    drawToggle(masterRect, L"Master X-Ray", xRaySettings.enabled);

    //
    // TWO-COLUMN LAYOUT
    //

    float columnGap = 22.0f * scale;

    float columnTop = masterRect.bottom + 40.0f * scale;

    float usableWidth = panelRect.getWidth() - padding * 2.0f;

    float columnWidth = (usableWidth - columnGap) * 0.5f;

    float leftX = panelRect.left + padding;

    float rightX = leftX + columnWidth + columnGap;

    //
    // ORES
    //

    d2d::Rect oresHeader = { leftX, columnTop - 26.0f * scale, leftX + columnWidth, columnTop };

    dc.drawText(oresHeader, L"ORES", d2d::Color::RGB(0xA8, 0xA8, 0xA8), Renderer::FontSelection::PrimaryRegular,
                13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float rowGap = 5.0f * scale;

    auto leftRow = [&](int index) {
        float top = columnTop + index * (rowHeight + rowGap);

        return d2d::Rect { leftX, top, leftX + columnWidth, top + rowHeight };
    };

    drawToggle(leftRow(0), L"Ore ESP", xRaySettings.oreESP);

    drawToggle(leftRow(1), L"Diamond", xRaySettings.diamond);

    drawToggle(leftRow(2), L"Ancient Debris", xRaySettings.ancientDebris);

    drawToggle(leftRow(3), L"Emerald", xRaySettings.emerald);

    drawToggle(leftRow(4), L"Gold", xRaySettings.gold);

    drawToggle(leftRow(5), L"Iron", xRaySettings.iron);

    drawToggle(leftRow(6), L"Copper", xRaySettings.copper);

    drawToggle(leftRow(7), L"Coal", xRaySettings.coal);

    drawToggle(leftRow(8), L"Lapis", xRaySettings.lapis);

    drawToggle(leftRow(9), L"Redstone", xRaySettings.redstone);

    //
    // CAVE / DISPLAY
    //

    d2d::Rect caveHeader = { rightX, columnTop - 26.0f * scale, rightX + columnWidth, columnTop };

    dc.drawText(caveHeader, L"CAVE / PATH ESP", d2d::Color::RGB(0xA8, 0xA8, 0xA8),
                Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    auto rightRow = [&](int index) {
        float top = columnTop + index * (rowHeight + rowGap);

        return d2d::Rect { rightX, top, rightX + columnWidth, top + rowHeight };
    };

    drawToggle(rightRow(0), L"Cave / Path ESP", xRaySettings.caveESP);

    drawToggle(rightRow(1), L"3x3x3 Air Check", xRaySettings.airCheck3x3x3);

    drawToggle(rightRow(2), L"Ignore Surface", xRaySettings.ignoreSurface);

    drawSlider(rightRow(3), L"Scan Range", xRaySettings.scanRange, 16, 128, 8, false);

    drawSlider(rightRow(4), L"Cave Opacity", xRaySettings.caveOpacity, 5, 100, 5, true);

    d2d::Rect displayHeader = { rightX, rightRow(5).top, rightX + columnWidth, rightRow(5).bottom };

    dc.drawText(displayHeader, L"DISPLAY", d2d::Color::RGB(0xA8, 0xA8, 0xA8), Renderer::FontSelection::PrimaryRegular,
                13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    drawToggle(rightRow(6), L"Outline", xRaySettings.outline);

    drawToggle(rightRow(7), L"Transparent Fill", xRaySettings.fill);

    //
    // BACK BUTTON
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
    // VERSION
    //

    d2d::Rect versionRect = { panelRect.right - 250.0f * scale, panelRect.bottom - 48.0f * scale,
                              panelRect.right - padding, panelRect.bottom - 18.0f * scale };

    dc.drawText(versionRect, L"X-Ray v0.1", d2d::Color::RGB(0x90, 0x90, 0x90), Renderer::FontSelection::PrimaryRegular,
                12.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (hoveringBack && justClicked[0]) {
        playClickSound();

        Nexus::NexusNavigation::backToHub();

        return;
    }
}
