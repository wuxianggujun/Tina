#include <tina/runtime/InputActions.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::InputActionId>);
static_assert(!Tina::InputActionId{}.hasValue());
static_assert(Tina::InputActionId{1}.hasValue());
