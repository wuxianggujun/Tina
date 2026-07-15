#include <gtest/gtest.h>

#include "core/base/EnumFlags.hpp"
#include "core/base/Types.hpp"

namespace Tina::Tests {

enum class Permission : u8 {
    None = 0,
    Read = 1U << 0U,
    Write = 1U << 1U,
    Execute = 1U << 2U,
};

enum class PlainEnum : u8 {
    Value,
};

} // namespace Tina::Tests

template <>
struct Tina::EnableEnumFlags<Tina::Tests::Permission> : std::true_type {
};

namespace Tina::Tests {

TEST(EnumFlagsTest, SupportsTypedBitOperations)
{
    static_assert(EnumFlagsEnabled<Permission>);
    static_assert(!EnumFlagsEnabled<PlainEnum>);

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

} // namespace Tina::Tests
