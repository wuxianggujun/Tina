#include <tina/core/text/ParseFloat.hpp>

#include <gtest/gtest.h>

#include <string>

namespace Tina::Tests {
namespace {

TEST(ParseStrictFloatTest, AcceptsTheDecimalSpellingsCookedTextUses)
{
    EXPECT_FLOAT_EQ(Core::parseStrictFloat("1.5").value(), 1.5F);
    EXPECT_FLOAT_EQ(Core::parseStrictFloat("-1.5").value(), -1.5F);
    EXPECT_FLOAT_EQ(Core::parseStrictFloat("0").value(), 0.0F);
    EXPECT_FLOAT_EQ(Core::parseStrictFloat("42").value(), 42.0F);
    EXPECT_FLOAT_EQ(Core::parseStrictFloat(".5").value(), 0.5F);
    EXPECT_FLOAT_EQ(Core::parseStrictFloat("1e3").value(), 1000.0F);
    EXPECT_FLOAT_EQ(Core::parseStrictFloat("1E-3").value(), 0.001F);
    EXPECT_FLOAT_EQ(Core::parseStrictFloat("+2.25").value(), 2.25F);
}

// The whole span must be consumed. A partial parse returning the leading number would let a
// malformed cooked field through as a plausible value, which is worse than rejecting it.
TEST(ParseStrictFloatTest, RejectsTrailingCharacters)
{
    EXPECT_FALSE(Core::parseStrictFloat("1.5abc").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("1.5 ").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("1.5,2.5").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("1..5").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("-").has_value());
    EXPECT_FALSE(Core::parseStrictFloat(".").has_value());
}

// strtof accepts all of these and std::from_chars accepts none, so the replacement has to
// reject them explicitly or swapping the implementation would silently widen the format.
TEST(ParseStrictFloatTest, RejectsTheSpellingsOnlyStrtofWouldAccept)
{
    EXPECT_FALSE(Core::parseStrictFloat(" 1.5").has_value()) << "leading whitespace is skipped by strtof";
    EXPECT_FALSE(Core::parseStrictFloat("\t1.5").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("0x1p3").has_value()) << "hex float";
    EXPECT_FALSE(Core::parseStrictFloat("inf").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("infinity").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("nan").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("NAN").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("-inf").has_value());
}

// An out-of-range exponent sets ERANGE and yields HUGE_VALF; that is a rejection, not a
// clamp, because a cooked field cannot mean "infinity".
TEST(ParseStrictFloatTest, RejectsOverflowAndOversizedText)
{
    EXPECT_FALSE(Core::parseStrictFloat("1e400").has_value());
    EXPECT_FALSE(Core::parseStrictFloat("-1e400").has_value());

    const std::string tooLong(Core::MaximumParsedFloatBytes + 1U, '1');
    EXPECT_FALSE(Core::parseStrictFloat(tooLong).has_value());

    // The advertised bound must itself be usable, or the boundary value is undeliverable.
    std::string longest(Core::MaximumParsedFloatBytes - 2U, '0');
    longest += ".5";
    ASSERT_EQ(longest.size(), Core::MaximumParsedFloatBytes);
    EXPECT_FLOAT_EQ(Core::parseStrictFloat(longest).value(), 0.5F);
}

// The span is not NUL-terminated in general: the parse must stop at the span's end rather
// than at whatever byte follows it in the caller's buffer.
TEST(ParseStrictFloatTest, ReadsOnlyTheGivenSpan)
{
    const std::string backing = "2.5999";
    const std::string_view prefix{backing.data(), 3U};
    EXPECT_FLOAT_EQ(Core::parseStrictFloat(prefix).value(), 2.5F);
}

} // namespace
} // namespace Tina::Tests
