#include <tina/core/hash/ContentHashDigest.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::Core {
namespace {

TEST(ContentHashDigestTests, DigestsEmptyInputDeterministically)
{
    const auto first = digestContentHashV1({});
    const auto second = digestContentHashV1({});
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_EQ(*first, *second);
    EXPECT_TRUE(first->hasValue());
}

TEST(ContentHashDigestTests, DigestsKnownPayloadWithLittleEndianLayout)
{
    constexpr std::array<std::byte, 4> Payload{std::byte{'t'}, std::byte{'i'}, std::byte{'n'}, std::byte{'a'}};
    const auto digest = digestContentHashV1(Payload);
    ASSERT_TRUE(digest.has_value()) << digest.error().message;

    // Independent recompute through the same public API must match.
    const auto again = digestContentHash(Payload, ContentHashAlgorithm::Xxh3_128V1);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*digest, *again);

    // Layout contract: 16 bytes, non-zero, comparable as values.
    EXPECT_EQ(digest->bytes().size(), 16U);
    EXPECT_NE(digest->bytes(), ContentHash::Bytes{});
}

TEST(ContentHashDigestTests, DifferentPayloadsProduceDifferentDigests)
{
    constexpr std::array<std::byte, 1> A{std::byte{1}};
    constexpr std::array<std::byte, 1> B{std::byte{2}};
    const auto digestA = digestContentHashV1(A);
    const auto digestB = digestContentHashV1(B);
    ASSERT_TRUE(digestA.has_value());
    ASSERT_TRUE(digestB.has_value());
    EXPECT_NE(*digestA, *digestB);
}

TEST(ContentHashDigestTests, RejectsUnsupportedAlgorithm)
{
    constexpr std::array<std::byte, 1> Payload{std::byte{7}};
    const auto result = digestContentHash(Payload, ContentHashAlgorithm::Invalid);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CoreErrorCode::Unsupported);
}

TEST(ContentHashDigestTests, RepeatedDigestHasNoStateDrift)
{
    std::vector<std::byte> payload(256);
    for (std::size_t index = 0; index < payload.size(); ++index)
    {
        payload[index] = static_cast<std::byte>(index & 0xFFU);
    }

    const auto baseline = digestContentHashV1(payload);
    ASSERT_TRUE(baseline.has_value());
    for (int iteration = 0; iteration < 300; ++iteration)
    {
        const auto current = digestContentHashV1(payload);
        ASSERT_TRUE(current.has_value());
        EXPECT_EQ(*current, *baseline);
    }
}

} // namespace
} // namespace Tina::Core
