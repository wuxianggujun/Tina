#include <gtest/gtest.h>

#include "core/Container.hpp"
#include "core/Core.hpp"
#include "core/Time.hpp"

#include <cstdint>
#include <type_traits>

namespace Tina::Tests {

TEST(LegacyCompatibilityTest, PreservesCoreAliasesAndContainerSemantics)
{
    static_assert(std::is_same_v<Tina::i8, std::int8_t>);
    static_assert(Core::MaxPathLength == 260U);

    constexpr int values[] = {1, 2, 3};
    static_assert(Tina::lengthOf(values) == 3U);
    EXPECT_EQ(Tina::lengthOf(values), 3U);

    Core::TimeConfig config;
    EXPECT_EQ(config.tick_rate, 60);

    static_assert(std::is_same_v<decltype(Container::Min(9, 4)), int>);
    static_assert(std::is_same_v<decltype(Container::Max(9, 4)), int>);
    EXPECT_EQ(Container::Min(9, 4), 4);
    EXPECT_EQ(Container::Max(9, 4), 9);

    struct Score {
        int value;
    };
    constexpr auto lessScore = [](const Score& left, const Score& right) {
        return left.value < right.value;
    };
    static_assert(std::is_same_v<decltype(Container::Min(Score{9}, Score{4}, lessScore)), Score>);
    static_assert(std::is_same_v<decltype(Container::Max(Score{9}, Score{4}, lessScore)), Score>);
    EXPECT_EQ(Container::Min(Score{9}, Score{4}, lessScore).value, 4);
    EXPECT_EQ(Container::Max(Score{9}, Score{4}, lessScore).value, 9);
}

} // namespace Tina::Tests
