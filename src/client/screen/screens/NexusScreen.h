#pragma once

#include "../Screen.h"

class NexusScreen final : public Screen {
public:
    NexusScreen();

    std::string getName() override { return "Nexus"; }

protected:
    void onEnable(bool ignoreAnimations) override;
    void onDisable() override;

private:
    void onRender(Event& event);
};
