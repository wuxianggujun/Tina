#include <tina/ui/UIMotionController.hpp>

#include <type_traits>
#include <utility>

using ReducedMotionResult = decltype(
    std::declval<const Tina::UI::UIMotionController&>().reducedMotion());
static_assert(std::is_same_v<ReducedMotionResult, bool>);
