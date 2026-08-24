#include <tina/ui/UITextSystem.hpp>

#include <type_traits>

static_assert(std::is_same_v<decltype(Tina::UI::UITextInputRouteResult{}.applied), bool>);
