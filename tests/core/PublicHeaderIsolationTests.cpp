#include <gtest/gtest.h>

#include <tina/core/base/Compiler.hpp>
#include <tina/core/base/EnumFlags.hpp>
#include <tina/core/base/Platform.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/base/SourceLocation.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/diagnostics/Assert.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/FixedStepAccumulator.hpp>
#include <tina/core/time/MonotonicClock.hpp>

#include <expected>
#include <type_traits>

namespace Tina::Tests {

TEST(PublicHeaderIsolationTest, PublicCoreSurfaceUsesOnlyTheInstalledIncludeRoot)
{
    static_assert(__cpp_lib_expected >= 202202L);
    static_assert(std::is_same_v<Core::Result<int>, std::expected<int, Core::Error>>);
    static_assert(Core::ProcessBitCount == sizeof(void*) * 8U);
    SUCCEED();
}

} // namespace Tina::Tests
