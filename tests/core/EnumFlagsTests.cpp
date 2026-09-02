#include <gtest/gtest.h>

#include <tina/core/base/EnumFlags.hpp>
#include <tina/core/base/Types.hpp>

namespace Tina::Tests {

enum class Permission : u8 {
    None = 0,
    Read = 1U << 0U,
    Write = 1U << 1U,
    Execute = 1U << 2U,
};

TINA_ENUM_FLAG_OPERATORS(Permission);

} // namespace Tina::Tests

// The reason the macro expands in the enum's own namespace: for an enum type, ADL
// considers only its innermost enclosing namespace. An operator defined one level up
// is unreachable from here, so this translation unit fails to compile if the macro is
// ever moved into a parent namespace.
namespace {

constexpr auto adlFromGlobalScope = Tina::Tests::Permission::Read | Tina::Tests::Permission::Write;
static_assert(Tina::hasAllFlags(adlFromGlobalScope, Tina::Tests::Permission::Read));

} // namespace

namespace Tina::Tests {

TEST(EnumFlagsTest, SupportsTypedBitOperations)
{
    Permission permissions = Permission::Read | Permission::Write;
    EXPECT_TRUE(isFlagSet(permissions, Permission::Read));
    EXPECT_FALSE(isFlagSet(permissions, Permission::Execute));
    EXPECT_TRUE(hasAllFlags(permissions, Permission::Read | Permission::Write));
    EXPECT_FALSE(hasAllFlags(Permission::Read, Permission::Read | Permission::Write));
    EXPECT_TRUE(hasAnyFlag(Permission::Read, Permission::Read | Permission::Execute));

    setFlag(permissions, Permission::Execute, true);
    EXPECT_TRUE(isFlagSet(permissions, Permission::Execute));
    setFlag(permissions, Permission::Write, false);
    EXPECT_FALSE(isFlagSet(permissions, Permission::Write));
}

TEST(EnumFlagsTest, CompoundAssignmentAndExclusiveOrRoundTrip)
{
    Permission permissions = Permission::None;
    permissions |= Permission::Read;
    permissions |= Permission::Write;
    EXPECT_TRUE(hasAllFlags(permissions, Permission::Read | Permission::Write));

    permissions &= Permission::Read;
    EXPECT_TRUE(isFlagSet(permissions, Permission::Read));
    EXPECT_FALSE(isFlagSet(permissions, Permission::Write));

    permissions ^= Permission::Read;
    EXPECT_EQ(permissions, Permission::None);

    EXPECT_EQ(Permission::Read ^ Permission::Read, Permission::None);
    EXPECT_EQ(Permission::Read & Permission::Write, Permission::None);
}

} // namespace Tina::Tests
