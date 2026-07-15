#include "TestHarness.hpp"

#include "core/base/Platform.hpp"
#include "core/base/Types.hpp"

#include <cstdint>
#include <type_traits>

namespace Tina::Tests {

void runTypesTests()
{
    static_assert(std::is_same_v<i8, std::int8_t>);
    static_assert(std::is_same_v<u64, std::uint64_t>);
    static_assert(std::is_same_v<uintptr, std::uintptr_t>);
    static_assert(Core::ProcessBitCount == sizeof(void*) * 8U);
    static_assert(Core::CurrentOperatingSystem != Core::OperatingSystem::Unknown);
    static_assert(Core::CurrentCompiler != Core::Compiler::Unknown);

    TINA_TEST_CHECK(sizeof(uintptr) == sizeof(void*));
    TINA_TEST_CHECK(Core::IsWindows || Core::IsLinux || Core::IsMacOS);
}

} // namespace Tina::Tests
