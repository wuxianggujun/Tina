#include <tina/gameplay/Scheduler.hpp>

#include <type_traits>

static_assert(!Tina::Gameplay::TimerId{}.hasValue());
// TimerDesc owns a MoveOnlyFunction, so it is move-only and not a literal type --
// which is why the defaults below are checked for type rather than by value.
static_assert(!std::is_copy_constructible_v<Tina::Gameplay::TimerDesc>);
static_assert(std::is_move_constructible_v<Tina::Gameplay::TimerDesc>);
static_assert(!std::is_copy_constructible_v<Tina::Gameplay::Scheduler>);
static_assert(std::is_move_constructible_v<Tina::Gameplay::Scheduler>);
// TimerEvent is a plain view passed to the callback by const reference.
static_assert(std::is_trivially_copyable_v<Tina::Gameplay::TimerEvent>);
