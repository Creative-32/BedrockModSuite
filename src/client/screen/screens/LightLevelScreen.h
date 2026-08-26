#pragma once

#include "../Screen.h"

#include <string>

class LightLevelScreen final : public Screen {
public:
    LightLevelScreen();

    std::string getName() override { return "LightLevel"; }

protected:
    void onEnable(bool ignoreAnimations) override;
    void onDisable() override;

private:
    void onRender(Event& event);

    std::string activeSliderId {};
    bool sliderDirty = false;
};
