#pragma once

#include <chrono>
#include <string>

namespace Nexus {

    enum class NotificationType {
        Info,
        Success,
        Warning,
        Error
    };

    struct NexusNotification {
        std::wstring title;
        std::wstring message;

        NotificationType type = NotificationType::Info;

        float durationSeconds = 4.0f;

        std::string dedupeKey {};

        std::chrono::steady_clock::time_point createdAt {};

        bool closing = false;

        std::chrono::steady_clock::time_point closingAt {};
    };

} // namespace Nexus
