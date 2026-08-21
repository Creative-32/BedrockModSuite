#pragma once

#include "client/Latite.h"
#include "client/screen/ScreenManager.h"
#include "client/screen/screens/NexusScreen.h"

namespace Nexus {

    class NexusNavigation final {
    public:
        template<typename T>
        static void openSubmenu(NexusScreen& source) {
            source.prepareForSubmenu();

            Latite::getScreenManager().showScreen<T>();
        }

        static void backToHub() { Latite::getScreenManager().showScreen<NexusScreen>(true); }

        static void closeToGame() { Latite::getScreenManager().exitCurrentScreen(); }
    };

} // namespace Nexus
