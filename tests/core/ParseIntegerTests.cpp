#include <tina/core/text/ParseInteger.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string_view>

namespace Tina::Tests {
namespace {

template <typename Value>
void checkUnsignedBoundary(std::string_view maximum, std::string_view overflow)
{
    Value value = 7;
    ASSERT_TRUE(Core::parseUnsigned(maximum, value));
    EXPECT_EQ(value, (std::numeric_limits<Value>::max)());
    EXPECT_FALSE(Core::parseUnsigned(overflow, value));
    EXPECT_EQ(value, (std::numeric_limits<Value>::max)());
    ASSERT_TRUE(Core::parseUnsigned("0", value));
    EXPECT_EQ(value, 0);
}

template <typename Value>
void checkSignedBoundary(std::string_view minimum, std::string_view maximum,
                         std::string_view underflow, std::string_view overflow)
{
    Value value = 7;
    ASSERT_TRUE(Core::parseSigned(minimum, value));
    EXPECT_EQ(value, (std::numeric_limits<Value>::min)());
    EXPECT_FALSE(Core::parseSigned(underflow, value));
    EXPECT_EQ(value, (std::numeric_limits<Value>::min)());
    ASSERT_TRUE(Core::parseSigned(maximum, value));
    EXPECT_EQ(value, (std::numeric_limits<Value>::max)());
    EXPECT_FALSE(Core::parseSigned(overflow, value));
    EXPECT_EQ(value, (std::numeric_limits<Value>::max)());
    ASSERT_TRUE(Core::parseSigned("-0", value));
    EXPECT_EQ(value, 0);
}

TEST(ParseIntegerTest, UnsignedBoundariesCoverEveryCoreWidth)
{
    checkUnsignedBoundary<Core::u8>("255", "256");
    checkUnsignedBoundary<Core::u16>("65535", "65536");
    checkUnsignedBoundary<Core::u32>("4294967295", "4294967296");
    checkUnsignedBoundary<Core::u64>("18446744073709551615", "18446744073709551616");
}

TEST(ParseIntegerTest, SignedBoundariesCoverEveryCoreWidth)
{
    checkSignedBoundary<Core::i8>("-128", "127", "-129", "128");
    checkSignedBoundary<Core::i16>("-32768", "32767", "-32769", "32768");
    checkSignedBoundary<Core::i32>("-2147483648", "2147483647", "-2147483649", "2147483648");
    checkSignedBoundary<Core::i64>("-9223372036854775808", "9223372036854775807",
                                  "-9223372036854775809", "9223372036854775808");
}

TEST(ParseIntegerTest, EmptyViewsAndMalformedTextPreserveOutput)
{
    constexpr std::array<std::string_view, 15> malformed{
        std::string_view{}, "", "12tail", " 12", "12 ", "\t12", "12\n", "+12", "+", "-",
        "0x12", "1.5", "1e2", "--2", "\xEF\xBC\x91\xEF\xBC\x92",
    };
    for (const auto text : malformed)
    {
        Core::u64 unsignedValue = 99;
        Core::i64 signedValue = -99;
        EXPECT_FALSE(Core::parseUnsigned(text, unsignedValue)) << text;
        EXPECT_FALSE(Core::parseSigned(text, signedValue)) << text;
        EXPECT_EQ(unsignedValue, 99U);
        EXPECT_EQ(signedValue, -99);
    }
}

TEST(ParseIntegerTest, UnsignedRejectsEveryNegativeSpelling)
{
    for (const auto text : {"-1", "-0", "-18446744073709551615"})
    {
        Core::u64 value = 17;
        EXPECT_FALSE(Core::parseUnsigned(text, value));
        EXPECT_EQ(value, 17U);
    }
}

TEST(ParseIntegerTest, BorrowedSubrangesDoNotNeedNullTermination)
{
    constexpr std::array<char, 4> bytes{'-', '4', '2', 'x'};
    Core::u32 unsignedValue = 0;
    Core::i32 signedValue = 0;
    EXPECT_TRUE(Core::parseUnsigned(std::string_view{bytes.data() + 1, 2}, unsignedValue));
    EXPECT_TRUE(Core::parseSigned(std::string_view{bytes.data(), 3}, signedValue));
    EXPECT_EQ(unsignedValue, 42U);
    EXPECT_EQ(signedValue, -42);
    EXPECT_FALSE(Core::parseUnsigned(std::string_view{bytes.data() + 1, 3}, unsignedValue));
    EXPECT_FALSE(Core::parseSigned(std::string_view{bytes.data(), 4}, signedValue));
    EXPECT_EQ(unsignedValue, 42U);
    EXPECT_EQ(signedValue, -42);
}

TEST(ParseIntegerTest, EmbeddedNullIsNotAnEndOfInputMarker)
{
    constexpr std::array<char, 4> bytes{'1', '2', '\0', '3'};
    Core::u32 unsignedValue = 7;
    Core::i32 signedValue = -7;
    const std::string_view text{bytes.data(), bytes.size()};
    EXPECT_FALSE(Core::parseUnsigned(text, unsignedValue));
    EXPECT_FALSE(Core::parseSigned(text, signedValue));
    EXPECT_EQ(unsignedValue, 7U);
    EXPECT_EQ(signedValue, -7);
}

TEST(ParseIntegerTest, SizeTypesUseTheirNativePlatformRange)
{
    if constexpr (sizeof(Core::usize) == sizeof(Core::u64))
    {
        checkUnsignedBoundary<Core::usize>("18446744073709551615", "18446744073709551616");
        checkSignedBoundary<Core::isize>("-9223372036854775808", "9223372036854775807",
                                        "-9223372036854775809", "9223372036854775808");
    }
    else
    {
        checkUnsignedBoundary<Core::usize>("4294967295", "4294967296");
        checkSignedBoundary<Core::isize>("-2147483648", "2147483647", "-2147483649", "2147483648");
    }
}

} // namespace
} // namespace Tina::Tests
