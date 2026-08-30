#include <tina/core/base/CancellationSignal.hpp>

#include <type_traits>

// A signal is the owner and must not be relocated out from under the tokens viewing it.
static_assert(!std::is_copy_constructible_v<Tina::Core::CancellationSignal>);
static_assert(!std::is_move_constructible_v<Tina::Core::CancellationSignal>);
static_assert(!std::is_copy_assignable_v<Tina::Core::CancellationSignal>);
static_assert(!std::is_move_assignable_v<Tina::Core::CancellationSignal>);

// A token is a cheap non-owning view, passed by value down a synchronous call chain.
static_assert(std::is_trivially_copyable_v<Tina::Core::CancellationToken>);
static_assert(std::is_nothrow_default_constructible_v<Tina::Core::CancellationToken>);
static_assert(std::is_nothrow_destructible_v<Tina::Core::CancellationToken>);
// Explicit so a CancellationSignal never converts to a token implicitly at a call boundary.
static_assert(!std::is_convertible_v<Tina::Core::CancellationSignal&, Tina::Core::CancellationToken>);
static_assert(std::is_constructible_v<Tina::Core::CancellationToken, Tina::Core::CancellationSignal&>);
