#include "TestHarness.hpp"

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

void runEnumFlagsTests()
{
    static_assert(EnumFlagsEnabled<Permission>);
    static_assert(!EnumFlagsEnabled<PlainEnum>);

    Permission permissions = Permission::Read | Permission::Write;
    TINA_TEST_CHECK(isFlagSet(permissions, Permission::Read));
    TINA_TEST_CHECK(!isFlagSet(permissions, Permission::Execute));
    TINA_TEST_CHECK(hasAllFlags(permissions, Permission::Read | Permission::Write));
    TINA_TEST_CHECK(!hasAllFlags(Permission::Read, Permission::Read | Permission::Write));
    TINA_TEST_CHECK(hasAnyFlag(Permission::Read, Permission::Read | Permission::Execute));

    setFlag(permissions, Permission::Execute, true);
    TINA_TEST_CHECK(isFlagSet(permissions, Permission::Execute));
    setFlag(permissions, Permission::Write, false);
    TINA_TEST_CHECK(!isFlagSet(permissions, Permission::Write));
}

} // namespace Tina::Tests
