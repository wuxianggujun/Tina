#include <tina/ui/InputRouting.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::UI::InputTransitionConsumptionView>);
static_assert(std::is_trivially_copyable_v<Tina::UI::ContinuousControlClaim>);
static_assert(std::is_trivially_copyable_v<Tina::UI::ContinuousControlClaimsView>);

[[maybe_unused]] const Tina::UI::InputTransitionConsumptionView EmptyConsumption =
    Tina::UI::InputTransitionConsumptionView::None(Tina::Platform::PlatformFrameId{1}, 0);
