#include <tina/ui/UITextSystem.hpp>

#include <type_traits>
#include <utility>

static_assert(std::is_same_v<decltype(Tina::UI::UITextInputRouteResult{}.applied), bool>);
static_assert(std::is_same_v<decltype(std::declval<const Tina::UI::UITextSystem&>().measureText(
                                 std::string_view{}, Tina::UI::UITextStyle{})),
                             Tina::Core::Result<Tina::UI::UITextMetrics>>);
