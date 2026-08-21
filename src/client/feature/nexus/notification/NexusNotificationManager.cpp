#include "pch.h"

#include "NexusNotificationManager.h"

#include "client/Latite.h"
#include "client/event/Eventing.h"
#include "client/event/events/RenderOverlayEvent.h"

#include "util/DrawContext.h"

#include <algorithm>
#include <chrono>

namespace Nexus {

    namespace {

        constexpr auto AnimationDuration = std::chrono::milliseconds(180);

        constexpr auto DedupeDuration = std::chrono::milliseconds(1500);

        float clamp01(float value) {
            return std::clamp(value, 0.0f, 1.0f);
        }

        float smoothStep(float value) {
            value = clamp01(value);

            return value * value * (3.0f - 2.0f * value);
        }

        d2d::Color getAccentColor(NotificationType type) {
            switch (type) {
            case NotificationType::Success:
                return d2d::Color::RGB(0x48, 0xA0, 0x6A);

            case NotificationType::Warning:
                return d2d::Color::RGB(0xD0, 0x8A, 0x38);

            case NotificationType::Error:
                return d2d::Color::RGB(0xC9, 0x4A, 0x4A);

            case NotificationType::Info:
            default:
                return d2d::Color::RGB(0x42, 0x78, 0xA8);
            }
        }

    } // namespace

    NexusNotificationManager& NexusNotificationManager::instance() {
        static NexusNotificationManager manager;

        return manager;
    }

