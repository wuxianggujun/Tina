#pragma once

#include <tina/ui/UITabView.hpp>

namespace Tina::UI::Detail {

[[nodiscard]] constexpr bool isValidTabViewCommand(UITabViewCommand command) noexcept
{
    return command >= UITabViewCommand::Previous && command <= UITabViewCommand::Last;
}

} // namespace Tina::UI::Detail
