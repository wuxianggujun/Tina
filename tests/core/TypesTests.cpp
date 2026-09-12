#include <gtest/gtest.h>

#include <tina/core/base/Platform.hpp>
#include <tina/core/base/Types.hpp>

#include <cstdint>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace Tina::Tests {

TEST(TypesTest, PlatformAndIntegerAliasesMatchTheProcess)
{
    static_assert(std::is_same_v<i8, std::int8_t>);
    static_assert(std::is_same_v<u8, std::uint8_t>);
    static_assert(std::is_same_v<i16, std::int16_t>);
    static_assert(std::is_same_v<u16, std::uint16_t>);
    static_assert(std::is_same_v<i32, std::int32_t>);
    static_assert(std::is_same_v<u32, std::uint32_t>);
    static_assert(std::is_same_v<i64, std::int64_t>);
    static_assert(std::is_same_v<u64, std::uint64_t>);
    static_assert(std::is_same_v<usize, std::size_t>);
    static_assert(std::is_same_v<isize, std::ptrdiff_t>);
    static_assert(std::is_same_v<uintptr, std::uintptr_t>);
    static_assert(Core::ProcessBitCount == sizeof(void*) * 8U);
    static_assert(Core::CurrentOperatingSystem != Core::OperatingSystem::Unknown);
    static_assert(Core::CurrentCompiler != Core::Compiler::Unknown);

    EXPECT_EQ(sizeof(uintptr), sizeof(void*));
    EXPECT_TRUE(Core::IsWindows || Core::IsLinux || Core::IsMacOS);
}

TEST(TypesTest, CoreNamesAreTheSameCanonicalTypesNotWrappers)
{
    static_assert(std::is_same_v<Core::i8, i8>);
    static_assert(std::is_same_v<Core::u8, u8>);
    static_assert(std::is_same_v<Core::i16, i16>);
    static_assert(std::is_same_v<Core::u16, u16>);
    static_assert(std::is_same_v<Core::i32, i32>);
    static_assert(std::is_same_v<Core::u32, u32>);
    static_assert(std::is_same_v<Core::i64, i64>);
    static_assert(std::is_same_v<Core::u64, u64>);
    static_assert(std::is_same_v<Core::usize, usize>);
    static_assert(std::is_same_v<Core::isize, isize>);
    static_assert(std::is_same_v<Core::uintptr, uintptr>);
    static_assert(sizeof(Core::usize) == sizeof(void*));
    static_assert(sizeof(Core::isize) == sizeof(void*));
    static_assert(std::numeric_limits<Core::isize>::is_signed);
    static_assert(!std::numeric_limits<Core::usize>::is_signed);
    EXPECT_EQ(sizeof(Core::u64), 8U);
}

TEST(TypesTest, ArrayLengthUsesTheCanonicalSizeType)
{
    constexpr int values[]{1, 2, 3, 4};
    static_assert(lengthOf(values) == 4U);
    static_assert(std::is_same_v<decltype(lengthOf(values)), Core::u32>);
    EXPECT_EQ(lengthOf(values), 4U);
}

} // namespace Tina::Tests
