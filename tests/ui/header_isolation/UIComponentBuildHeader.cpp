#include <tina/ui/UIComponentBuild.hpp>

#include <type_traits>

static_assert(std::is_aggregate_v<Tina::UI::UIBehaviorSlotBudget>);
static_assert(std::is_aggregate_v<Tina::UI::UIComponentBuildBudget>);
static_assert(std::is_aggregate_v<Tina::UI::UIComponentBuildPoolStatistics>);
static_assert(std::is_aggregate_v<Tina::UI::UIComponentBehaviorBuildStatistics>);
static_assert(std::is_aggregate_v<Tina::UI::UIComponentBuildStatistics>);
static_assert(Tina::UI::UIComponentBuildBudget{}.nodes == 0);
