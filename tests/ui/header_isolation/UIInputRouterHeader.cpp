#include <tina/ui/UIInputRouter.hpp>

#include <type_traits>

static_assert(std::is_same_v<decltype(Tina::UI::UIDefaultActionResult{}.consumed), bool>);
static_assert(std::is_same_v<decltype(Tina::UI::UIDefaultFocusStepResult{}.focus),
                             Tina::UI::UINodeId>);
