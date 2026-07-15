#include "TestHarness.hpp"

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

} // namespace

void runScopeExitTests()
{
    bool invoked = false;
    {
        auto guard = Core::makeScopeExit([&invoked]() noexcept { invoked = true; });
        TINA_TEST_CHECK(!invoked);
    }
    TINA_TEST_CHECK(invoked);

    invoked = false;
    {
        auto guard = Core::makeScopeExit([&invoked]() noexcept { invoked = true; });
        guard.release();
    }
    TINA_TEST_CHECK(!invoked);

    int movedValue = 0;
    {
        auto guard = Core::makeScopeExit(
            [state = std::make_unique<int>(42), &movedValue]() noexcept {
                movedValue = *state;
            });
        static_assert(!std::is_copy_constructible_v<decltype(guard)>);
    }
    TINA_TEST_CHECK(movedValue == 42);
}

} // namespace Tina::Tests