    NexusNotificationManager::NexusNotificationManager() {
        Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&NexusNotificationManager::onRender, -100,
                                                   true);
    }

    void NexusNotificationManager::initialize() {
        (void)instance();
    }

    float NexusNotificationManager::getDefaultDuration(NotificationType type) {
        switch (type) {
        case NotificationType::Warning:
        case NotificationType::Error:
            return 6.0f;

        case NotificationType::Success:
        case NotificationType::Info:
        default:
            return 4.0f;
        }
    }

    void NexusNotificationManager::push(const std::wstring& title, const std::wstring& message, NotificationType type,
                                        float durationSeconds, const std::string& dedupeKey) {
        initialize();

        instance().pushInternal(title, message, type, durationSeconds, dedupeKey);
    }

    void NexusNotificationManager::pushInternal(const std::wstring& title, const std::wstring& message,
                                                NotificationType type, float durationSeconds,
                                                const std::string& dedupeKey) {
        std::lock_guard lock(mutex);

        auto now = std::chrono::steady_clock::now();

        //
        // DEDUPE / RATE LIMIT
        //

        if (!dedupeKey.empty()) {
            auto found = lastDedupe.find(dedupeKey);

            if (found != lastDedupe.end()) {
                if (now - found->second < DedupeDuration) {
                    return;
                }
            }

            lastDedupe[dedupeKey] = now;
        }

        NexusNotification notification;

        notification.title = title;

        notification.message = message;

        notification.type = type;

        notification.durationSeconds = durationSeconds > 0.0f ? durationSeconds : getDefaultDuration(type);

        notification.dedupeKey = dedupeKey;

        //
        // createdAt is assigned when the
        // notification actually becomes visible.
        //

        notification.createdAt = {};

        pending.push_back(notification);

        //
        // HISTORY
        //

        history.push_back(notification);

        if (history.size() > MaxHistory) {
            history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - MaxHistory));
        }
    }

    void NexusNotificationManager::promoteQueued() {
        auto now = std::chrono::steady_clock::now();

        while (active.size() < MaxVisible && !pending.empty()) {
            NexusNotification next = std::move(pending.front());

            pending.pop_front();

            next.createdAt = now;

            next.closing = false;

            active.push_back(std::move(next));
        }
    }

    void NexusNotificationManager::update() {
        auto now = std::chrono::steady_clock::now();

        promoteQueued();

        for (auto& notification : active) {
            if (!notification.closing) {
                float ageSeconds = std::chrono::duration<float>(now - notification.createdAt).count();

                if (ageSeconds >= notification.durationSeconds) {
                    notification.closing = true;

                    notification.closingAt = now;
                }
            }
        }

        std::erase_if(active, [&](const NexusNotification& notification) {
            if (!notification.closing) {
                return false;
            }

            return now - notification.closingAt >= AnimationDuration;
        });

        promoteQueued();
    }

    void NexusNotificationManager::clear() {
        auto& manager = instance();

        std::lock_guard lock(manager.mutex);

        manager.pending.clear();
        manager.active.clear();
    }

    const std::vector<NexusNotification>& NexusNotificationManager::getHistory() {
        return instance().history;
    }

    void NexusNotificationManager::onRender(Event&) {
        std::lock_guard lock(mutex);

        update();

        if (active.empty()) {
            return;
        }

        D2DUtil dc;

        D2D1_SIZE_F screenSize = Latite::getRenderer().getScreenSize();

        float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);

        float width = 350.0f * scale;

        float height = 72.0f * scale;

        float gap = 10.0f * scale;

        float rightMargin = 22.0f * scale;

        float bottomMargin = 22.0f * scale;

        auto now = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < active.size(); ++i) {
            auto& notification = active[i];

            float animation = 1.0f;

            //
            // ENTER
            //

            if (!notification.closing) {
                float enterProgress =
                    static_cast<float>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - notification.createdAt).count()) /
                    static_cast<float>(AnimationDuration.count());

                animation = smoothStep(enterProgress);
            }

            //
            // EXIT
            //

            if (notification.closing) {
                float exitProgress =
                    static_cast<float>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - notification.closingAt).count()) /
                    static_cast<float>(AnimationDuration.count());

                animation = 1.0f - smoothStep(exitProgress);
            }

            float targetRight = screenSize.width - rightMargin;

            float slideDistance = width + 30.0f * scale;

            float right = targetRight + (1.0f - animation) * slideDistance;

            float bottom = screenSize.height - bottomMargin - static_cast<float>(i) * (height + gap);

            float top = bottom - height;

            d2d::Rect notificationRect = { right - width, top, right, bottom };

            //
            // BACKGROUND
            //

            d2d::Color background = d2d::Color::RGB(0x10, 0x10, 0x10).asAlpha(0.94f * animation);

            dc.fillRoundedRectangle(notificationRect, background, 10.0f * scale);

            dc.drawRoundedRectangle(notificationRect, d2d::Color::RGB(0x48, 0x48, 0x48).asAlpha(0.80f * animation),
                                    10.0f * scale, 1.0f * scale);

            //
            // ACCENT BAR
            //

            d2d::Rect accentRect = { notificationRect.left + 5.0f * scale, notificationRect.top + 7.0f * scale,
                                     notificationRect.left + 9.0f * scale, notificationRect.bottom - 7.0f * scale };

            d2d::Color accent = getAccentColor(notification.type).asAlpha(animation);

            dc.fillRoundedRectangle(accentRect, accent, 2.0f * scale);

            //
            // TITLE
            //

            d2d::Rect titleRect = { notificationRect.left + 20.0f * scale, notificationRect.top + 8.0f * scale,
                                    notificationRect.right - 12.0f * scale, notificationRect.top + 32.0f * scale };

            dc.drawText(titleRect, notification.title, d2d::Colors::WHITE.asAlpha(animation),
                        Renderer::FontSelection::PrimaryRegular, 14.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            //
            // MESSAGE
            //

            d2d::Rect messageRect = { notificationRect.left + 20.0f * scale, notificationRect.top + 30.0f * scale,
                                      notificationRect.right - 12.0f * scale, notificationRect.bottom - 7.0f * scale };

            dc.drawText(messageRect, notification.message, d2d::Color::RGB(0xB8, 0xB8, 0xB8).asAlpha(animation),
                        Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

} // namespace Nexus
