#include <tina/ui/UIContextStatistics.hpp>

constexpr Tina::UI::UIContextStatistics DefaultStatistics{};
static_assert(DefaultStatistics.liveNodeCount == 0);
static_assert(DefaultStatistics.style.activeRuleCount == 0);
