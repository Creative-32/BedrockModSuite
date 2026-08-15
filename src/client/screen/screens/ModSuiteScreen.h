#pragma once

#include "../Screen.h"

class ModSuiteScreen final : public Screen {
public:
    ModSuiteScreen();

    std::string getName() override { return "ModSuite"; }

protected:
    void onEnable(bool ignoreAnimations) override;
    void onDisable() override;

private:
    void onRender(Event& event);
};
