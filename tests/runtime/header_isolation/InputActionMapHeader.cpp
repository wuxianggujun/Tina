#include <tina/runtime/InputActionMap.hpp>

#include <type_traits>

static_assert(std::is_copy_constructible_v<Tina::InputActionBinding>);
static_assert(std::is_copy_constructible_v<Tina::RebindOptions>);
static_assert(std::is_same_v<decltype(Tina::RebindCommitResult{}.conflictingBindings),
                             std::span<const Tina::InputBindingId>>);
static_assert(std::is_same_v<decltype(Tina::InputActionMapConfig{}.validate()), Tina::Core::Status>);
static_assert(Tina::InputActionMapCapacityConfig::DefaultActionBindingCapacity == 64);
static_assert(Tina::InputActionMapCapacityConfig::DefaultSimulationActionTransitionCapacity == 128);
