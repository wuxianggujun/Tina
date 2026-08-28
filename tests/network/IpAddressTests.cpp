#include <tina/network/NetworkEndpoint.hpp>
#include <tina/network/NetworkErrors.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

namespace Tina::Tests {
namespace {

[[nodiscard]] std::string formatToString(const Network::IpAddress& address)
{
    std::array<char, Network::IpAddress::MaximumTextLength> buffer{};
    const Core::usize written = address.format(buffer);
    return std::string{buffer.data(), written};
}

// Parsing then formatting must reproduce the canonical text, and parsing that
// text again must reproduce the same bytes.
void expectRoundTrip(std::string_view input, std::string_view canonical)
{
    const auto parsed = Network::IpAddress::parse(input);
    ASSERT_TRUE(parsed.has_value()) << "failed to parse " << input;
    EXPECT_EQ(formatToString(*parsed), canonical) << "input " << input;

    const auto reparsed = Network::IpAddress::parse(canonical);
    ASSERT_TRUE(reparsed.has_value()) << "failed to reparse " << canonical;
    EXPECT_EQ(*reparsed, *parsed) << "input " << input;
}

} // namespace

TEST(IpAddressTest, DefaultConstructedHasNoValue)
{
    constexpr Network::IpAddress address{};
    static_assert(!address.hasValue());
    static_assert(address.family() == Network::IpFamily::Unspecified);
    static_assert(address.byteCount() == 0);
    EXPECT_FALSE(static_cast<bool>(address));
    EXPECT_EQ(formatToString(address), "");
}

TEST(IpAddressTest, V4FactoriesProduceExpectedBytes)
{
    static_assert(Network::IpAddress::v4Loopback().isLoopback());
    static_assert(Network::IpAddress::v4Any().isUnspecifiedAddress());
    static_assert(Network::IpAddress::v4(10, 0, 0, 1).byteCount() == 4);

    EXPECT_EQ(formatToString(Network::IpAddress::v4Loopback()), "127.0.0.1");
    EXPECT_EQ(formatToString(Network::IpAddress::v4Any()), "0.0.0.0");
    EXPECT_EQ(formatToString(Network::IpAddress::v4(255, 255, 255, 255)), "255.255.255.255");
}

TEST(IpAddressTest, V6FactoriesProduceExpectedBytes)
{
    static_assert(Network::IpAddress::v6Loopback().isLoopback());
    static_assert(Network::IpAddress::v6Any().isUnspecifiedAddress());

    EXPECT_EQ(formatToString(Network::IpAddress::v6Loopback()), "::1");
    EXPECT_EQ(formatToString(Network::IpAddress::v6Any()), "::");
}

TEST(IpAddressTest, ParsesV4RoundTrip)
{
    expectRoundTrip("0.0.0.0", "0.0.0.0");
    expectRoundTrip("127.0.0.1", "127.0.0.1");
    expectRoundTrip("10.0.0.1", "10.0.0.1");
    expectRoundTrip("192.168.1.254", "192.168.1.254");
    expectRoundTrip("255.255.255.255", "255.255.255.255");
    expectRoundTrip("8.8.8.8", "8.8.8.8");
}

TEST(IpAddressTest, RejectsMalformedV4)
{
    // Out of range, wrong field count, empty fields, and non-digits.
    for (const std::string_view input : {
             "256.0.0.1",
             "1.2.3",
             "1.2.3.4.5",
             "1.2.3.",
             ".1.2.3",
             "1..2.3",
             "1.2.3.-1",
             "1.2.3.a",
             "1 2 3 4",
             "1.2.3.4 ",
             " 1.2.3.4",
             "",
         }) {
        const auto parsed = Network::IpAddress::parse(input);
        EXPECT_FALSE(parsed.has_value()) << "unexpectedly accepted " << input;
        if (!parsed.has_value()) {
            EXPECT_EQ(parsed.error().code, Network::NetworkErrorCode::InvalidEndpoint);
        }
    }
}

// "010" is octal to some resolvers and decimal to others. Accepting it would
// make one address parse as two different hosts depending on the reader.
TEST(IpAddressTest, RejectsLeadingZeroOctets)
{
    for (const std::string_view input : {"010.0.0.1", "1.2.3.04", "00.0.0.0", "1.02.3.4"}) {
        EXPECT_FALSE(Network::IpAddress::parse(input).has_value())
            << "unexpectedly accepted " << input;
    }

    // A bare zero octet is still legal.
    EXPECT_TRUE(Network::IpAddress::parse("0.0.0.0").has_value());
}

TEST(IpAddressTest, ParsesV6RoundTrip)
{
    expectRoundTrip("::", "::");
    expectRoundTrip("::1", "::1");
    expectRoundTrip("2001:db8::1", "2001:db8::1");
    expectRoundTrip(
        "2001:0db8:0000:0000:0000:0000:0000:0001",
        "2001:db8::1");
    expectRoundTrip("fe80::1", "fe80::1");
    expectRoundTrip(
        "2001:db8:85a3:0:0:8a2e:370:7334",
        "2001:db8:85a3::8a2e:370:7334");
    expectRoundTrip(
        "0001:0002:0003:0004:0005:0006:0007:0008",
        "1:2:3:4:5:6:7:8");
}

// Uppercase input is accepted; output is always lowercase per RFC 5952.
TEST(IpAddressTest, NormalisesV6HexCaseToLower)
{
    const auto upper = Network::IpAddress::parse("2001:DB8::ABCD");
    ASSERT_TRUE(upper.has_value());
    EXPECT_EQ(formatToString(*upper), "2001:db8::abcd");

    const auto lower = Network::IpAddress::parse("2001:db8::abcd");
    ASSERT_TRUE(lower.has_value());
    EXPECT_EQ(*upper, *lower);
}

TEST(IpAddressTest, CompressesLongestZeroRunLeftmostOnTie)
{
    // Two runs of two groups: the leftmost is compressed.
    const auto tie = Network::IpAddress::parse("1:0:0:2:3:0:0:4");
    ASSERT_TRUE(tie.has_value());
    EXPECT_EQ(formatToString(*tie), "1::2:3:0:0:4");

    // The longer run wins even when it appears later.
    const auto longer = Network::IpAddress::parse("1:0:0:2:0:0:0:3");
    ASSERT_TRUE(longer.has_value());
    EXPECT_EQ(formatToString(*longer), "1:0:0:2::3");
}

// RFC 5952 forbids compressing a single group -- "1:0:2::" style output would
// re-parse differently than intended.
TEST(IpAddressTest, DoesNotCompressSingleZeroGroup)
{
    const auto parsed = Network::IpAddress::parse("1:2:3:4:5:6:0:8");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(formatToString(*parsed), "1:2:3:4:5:6:0:8");
}

TEST(IpAddressTest, ParsesEmbeddedV4InV6)
{
    const auto mapped = Network::IpAddress::parse("::ffff:192.168.1.1");
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->family(), Network::IpFamily::V6);

