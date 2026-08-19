#include "pch.h"
#include "XRayScreen.h"

#include "ModSuiteScreen.h"

#include "client/event/Eventing.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/Latite.h"
#include "client/screen/ScreenManager.h"
#include "util/DrawContext.h"

XRayScreen::XRayScreen() {
    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&XRayScreen::onRender, 1, true);
}

void XRayScreen::onEnable(bool) {
    resetInputState();
}

void XRayScreen::onDisable() {
    resetInputState();
}

void XRayScreen::onRender(Event&) {
    if (!isActive()) return;

    D2DUtil dc;

    auto screenSize = Latite::getRenderer().getScreenSize();

    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;

    float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);

    float panelWidth = std::min(screenSize.width * 0.78f, 900.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.76f, 620.0f * scale);

    d2d::Rect panelRect = { (screenSize.width - panelWidth) * 0.5f, (screenSize.height - panelHeight) * 0.5f,
                            (screenSize.width + panelWidth) * 0.5f, (screenSize.height + panelHeight) * 0.5f };

    //
    // Stable background.
    // No blur or full-screen tint.
    //

    dc.fillRoundedRectangle(panelRect, d2d::Color::RGB(0x0B, 0x0B, 0x0B).asAlpha(0.92f), 18.0f * scale);

    dc.drawRoundedRectangle(panelRect, d2d::Color::RGB(0x45, 0x45, 0x45).asAlpha(0.75f), 18.0f * scale, 1.5f * scale);

    float padding = 28.0f * scale;

    //
    // Title
    //

    d2d::Rect titleRect = { panelRect.left + padding, panelRect.top + 16.0f * scale, panelRect.right - padding,
                            panelRect.top + 58.0f * scale };

    dc.drawText(titleRect, L"X-Ray", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryLight, 28.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // Temporary settings preview
    //

    d2d::Rect previewRect = { panelRect.left + padding, panelRect.top + 95.0f * scale, panelRect.right - padding,
                              panelRect.bottom - 85.0f * scale };

    dc.drawText(previewRect,
                L"Ore ESP\n\n"
                L"Diamond            [ ON ]\n"
                L"Ancient Debris     [ ON ]\n"
                L"Emerald            [ ON ]\n"
                L"Gold               [ ON ]\n"
                L"Iron               [ OFF ]\n\n"
                L"Cave / Path ESP    [ ON ]\n"
                L"3x3x3 Air Check    [ ON ]\n"
                L"Ignore Surface     [ ON ]\n"
                L"Range              64\n"
                L"Opacity            25%",
                d2d::Color::RGB(0xD0, 0xD0, 0xD0), Renderer::FontSelection::PrimaryRegular, 17.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    //
    // Back button
    //

    d2d::Rect backRect = { panelRect.left + padding, panelRect.bottom - 62.0f * scale,
                           panelRect.left + padding + 130.0f * scale, panelRect.bottom - 20.0f * scale };

    bool hoveringBack = shouldSelect(backRect, cursorPos);

    d2d::Color backColor = hoveringBack ? d2d::Color::RGB(0x35, 0x35, 0x35) : d2d::Color::RGB(0x20, 0x20, 0x20);

    dc.fillRoundedRectangle(backRect, backColor, 10.0f * scale);

    dc.drawRoundedRectangle(backRect, d2d::Color::RGB(0x55, 0x55, 0x55), 10.0f * scale, 1.0f * scale);

    dc.drawText(backRect, L"< Back", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 16.0f * scale,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (hoveringBack && justClicked[0]) {
        playClickSound();

        Latite::getScreenManager().showScreen<ModSuiteScreen>();
        return;
    }
}
