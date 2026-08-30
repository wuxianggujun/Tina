#include <tina/core/base/MoveOnlyFunction.hpp>

#include <memory>
#include <type_traits>

// The point of the type is that a move-only target is accepted and a copy is not.
using OwningFactory = Tina::Core::MoveOnlyFunction<std::unique_ptr<int>()>;

static_assert(!std::is_copy_constructible_v<OwningFactory>);
static_assert(!std::is_copy_assignable_v<OwningFactory>);
static_assert(std::is_nothrow_move_constructible_v<OwningFactory>);
static_assert(std::is_nothrow_move_assignable_v<OwningFactory>);
static_assert(std::is_invocable_r_v<std::unique_ptr<int>, OwningFactory&>);
