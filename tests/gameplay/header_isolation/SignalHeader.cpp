#include <tina/gameplay/Signal.hpp>

#include <type_traits>

// Move-only: destruction or reset() unsubscribes, which is what makes the
// registration scoped rather than unsubscribe-by-callback.
static_assert(!std::is_copy_constructible_v<Tina::Gameplay::SignalSubscription>);
static_assert(std::is_move_constructible_v<Tina::Gameplay::SignalSubscription>);
static_assert(!std::is_copy_constructible_v<Tina::Gameplay::Signal<Tina::Gameplay::Unit>>);
// Instantiated here so the header-isolation TU actually compiles the template
// body rather than only parsing its declaration.
template class Tina::Gameplay::Signal<Tina::Gameplay::Unit>;
