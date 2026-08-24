#include "pch.h"

#include "XRayBlockListScreen.h"
#include "XRayScreen.h"

#include "client/Latite.h"

#include "client/event/Eventing.h"
#include "client/event/events/CharEvent.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/KeyUpdateEvent.h"
#include "client/event/events/RenderOverlayEvent.h"

#include "client/feature/nexus/NexusConfig.h"
#include "client/feature/nexus/ui/NexusControls.h"
#include "client/feature/nexus/xray/XRayBlockCatalog.h"
#include "client/feature/nexus/xray/XRayTargets.h"

#include "client/screen/ScreenManager.h"

#include "util/DrawContext.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace {

    std::wstring lowerText(std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }

    std::wstring normalizeSearchText(std::wstring value) {
        value = lowerText(std::move(value));

        for (wchar_t& ch : value) {
            if (ch == L'_' || ch == L'-' || ch == L'.' || ch == L':') ch = L' ';
        }

        std::wstring result;
        result.reserve(value.size());
        bool lastWasSpace = true;

        for (wchar_t ch : value) {
            bool space = std::iswspace(ch) != 0;
            if (space) {
                if (!lastWasSpace) result.push_back(L' ');
                lastWasSpace = true;
            } else {
                result.push_back(ch);
                lastWasSpace = false;
            }
        }

        while (!result.empty() && result.back() == L' ')
            result.pop_back();

        return result;
    }

} // namespace

XRayBlockListScreen::XRayBlockListScreen() {
    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&XRayBlockListScreen::onRender, 1, true);
    Eventing::get().listen<ClickEvent>(this, (EventListenerFunc)&XRayBlockListScreen::onClick, 1);
    Eventing::get().listen<CharEvent>(this, (EventListenerFunc)&XRayBlockListScreen::onChar, 2);
    Eventing::get().listen<KeyUpdateEvent>(this, (EventListenerFunc)&XRayBlockListScreen::onKey, 2);
}

void XRayBlockListScreen::onEnable(bool) {
    scroll = 0.0f;
    lerpScroll = 0.0f;
    scrollMax = 0.0f;

    selectedOnly = false;
    layoutDropdownOpen = false;

    Nexus::NexusConfig::load();
    Nexus::NexusConfig::blockListColumns = std::clamp(Nexus::NexusConfig::blockListColumns, 1, 3);

    searchBox.reset();
    searchBox.setSelected(false);

    resetInputState();
}

void XRayBlockListScreen::onDisable() {
    Nexus::NexusConfig::save();
    layoutDropdownOpen = false;
    searchBox.setSelected(false);
    resetInputState();
}

void XRayBlockListScreen::onClick(Event& event) {
    if (!isActive()) return;

    auto& clickEvent = reinterpret_cast<ClickEvent&>(event);
    if (clickEvent.getClickType() != ClickEvent::ClickType::Wheel) return;

    auto client = SDK::ClientInstance::get();
    if (!client) return;

    auto& cursorPos = client->cursorPos;
    bool overList =
        cursorPos.x >= listLeft && cursorPos.x <= listRight && cursorPos.y >= listTop && cursorPos.y <= listBottom;

    if (!overList) return;

    scroll = std::clamp(scroll - static_cast<float>(clickEvent.getWheelDelta()) / 3.0f, 0.0f, scrollMax);
    clickEvent.setCancelled(true);
}

void XRayBlockListScreen::onChar(Event& event) {
    if (!isActive() || !searchBox.isSelected()) return;

    auto& charEvent = reinterpret_cast<CharEvent&>(event);
    if (!charEvent.isChar()) return;

    std::wstring oldText = searchBox.getText();
    searchBox.onChar(charEvent.getChar());

    if (oldText != searchBox.getText()) {
        scroll = 0.0f;
        lerpScroll = 0.0f;
    }

    charEvent.setCancelled(true);
}

