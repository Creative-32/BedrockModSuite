#include "pch.h"
#include "NexusControls.h"

namespace Nexus::UI {

    bool drawToggle(D2DUtil& dc, const d2d::Rect& rowRect, const std::wstring& label, bool& value, bool hovering,
                    bool clicked, float scale) {
        d2d::Color rowColor = hovering ? d2d::Color::RGB(0x25, 0x25, 0x25) : d2d::Color::RGB(0x17, 0x17, 0x17);

        dc.fillRoundedRectangle(rowRect, rowColor, 8.0f * scale);

        d2d::Rect labelRect = { rowRect.left + 12.0f * scale, rowRect.top, rowRect.right - 80.0f * scale,
                                rowRect.bottom };

        dc.drawText(labelRect, label, d2d::Color::RGB(0xE0, 0xE0, 0xE0), Renderer::FontSelection::PrimaryRegular,
                    15.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        float switchWidth = 52.0f * scale;
        float switchHeight = 24.0f * scale;

        d2d::Rect switchRect = { rowRect.right - switchWidth - 10.0f * scale, rowRect.center().y - switchHeight * 0.5f,
                                 rowRect.right - 10.0f * scale, rowRect.center().y + switchHeight * 0.5f };

        d2d::Color switchColor = value ? d2d::Color::RGB(0x42, 0x78, 0xA8) : d2d::Color::RGB(0x42, 0x42, 0x42);

        dc.fillRoundedRectangle(switchRect, switchColor, switchHeight * 0.5f);

        float knobSize = 18.0f * scale;

        float knobLeft = value ? switchRect.right - knobSize - 3.0f * scale : switchRect.left + 3.0f * scale;

        d2d::Rect knobRect = { knobLeft, switchRect.center().y - knobSize * 0.5f, knobLeft + knobSize,
                               switchRect.center().y + knobSize * 0.5f };

        dc.fillRoundedRectangle(knobRect, d2d::Colors::WHITE, knobSize * 0.5f);

        if (hovering && clicked) {
            value = !value;
            return true;
        }

        return false;
    }

    bool drawSlider(D2DUtil& dc, const d2d::Rect& rowRect, const std::wstring& label, int& value, int minimum,
                    int maximum, int step, bool percent, bool hovering, bool clicked, bool held, float cursorX,
                    float scale) {
        dc.fillRoundedRectangle(rowRect, d2d::Color::RGB(0x17, 0x17, 0x17), 8.0f * scale);

        std::wstring valueText = std::to_wstring(value);

        if (percent) {
            valueText += L"%";
        }

        d2d::Rect labelRect = { rowRect.left + 12.0f * scale, rowRect.top, rowRect.left + 145.0f * scale,
                                rowRect.bottom };

        dc.drawText(labelRect, label, d2d::Color::RGB(0xE0, 0xE0, 0xE0), Renderer::FontSelection::PrimaryRegular,
                    15.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        d2d::Rect valueRect = { rowRect.right - 58.0f * scale, rowRect.top, rowRect.right - 10.0f * scale,
                                rowRect.bottom };

        dc.drawText(valueRect, valueText, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 14.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        d2d::Rect trackRect = { rowRect.left + 150.0f * scale, rowRect.center().y - 3.0f * scale,
                                rowRect.right - 72.0f * scale, rowRect.center().y + 3.0f * scale };

        float normalized = static_cast<float>(value - minimum) / static_cast<float>(maximum - minimum);

        normalized = std::clamp(normalized, 0.0f, 1.0f);

        dc.fillRoundedRectangle(trackRect, d2d::Color::RGB(0x3A, 0x3A, 0x3A), 3.0f * scale);

        d2d::Rect progressRect = { trackRect.left, trackRect.top, trackRect.left + trackRect.getWidth() * normalized,
                                   trackRect.bottom };

        dc.fillRoundedRectangle(progressRect, d2d::Color::RGB(0x42, 0x78, 0xA8), 3.0f * scale);

        float knobX = trackRect.left + trackRect.getWidth() * normalized;

        float knobRadius = 7.0f * scale;

        d2d::Rect knobRect = { knobX - knobRadius, rowRect.center().y - knobRadius, knobX + knobRadius,
                               rowRect.center().y + knobRadius };

        dc.fillRoundedRectangle(knobRect, d2d::Colors::WHITE, knobRadius);

        if (!hovering || (!clicked && !held)) {
            return false;
        }

        float mouseNormalized = (cursorX - trackRect.left) / trackRect.getWidth();

        mouseNormalized = std::clamp(mouseNormalized, 0.0f, 1.0f);

        float rawValue = static_cast<float>(minimum) + mouseNormalized * static_cast<float>(maximum - minimum);

        int steppedValue = static_cast<int>(std::round(rawValue / static_cast<float>(step))) * step;

        int newValue = std::clamp(steppedValue, minimum, maximum);

        if (newValue == value) {
            return false;
        }

        value = newValue;
        return true;
    }

    bool drawSwitch(D2DUtil& dc, const d2d::Rect& switchRect, bool& value, bool hovering, bool clicked, float scale) {
        d2d::Color switchColor;

        if (value) {
            switchColor = hovering ? d2d::Color::RGB(0x4B, 0x86, 0xBA) : d2d::Color::RGB(0x42, 0x78, 0xA8);
        } else {
            switchColor = hovering ? d2d::Color::RGB(0x50, 0x50, 0x50) : d2d::Color::RGB(0x3C, 0x3C, 0x3C);
        }

        dc.fillRoundedRectangle(switchRect, switchColor, switchRect.getHeight() * 0.5f);

        float knobSize = switchRect.getHeight() - 6.0f * scale;

        float knobLeft = value ? switchRect.right - knobSize - 3.0f * scale : switchRect.left + 3.0f * scale;

        d2d::Rect knobRect = { knobLeft, switchRect.center().y - knobSize * 0.5f, knobLeft + knobSize,
                               switchRect.center().y + knobSize * 0.5f };

        dc.fillRoundedRectangle(knobRect, d2d::Colors::WHITE, knobSize * 0.5f);

        if (hovering && clicked) {
            value = !value;
            return true;
        }

        return false;
    }

} // namespace Nexus::UI

