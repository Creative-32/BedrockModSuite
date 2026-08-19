#pragma once

#include "../Screen.h"

class XRayScreen final : public Screen {
public:
    XRayScreen();

    std::string getName() override { return "XRay"; }

protected:
    void onEnable(bool ignoreAnimations) override;
    void onDisable() override;

private:
    void onRender(Event& event);
};