void XRayBlockListScreen::onKey(Event& event) {
    if (!isActive() || !searchBox.isSelected()) return;

    auto& keyEvent = reinterpret_cast<KeyUpdateEvent&>(event);
    if (!keyEvent.isDown()) return;

    int pressedKey = keyEvent.getKey();

    if (pressedKey == VK_ESCAPE) {
        searchBox.reset();
        searchBox.setSelected(false);
        scroll = 0.0f;
        lerpScroll = 0.0f;
        keyEvent.setCancelled(true);
        return;
    }

    if (pressedKey == VK_LEFT || pressedKey == VK_RIGHT) searchBox.onKeyDown(pressedKey);

    keyEvent.setCancelled(true);
}

void XRayBlockListScreen::onRender(Event&) {
    if (!isActive()) return;

    using namespace Nexus;

    auto client = SDK::ClientInstance::get();
    if (!client) return;

    D2DUtil dc;
    auto screenSize = Latite::getRenderer().getScreenSize();
    auto& cursorPos = client->cursorPos;

    cursor = Cursor::Arrow;

    float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);
    float panelWidth = std::min(screenSize.width * 0.92f, 1180.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.94f, 900.0f * scale);

    d2d::Rect panelRect = { (screenSize.width - panelWidth) * 0.5f, (screenSize.height - panelHeight) * 0.5f,
                            (screenSize.width + panelWidth) * 0.5f, (screenSize.height + panelHeight) * 0.5f };

    float padding = 26.0f * scale;

    dc.fillRoundedRectangle(panelRect, d2d::Color::RGB(0x0B, 0x0B, 0x0B).asAlpha(0.95f), 18.0f * scale);
    dc.drawRoundedRectangle(panelRect, d2d::Color::RGB(0x45, 0x45, 0x45).asAlpha(0.75f), 18.0f * scale, 1.5f * scale);

    d2d::Rect titleRect = { panelRect.left + padding, panelRect.top + 10.0f * scale, panelRect.right - padding,
                            panelRect.top + 48.0f * scale };

    dc.drawText(titleRect, L"Block List", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryLight, 27.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect subtitleRect = { panelRect.left + padding, panelRect.top + 42.0f * scale, panelRect.right - padding,
                               panelRect.top + 66.0f * scale };

    dc.drawText(subtitleRect, L"Choose which blocks X-Ray should highlight", d2d::Color::RGB(0x9A, 0x9A, 0x9A),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    struct BlockEntry {
        std::wstring name;
        std::wstring ids;
        std::string namespacedId;
    };

    std::vector<BlockEntry> entries;
    const auto& catalogEntries = XRayBlockCatalog::getEntries();
    entries.reserve(catalogEntries.size());

    for (const auto& catalogEntry : catalogEntries) {
        entries.push_back({ catalogEntry.displayName,
                            std::wstring(catalogEntry.namespacedId.begin(), catalogEntry.namespacedId.end()),
                            catalogEntry.namespacedId });
    }

    int selectedCount = 0;
    for (const auto& entry : entries) {
        if (XRayTargets::isSelected(entry.namespacedId)) ++selectedCount;
    }

    d2d::Rect selectedRect = { panelRect.right - padding - 180.0f * scale, titleRect.top, panelRect.right - padding,
                               titleRect.bottom };

    dc.drawText(selectedRect, std::to_wstring(selectedCount) + L" selected", d2d::Color::RGB(0x92, 0x92, 0x92),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Search box.
    d2d::Rect searchRect = { panelRect.left + padding, panelRect.top + 78.0f * scale, panelRect.right - padding,
                             panelRect.top + 112.0f * scale };

    bool hasSearch = !searchBox.getText().empty();

    d2d::Rect searchTextRect = { searchRect.left + 11.0f * scale, searchRect.top + 4.0f * scale,
                                 searchRect.right - (hasSearch ? 36.0f * scale : 10.0f * scale),
                                 searchRect.bottom - 4.0f * scale };
    searchBox.setRect(searchTextRect);

    d2d::Rect clearRect = { searchRect.right - 29.0f * scale, searchRect.top + 5.0f * scale,
                            searchRect.right - 6.0f * scale, searchRect.bottom - 5.0f * scale };

    bool searchHovered = shouldSelect(searchRect, cursorPos);
    bool clearHovered = hasSearch && shouldSelect(clearRect, cursorPos);

    if (clearHovered)
        cursor = Cursor::Hand;
    else if (searchHovered)
        cursor = Cursor::IBeam;

    if (justClicked[0]) {
        if (clearHovered) {
            searchBox.reset();
            searchBox.setSelected(true);
            scroll = 0.0f;
            lerpScroll = 0.0f;
            playClickSound();
        } else {
            searchBox.setSelected(searchHovered);
        }
    }

    d2d::Color searchBackground = searchBox.isSelected() ? d2d::Color::RGB(0x24, 0x24, 0x24)
                                  : searchHovered        ? d2d::Color::RGB(0x20, 0x20, 0x20)
                                                         : d2d::Color::RGB(0x17, 0x17, 0x17);

    dc.fillRoundedRectangle(searchRect, searchBackground, 8.0f * scale);
    dc.drawRoundedRectangle(
        searchRect, searchBox.isSelected() ? d2d::Color::RGB(0x42, 0x78, 0xA8) : d2d::Color::RGB(0x48, 0x48, 0x48),
        8.0f * scale, searchBox.isSelected() ? 1.5f * scale : 1.0f * scale);

    searchBox.render(dc, 0.0f, d2d::Color::RGB(0, 0, 0).asAlpha(0.0f), d2d::Colors::WHITE,
                     DWRITE_TEXT_ALIGNMENT_LEADING);

    if (searchBox.getText().empty() && !searchBox.isSelected()) {
        dc.drawText(searchTextRect, L"Search blocks...", d2d::Color::RGB(0x82, 0x82, 0x82),
                    Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (hasSearch) {
        dc.fillRoundedRectangle(clearRect,
                                clearHovered ? d2d::Color::RGB(0x45, 0x45, 0x45) : d2d::Color::RGB(0x2A, 0x2A, 0x2A),
                                6.0f * scale);
        dc.drawText(clearRect, L"X", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 11.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Helper buttons + layout dropdown.
    float helperTop = searchRect.bottom + 8.0f * scale;
    float helperHeight = 26.0f * scale;
    float helperGap = 7.0f * scale;
    float helperWidth = 110.0f * scale;

    d2d::Rect allOnRect = { searchRect.left, helperTop, searchRect.left + helperWidth, helperTop + helperHeight };
    d2d::Rect allOffRect = { allOnRect.right + helperGap, helperTop, allOnRect.right + helperGap + helperWidth,
                             helperTop + helperHeight };
    d2d::Rect selectedOnlyRect = { allOffRect.right + helperGap, helperTop,
                                   allOffRect.right + helperGap + 125.0f * scale, helperTop + helperHeight };

    float layoutWidth = 160.0f * scale;
    d2d::Rect layoutSelectorRect = { searchRect.right - layoutWidth, helperTop, searchRect.right,
                                     helperTop + helperHeight };

    float layoutOptionHeight = 28.0f * scale;
    float layoutOptionGap = 3.0f * scale;
    float layoutMenuTop = layoutSelectorRect.bottom + 4.0f * scale;
    d2d::Rect layoutMenuRect = { layoutSelectorRect.left, layoutMenuTop, layoutSelectorRect.right,
                                 layoutMenuTop + layoutOptionHeight * 3.0f + layoutOptionGap * 2.0f };

    bool layoutConsumedClick = false;

    auto drawButton = [&](const d2d::Rect& rect, const std::wstring& label, bool selected = false) -> bool {
        bool hovering = shouldSelect(rect, cursorPos);
        if (hovering) cursor = Cursor::Hand;

        d2d::Color background = selected   ? d2d::Color::RGB(0x34, 0x5E, 0x82)
                                : hovering ? d2d::Color::RGB(0x2D, 0x2D, 0x2D)
                                           : d2d::Color::RGB(0x1C, 0x1C, 0x1C);

        dc.fillRoundedRectangle(rect, background, 7.0f * scale);
        dc.drawRoundedRectangle(rect, selected ? d2d::Color::RGB(0x62, 0x92, 0xBC) : d2d::Color::RGB(0x48, 0x48, 0x48),
                                7.0f * scale, 1.0f * scale);
        dc.drawText(rect, label, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 11.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return hovering && justClicked[0];
    };

    if (drawButton(allOnRect, L"All On")) {
        bool changed = false;
        for (const auto& entry : entries)
            changed |= XRayTargets::setSelected(entry.namespacedId, true);
        if (changed) NexusConfig::save();
        playClickSound();
    }

    if (drawButton(allOffRect, L"All Off")) {
        bool changed = false;
        for (const auto& entry : entries)
            changed |= XRayTargets::setSelected(entry.namespacedId, false);
        if (changed) NexusConfig::save();
        playClickSound();
    }

    if (drawButton(selectedOnlyRect, L"Selected Only", selectedOnly)) {
        selectedOnly = !selectedOnly;
        scroll = 0.0f;
        lerpScroll = 0.0f;
        playClickSound();
    }

    int blockListColumns = std::clamp(NexusConfig::blockListColumns, 1, 3);

    std::wstring layoutLabel = blockListColumns == 1 ? L"List" : blockListColumns == 2 ? L"Grid" : L"Compact Grid";

    bool layoutSelectorHovered = shouldSelect(layoutSelectorRect, cursorPos);
    if (layoutSelectorHovered) cursor = Cursor::Hand;

    dc.fillRoundedRectangle(
        layoutSelectorRect,
        layoutSelectorHovered ? d2d::Color::RGB(0x2D, 0x2D, 0x2D) : d2d::Color::RGB(0x1C, 0x1C, 0x1C), 7.0f * scale);
    dc.drawRoundedRectangle(layoutSelectorRect,
                            layoutDropdownOpen ? d2d::Color::RGB(0x62, 0x92, 0xBC) : d2d::Color::RGB(0x48, 0x48, 0x48),
                            7.0f * scale, layoutDropdownOpen ? 1.5f * scale : 1.0f * scale);

    d2d::Rect layoutTextRect = { layoutSelectorRect.left + 10.0f * scale, layoutSelectorRect.top,
                                 layoutSelectorRect.right - 28.0f * scale, layoutSelectorRect.bottom };
    dc.drawText(layoutTextRect, layoutLabel, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 11.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect layoutArrowRect = { layoutSelectorRect.right - 28.0f * scale, layoutSelectorRect.top,
                                  layoutSelectorRect.right - 5.0f * scale, layoutSelectorRect.bottom };
    dc.drawText(layoutArrowRect, layoutDropdownOpen ? L"\u25B4" : L"\u25BE", d2d::Color::RGB(0xC8, 0xC8, 0xC8),
                Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (layoutSelectorHovered && justClicked[0]) {
        layoutDropdownOpen = !layoutDropdownOpen;
        layoutConsumedClick = true;
        playClickSound();
    }

    d2d::Rect layoutOptionRects[3];
    for (int index = 0; index < 3; ++index) {
        float top = layoutMenuTop + static_cast<float>(index) * (layoutOptionHeight + layoutOptionGap);
        layoutOptionRects[index] = { layoutMenuRect.left, top, layoutMenuRect.right, top + layoutOptionHeight };
    }

    if (layoutDropdownOpen) {
        for (int index = 0; index < 3; ++index) {
            bool hovering = shouldSelect(layoutOptionRects[index], cursorPos);
            if (hovering) cursor = Cursor::Hand;

            if (hovering && justClicked[0] && !layoutConsumedClick) {
                int newColumnCount = index + 1;
                if (NexusConfig::blockListColumns != newColumnCount) {
                    NexusConfig::blockListColumns = newColumnCount;
                    NexusConfig::save();
                    scroll = 0.0f;
                    lerpScroll = 0.0f;
                    playClickSound();
                }
                layoutDropdownOpen = false;
                layoutConsumedClick = true;
                break;
            }
        }

        bool overMenu = shouldSelect(layoutMenuRect, cursorPos);
        if (justClicked[0] && !layoutConsumedClick && !layoutSelectorHovered && !overMenu) {
            layoutDropdownOpen = false;
            layoutConsumedClick = true;
        }
    }

    // Filter.
    std::wstring rawSearch = lowerText(searchBox.getText());
    std::wstring normalizedSearch = normalizeSearchText(searchBox.getText());

    std::vector<BlockEntry*> visibleEntries;
    visibleEntries.reserve(entries.size());

    for (auto& entry : entries) {
        bool selected = XRayTargets::isSelected(entry.namespacedId);
        if (selectedOnly && !selected) continue;

        if (rawSearch.empty()) {
            visibleEntries.push_back(&entry);
            continue;
        }

        std::wstring nameRaw = lowerText(entry.name);
        std::wstring idRaw = lowerText(entry.ids);
        std::wstring nameNormalized = normalizeSearchText(entry.name);
        std::wstring idNormalized = normalizeSearchText(entry.ids);

        bool matches = nameRaw.find(rawSearch) != std::wstring::npos || idRaw.find(rawSearch) != std::wstring::npos ||
                       (!normalizedSearch.empty() && (nameNormalized.find(normalizedSearch) != std::wstring::npos ||
                                                      idNormalized.find(normalizedSearch) != std::wstring::npos));

        if (matches) visibleEntries.push_back(&entry);
    }

    // List/grid.
    d2d::Rect listRect = { panelRect.left + padding, helperTop + helperHeight + 10.0f * scale,
                           panelRect.right - padding, panelRect.bottom - 70.0f * scale };

    listLeft = listRect.left;
    listTop = listRect.top;
    listRight = listRect.right;
    listBottom = listRect.bottom;

    dc.fillRoundedRectangle(listRect, d2d::Color::RGB(0x09, 0x09, 0x09).asAlpha(0.55f), 10.0f * scale);
    dc.drawRoundedRectangle(listRect, d2d::Color::RGB(0x36, 0x36, 0x36), 10.0f * scale, 1.0f * scale);

    float innerPadding = 6.0f * scale;
    float rowHeight = 54.0f * scale;
    float rowGap = 6.0f * scale;
    float columnGap = 6.0f * scale;
    float scrollbarSpace = 10.0f * scale;
    int columnCount = std::clamp(NexusConfig::blockListColumns, 1, 3);

    float usableWidth = listRect.getWidth() - innerPadding * 2.0f - scrollbarSpace;
    float cardWidth = (usableWidth - columnGap * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount);

    std::size_t gridRowCount = visibleEntries.empty()
                                   ? 0
                                   : (visibleEntries.size() + static_cast<std::size_t>(columnCount) - 1) /
                                         static_cast<std::size_t>(columnCount);

    float contentHeight = innerPadding * 2.0f;
    if (gridRowCount > 0) {
        contentHeight += static_cast<float>(gridRowCount) * rowHeight;
        contentHeight += static_cast<float>(gridRowCount - 1) * rowGap;
    }

    scrollMax = std::max(0.0f, contentHeight - listRect.getHeight());
    scroll = std::clamp(scroll, 0.0f, scrollMax);
    lerpScroll = std::lerp(lerpScroll, scroll, std::clamp(Latite::getRenderer().getDeltaTime() / 5.0f, 0.0f, 1.0f));
    lerpScroll = std::clamp(lerpScroll, 0.0f, scrollMax);

    dc.ctx->PushAxisAlignedClip(listRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    if (visibleEntries.empty()) {
        dc.drawText(listRect, L"No matching blocks", d2d::Color::RGB(0x78, 0x78, 0x78),
                    Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    bool cursorOverLayoutMenu = layoutDropdownOpen && shouldSelect(layoutMenuRect, cursorPos);

    for (std::size_t index = 0; index < visibleEntries.size(); ++index) {
        BlockEntry* entry = visibleEntries[index];
        if (!entry) continue;

        int gridColumn = static_cast<int>(index % static_cast<std::size_t>(columnCount));
        int gridRow = static_cast<int>(index / static_cast<std::size_t>(columnCount));

        float left = listRect.left + innerPadding + static_cast<float>(gridColumn) * (cardWidth + columnGap);
        float top = listRect.top + innerPadding + static_cast<float>(gridRow) * (rowHeight + rowGap) - lerpScroll;

        d2d::Rect rowRect = { left, top, left + cardWidth, top + rowHeight };
        if (rowRect.bottom < listRect.top || rowRect.top > listRect.bottom) continue;

        bool hovering = !cursorOverLayoutMenu && listRect.contains(cursorPos) && shouldSelect(rowRect, cursorPos);
        if (hovering) cursor = Cursor::Hand;

        dc.fillRoundedRectangle(
            rowRect, hovering ? d2d::Color::RGB(0x25, 0x25, 0x25) : d2d::Color::RGB(0x17, 0x17, 0x17), 9.0f * scale);
        dc.drawRoundedRectangle(rowRect, d2d::Color::RGB(0x48, 0x48, 0x48).asAlpha(0.70f), 9.0f * scale, 1.0f * scale);

        float switchWidth = 48.0f * scale;
        float switchHeight = 22.0f * scale;
        d2d::Rect switchRect = { rowRect.right - switchWidth - 10.0f * scale, rowRect.center().y - switchHeight * 0.5f,
                                 rowRect.right - 10.0f * scale, rowRect.center().y + switchHeight * 0.5f };

        bool switchHovered =
            !cursorOverLayoutMenu && listRect.contains(cursorPos) && shouldSelect(switchRect, cursorPos);
        if (switchHovered) cursor = Cursor::Hand;

        bool enabledValue = XRayTargets::isSelected(entry->namespacedId);
        if (Nexus::UI::drawSwitch(dc, switchRect, enabledValue, switchHovered, justClicked[0] && !layoutConsumedClick,
                                  scale)) {
            if (XRayTargets::setSelected(entry->namespacedId, enabledValue)) NexusConfig::save();
            playClickSound();
        }

        d2d::Rect nameRect = { rowRect.left + 12.0f * scale, rowRect.top + 4.0f * scale,
                               switchRect.left - 10.0f * scale, rowRect.top + 29.0f * scale };
        dc.drawText(nameRect, entry->name, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 14.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        d2d::Rect idRect = { rowRect.left + 12.0f * scale, rowRect.top + 26.0f * scale, switchRect.left - 10.0f * scale,
                             rowRect.bottom - 4.0f * scale };
        dc.drawText(idRect, entry->ids, d2d::Color::RGB(0x91, 0x91, 0x91), Renderer::FontSelection::PrimaryRegular,
                    10.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    dc.ctx->PopAxisAlignedClip();

    // Scrollbar.
    if (scrollMax > 0.0f) {
        float trackWidth = 4.0f * scale;
        d2d::Rect trackRect = { listRect.right - 7.0f * scale, listRect.top + 6.0f * scale,
                                listRect.right - 3.0f * scale, listRect.bottom - 6.0f * scale };

        dc.fillRoundedRectangle(trackRect, d2d::Color::RGB(0x2D, 0x2D, 0x2D), trackWidth * 0.5f);

        float visibleRatio = listRect.getHeight() / std::max(contentHeight, 1.0f);
        float thumbHeight = std::max(28.0f * scale, trackRect.getHeight() * visibleRatio);
        thumbHeight = std::min(thumbHeight, trackRect.getHeight());

        float availableTrack = trackRect.getHeight() - thumbHeight;
        float scrollPercent = scrollMax > 0.0f ? lerpScroll / scrollMax : 0.0f;
        float thumbTop = trackRect.top + availableTrack * scrollPercent;

        d2d::Rect thumbRect = { trackRect.left, thumbTop, trackRect.right, thumbTop + thumbHeight };
        dc.fillRoundedRectangle(thumbRect, d2d::Color::RGB(0x72, 0x72, 0x72), trackWidth * 0.5f);
    }

    // Dropdown overlay is intentionally drawn after the clipped list.
    if (layoutDropdownOpen) {
        dc.fillRoundedRectangle(layoutMenuRect, d2d::Color::RGB(0x10, 0x10, 0x10).asAlpha(0.98f), 8.0f * scale);
        dc.drawRoundedRectangle(layoutMenuRect, d2d::Color::RGB(0x4F, 0x4F, 0x4F), 8.0f * scale, 1.0f * scale);

        const wchar_t* layoutNames[3] = { L"List", L"Grid", L"Compact Grid" };

        int activeColumns = std::clamp(NexusConfig::blockListColumns, 1, 3);

        for (int index = 0; index < 3; ++index) {
            const d2d::Rect& optionRect = layoutOptionRects[index];
            int optionColumns = index + 1;
            bool selected = optionColumns == activeColumns;
            bool hovering = shouldSelect(optionRect, cursorPos);

            d2d::Color background = selected   ? d2d::Color::RGB(0x34, 0x5E, 0x82)
                                    : hovering ? d2d::Color::RGB(0x2B, 0x2B, 0x2B)
                                               : d2d::Color::RGB(0x18, 0x18, 0x18);

            dc.fillRoundedRectangle(optionRect, background, 6.0f * scale);
            if (selected)
                dc.drawRoundedRectangle(optionRect, d2d::Color::RGB(0x62, 0x92, 0xBC), 6.0f * scale, 1.0f * scale);

            d2d::Rect optionTextRect = { optionRect.left + 10.0f * scale, optionRect.top,
                                         optionRect.right - 26.0f * scale, optionRect.bottom };
            dc.drawText(optionTextRect, layoutNames[index],
                        selected ? d2d::Colors::WHITE : d2d::Color::RGB(0xD0, 0xD0, 0xD0),
                        Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            if (selected) {
                d2d::Rect checkRect = { optionRect.right - 27.0f * scale, optionRect.top,
                                        optionRect.right - 7.0f * scale, optionRect.bottom };
                dc.drawText(checkRect, L"\u2713", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                            11.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
    }

    // Back.
    d2d::Rect backRect = { panelRect.left + padding, panelRect.bottom - 54.0f * scale,
                           panelRect.left + padding + 130.0f * scale, panelRect.bottom - 16.0f * scale };

    bool backHovered = !layoutDropdownOpen && shouldSelect(backRect, cursorPos);
    if (backHovered) cursor = Cursor::Hand;

    dc.fillRoundedRectangle(
        backRect, backHovered ? d2d::Color::RGB(0x35, 0x35, 0x35) : d2d::Color::RGB(0x20, 0x20, 0x20), 9.0f * scale);
    dc.drawRoundedRectangle(backRect, d2d::Color::RGB(0x55, 0x55, 0x55), 9.0f * scale, 1.0f * scale);
    dc.drawText(backRect, L"< Back", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 15.0f * scale,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (backHovered && justClicked[0]) {
        playClickSound();
        Latite::getScreenManager().showScreen<XRayScreen>(true);
        return;
    }
}
