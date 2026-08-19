#pragma once

#include <tina/ui/UIMenu.hpp>

namespace Tina::UI::Detail {

[[nodiscard]] constexpr bool isValidMenuCommand(UIMenuCommand command) noexcept
{
    return command >= UIMenuCommand::Previous && command <= UIMenuCommand::Dismiss;
}

} // namespace Tina::UI::Detail
