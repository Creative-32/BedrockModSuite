#pragma once

#include "util/DrawContext.h"

#include <string>

namespace Nexus::UI {

    bool drawToggle(D2DUtil& dc, const d2d::Rect& rowRect, const std::wstring& label, bool& value, bool hovering,
                    bool clicked, float scale);

    bool drawSlider(D2DUtil& dc, const d2d::Rect& rowRect, const std::wstring& label, int& value, int minimum,
                    int maximum, int step, bool percent, bool hovering, bool clicked, bool held, float cursorX,
                    float scale);
    
    bool drawSwitch(D2DUtil& dc, const d2d::Rect& switchRect, bool& value, bool hovering, bool clicked, float scale);

} // namespace Nexus::UI
