#include "PrimaryWindowUIColorSchemeCoordinator.hpp"

#include <tina/ui/UIStyleController.hpp>

#include <utility>
#include <variant>

namespace Tina::Runtime::Detail {
namespace {

[[nodiscard]] UI::UIColorScheme toUIColorScheme(Platform::SystemColorScheme colorScheme) noexcept
{
    return colorScheme == Platform::SystemColorScheme::Light
               ? UI::UIColorScheme::Light
               : UI::UIColorScheme::Dark;
}

} // namespace

void PrimaryWindowUIColorSchemeCoordinator::observe(
    std::span<const Platform::PlatformEvent> events) noexcept
{
    for (const Platform::PlatformEvent& event : events)
    {
        if (std::holds_alternative<Platform::PlatformEventStreamReset>(event.payload))
        {
            return;
        }
    }

    for (const Platform::PlatformEvent& event : events)
    {
        const auto* changed =
            std::get_if<Platform::SystemColorSchemeChangedEvent>(&event.payload);
        if (changed != nullptr)
        {
            pendingColorScheme_ = toUIColorScheme(changed->colorScheme);
        }
    }
}

Core::Status PrimaryWindowUIColorSchemeCoordinator::apply(UI::UIContext* context)
{
    if (!pendingColorScheme_.has_value() || context == nullptr)
    {
        return Core::success();
    }

    const UI::UITheme& activeTheme = context->style().productTheme();
    if (activeTheme.colorScheme == *pendingColorScheme_)
    {
        pendingColorScheme_.reset();
        return Core::success();
    }

    const UI::UITheme candidate =
        UI::makeModernDesktopTheme(*pendingColorScheme_, activeTheme.density);
    if (Core::Status status = context->style().setProductTheme(candidate); !status)
    {
        auto error = std::move(status.error());
        error.addContext("PrimaryWindowUIColorSchemeCoordinator::apply");
        return Core::failure(std::move(error));
    }

    pendingColorScheme_.reset();
    return Core::success();
}

} // namespace Tina::Runtime::Detail
