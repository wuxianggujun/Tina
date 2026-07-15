#include "TestHarness.hpp"

#include "core/Container.hpp"
#include "core/Core.hpp"
#include "core/Time.hpp"

#include <cstdint>
#include <type_traits>

namespace Tina::Tests {

void runLegacyCompatibilityTests()
{
    static_assert(std::is_same_v<Tina::i8, std::int8_t>);
    static_assert(Core::MaxPathLength == 260U);

    constexpr int values[] = {1, 2, 3};
    static_assert(Tina::lengthOf(values) == 3U);
    TINA_TEST_CHECK(Tina::lengthOf(values) == 3U);

    Core::TimeConfig config;
    TINA_TEST_CHECK(config.tick_rate == 60);

    // Scalar EASTL min/max overloads return by value. The compatibility wrappers must also
    // return by value so calls with temporaries cannot produce dangling references.
    static_assert(std::is_same_v<decltype(Container::Min(9, 4)), int>);
    static_assert(std::is_same_v<decltype(Container::Max(9, 4)), int>);
    TINA_TEST_CHECK(Container::Min(9, 4) == 4);
    TINA_TEST_CHECK(Container::Max(9, 4) == 9);

    struct Score {
        int value;
    };
    constexpr auto lessScore = [](const Score& left, const Score& right) {
        return left.value < right.value;
    };
    static_assert(std::is_same_v<decltype(Container::Min(Score{9}, Score{4}, lessScore)), Score>);
    static_assert(std::is_same_v<decltype(Container::Max(Score{9}, Score{4}, lessScore)), Score>);
    TINA_TEST_CHECK(Container::Min(Score{9}, Score{4}, lessScore).value == 4);
    TINA_TEST_CHECK(Container::Max(Score{9}, Score{4}, lessScore).value == 9);
}

} // namespace Tina::Tests
