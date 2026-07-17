#include <tina/platform/Input.hpp>

#include <type_traits>

static_assert(Tina::Platform::KeyCount > 0);
static_assert(std::is_copy_constructible_v<Tina::Platform::InputTransition>);
