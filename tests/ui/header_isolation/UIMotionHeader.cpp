#include <tina/ui/UIMotion.hpp>

static_assert(Tina::UI::isValidUIEasing(Tina::UI::UIEasing::Linear));
static_assert(Tina::UI::isPaintOnlyAnimatableProperty(
    Tina::UI::UIAnimatableProperty::BackgroundColor));
static_assert(Tina::UI::evaluateUIEasing(Tina::UI::UIEasing::Linear, 0.0F) == 0.0F);
static_assert(Tina::UI::evaluateUIEasing(Tina::UI::UIEasing::Linear, 1.0F) == 1.0F);
constexpr Tina::UI::UIMotionStatistics DefaultMotionStatistics{};
static_assert(DefaultMotionStatistics.reservedTrackCount == 0);
static_assert(DefaultMotionStatistics.reservedTrackHighWater == 0);
