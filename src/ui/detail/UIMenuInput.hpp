#pragma once

#include <tina/ui/UIMenu.hpp>

namespace Tina::UI::Detail {

[[nodiscard]] constexpr bool isValidMenuCommand(UIMenuCommand command) noexcept
{
    return command >= UIMenuCommand::Previous &&
           command <= UIMenuCommand::CloseSubmenu;
}

[[nodiscard]] constexpr bool
isValidMenuInvocationCommand(UIMenuInvocationCommand command) noexcept
{
    return command >= UIMenuInvocationCommand::ContextMenuKey &&
           command <= UIMenuInvocationCommand::ShiftF10;
}

} // namespace Tina::UI::Detail
