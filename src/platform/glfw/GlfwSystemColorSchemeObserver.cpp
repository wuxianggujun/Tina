#include "GlfwSystemColorSchemeObserver.hpp"

#include <variant>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace Tina::Platform::Detail {

std::optional<SystemColorScheme> queryHostSystemColorScheme() noexcept
{
#if defined(_WIN32)
    DWORD appsUseLightTheme = 0;
    DWORD valueSize = sizeof(appsUseLightTheme);
    const LSTATUS status = ::RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &appsUseLightTheme,
        &valueSize);
    if (status != ERROR_SUCCESS || valueSize != sizeof(appsUseLightTheme))
    {
        return std::nullopt;
    }
    return appsUseLightTheme == 0 ? SystemColorScheme::Dark : SystemColorScheme::Light;
#else
    return std::nullopt;
#endif
}

GlfwSystemColorSchemeObserver::GlfwSystemColorSchemeObserver(
    bool enabled, GlfwSystemColorSchemeQuery query) noexcept
    : query_(query), enabled_(enabled)
{
}

std::optional<SystemColorScheme> GlfwSystemColorSchemeObserver::pendingPreference() const noexcept
{
    if (!enabled_ || query_ == nullptr)
    {
        return std::nullopt;
    }
    const std::optional<SystemColorScheme> observed = query_();
    if (!observed.has_value() || observed == publishedPreference_)
    {
        return std::nullopt;
    }
    return observed;
}

void GlfwSystemColorSchemeObserver::commitPublishedPreference(
    SystemColorScheme colorScheme, std::span<const PlatformEvent> publishedEvents) noexcept
{
    bool matchingPreferencePublished = false;
    for (const PlatformEvent& event : publishedEvents)
    {
        if (std::holds_alternative<PlatformEventStreamReset>(event.payload))
        {
            return;
        }
        const auto* changed = std::get_if<SystemColorSchemeChangedEvent>(&event.payload);
        if (changed != nullptr && changed->colorScheme == colorScheme)
        {
            matchingPreferencePublished = true;
        }
    }
    if (matchingPreferencePublished)
    {
        publishedPreference_ = colorScheme;
    }
}

} // namespace Tina::Platform::Detail
