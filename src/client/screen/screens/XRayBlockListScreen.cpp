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
#include "client/feature/nexus/xray/XRaySettings.h"

#include "client/screen/ScreenManager.h"

#include "util/DrawContext.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

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

    bool overList =
        cursorPos.x >= listLeft && cursorPos.x <= listRight && cursorPos.y >= listTop && cursorPos.y <= listBottom;

    if (!overList) {
        return;
    }

    scroll = std::clamp(scroll - static_cast<float>(clickEvent.getWheelDelta()) / 3.0f, 0.0f, scrollMax);

    clickEvent.setCancelled(true);
}

void XRayBlockListScreen::onChar(Event& event) {
    if (!isActive() || !searchBox.isSelected()) {
        return;
    }

    auto& charEvent = reinterpret_cast<CharEvent&>(event);

    if (!charEvent.isChar()) {
        return;
    }

    std::wstring oldText = searchBox.getText();

    searchBox.onChar(charEvent.getChar());

    if (oldText != searchBox.getText()) {
        scroll = 0.0f;
        lerpScroll = 0.0f;
    }

    charEvent.setCancelled(true);
}

void XRayBlockListScreen::onKey(Event& event) {
    if (!isActive() || !searchBox.isSelected()) {
        return;
    }

    auto& keyEvent = reinterpret_cast<KeyUpdateEvent&>(event);

    if (!keyEvent.isDown()) {
        return;
    }

    int pressedKey = keyEvent.getKey();

    if (pressedKey == VK_ESCAPE) {
        searchBox.reset();
        searchBox.setSelected(false);

        scroll = 0.0f;
        lerpScroll = 0.0f;

        keyEvent.setCancelled(true);

        return;
    }

    if (pressedKey == VK_LEFT || pressedKey == VK_RIGHT) {
        searchBox.onKeyDown(pressedKey);
    }

    keyEvent.setCancelled(true);
}

