#include <gtest/gtest.h>

#include "core/base/ScopeExit.hpp"

#include <memory>
#include <type_traits>

namespace Tina::Tests {
namespace {

struct ThrowingCallback {
    void operator()() const { }
};

template <typename Callback>
concept SupportsScopeExit = requires {
    typename Core::ScopeExit<Callback>;
};

static_assert(!SupportsScopeExit<ThrowingCallback>);

TEST(ScopeExitTest, RunsUnlessReleasedAndSupportsMoveOnlyCallbacks)
{
    bool invoked = false;
    {
        auto guard = Core::makeScopeExit([&invoked]() noexcept { invoked = true; });
        EXPECT_FALSE(invoked);
    }
    EXPECT_TRUE(invoked);

    invoked = false;
    {
        auto guard = Core::makeScopeExit([&invoked]() noexcept { invoked = true; });
        guard.release();
    }
    EXPECT_FALSE(invoked);

    int movedValue = 0;
    {
        auto guard = Core::makeScopeExit(
            [state = std::make_unique<int>(42), &movedValue]() noexcept {
                movedValue = *state;
            });
        static_assert(!std::is_copy_constructible_v<decltype(guard)>);
    }
    EXPECT_EQ(movedValue, 42);
}

} // namespace
} // namespace Tina::Tests
