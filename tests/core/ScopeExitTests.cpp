#include <gtest/gtest.h>

#include <tina/core/base/ScopeExit.hpp>

#include <memory>
#include <type_traits>

namespace Tina::Tests {
namespace {

struct ThrowingCallback {
    void operator()() const { }
};

struct ThrowingMoveCallback {
    ThrowingMoveCallback() = default;
    ThrowingMoveCallback(ThrowingMoveCallback&&) noexcept(false) { }
    void operator()() noexcept { }
};

struct ThrowingCopyCallback {
    ThrowingCopyCallback() = default;
    ThrowingCopyCallback(const ThrowingCopyCallback&) noexcept(false) { }
    ThrowingCopyCallback(ThrowingCopyCallback&&) noexcept = default;
    void operator()() noexcept { }
};

template <typename Callback>
concept SupportsScopeExit = requires {
    typename Core::ScopeExit<Callback>;
};

template <typename Callback>
concept SupportsMakeScopeExit = requires(Callback&& callback) {
    Core::makeScopeExit(std::forward<Callback>(callback));
};

static_assert(!SupportsScopeExit<ThrowingCallback>);
static_assert(!SupportsScopeExit<ThrowingMoveCallback>);
static_assert(SupportsScopeExit<ThrowingCopyCallback>);
static_assert(SupportsMakeScopeExit<ThrowingCopyCallback>);
static_assert(!SupportsMakeScopeExit<ThrowingCopyCallback&>);

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

    int moveInvocationCount = 0;
    {
        auto source = Core::makeScopeExit(
            [&moveInvocationCount]() noexcept { ++moveInvocationCount; });
        auto destination = std::move(source);
        (void)destination;
    }
    EXPECT_EQ(moveInvocationCount, 1);
}

} // namespace
} // namespace Tina::Tests