void XRayBlockListScreen::onRender(Event&) {
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
    // PANEL
    // ============================================================
    //

    float panelWidth = std::min(screenSize.width * 0.82f, 920.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.86f, 720.0f * scale);

    d2d::Rect panelRect = { (screenSize.width - panelWidth) * 0.5f, (screenSize.height - panelHeight) * 0.5f,

                            (screenSize.width + panelWidth) * 0.5f, (screenSize.height + panelHeight) * 0.5f };

    float padding = 26.0f * scale;

    dc.fillRoundedRectangle(panelRect, d2d::Color::RGB(0x0B, 0x0B, 0x0B).asAlpha(0.95f), 18.0f * scale);

    dc.drawRoundedRectangle(panelRect, d2d::Color::RGB(0x45, 0x45, 0x45).asAlpha(0.75f), 18.0f * scale, 1.5f * scale);

    //
    // ============================================================
    // TITLE
    // ============================================================
    //

    d2d::Rect titleRect = { panelRect.left + padding, panelRect.top + 10.0f * scale,

                            panelRect.right - padding, panelRect.top + 48.0f * scale };

    dc.drawText(titleRect, L"Block List", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryLight, 27.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect subtitleRect = { panelRect.left + padding, panelRect.top + 42.0f * scale,

                               panelRect.right - padding, panelRect.top + 66.0f * scale };

    dc.drawText(subtitleRect, L"Choose which blocks X-Ray should highlight", d2d::Color::RGB(0x9A, 0x9A, 0x9A),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // RUNTIME BLOCK CATALOG
    // ============================================================
    //
    // XRayBlockCatalog currently learns blocks encountered by the
    // scanner.
    //
    // Built-in ore IDs connect directly to the existing ore settings.
    //
    // Other blocks are visible/searchable now, but remain disabled
    // until the persistent custom-target system is added.
    //

    struct BlockEntry {
        std::wstring name;
        std::wstring ids;

        std::string namespacedId;

        bool* enabled = nullptr;
    };

    std::vector<BlockEntry> entries;

    const auto& catalogEntries = XRayBlockCatalog::getEntries();

    entries.reserve(catalogEntries.size());

    //
    // ============================================================
    // BUILT-IN ORE SETTING LOOKUP
    // ============================================================
    //

    auto getBuiltInSetting = [&](const std::string& id) -> bool* {
        if (id == "minecraft:diamond_ore" || id == "minecraft:deepslate_diamond_ore") {
            return &xRaySettings.diamond;
        }

        if (id == "minecraft:emerald_ore" || id == "minecraft:deepslate_emerald_ore") {
            return &xRaySettings.emerald;
        }

        if (id == "minecraft:gold_ore" || id == "minecraft:deepslate_gold_ore" || id == "minecraft:nether_gold_ore") {
            return &xRaySettings.gold;
        }

        if (id == "minecraft:iron_ore" || id == "minecraft:deepslate_iron_ore") {
            return &xRaySettings.iron;
        }

        if (id == "minecraft:copper_ore" || id == "minecraft:deepslate_copper_ore") {
            return &xRaySettings.copper;
        }

        if (id == "minecraft:redstone_ore" || id == "minecraft:deepslate_redstone_ore" ||
            id == "minecraft:lit_redstone_ore") {
            return &xRaySettings.redstone;
        }

        if (id == "minecraft:lapis_ore" || id == "minecraft:deepslate_lapis_ore") {
            return &xRaySettings.lapis;
        }

        if (id == "minecraft:coal_ore" || id == "minecraft:deepslate_coal_ore") {
            return &xRaySettings.coal;
        }

        if (id == "minecraft:ancient_debris") {
            return &xRaySettings.ancientDebris;
        }

        return nullptr;
    };

    for (const auto& catalogEntry : catalogEntries) {
        entries.push_back({ catalogEntry.displayName,

                            std::wstring(catalogEntry.namespacedId.begin(), catalogEntry.namespacedId.end()),

                            catalogEntry.namespacedId,

                            getBuiltInSetting(catalogEntry.namespacedId) });
    }

    //
    // ============================================================
    // SELECTED COUNT
    // ============================================================
    //

    int selectedCount = 0;

    for (const auto& entry : entries) {
        if (entry.enabled != nullptr && *entry.enabled) {
            ++selectedCount;
        }
    }

    d2d::Rect selectedRect = { panelRect.right - padding - 180.0f * scale,

                               titleRect.top,

                               panelRect.right - padding,

                               titleRect.bottom };

    dc.drawText(selectedRect,

                std::to_wstring(selectedCount) + L" selected",

                d2d::Color::RGB(0x92, 0x92, 0x92),

                Renderer::FontSelection::PrimaryRegular,

                12.0f * scale,

                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // SEARCH
    // ============================================================
    //

    d2d::Rect searchRect = { panelRect.left + padding, panelRect.top + 78.0f * scale,

                             panelRect.right - padding, panelRect.top + 112.0f * scale };

    bool hasSearch = !searchBox.getText().empty();

    d2d::Rect searchTextRect = { searchRect.left + 11.0f * scale,

                                 searchRect.top + 4.0f * scale,

                                 searchRect.right - (hasSearch ? 36.0f * scale : 10.0f * scale),

                                 searchRect.bottom - 4.0f * scale };

    searchBox.setRect(searchTextRect);

    d2d::Rect clearRect = { searchRect.right - 29.0f * scale,

                            searchRect.top + 5.0f * scale,

                            searchRect.right - 6.0f * scale,

                            searchRect.bottom - 5.0f * scale };

    bool searchHovered = shouldSelect(searchRect, cursorPos);

    bool clearHovered = hasSearch && shouldSelect(clearRect, cursorPos);

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

            playClickSound();
        } else {
            searchBox.setSelected(searchHovered);
        }
    }

    d2d::Color searchBackground = searchBox.isSelected() ? d2d::Color::RGB(0x24, 0x24, 0x24)

                                  : searchHovered ? d2d::Color::RGB(0x20, 0x20, 0x20)

                                                  : d2d::Color::RGB(0x17, 0x17, 0x17);

    dc.fillRoundedRectangle(searchRect, searchBackground, 8.0f * scale);

    dc.drawRoundedRectangle(searchRect,

                            searchBox.isSelected() ? d2d::Color::RGB(0x42, 0x78, 0xA8)

                                                   : d2d::Color::RGB(0x48, 0x48, 0x48),

                            8.0f * scale,

                            searchBox.isSelected() ? 1.5f * scale : 1.0f * scale);

    searchBox.render(dc, 0.0f,

                     d2d::Color::RGB(0x00, 0x00, 0x00).asAlpha(0.0f),

                     d2d::Colors::WHITE,

                     DWRITE_TEXT_ALIGNMENT_LEADING);

    if (searchBox.getText().empty() && !searchBox.isSelected()) {
        dc.drawText(searchTextRect,

                    L"Search blocks...",

                    d2d::Color::RGB(0x82, 0x82, 0x82),

                    Renderer::FontSelection::PrimaryRegular,

                    13.0f * scale,

                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (hasSearch) {
        dc.fillRoundedRectangle(clearRect,

                                clearHovered ? d2d::Color::RGB(0x45, 0x45, 0x45)

                                             : d2d::Color::RGB(0x2A, 0x2A, 0x2A),

                                6.0f * scale);

        dc.drawText(clearRect,

                    L"X",

                    d2d::Colors::WHITE,

                    Renderer::FontSelection::PrimaryRegular,

                    11.0f * scale,

                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    //
    // ============================================================
    // HELPER BUTTONS + LAYOUT DROPDOWN
    // ============================================================
    //

    float helperTop = searchRect.bottom + 8.0f * scale;

    float helperHeight = 26.0f * scale;

    float helperGap = 7.0f * scale;

    float helperWidth = 110.0f * scale;

    d2d::Rect allOnRect = { searchRect.left, helperTop, searchRect.left + helperWidth, helperTop + helperHeight };

    d2d::Rect allOffRect = { allOnRect.right + helperGap, helperTop, allOnRect.right + helperGap + helperWidth,
                             helperTop + helperHeight };

    d2d::Rect selectedOnlyRect = { allOffRect.right + helperGap, helperTop,
                                   allOffRect.right + helperGap + 125.0f * scale, helperTop + helperHeight };

    //
    // ============================================================
    // LAYOUT SELECTOR
    // ============================================================
    //

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

        if (hovering) {
            cursor = Cursor::Hand;
        }

        d2d::Color background;

        if (selected) {
            background = d2d::Color::RGB(0x34, 0x5E, 0x82);
        }

        else if (hovering) {
            background = d2d::Color::RGB(0x2D, 0x2D, 0x2D);
        }

        else {
            background = d2d::Color::RGB(0x1C, 0x1C, 0x1C);
        }

        dc.fillRoundedRectangle(rect, background, 7.0f * scale);

        dc.drawRoundedRectangle(rect, selected ? d2d::Color::RGB(0x62, 0x92, 0xBC) : d2d::Color::RGB(0x48, 0x48, 0x48),
                                7.0f * scale, 1.0f * scale);

        dc.drawText(rect, label, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 11.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        return hovering && justClicked[0];
    };

    //
    // ============================================================
    // ALL ON
    // ============================================================
    //

    if (drawButton(allOnRect, L"All On")) {
        for (auto& entry : entries) {
            if (entry.enabled != nullptr) {
                *entry.enabled = true;
            }
        }

        NexusConfig::save();

        playClickSound();
    }

    //
    // ============================================================
    // ALL OFF
    // ============================================================
    //

    if (drawButton(allOffRect, L"All Off")) {
        for (auto& entry : entries) {
            if (entry.enabled != nullptr) {
                *entry.enabled = false;
            }
        }

        NexusConfig::save();

        playClickSound();
    }

    //
    // ============================================================
    // SELECTED ONLY
    // ============================================================
    //

    if (drawButton(selectedOnlyRect, L"Selected Only", selectedOnly)) {
        selectedOnly = !selectedOnly;

        scroll = 0.0f;
        lerpScroll = 0.0f;

        playClickSound();
    }

    //
    // ============================================================
    // CURRENT LAYOUT LABEL
    // ============================================================
    //

    int blockListColumns = std::clamp(NexusConfig::blockListColumns, 1, 3);

    std::wstring layoutLabel;

    switch (blockListColumns) {
    case 2:
        layoutLabel = L"Grid";
        break;

    case 1:
        layoutLabel = L"List";
        break;

    case 3:
    default:
        layoutLabel = L"Compact Grid";
        break;
    }

    bool layoutSelectorHovered = shouldSelect(layoutSelectorRect, cursorPos);

    if (layoutSelectorHovered) {
        cursor = Cursor::Hand;
    }

    d2d::Color layoutSelectorBackground =
        layoutSelectorHovered ? d2d::Color::RGB(0x2D, 0x2D, 0x2D) : d2d::Color::RGB(0x1C, 0x1C, 0x1C);

    dc.fillRoundedRectangle(layoutSelectorRect, layoutSelectorBackground, 7.0f * scale);

    dc.drawRoundedRectangle(layoutSelectorRect,
                            layoutDropdownOpen ? d2d::Color::RGB(0x62, 0x92, 0xBC) : d2d::Color::RGB(0x48, 0x48, 0x48),
                            7.0f * scale, layoutDropdownOpen ? 1.5f * scale : 1.0f * scale);

    //
    // Text.
    //
    // Separate the arrow from the text so it remains aligned.
    //

    d2d::Rect layoutTextRect = { layoutSelectorRect.left + 10.0f * scale,

                                 layoutSelectorRect.top,

                                 layoutSelectorRect.right - 28.0f * scale,

                                 layoutSelectorRect.bottom };

    dc.drawText(layoutTextRect, layoutLabel, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 11.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect layoutArrowRect = { layoutSelectorRect.right - 28.0f * scale,

                                  layoutSelectorRect.top,

                                  layoutSelectorRect.right - 5.0f * scale,

                                  layoutSelectorRect.bottom };

    dc.drawText(layoutArrowRect, layoutDropdownOpen ? L"\u25B4" : L"\u25BE", d2d::Color::RGB(0xC8, 0xC8, 0xC8),
                Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // OPEN / CLOSE DROPDOWN
    // ============================================================
    //

    if (layoutSelectorHovered && justClicked[0]) {
        layoutDropdownOpen = !layoutDropdownOpen;

        layoutConsumedClick = true;

        playClickSound();
    }

    //
    // ============================================================
    // DROPDOWN OPTION GEOMETRY
    // ============================================================
    //

    d2d::Rect layoutOptionRects[3];

    for (int index = 0; index < 3; ++index) {
        float top = layoutMenuTop + static_cast<float>(index) * (layoutOptionHeight + layoutOptionGap);

        layoutOptionRects[index] = { layoutMenuRect.left, top, layoutMenuRect.right, top + layoutOptionHeight };
    }

    //
    // ============================================================
    // OPTION INTERACTION
    // ============================================================
    //

    if (layoutDropdownOpen) {
        for (int index = 0; index < 3; ++index) {
            bool hovering = shouldSelect(layoutOptionRects[index], cursorPos);

            if (hovering) {
                cursor = Cursor::Hand;
            }

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

        //
        // Click outside closes the dropdown.
        //

        bool overMenu = shouldSelect(layoutMenuRect, cursorPos);

        if (justClicked[0] && !layoutConsumedClick && !layoutSelectorHovered && !overMenu) {
            layoutDropdownOpen = false;
        }
    }

    //
    // ============================================================
    // FILTER
    // ============================================================
    //

    std::wstring search = searchBox.getText();

    std::transform(search.begin(), search.end(), search.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });

    std::vector<BlockEntry*> visibleEntries;

    for (auto& entry : entries) {
        //
        // Selected Only shows only entries connected to an enabled
        // target.
        //
        // Custom blocks are currently nullptr, so they are hidden in
        // Selected Only mode until the custom-target system exists.
        //

        if (selectedOnly && (entry.enabled == nullptr || !*entry.enabled)) {
            continue;
        }

        if (search.empty()) {
            visibleEntries.push_back(&entry);

            continue;
        }

        std::wstring name = entry.name;

        std::wstring ids = entry.ids;

        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t value) {
            return static_cast<wchar_t>(std::towlower(value));
        });

        std::transform(ids.begin(), ids.end(), ids.begin(), [](wchar_t value) {
            return static_cast<wchar_t>(std::towlower(value));
        });

        if (name.find(search) != std::wstring::npos || ids.find(search) != std::wstring::npos) {
            visibleEntries.push_back(&entry);
        }
    }
    //
    // ============================================================
    // LIST
    // ============================================================
    //

    d2d::Rect listRect = { panelRect.left + padding,

                           helperTop + helperHeight + 10.0f * scale,

                           panelRect.right - padding,

                           panelRect.bottom - 70.0f * scale };

    listLeft = listRect.left;
    listTop = listRect.top;
    listRight = listRect.right;
    listBottom = listRect.bottom;

    dc.fillRoundedRectangle(listRect, d2d::Color::RGB(0x09, 0x09, 0x09).asAlpha(0.55f), 10.0f * scale);

    dc.drawRoundedRectangle(listRect, d2d::Color::RGB(0x36, 0x36, 0x36), 10.0f * scale, 1.0f * scale);

    //
    // ============================================================
    // GRID LAYOUT
    // ============================================================
    //

    float innerPadding = 6.0f * scale;

    float rowHeight = 54.0f * scale;

    float rowGap = 6.0f * scale;

    float columnGap = 6.0f * scale;

    float scrollbarSpace = 10.0f * scale;

    int columnCount = std::clamp(NexusConfig::blockListColumns, 1, 3);

    float usableWidth = listRect.getWidth() - innerPadding * 2.0f - scrollbarSpace;

    float cardWidth = (usableWidth - columnGap * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount);

    //
    // Number of horizontal rows in the grid.
    //
    // Examples:
    //
    // 8 blocks / 1 column = 8 rows
    // 8 blocks / 2 columns = 4 rows
    // 8 blocks / 3 columns = 3 rows
    //

    std::size_t gridRowCount = 0;

    if (!visibleEntries.empty()) {
        gridRowCount =
            (visibleEntries.size() + static_cast<std::size_t>(columnCount) - 1) / static_cast<std::size_t>(columnCount);
    }

    //
    // ============================================================
    // CONTENT HEIGHT
    // ============================================================
    //

    float contentHeight = innerPadding * 2.0f;

    if (gridRowCount > 0) {
        contentHeight += static_cast<float>(gridRowCount) * rowHeight;

        contentHeight += static_cast<float>(gridRowCount - 1) * rowGap;
    }

    scrollMax = std::max(0.0f, contentHeight - listRect.getHeight());

    scroll = std::clamp(scroll, 0.0f, scrollMax);

    lerpScroll = std::lerp(lerpScroll, scroll, std::clamp(Latite::getRenderer().getDeltaTime() / 5.0f, 0.0f, 1.0f));

    lerpScroll = std::clamp(lerpScroll, 0.0f, scrollMax);

    //
    // ============================================================
    // CLIP BLOCK LIST
    // ============================================================
    //

    dc.ctx->PushAxisAlignedClip(listRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    if (visibleEntries.empty()) {
        dc.drawText(listRect, L"No matching blocks", d2d::Color::RGB(0x78, 0x78, 0x78),
                    Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    //
    // ============================================================
    // BLOCK CARDS
    // ============================================================
    //

    for (std::size_t index = 0; index < visibleEntries.size(); ++index) {
        BlockEntry* entry = visibleEntries[index];

        if (entry == nullptr) {
            continue;
        }

        //
        // Convert linear entry index into grid row + column.
        //

        int gridColumn = static_cast<int>(index % static_cast<std::size_t>(columnCount));

        int gridRow = static_cast<int>(index / static_cast<std::size_t>(columnCount));

        float left = listRect.left + innerPadding + static_cast<float>(gridColumn) * (cardWidth + columnGap);

        float top = listRect.top + innerPadding + static_cast<float>(gridRow) * (rowHeight + rowGap) - lerpScroll;

        d2d::Rect rowRect = { left, top, left + cardWidth, top + rowHeight };

        //
        // Don't draw rows completely outside the viewport.
        //

        if (rowRect.bottom < listRect.top || rowRect.top > listRect.bottom) {
            continue;
        }

        //
        // Prevent cards underneath the open layout dropdown
        // from receiving hover/click input.
        //

        bool cursorOverLayoutMenu = layoutDropdownOpen && shouldSelect(layoutMenuRect, cursorPos);

        bool hovering = !cursorOverLayoutMenu && listRect.contains(cursorPos) && shouldSelect(rowRect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        dc.fillRoundedRectangle(
            rowRect, hovering ? d2d::Color::RGB(0x25, 0x25, 0x25) : d2d::Color::RGB(0x17, 0x17, 0x17), 9.0f * scale);

        dc.drawRoundedRectangle(rowRect, d2d::Color::RGB(0x48, 0x48, 0x48).asAlpha(0.70f), 9.0f * scale, 1.0f * scale);

        //
        // ========================================================
        // SWITCH
        // ========================================================
        //

        float switchWidth = 48.0f * scale;

        float switchHeight = 22.0f * scale;

        d2d::Rect switchRect = { rowRect.right - switchWidth - 10.0f * scale,

                                 rowRect.center().y - switchHeight * 0.5f,

                                 rowRect.right - 10.0f * scale,

                                 rowRect.center().y + switchHeight * 0.5f };

        bool selectable = entry->enabled != nullptr;

        bool switchHovered =
            selectable && !cursorOverLayoutMenu && listRect.contains(cursorPos) && shouldSelect(switchRect, cursorPos);

        if (switchHovered) {
            cursor = Cursor::Hand;
        }

        if (selectable) {
            if (Nexus::UI::drawSwitch(dc, switchRect, *entry->enabled, switchHovered,
                                      justClicked[0] && !layoutConsumedClick, scale)) {
                NexusConfig::save();

                playClickSound();
            }
        }

        else {
            //
            // Custom block targets are not wired up yet,
            // so render a disabled switch.
            //

            bool disabledValue = false;

            Nexus::UI::drawSwitch(dc, switchRect, disabledValue, false, false, scale);
        }

        //
        // ========================================================
        // BLOCK NAME
        // ========================================================
        //

        d2d::Rect nameRect = { rowRect.left + 12.0f * scale,

                               rowRect.top + 4.0f * scale,

                               switchRect.left - 10.0f * scale,

                               rowRect.top + 29.0f * scale };

        dc.drawText(nameRect, entry->name, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 14.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        //
        // ========================================================
        // NAMESPACED ID
        // ========================================================
        //

        d2d::Rect idRect = { rowRect.left + 12.0f * scale,

                             rowRect.top + 26.0f * scale,

                             switchRect.left - 10.0f * scale,

                             rowRect.bottom - 4.0f * scale };

        dc.drawText(idRect, entry->ids, d2d::Color::RGB(0x91, 0x91, 0x91), Renderer::FontSelection::PrimaryRegular,
                    10.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    dc.ctx->PopAxisAlignedClip();

    //
    // ============================================================
    // SCROLLBAR
    // ============================================================
    //

    if (scrollMax > 0.0f) {
        float trackWidth = 4.0f * scale;

        d2d::Rect trackRect = { listRect.right - 7.0f * scale,

                                listRect.top + 6.0f * scale,

                                listRect.right - 3.0f * scale,

                                listRect.bottom - 6.0f * scale };

        dc.fillRoundedRectangle(trackRect,

                                d2d::Color::RGB(0x2D, 0x2D, 0x2D),

                                trackWidth * 0.5f);

        float visibleRatio = listRect.getHeight() / std::max(contentHeight, 1.0f);

        float thumbHeight = std::max(28.0f * scale,

                                     trackRect.getHeight() * visibleRatio);

        thumbHeight = std::min(thumbHeight, trackRect.getHeight());

        float availableTrack = trackRect.getHeight() - thumbHeight;

        float scrollPercent = scrollMax > 0.0f ? lerpScroll / scrollMax : 0.0f;

        float thumbTop = trackRect.top + availableTrack * scrollPercent;

        d2d::Rect thumbRect = { trackRect.left,

                                thumbTop,

                                trackRect.right,

                                thumbTop + thumbHeight };

        dc.fillRoundedRectangle(thumbRect,

                                d2d::Color::RGB(0x72, 0x72, 0x72),

                                trackWidth * 0.5f);
    }

    //
    // ============================================================
    // LAYOUT DROPDOWN OVERLAY
    // ============================================================
    //

    if (layoutDropdownOpen) {
        //
        // Outer menu background.
        //

        dc.fillRoundedRectangle(layoutMenuRect, d2d::Color::RGB(0x10, 0x10, 0x10).asAlpha(0.98f), 8.0f * scale);

        dc.drawRoundedRectangle(layoutMenuRect, d2d::Color::RGB(0x4F, 0x4F, 0x4F), 8.0f * scale, 1.0f * scale);

        const wchar_t* layoutNames[3] = { L"List", L"Grid", L"Compact Grid" };

        int activeColumns = std::clamp(NexusConfig::blockListColumns, 1, 3);

        for (int index = 0; index < 3; ++index) {
            const d2d::Rect& optionRect = layoutOptionRects[index];

            int optionColumns = index + 1;

            bool selected = optionColumns == activeColumns;

            bool hovering = shouldSelect(optionRect, cursorPos);

            d2d::Color background;

            if (selected) {
                background = d2d::Color::RGB(0x34, 0x5E, 0x82);
            }

            else if (hovering) {
                background = d2d::Color::RGB(0x2B, 0x2B, 0x2B);
            }

            else {
                background = d2d::Color::RGB(0x18, 0x18, 0x18);
            }

            dc.fillRoundedRectangle(optionRect, background, 6.0f * scale);

            if (selected) {
                dc.drawRoundedRectangle(optionRect, d2d::Color::RGB(0x62, 0x92, 0xBC), 6.0f * scale, 1.0f * scale);
            }

            d2d::Rect optionTextRect = { optionRect.left + 10.0f * scale,

                                         optionRect.top,

                                         optionRect.right - 26.0f * scale,

                                         optionRect.bottom };

            dc.drawText(optionTextRect, layoutNames[index],
                        selected ? d2d::Colors::WHITE : d2d::Color::RGB(0xD0, 0xD0, 0xD0),
                        Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            if (selected) {
                d2d::Rect checkRect = { optionRect.right - 27.0f * scale,

                                        optionRect.top,

                                        optionRect.right - 7.0f * scale,

                                        optionRect.bottom };

                dc.drawText(checkRect, L"\u2713", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                            11.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
    }

    //
    // ============================================================
    // BACK
    // ============================================================
    //

    d2d::Rect backRect = { panelRect.left + padding,

                           panelRect.bottom - 54.0f * scale,

                           panelRect.left + padding + 130.0f * scale,

                           panelRect.bottom - 16.0f * scale };

    bool backHovered = shouldSelect(backRect, cursorPos);

    if (backHovered) {
        cursor = Cursor::Hand;
    }

    dc.fillRoundedRectangle(backRect,

                            backHovered ? d2d::Color::RGB(0x35, 0x35, 0x35)

                                        : d2d::Color::RGB(0x20, 0x20, 0x20),

                            9.0f * scale);

    dc.drawRoundedRectangle(backRect,

                            d2d::Color::RGB(0x55, 0x55, 0x55),

                            9.0f * scale, 1.0f * scale);

    dc.drawText(backRect,

                L"< Back",

                d2d::Colors::WHITE,

                Renderer::FontSelection::PrimaryRegular,

                15.0f * scale,

                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (backHovered && justClicked[0]) {
        playClickSound();

        Latite::getScreenManager().showScreen<XRayScreen>(true);

        return;
    }
}
