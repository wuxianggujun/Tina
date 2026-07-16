#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace Tina {

using i8 = std::int8_t;
using u8 = std::uint8_t;
using i16 = std::int16_t;
using u16 = std::uint16_t;
using i32 = std::int32_t;
using u32 = std::uint32_t;
using i64 = std::int64_t;
using u64 = std::uint64_t;
using isize = std::ptrdiff_t;
using usize = std::size_t;
using uintptr = std::uintptr_t;

template <typename Value, std::size_t Count>
[[nodiscard]] constexpr u32 lengthOf(const Value (&)[Count]) noexcept
{
    static_assert(Count <= static_cast<std::size_t>((std::numeric_limits<u32>::max)()));
    return static_cast<u32>(Count);
}

static_assert(sizeof(uintptr) == sizeof(void*));
static_assert(sizeof(i64) == 8);
static_assert(sizeof(i32) == 4);
static_assert(sizeof(i16) == 2);
static_assert(sizeof(i8) == 1);

} // namespace Tina

namespace Tina::Core {

using Tina::i8;
using Tina::i16;
using Tina::i32;
using Tina::i64;
using Tina::isize;
using Tina::u8;
using Tina::u16;
using Tina::u32;
using Tina::u64;
using Tina::uintptr;
using Tina::usize;

} // namespace Tina::Core
