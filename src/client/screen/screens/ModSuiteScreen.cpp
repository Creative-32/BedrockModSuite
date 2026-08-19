#include "pch.h"
#include "ModSuiteScreen.h"

#include "client/event/Eventing.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/Latite.h"
#include "client/screen/ScreenManager.h"
#include "util/DrawContext.h"

#include "XRayScreen.h"

ModSuiteScreen::ModSuiteScreen() {
    // Temporary default.
    // We will make this configurable after the base screen is working.
    this->key = KeyValue('N');

    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&ModSuiteScreen::onRender, 1, true);
}

void ModSuiteScreen::onEnable(bool) {
    resetInputState();
}

void ModSuiteScreen::onDisable() {
    resetInputState();
}

void ModSuiteScreen::onRender(Event&) {
    if (!isActive()) return;

    D2DUtil dc;

    D2D1_SIZE_F screenSize = Latite::getRenderer().getScreenSize();

    float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);

    float panelWidth = std::min(screenSize.width * 0.78f, 900.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.76f, 620.0f * scale);

    d2d::Rect panelRect = { (screenSize.width - panelWidth) * 0.5f, (screenSize.height - panelHeight) * 0.5f,
                            (screenSize.width + panelWidth) * 0.5f, (screenSize.height + panelHeight) * 0.5f };

    //
    // IMPORTANT:
    // NO Gaussian blur and NO fullscreen tint.
    // We're deliberately avoiding the effect that caused
    // the white shading / first-open flicker.
    //

    d2d::Color panelColor = d2d::Color::RGB(0x0B, 0x0B, 0x0B).asAlpha(0.90f);

    d2d::Color panelOutline = d2d::Color::RGB(0x45, 0x45, 0x45).asAlpha(0.75f);

    float panelRadius = 18.0f * scale;

    dc.fillRoundedRectangle(panelRect, panelColor, panelRadius);

    dc.drawRoundedRectangle(panelRect, panelOutline, panelRadius, 1.5f * scale);

    float padding = 28.0f * scale;

    //
    // TITLE
    //

    d2d::Rect titleRect = { panelRect.left + padding, panelRect.top + 16.0f * scale, panelRect.right - padding,
                            panelRect.top + 58.0f * scale };

    dc.drawText(titleRect, L"Bedrock Mod Suite", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryLight,
                28.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    //
    // SUBTITLE
    //

    d2d::Rect subtitleRect = { panelRect.left + padding, panelRect.top + 53.0f * scale, panelRect.right - padding,
                               panelRect.top + 78.0f * scale };

    dc.drawText(subtitleRect, L"Select a feature", d2d::Color::RGB(0xB0, 0xB0, 0xB0).asAlpha(0.80f),
                Renderer::FontSelection::PrimaryRegular, 14.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // FEATURE CARDS
    //

    float cardGap = 18.0f * scale;

    float cardWidth = (panelRect.getWidth() - padding * 2.0f - cardGap) / 2.0f;

    float cardHeight = 92.0f * scale;

    float firstY = panelRect.top + 102.0f * scale;

    auto drawCard = [&](int column, int row, const std::wstring& name) {
        float left = panelRect.left + padding + column * (cardWidth + cardGap);

        float top = firstY + row * (cardHeight + cardGap);

        d2d::Rect cardRect = { left, top, left + cardWidth, top + cardHeight };

        bool hovering = shouldSelect(cardRect, SDK::ClientInstance::get()->cursorPos);

        d2d::Color cardColor = hovering ? d2d::Color::RGB(0x28, 0x28, 0x28) : d2d::Color::RGB(0x18, 0x18, 0x18);

        dc.fillRoundedRectangle(cardRect, cardColor, 12.0f * scale);

        dc.drawRoundedRectangle(cardRect, d2d::Color::RGB(0x50, 0x50, 0x50).asAlpha(0.65f), 12.0f * scale,
                                1.0f * scale);

        dc.drawText(cardRect, name, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 18.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        return cardRect;
    };

    auto xRayCard = drawCard(0, 0, L"X-Ray");

    if (shouldSelect(xRayCard, SDK::ClientInstance::get()->cursorPos) && justClicked[0]) {
        playClickSound();

        Latite::getScreenManager().showScreen<XRayScreen>();
        return;
    }

    drawCard(1, 0, L"Light Levels");

    drawCard(0, 1, L"Chest Sort");

    drawCard(1, 1, L"Armor HUD");

    drawCard(0, 2, L"Animal Feeder");

    drawCard(1, 2, L"Animal Timers");

    //
    // VERSION
    //

    d2d::Rect versionRect = { panelRect.left + padding, panelRect.bottom - 38.0f * scale, panelRect.right - padding,
                              panelRect.bottom - 12.0f * scale };

    dc.drawText(versionRect, L"Bedrock Mod Suite v0.1.0", d2d::Color::RGB(0xA0, 0xA0, 0xA0).asAlpha(0.70f),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}
