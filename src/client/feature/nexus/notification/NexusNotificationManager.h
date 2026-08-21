#pragma once

#include "NexusNotification.h"

#include "client/event/Event.h"
#include "client/event/Listener.h"

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nexus {

    class NexusNotificationManager final : public Listener {
    public:
        static void initialize();

        static void push(const std::wstring& title, const std::wstring& message,
                         NotificationType type = NotificationType::Info, float durationSeconds = 0.0f,
                         const std::string& dedupeKey = {});

        static void clear();

        static const std::vector<NexusNotification>& getHistory();

        [[nodiscard]]
        bool shouldListen() override {
            return true;
        }

    private:
        NexusNotificationManager();

        static NexusNotificationManager& instance();

        void onRender(Event& event);

        void pushInternal(const std::wstring& title, const std::wstring& message, NotificationType type,
                          float durationSeconds, const std::string& dedupeKey);

        void update();

        void promoteQueued();

        static float getDefaultDuration(NotificationType type);

        std::deque<NexusNotification> pending {};
        std::vector<NexusNotification> active {};
        std::vector<NexusNotification> history {};

        std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastDedupe {};

        std::mutex mutex;

        static constexpr std::size_t MaxVisible = 3;
        static constexpr std::size_t MaxHistory = 50;
    };

} // namespace Nexus
