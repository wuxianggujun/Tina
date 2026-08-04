#include <gtest/gtest.h>

#include <tina/core/trace/Trace.hpp>

namespace Tina::Tests {

#if defined(TINA_TRACE_BACKEND_NONE)

namespace {

struct TraceBitFieldValue {
    unsigned enabled : 1;
};

// sizeof(bit-field) is ill-formed. This compiles only when the None frontend
// discards its argument before C++ semantic analysis.
[[maybe_unused]] void compileAwayTraceArgument([[maybe_unused]] TraceBitFieldValue value)
{
    TINA_TRACE_ZONE(sizeof(value.enabled));
}

} // namespace

TEST(TraceCompileTest, NoneBackendDoesNotEvaluateZoneArgument)
{
    int evaluationCount = 0;
    TINA_TRACE_ZONE(++evaluationCount);

    EXPECT_EQ(evaluationCount, 0);
}

#elif defined(TINA_TRACE_BACKEND_ENABLED)

TEST(TraceCompileTest, EnabledBackendSupportsNestedZones)
{
    TINA_TRACE_ZONE("Tests.Trace.Outer");
    {
        TINA_TRACE_ZONE("Tests.Trace.Inner");
    }

    SUCCEED();
}

#else
#error "TraceCompileTests requires a selected Tina trace backend"
#endif

} // namespace Tina::Tests
