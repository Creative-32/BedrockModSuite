#include "pch.h"
#include "NexusScreen.h"

#include "XRayScreen.h"

#include "client/Latite.h"
#include "client/event/Eventing.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/CharEvent.h"
#include "client/event/events/KeyUpdateEvent.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/screen/ScreenManager.h"
#include "client/feature/nexus/navigation/NexusNavigation.h"

#include "client/feature/nexus/NexusConfig.h"
#include "client/feature/nexus/module/NexusModuleRegistry.h"
#include "client/feature/nexus/ui/NexusControls.h"
#include "client/feature/nexus/xray/XRaySettings.h"
#include "client/feature/nexus/notification/NexusNotificationManager.h"

#include "util/DrawContext.h"

#include <algorithm>
#include <vector>

#include <cwctype>

NexusScreen::NexusScreen() {
    Nexus::NexusConfig::load();
    Nexus::NexusNotificationManager::initialize();

    this->key = KeyValue(Nexus::NexusConfig::menuKey);

    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&NexusScreen::onRender, 1, true);

    Eventing::get().listen<ClickEvent>(this, (EventListenerFunc)&NexusScreen::onClick, 1);

    Eventing::get().listen<CharEvent>(this, (EventListenerFunc)&NexusScreen::onChar, 2);

    Eventing::get().listen<KeyUpdateEvent>(this, (EventListenerFunc)&NexusScreen::onKey, 2);
}

void NexusScreen::onEnable(bool ignoreAnimations) {
    openAnim = ignoreAnimations ? 1.0f : 0.0f;
    skipCloseAnimation = false;

    draggingFavorite = false;
    draggingFavoriteId.clear();
    dragTargetIndex = 0;
    dragOffsetX = 0.0f;
    dragOffsetY = 0.0f;

    favoriteAnimX.clear();
    favoriteAnimY.clear();

    searchBox.setSelected(false);

    resetInputState();
}

void NexusScreen::onDisable() {
    resetInputState();

    draggingFavorite = false;
    draggingFavoriteId.clear();
    dragTargetIndex = 0;
    dragOffsetX = 0.0f;
    dragOffsetY = 0.0f;

    favoriteAnimX.clear();
    favoriteAnimY.clear();

    searchBox.setSelected(false);

    if (skipCloseAnimation) {
        openAnim = 0.0f;
        skipCloseAnimation = false;
    }
}

void NexusScreen::onClick(Event& event) {
    if (!isActive()) return;

    auto& clickEvent = reinterpret_cast<ClickEvent&>(event);

    if (clickEvent.getClickType() != ClickEvent::ClickType::Wheel) {
        return;
    }

    scroll = std::clamp(scroll - static_cast<float>(clickEvent.getWheelDelta()) / 3.0f, 0.0f, scrollMax);

    clickEvent.setCancelled(true);
}

void NexusScreen::onChar(Event& event) {
    if (!isActive() || !searchBox.isSelected()) {
        return;
    }

    auto& charEvent = reinterpret_cast<CharEvent&>(event);

    if (!charEvent.isChar()) {
        return;
    }

    std::wstring before = searchBox.getText();

    searchBox.onChar(charEvent.getChar());

    if (searchBox.getText() != before) {
        scroll = 0.0f;
        lerpScroll = 0.0f;

        favoriteAnimX.clear();
        favoriteAnimY.clear();
    }

    charEvent.setCancelled(true);
}

void NexusScreen::onKey(Event& event) {
    if (!isActive() || !searchBox.isSelected()) {
        return;
    }

    auto& keyEvent = reinterpret_cast<KeyUpdateEvent&>(event);

    if (!keyEvent.isDown()) {
        return;
    }

    int pressedKey = keyEvent.getKey();
    
    //
    // ESC clears search and stops typing.
    //

    if (pressedKey == VK_ESCAPE) {
        searchBox.reset();
        searchBox.setSelected(false);

        scroll = 0.0f;
        lerpScroll = 0.0f;

        favoriteAnimX.clear();
        favoriteAnimY.clear();

        keyEvent.setCancelled(true);
        return;
    }

    //
    // Move the text caret.
    //

    if (pressedKey == VK_LEFT || pressedKey == VK_RIGHT) {
        searchBox.onKeyDown(pressedKey);
    }

    //
    // Block Nexus/Latite keybinds while typing.
    // Actual letters and Backspace come through CharEvent.
    //

    keyEvent.setCancelled(true);
}

