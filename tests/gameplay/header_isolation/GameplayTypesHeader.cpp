#include <tina/gameplay/GameplayTypes.hpp>

// count == 0 without infinite is rejected rather than reinterpreted as "forever";
// see ADR 0036 D13.
static_assert(Tina::Gameplay::Repeat::forever().isValid());
static_assert(Tina::Gameplay::Repeat::times(3).isValid());
static_assert(!Tina::Gameplay::Repeat{.count = 0}.isValid());
static_assert(!Tina::Gameplay::Repeat::forever().isComplete(1000));
static_assert(Tina::Gameplay::Repeat::times(2).isComplete(2));
