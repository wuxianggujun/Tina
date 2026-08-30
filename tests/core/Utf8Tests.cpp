#include <tina/core/text/Utf8.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <span>
#include <string>
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

// Runs a conversion and returns the produced bytes, so each case below reads as input -> output rather
// than as span bookkeeping.
[[nodiscard]] std::optional<std::string> convert(std::u16string_view utf16, usize outputBytes = 64)
{
    std::string output(outputBytes, '\0');
    const auto written = convertUtf16ToStrictUtf8(utf16, std::span<char>{output});
    if (!written)
    {
        return std::nullopt;
    }
    output.resize(*written);
    return output;
}

// The four sequence lengths at their boundaries. One below and at each threshold is where an encoder
// either gets the shortest form right or silently emits an overlong sequence.
TEST(Utf8Tests, ConvertsEachSequenceLengthAtItsBoundary)
{
    EXPECT_EQ(convert(std::u16string{u'A'}), "A");
    EXPECT_EQ(convert(std::u16string{u'\x007F'}), "\x7F");
    EXPECT_EQ(convert(std::u16string{u'\x0080'}), "\xC2\x80");
    EXPECT_EQ(convert(std::u16string{u'\x07FF'}), "\xDF\xBF");
    EXPECT_EQ(convert(std::u16string{u'\x0800'}), "\xE0\xA0\x80");
    EXPECT_EQ(convert(std::u16string{u'\xFFFF'}), "\xEF\xBF\xBF");
    // U+10000, the first codepoint needing a surrogate pair.
    EXPECT_EQ(convert(std::u16string{u'\xD800', u'\xDC00'}), "\xF0\x90\x80\x80");
    // U+10FFFF, the last valid codepoint.
    EXPECT_EQ(convert(std::u16string{u'\xDBFF', u'\xDFFF'}), "\xF4\x8F\xBF\xBF");
}

// The reason this function exists. An emoji is a surrogate pair in UTF-16, and JNI's modified UTF-8
// emits it as two invalid three-byte CESU-8 sequences that strict validation rejects -- losing the
// character. Proper conversion yields one four-byte sequence that validates.
TEST(Utf8Tests, ConvertsSurrogatePairsIntoFourByteSequences)
{
    // U+1F600 GRINNING FACE, as the surrogate pair Java actually stores.
    const auto converted = convert(std::u16string{u'\xD83D', u'\xDE00'});
    ASSERT_TRUE(converted.has_value());
    EXPECT_EQ(*converted, "\xF0\x9F\x98\x80");
    EXPECT_TRUE(isStrictUtf8WithoutNul(*converted));

    // Mixed with ordinary text, which is what a real IME commit looks like.
    const auto mixed = convert(std::u16string{u'h', u'i', u'\xD83D', u'\xDE00'});
    ASSERT_TRUE(mixed.has_value());
    EXPECT_EQ(*mixed, "hi\xF0\x9F\x98\x80");
    EXPECT_TRUE(isStrictUtf8WithoutNul(*mixed));
}

// An unpaired surrogate is not a character. Passing one through would produce exactly the invalid bytes
// this function exists to avoid, so it must fail rather than emit something.
TEST(Utf8Tests, RejectsUnpairedSurrogates)
{
    EXPECT_FALSE(convert(std::u16string{u'\xD83D'}).has_value()) << "high surrogate at end of input";
    EXPECT_FALSE(convert(std::u16string{u'\xDE00'}).has_value()) << "lone low surrogate";
    EXPECT_FALSE(convert(std::u16string{u'\xD83D', u'A'}).has_value()) << "high surrogate not followed by low";
    EXPECT_FALSE(convert(std::u16string{u'\xD83D', u'\xD83D'}).has_value()) << "two high surrogates";
}

// Every Tina text contract is NUL-free, and modified UTF-8's two-byte NUL is precisely the encoding this
// function must never produce.
TEST(Utf8Tests, RejectsEmbeddedNulWhenConverting)
{
    EXPECT_FALSE(convert(std::u16string{u'a', u'\0', u'b'}).has_value());
    EXPECT_FALSE(convert(std::u16string{u'\0'}).has_value());
}

// Overflow must fail rather than truncate mid-sequence: a half-written multi-byte character is invalid
// UTF-8, so a caller ignoring the result would corrupt the stream.
TEST(Utf8Tests, RejectsOutputThatDoesNotFit)
{
    const std::u16string emoji{u'\xD83D', u'\xDE00'};
    EXPECT_TRUE(convert(emoji, 4).has_value()) << "the advertised size must be usable in full";
    EXPECT_FALSE(convert(emoji, 3).has_value()) << "one byte short must fail, not truncate";

    EXPECT_TRUE(convert(std::u16string{u'a', u'b'}, 2).has_value());
    EXPECT_FALSE(convert(std::u16string{u'a', u'b'}, 1).has_value());
}

TEST(Utf8Tests, ConvertsEmptyInputToZeroBytes)
{
    const auto converted = convert(std::u16string{});
    ASSERT_TRUE(converted.has_value());
    EXPECT_TRUE(converted->empty());
}

} // namespace
} // namespace Tina::Core