void NexusScreen::onRender(Event&) {
    if (!isActive() && openAnim <= 0.005f) {
        openAnim = 0.0f;
        return;
    }

    D2DUtil dc;

    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;

    cursor = Cursor::Arrow;

    D2D1_SIZE_F screenSize = Latite::getRenderer().getScreenSize();

    float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);

    float panelWidth = std::min(screenSize.width * 0.78f, 900.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.76f, 620.0f * scale);

    d2d::Rect panelRect = { (screenSize.width - panelWidth) * 0.5f, (screenSize.height - panelHeight) * 0.5f,
                            (screenSize.width + panelWidth) * 0.5f, (screenSize.height + panelHeight) * 0.5f };

    D2D1::Matrix3x2F originalTransform;

    dc.ctx->GetTransform(&originalTransform);

    float targetAnim = isActive() ? 1.0f : 0.0f;

    openAnim = std::lerp(openAnim, targetAnim, Latite::getRenderer().getDeltaTime() * 0.2f);

    if (isActive() && openAnim > 0.995f) {
        openAnim = 1.0f;
    }

    if (!isActive() && openAnim < 0.005f) {
        openAnim = 0.0f;
        return;
    }

    float easedAnim = openAnim * openAnim * (3.0f - 2.0f * openAnim);

    dc.ctx->SetTransform(
        D2D1::Matrix3x2F::Scale({ easedAnim, easedAnim }, D2D1_POINT_2F(panelRect.center().x, panelRect.center().y)) *
        originalTransform);

    //
    // PANEL
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

    d2d::Rect titleRect = { panelRect.left + padding, panelRect.top + 14.0f * scale, panelRect.right - padding,
                            panelRect.top + 54.0f * scale };

    dc.drawText(titleRect, L"Nexus", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryLight, 28.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    //
    // VIEW MODE
    //

    float viewButtonHeight = 26.0f * scale;

    float viewGap = 6.0f * scale;

    float compactWidth = 72.0f * scale;

    float mediumWidth = 72.0f * scale;

    float listWidth = 52.0f * scale;

    float viewRight = panelRect.right - padding;

    float viewTop = panelRect.top + 49.0f * scale;

    d2d::Rect compactViewRect = { viewRight - compactWidth, viewTop, viewRight, viewTop + viewButtonHeight };

    d2d::Rect mediumViewRect = { compactViewRect.left - viewGap - mediumWidth, viewTop, compactViewRect.left - viewGap,
                                 viewTop + viewButtonHeight };

    d2d::Rect listViewRect = { mediumViewRect.left - viewGap - listWidth, viewTop, mediumViewRect.left - viewGap,
                               viewTop + viewButtonHeight };

    auto drawViewButton = [&](const d2d::Rect& rect, const std::wstring& text, Nexus::NexusViewMode mode) {
        bool selected = Nexus::NexusConfig::viewMode == mode;

        bool hovering = isActive() && shouldSelect(rect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        d2d::Color background;

        if (selected) {
            background = d2d::Color::RGB(0x42, 0x78, 0xA8);
        } else if (hovering) {
            background = d2d::Color::RGB(0x32, 0x32, 0x32);
        } else {
            background = d2d::Color::RGB(0x20, 0x20, 0x20);
        }

        dc.fillRoundedRectangle(rect, background, 7.0f * scale);

        dc.drawText(rect, text, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 11.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (hovering && justClicked[0] && !selected) {
            Nexus::NexusConfig::viewMode = mode;

            Nexus::NexusConfig::save();

            scroll = 0.0f;
            lerpScroll = 0.0f;

            draggingFavorite = false;
            draggingFavoriteId.clear();
            dragTargetIndex = 0;

            favoriteAnimX.clear();
            favoriteAnimY.clear();

            playClickSound();
        }
    };

    drawViewButton(listViewRect, L"List", Nexus::NexusViewMode::List);

    drawViewButton(mediumViewRect, L"Medium", Nexus::NexusViewMode::Medium);

    drawViewButton(compactViewRect, L"Compact", Nexus::NexusViewMode::Compact);

    //
    // SUBTITLE
    //

    d2d::Rect subtitleRect = { panelRect.left + padding, panelRect.top + 48.0f * scale, panelRect.right - padding,
                               panelRect.top + 76.0f * scale };

    dc.drawText(subtitleRect, L"Select a module", d2d::Color::RGB(0xB0, 0xB0, 0xB0).asAlpha(0.80f),
                Renderer::FontSelection::PrimaryRegular, 14.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    //
    // SEARCH
    //

    d2d::Rect searchRect = { panelRect.left + padding, panelRect.top + 80.0f * scale, panelRect.right - padding,
                             panelRect.top + 110.0f * scale };

    bool hasSearchText = !searchBox.getText().empty();

    //
    // Leave padding on the left and room
    // for the clear button on the right.
    //

    d2d::Rect searchTextRect = { searchRect.left + 10.0f * scale, searchRect.top + 4.0f * scale,
                                 searchRect.right - (hasSearchText ? 34.0f * scale : 10.0f * scale),
                                 searchRect.bottom - 4.0f * scale };

    searchBox.setRect(searchTextRect);

    bool searchHovered = isActive() && shouldSelect(searchRect, cursorPos);

    d2d::Rect clearRect = { searchRect.right - 28.0f * scale, searchRect.top + 4.0f * scale,
                            searchRect.right - 6.0f * scale, searchRect.bottom - 4.0f * scale };

    bool clearHovered = hasSearchText && isActive() && shouldSelect(clearRect, cursorPos);

    if (clearHovered) {
        cursor = Cursor::Hand;
    } else if (searchHovered) {
        cursor = Cursor::IBeam;
    }

    if (justClicked[0]) {
        if (clearHovered) {
            searchBox.reset();
            searchBox.setSelected(true);

            scroll = 0.0f;
            lerpScroll = 0.0f;

            favoriteAnimX.clear();
            favoriteAnimY.clear();

            playClickSound();
        } else {
            searchBox.setSelected(searchHovered);
        }
    }

    d2d::Color searchBackground = searchBox.isSelected() ? d2d::Color::RGB(0x24, 0x24, 0x24)
                                  : searchHovered        ? d2d::Color::RGB(0x20, 0x20, 0x20)
                                                         : d2d::Color::RGB(0x18, 0x18, 0x18);

    //
    // Draw the full search box ourselves.
    //

    dc.fillRoundedRectangle(searchRect, searchBackground, 8.0f * scale);

    dc.drawRoundedRectangle(
        searchRect, searchBox.isSelected() ? d2d::Color::RGB(0x42, 0x78, 0xA8) : d2d::Color::RGB(0x48, 0x48, 0x48),
        8.0f * scale, searchBox.isSelected() ? 1.5f * scale : 1.0f * scale);

    //
    // TextBox now only supplies text + caret.
    // Transparent background keeps our outer box visible.
    //

    searchBox.render(dc, 0.0f, d2d::Color::RGB(0x00, 0x00, 0x00).asAlpha(0.0f), d2d::Colors::WHITE,
                     DWRITE_TEXT_ALIGNMENT_LEADING);

    if (searchBox.getText().empty() && !searchBox.isSelected()) {
        dc.drawText(searchTextRect, L"Search modules...", d2d::Color::RGB(0x82, 0x82, 0x82),
                    Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    //
    // CLEAR BUTTON
    //

    if (hasSearchText) {
        d2d::Color clearColor = clearHovered ? d2d::Color::RGB(0x45, 0x45, 0x45) : d2d::Color::RGB(0x2A, 0x2A, 0x2A);

        dc.fillRoundedRectangle(clearRect, clearColor, 6.0f * scale);

        dc.drawText(clearRect, L"X", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 12.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    //
    // BUILD DISPLAY ORDER
    //

    const auto& modules = Nexus::NexusModuleRegistry::getModules();

    const auto& favoriteOrder = Nexus::NexusConfig::getFavoriteOrder();

    std::vector<const Nexus::NexusModuleInfo*> favoriteModules;

    std::vector<const Nexus::NexusModuleInfo*> normalModules;

    for (const auto& favoriteId : favoriteOrder) {
        const auto* module = Nexus::NexusModuleRegistry::find(favoriteId);

        if (module != nullptr) {
            favoriteModules.push_back(module);
        }
    }

    for (const auto& module : modules) {
        bool favorite = std::find(favoriteOrder.begin(), favoriteOrder.end(), module.id) != favoriteOrder.end();

        if (!favorite) {
            normalModules.push_back(&module);
        }
    }

    std::sort(normalModules.begin(), normalModules.end(),
              [](const Nexus::NexusModuleInfo* a, const Nexus::NexusModuleInfo* b) {
                  return a->name < b->name;
              });

    //
    // SEARCH FILTER
    //

    std::wstring searchText = searchBox.getText();

    std::transform(searchText.begin(), searchText.end(), searchText.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });

    if (!searchText.empty()) {
        auto matchesSearch = [&](const Nexus::NexusModuleInfo* module) {
            std::wstring name = module->name;

            std::wstring description = module->description;

            std::transform(name.begin(), name.end(), name.begin(), [](wchar_t ch) {
                return static_cast<wchar_t>(std::towlower(ch));
            });

            std::transform(description.begin(), description.end(), description.begin(), [](wchar_t ch) {
                return static_cast<wchar_t>(std::towlower(ch));
            });

            return name.find(searchText) != std::wstring::npos || description.find(searchText) != std::wstring::npos;
        };

        std::erase_if(favoriteModules, [&](const Nexus::NexusModuleInfo* module) {
            return !matchesSearch(module);
        });

        std::erase_if(normalModules, [&](const Nexus::NexusModuleInfo* module) {
            return !matchesSearch(module);
        });
    }

    //
    // LIST AREA
    //

    d2d::Rect listRect = { panelRect.left + padding, panelRect.top + 120.0f * scale, panelRect.right - padding,
                           panelRect.bottom - 56.0f * scale };

    int columnCount = 1;

    float tileHeight = 58.0f * scale;

    bool showDescription = true;

    switch (Nexus::NexusConfig::viewMode) {
    case Nexus::NexusViewMode::Medium:
        columnCount = 2;
        tileHeight = 72.0f * scale;
        showDescription = true;
        break;

    case Nexus::NexusViewMode::Compact:
        columnCount = 3;
        tileHeight = 50.0f * scale;
        showDescription = false;
        break;

    case Nexus::NexusViewMode::List:
    default:
        columnCount = 1;
        tileHeight = 58.0f * scale;
        showDescription = true;
        break;
    }

    //
    // RESPONSIVE FALLBACK
    //

    float minimumTileWidth =
        Nexus::NexusConfig::viewMode == Nexus::NexusViewMode::Compact ? 210.0f * scale : 300.0f * scale;

    while (columnCount > 1) {
        float testWidth =
            (listRect.getWidth() - 10.0f * scale - (columnCount - 1) * 8.0f * scale) / static_cast<float>(columnCount);

        if (testWidth >= minimumTileWidth) {
            break;
        }

        columnCount--;
    }

    float tileGap = 8.0f * scale;

    float sectionGap = 14.0f * scale;

    float availableWidth = listRect.getWidth() - 10.0f * scale;

    float tileWidth =
        (availableWidth - tileGap * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount);

    auto sectionHeight = [&](std::size_t count) {
        if (count == 0) {
            return 0.0f;
        }

        std::size_t rows = (count + static_cast<std::size_t>(columnCount) - 1) / static_cast<std::size_t>(columnCount);

        return static_cast<float>(rows) * tileHeight + static_cast<float>(rows - 1) * tileGap;
    };

    float favoritesHeight = sectionHeight(favoriteModules.size());

    float normalHeight = sectionHeight(normalModules.size());

    float contentHeight = favoritesHeight + normalHeight;

    if (!favoriteModules.empty() && !normalModules.empty()) {
        contentHeight += sectionGap;
    }

    scrollMax = std::max(0.0f, contentHeight - listRect.getHeight());

    scroll = std::clamp(scroll, 0.0f, scrollMax);

    lerpScroll = std::lerp(lerpScroll, scroll, Latite::getRenderer().getDeltaTime() / 5.0f);

    lerpScroll = std::clamp(lerpScroll, 0.0f, scrollMax);

    //
    // FAVORITE DRAG TARGET
    //

    if (draggingFavorite) {
        if (favoriteModules.empty()) {
            draggingFavorite = false;
            draggingFavoriteId.clear();
            dragTargetIndex = 0;
            favoriteAnimX.clear();
            favoriteAnimY.clear();
        } else if (mouseButtons[0]) {
            if (listRect.contains(cursorPos)) {
                float localX = cursorPos.x - listRect.left;

                float localY = cursorPos.y + lerpScroll - listRect.top;

                int targetColumn = static_cast<int>(localX / (tileWidth + tileGap));

                int targetRow = static_cast<int>(localY / (tileHeight + tileGap));

                targetColumn = std::clamp(targetColumn, 0, columnCount - 1);

                targetRow = std::max(targetRow, 0);

                std::size_t targetIndex = static_cast<std::size_t>(targetRow * columnCount + targetColumn);

                dragTargetIndex = std::min(targetIndex, favoriteModules.size() - 1);
            }
        } else {
            auto current = std::find(favoriteOrder.begin(), favoriteOrder.end(), draggingFavoriteId);

            if (current != favoriteOrder.end()) {
                std::size_t currentIndex = static_cast<std::size_t>(current - favoriteOrder.begin());

                if (currentIndex != dragTargetIndex) {
                    Nexus::NexusConfig::moveFavorite(draggingFavoriteId, dragTargetIndex);
                }
            }

            playClickSound();

            draggingFavorite = false;
            draggingFavoriteId.clear();
            dragTargetIndex = 0;
        }
    }

    //
    // LIVE FAVORITE PREVIEW ORDER
    //

    std::vector<const Nexus::NexusModuleInfo*> displayFavoriteModules = favoriteModules;

    if (draggingFavorite && !displayFavoriteModules.empty()) {
        auto draggedIt = std::find_if(displayFavoriteModules.begin(), displayFavoriteModules.end(),
                                      [&](const Nexus::NexusModuleInfo* module) {
                                          return module->id == draggingFavoriteId;
                                      });

        if (draggedIt != displayFavoriteModules.end()) {
            const Nexus::NexusModuleInfo* dragged = *draggedIt;

            displayFavoriteModules.erase(draggedIt);

            std::size_t insertIndex = std::min(dragTargetIndex, displayFavoriteModules.size());

            displayFavoriteModules.insert(displayFavoriteModules.begin() + insertIndex, dragged);
        }
    }

    dc.ctx->PushAxisAlignedClip(listRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    bool openXRay = false;

auto drawModule = [&](const Nexus::NexusModuleInfo& module, bool favorite, int column, int row, float sectionTop, std::size_t favoriteIndex) {
        bool isDraggingThis = favorite && draggingFavorite && draggingFavoriteId == module.id;

        float targetLeft = listRect.left + static_cast<float>(column) * (tileWidth + tileGap);

        float targetContentTop = sectionTop + static_cast<float>(row) * (tileHeight + tileGap);

        float left = targetLeft;

        float contentTop = targetContentTop;

        //
        // Smoothly move favorites toward their
        // temporary preview positions.
        //

        if (favorite) {
            auto [xIt, insertedX] = favoriteAnimX.try_emplace(module.id, targetLeft);

            auto [yIt, insertedY] = favoriteAnimY.try_emplace(module.id, targetContentTop);

            if (isDraggingThis) {
                //
                // The placeholder should immediately
                // occupy the current destination slot.
                //

                xIt->second = targetLeft;

                yIt->second = targetContentTop;
            } else {
                float reflowAmount = std::clamp(Latite::getRenderer().getDeltaTime() * 14.0f, 0.0f, 1.0f);

                xIt->second = std::lerp(xIt->second, targetLeft, reflowAmount);

                yIt->second = std::lerp(yIt->second, targetContentTop, reflowAmount);
            }

            left = xIt->second;

            contentTop = yIt->second;
        }

        float top = contentTop - lerpScroll;

        d2d::Rect tileRect = { left, top, left + tileWidth, top + tileHeight };

        if (isDraggingThis) {
            dc.fillRoundedRectangle(tileRect, d2d::Color::RGB(0x12, 0x12, 0x12), 10.0f * scale);

            return;
        }

        if (tileRect.bottom < listRect.top || tileRect.top > listRect.bottom) {
            return;
        }

        bool cursorInsideList = listRect.contains(cursorPos);

        bool tileHovered = isActive() && cursorInsideList && shouldSelect(tileRect, cursorPos);

        d2d::Color tileColor = tileHovered ? d2d::Color::RGB(0x27, 0x27, 0x27) : d2d::Color::RGB(0x17, 0x17, 0x17);

        dc.fillRoundedRectangle(tileRect, tileColor, 10.0f * scale);

        dc.drawRoundedRectangle(tileRect, d2d::Color::RGB(0x48, 0x48, 0x48).asAlpha(0.65f), 10.0f * scale,
                                1.0f * scale);

        if (favorite && draggingFavorite && favoriteIndex == dragTargetIndex) {
            dc.drawRoundedRectangle(tileRect, d2d::Color::RGB(0x42, 0x78, 0xA8), 10.0f * scale, 2.0f * scale);
        }

        //
        // ACTION
        //

        float actionWidth = 48.0f * scale;

        float actionHeight = 22.0f * scale;

        d2d::Rect actionRect = { tileRect.right - 10.0f * scale - actionWidth,
                                 tileRect.center().y - actionHeight * 0.5f, tileRect.right - 10.0f * scale,
                                 tileRect.center().y + actionHeight * 0.5f };

        //
        // STAR
        //

        float starWidth = 30.0f * scale;

        d2d::Rect starRect = { actionRect.left - 6.0f * scale - starWidth, tileRect.top, actionRect.left - 6.0f * scale,
                               tileRect.bottom };

        bool starHovered = isActive() && cursorInsideList && shouldSelect(starRect, cursorPos);

        if (starHovered) {
            cursor = Cursor::Hand;
        }

        dc.drawText(starRect, favorite ? L"\u2605" : L"\u2606",
                    favorite ? d2d::Colors::WHITE : d2d::Color::RGB(0x88, 0x88, 0x88),
                    Renderer::FontSelection::PrimaryRegular, 20.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (starHovered && justClicked[0]) {
            Nexus::NexusConfig::setFavorite(module.id, !favorite);

            playClickSound();
        }

        //
        // DRAG HANDLE
        //

        d2d::Rect dragRect = { tileRect.left + 4.0f * scale, tileRect.top, tileRect.left + 30.0f * scale,
                               tileRect.bottom };

        bool dragHovered = favorite && searchText.empty() && isActive() && cursorInsideList && shouldSelect(dragRect, cursorPos);
        
        if (favorite) {

            if (dragHovered || isDraggingThis) {
                cursor = Cursor::Hand;
            }

            dc.drawText(dragRect, L"\u2261", isDraggingThis ? d2d::Colors::WHITE : d2d::Color::RGB(0x86, 0x86, 0x86),
                        Renderer::FontSelection::PrimaryRegular, 18.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            if (dragHovered && justClicked[0]) {
                draggingFavorite = true;
                draggingFavoriteId = module.id;
                dragTargetIndex = favoriteIndex;

                dragOffsetX = cursorPos.x - tileRect.left;
                dragOffsetY = cursorPos.y - tileRect.top;
            }
        }

        //
        // TEXT
        //

        float textLeft = tileRect.left + (favorite ? 34.0f : 12.0f) * scale;

        d2d::Rect nameRect;

        if (showDescription) {
            nameRect = { textLeft, tileRect.top + 4.0f * scale, starRect.left - 6.0f * scale,
                         tileRect.top + 31.0f * scale };
        } else {
            nameRect = { textLeft, tileRect.top, starRect.left - 6.0f * scale, tileRect.bottom };
        }

        dc.drawText(nameRect, module.name, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                    Nexus::NexusConfig::viewMode == Nexus::NexusViewMode::Compact ? 14.0f * scale : 16.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (showDescription) {
            d2d::Rect descriptionRect = { textLeft, tileRect.top + 28.0f * scale, starRect.left - 6.0f * scale,
                                          tileRect.bottom - 4.0f * scale };

            dc.drawText(descriptionRect, module.description, d2d::Color::RGB(0xA0, 0xA0, 0xA0),
                        Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        //
        // SWITCH / SOON
        //

        bool actionHovered = isActive() && cursorInsideList && shouldSelect(actionRect, cursorPos);

        bool implemented = module.id == "xray";

        if (implemented) {
            if (actionHovered) {
                cursor = Cursor::Hand;
            }

            if (Nexus::UI::drawSwitch(dc, actionRect, Nexus::xRaySettings.enabled, actionHovered, justClicked[0],
                                      scale)) {
                playClickSound();

                Nexus::NexusConfig::save();
            }
        } else {
            dc.fillRoundedRectangle(actionRect, d2d::Color::RGB(0x2C, 0x2C, 0x2C), actionRect.getHeight() * 0.5f);

            dc.drawText(actionRect, L"SOON", d2d::Color::RGB(0x91, 0x91, 0x91), Renderer::FontSelection::PrimaryRegular,
                        9.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        //
        // OPEN SETTINGS
        //

        bool bodyHovered = tileHovered && !starHovered && !actionHovered && !dragHovered;

        if (implemented && bodyHovered) {
            cursor = Cursor::Hand;
        }

        if (implemented && bodyHovered && justClicked[0]) {
            playClickSound();
            openXRay = true;
        }
    };

    //
    // FAVORITES
    //

    float favoritesTop = listRect.top;

    for (std::size_t i = 0; i < displayFavoriteModules.size(); ++i) {
        int row = static_cast<int>(i / columnCount);

        int column = static_cast<int>(i % columnCount);

        drawModule(*displayFavoriteModules[i], true, column, row, favoritesTop, i);
    }

    //
    // NORMAL MODULES
    //

    float normalTop = favoritesTop + favoritesHeight;

    if (!favoriteModules.empty() && !normalModules.empty()) {
        normalTop += sectionGap;
    }

    for (std::size_t i = 0; i < normalModules.size(); ++i) {
        int row = static_cast<int>(i / columnCount);

        int column = static_cast<int>(i % columnCount);

        drawModule(*normalModules[i], false, column, row, normalTop, 0);
    }
    
    //
    // FLOATING DRAGGED FAVORITE
    //

    if (draggingFavorite) {
        const auto* draggedModule = Nexus::NexusModuleRegistry::find(draggingFavoriteId);

        if (draggedModule != nullptr) {
            float draggedLeft = cursorPos.x - dragOffsetX;

            float draggedTop = cursorPos.y - dragOffsetY;

            //
            // Keep the floating tile inside the module area.
            //

            draggedLeft = std::clamp(draggedLeft, listRect.left, listRect.right - 10.0f * scale - tileWidth);

            draggedTop = std::clamp(draggedTop, listRect.top, listRect.bottom - tileHeight);

            d2d::Rect draggedRect = { draggedLeft, draggedTop, draggedLeft + tileWidth, draggedTop + tileHeight };

            //
            // Slightly brighter "lifted" appearance.
            //

            dc.fillRoundedRectangle(draggedRect, d2d::Color::RGB(0x2C, 0x2C, 0x2C), 11.0f * scale);

            dc.drawRoundedRectangle(draggedRect, d2d::Color::RGB(0x62, 0x92, 0xBC), 11.0f * scale, 2.0f * scale);

            float floatingActionWidth = 48.0f * scale;

            float floatingActionHeight = 22.0f * scale;

            d2d::Rect floatingActionRect = { draggedRect.right - 10.0f * scale - floatingActionWidth,
                                             draggedRect.center().y - floatingActionHeight * 0.5f,
                                             draggedRect.right - 10.0f * scale,
                                             draggedRect.center().y + floatingActionHeight * 0.5f };

            float floatingStarWidth = 30.0f * scale;

            d2d::Rect floatingStarRect = { floatingActionRect.left - 6.0f * scale - floatingStarWidth, draggedRect.top,
                                           floatingActionRect.left - 6.0f * scale, draggedRect.bottom };

            d2d::Rect floatingHandleRect = { draggedRect.left + 4.0f * scale, draggedRect.top,
                                             draggedRect.left + 30.0f * scale, draggedRect.bottom };

            dc.drawText(floatingHandleRect, L"\u2261", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                        18.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            dc.drawText(floatingStarRect, L"\u2605", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                        20.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            float floatingTextLeft = draggedRect.left + 34.0f * scale;

            d2d::Rect floatingNameRect;

            if (showDescription) {
                floatingNameRect = { floatingTextLeft, draggedRect.top + 4.0f * scale,
                                     floatingStarRect.left - 6.0f * scale, draggedRect.top + 31.0f * scale };
            } else {
                floatingNameRect = { floatingTextLeft, draggedRect.top, floatingStarRect.left - 6.0f * scale,
                                     draggedRect.bottom };
            }

            dc.drawText(floatingNameRect, draggedModule->name, d2d::Colors::WHITE,
                        Renderer::FontSelection::PrimaryRegular,
                        Nexus::NexusConfig::viewMode == Nexus::NexusViewMode::Compact ? 14.0f * scale : 16.0f * scale,
                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            if (showDescription) {
                d2d::Rect floatingDescriptionRect = { floatingTextLeft, draggedRect.top + 28.0f * scale,
                                                      floatingStarRect.left - 6.0f * scale,
                                                      draggedRect.bottom - 4.0f * scale };

                dc.drawText(floatingDescriptionRect, draggedModule->description, d2d::Color::RGB(0xB0, 0xB0, 0xB0),
                            Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }

            if (draggedModule->id == "xray") {
                //
                // Draw-only copy of the current X-Ray switch.
                // Do not make it clickable while dragging.
                //

                bool floatingEnabled = Nexus::xRaySettings.enabled;

                Nexus::UI::drawSwitch(dc, floatingActionRect, floatingEnabled, false, false, scale);
            } else {
                dc.fillRoundedRectangle(floatingActionRect, d2d::Color::RGB(0x34, 0x34, 0x34),
                                        floatingActionRect.getHeight() * 0.5f);

                dc.drawText(floatingActionRect, L"SOON", d2d::Color::RGB(0xA0, 0xA0, 0xA0),
                            Renderer::FontSelection::PrimaryRegular, 9.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
    }

    dc.ctx->PopAxisAlignedClip();

    //
    // SCROLLBAR
    //

    if (scrollMax > 0.0f) {
        float trackWidth = 4.0f * scale;

        d2d::Rect trackRect = { listRect.right - trackWidth, listRect.top, listRect.right, listRect.bottom };

        dc.fillRoundedRectangle(trackRect, d2d::Color::RGB(0x2D, 0x2D, 0x2D), trackWidth * 0.5f);

        float visibleRatio = listRect.getHeight() / contentHeight;

        float thumbHeight = std::max(30.0f * scale, listRect.getHeight() * visibleRatio);

        thumbHeight = std::min(thumbHeight, listRect.getHeight());

        float availableTrack = listRect.getHeight() - thumbHeight;

        float scrollPercent = scrollMax > 0.0f ? lerpScroll / scrollMax : 0.0f;

        float thumbTop = listRect.top + availableTrack * scrollPercent;

        d2d::Rect thumbRect = { trackRect.left, thumbTop, trackRect.right, thumbTop + thumbHeight };

        dc.fillRoundedRectangle(thumbRect, d2d::Color::RGB(0x72, 0x72, 0x72), trackWidth * 0.5f);
    }

    //
    // VERSION
    //

    d2d::Rect versionRect = { panelRect.left + padding, panelRect.bottom - 38.0f * scale, panelRect.right - padding,
                              panelRect.bottom - 12.0f * scale };

    dc.drawText(versionRect, L"Nexus v0.1.0", d2d::Color::RGB(0xA0, 0xA0, 0xA0).asAlpha(0.70f),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // X-RAY PAGE SWITCH
    //

    if (openXRay) {
        dc.ctx->SetTransform(originalTransform);

        Nexus::NexusNavigation::openSubmenu<XRayScreen>(*this);

        return;
    }

    dc.ctx->SetTransform(originalTransform);
}
