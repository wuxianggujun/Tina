#include <tina/core/text/Utf8.hpp>

#include <gtest/gtest.h>

#include <string_view>

namespace Tina::Core {
namespace {

using namespace std::literals;

TEST(Utf8Tests, CountsAsciiAndMultibyteScalars)
{
    EXPECT_EQ(countStrictUtf8CodepointsWithoutNul(""sv), 0U);
    EXPECT_EQ(countStrictUtf8CodepointsWithoutNul("Tina"sv), 4U);
    EXPECT_EQ(countStrictUtf8CodepointsWithoutNul("Tina引擎🙂"sv), 7U);
}

TEST(Utf8Tests, RejectsNulMalformedOverlongSurrogateAndOutOfRangeSequences)
{
    EXPECT_FALSE(isStrictUtf8WithoutNul(std::string_view{"A\0B", 3}));
    EXPECT_FALSE(isStrictUtf8WithoutNul("\xC0\xAF"sv));
    EXPECT_FALSE(isStrictUtf8WithoutNul("\xE2\x82"sv));
    EXPECT_FALSE(isStrictUtf8WithoutNul("\xED\xA0\x80"sv));
    EXPECT_FALSE(isStrictUtf8WithoutNul("\xF4\x90\x80\x80"sv));
}

} // namespace
} // namespace Tina::Core