    const auto& bytes = mapped->bytes();
    EXPECT_EQ(bytes[10], 0xFF);
    EXPECT_EQ(bytes[11], 0xFF);
    EXPECT_EQ(bytes[12], 192);
    EXPECT_EQ(bytes[13], 168);
    EXPECT_EQ(bytes[14], 1);
    EXPECT_EQ(bytes[15], 1);
}

TEST(IpAddressTest, RejectsMalformedV6)
{
    for (const std::string_view input : {
             ":::",
             "1:::2",
             "::1::2",
             "1:2:3:4:5:6:7:8:9",
             "1:2:3:4:5:6:7",
             "12345::1",
             "1:2:3:4:5:6:7:",
             ":1:2:3:4:5:6:7",
             "1:2:3:4:5:6:7:8::",
             "gggg::1",
             "1:2:3:4:5:6:1.2.3.4.5",
             "::1.2.3",
             "1.2.3.4::",
         }) {
        const auto parsed = Network::IpAddress::parse(input);
        EXPECT_FALSE(parsed.has_value()) << "unexpectedly accepted " << input;
    }
}

// Brackets belong to URI authority syntax, not to an address. Accepting them
// here would let "[::1]" and "::1" mean the same thing in one place and not
// another.
TEST(IpAddressTest, RejectsBracketedV6Form)
{
    EXPECT_FALSE(Network::IpAddress::parse("[::1]").has_value());
    EXPECT_FALSE(Network::IpAddress::parse("[2001:db8::1]").has_value());
}

// Anything requiring a resolver must be refused rather than partially handled.
TEST(IpAddressTest, RejectsHostnamesAndPortSuffixes)
{
    for (const std::string_view input : {
             "localhost",
             "example.com",
             "127.0.0.1:8080",
             "[::1]:8080",
             "http://127.0.0.1",
         }) {
        EXPECT_FALSE(Network::IpAddress::parse(input).has_value())
            << "unexpectedly accepted " << input;
    }
}

TEST(IpAddressTest, FormatFailsClosedOnShortBuffer)
{
    const auto address = Network::IpAddress::parse("255.255.255.255");
    ASSERT_TRUE(address.has_value());

    std::array<char, 4> tooSmall{};
    EXPECT_EQ(address->format(tooSmall), 0U);

    std::array<char, Network::IpAddress::MaximumTextLength> justRight{};
    EXPECT_GT(address->format(justRight), 0U);
}

// The documented buffer size must hold the longest form this type can produce.
TEST(IpAddressTest, MaximumTextLengthHoldsLongestOutput)
{
    const auto longest = Network::IpAddress::parse("1:2:3:4:5:6:7:8");
    ASSERT_TRUE(longest.has_value());

    std::array<char, Network::IpAddress::MaximumTextLength> buffer{};
    const Core::usize written = longest->format(buffer);
    EXPECT_GT(written, 0U);
    EXPECT_LT(written, Network::IpAddress::MaximumTextLength);
}

TEST(IpAddressTest, DistinctFamiliesAreNeverEqual)
{
    const auto v4 = Network::IpAddress::parse("0.0.0.0");
    const auto v6 = Network::IpAddress::parse("::");
    ASSERT_TRUE(v4.has_value());
    ASSERT_TRUE(v6.has_value());

    // Both are all-zero bytes, so only the family distinguishes them.
    EXPECT_NE(*v4, *v6);
    EXPECT_TRUE(v4->isUnspecifiedAddress());
    EXPECT_TRUE(v6->isUnspecifiedAddress());
}

TEST(NetworkEndpointTest, DefaultHasNoValueAndComparesByAddressAndPort)
{
    constexpr Network::NetworkEndpoint empty{};
    static_assert(!empty.hasValue());

    const Network::NetworkEndpoint first{Network::IpAddress::v4Loopback(), 9000};
    const Network::NetworkEndpoint same{Network::IpAddress::v4Loopback(), 9000};
    const Network::NetworkEndpoint otherPort{Network::IpAddress::v4Loopback(), 9001};

    EXPECT_TRUE(first.hasValue());
    EXPECT_EQ(first, same);
    EXPECT_NE(first, otherPort);
}

} // namespace Tina::Tests
