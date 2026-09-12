#include <tina/core/base/Types.hpp>

static_assert(sizeof(Tina::Core::u8) == 1);
static_assert(sizeof(Tina::Core::u16) == 2);
static_assert(sizeof(Tina::Core::u32) == 4);
static_assert(sizeof(Tina::Core::u64) == 8);
static_assert(sizeof(Tina::Core::usize) == sizeof(void*));
static_assert(sizeof(Tina::Core::isize) == sizeof(void*));
static_assert(sizeof(Tina::Core::uintptr) == sizeof(void*));

namespace {
constexpr Tina::Core::u8 values[]{1, 2, 3};
static_assert(Tina::lengthOf(values) == 3);
}
