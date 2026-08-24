#include <tina/ui/UIStyleController.hpp>

#include <type_traits>
#include <utility>

using ThemeResult = decltype(
    std::declval<const Tina::UI::UIStyleController&>().productTheme());
static_assert(std::is_same_v<ThemeResult, const Tina::UI::UITheme&>);
