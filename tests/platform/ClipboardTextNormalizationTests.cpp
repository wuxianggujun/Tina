#include "platform/ClipboardTextNormalization.hpp"

#include <tina/platform/ProcessLocalClipboard.hpp>

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>
#include <string_view>

namespace {

using Tina::Platform::Detail::clipboardTextSizeWithCrlf;
using Tina::Platform::Detail::expandClipboardTextToCrlf;
using Tina::Platform::Detail::normalizeClipboardTextToLf;

// The normalization helpers are constexpr, so the interesting cases are proved
// at compile time rather than merely observed at run time. These are kept as
// static_asserts because each one stands for a specific way the conversion can
// go wrong, and a compile-time failure names the case directly.

// A destination sized to the whole input is the ordinary path.
constexpr bool normalizesCrlfPair()
{
    std::array<char, 8> buffer{};
    const auto result = normalizeClipboardTextToLf("a\r\nb", buffer);
    return result.bytesWritten == 3 && result.totalBytes == 3 &&
           buffer[0] == 'a' && buffer[1] == '\n' && buffer[2] == 'b';
}
static_assert(normalizesCrlfPair());

// A lone CR is old-Mac line ending. It has to collapse too: the UI text model
// rejects any insertion containing CR, so leaving one in place would turn the
// paste into a silent no-op rather than a visible error.
constexpr bool normalizesLoneCr()
{
    std::array<char, 8> buffer{};
    const auto result = normalizeClipboardTextToLf("a\rb", buffer);
    return result.bytesWritten == 3 && result.totalBytes == 3 &&
           buffer[1] == '\n';
}
static_assert(normalizesLoneCr());

// Trailing CR with no LF after it still collapses.
constexpr bool normalizesTrailingCr()
{
    std::array<char, 4> buffer{};
    const auto result = normalizeClipboardTextToLf("a\r", buffer);
    return result.bytesWritten == 2 && result.totalBytes == 2 && buffer[1] == '\n';
}
static_assert(normalizesTrailingCr());

// A CRLF pair is consumed atomically, so a destination that stops exactly on the
// CR cannot publish half a pair and then emit the LF as a second newline.
constexpr bool doesNotSplitCrlfAcrossTruncation()
{
    std::array<char, 2> buffer{};
    const auto result = normalizeClipboardTextToLf("a\r\nb", buffer);
    return result.bytesWritten == 2 && result.totalBytes == 3 &&
           buffer[0] == 'a' && buffer[1] == '\n';
}
static_assert(doesNotSplitCrlfAcrossTruncation());

// Truncation must land on a UTF-8 sequence boundary. This is the case that
// caught a real defect: retreating whenever the last byte is a continuation
// byte also destroys a *complete* trailing scalar, because a complete
// multi-byte scalar likewise ends on a continuation byte. Here the euro sign
// fits exactly and must survive.
constexpr bool keepsCompleteTrailingScalar()
{
    std::array<char, 4> buffer{};
    // "A" + U+20AC (3 bytes) + "B" -> only the first four bytes fit.
    const auto result = normalizeClipboardTextToLf("A€B", buffer);
    return result.bytesWritten == 4 && result.totalBytes == 5;
}
static_assert(keepsCompleteTrailingScalar());

// The mirror image: the scalar genuinely does not fit and must be dropped whole
// rather than published as a partial sequence.
constexpr bool dropsIncompleteTrailingScalar()
{
    std::array<char, 4> buffer{};
    // "AB" + U+20AC -> two of the euro sign's three bytes would fit.
    const auto result = normalizeClipboardTextToLf("AB€", buffer);
    return result.bytesWritten == 2 && result.totalBytes == 5 &&
           buffer[0] == 'A' && buffer[1] == 'B';
}
static_assert(dropsIncompleteTrailingScalar());

// Same boundary rule for a 4-byte scalar.
constexpr bool dropsIncompleteFourByteScalar()
{
    std::array<char, 3> buffer{};
    // U+1F600 is 4 bytes; three of them fit and none may be published.
    const auto result = normalizeClipboardTextToLf("\U0001F600", buffer);
    return result.bytesWritten == 0 && result.totalBytes == 4;
}
static_assert(dropsIncompleteFourByteScalar());

// An empty destination reports the required size without writing, which is what
// makes a single read usable as a size query. Two calls would leave room for the
// clipboard to change in between.
constexpr bool emptyDestinationIsSizeQuery()
{
    const auto result = normalizeClipboardTextToLf("a\r\nb", {});
    return result.bytesWritten == 0 && result.totalBytes == 3;
}
static_assert(emptyDestinationIsSizeQuery());

static_assert(clipboardTextSizeWithCrlf("") == 0);
static_assert(clipboardTextSizeWithCrlf("ab") == 2);
static_assert(clipboardTextSizeWithCrlf("a\nb") == 4);
static_assert(clipboardTextSizeWithCrlf("\n\n") == 4);

constexpr bool expandsLfToCrlf()
{
    std::array<char, 4> buffer{};
    const auto written = expandClipboardTextToCrlf("a\nb", buffer);
    return written == 4 && buffer[0] == 'a' && buffer[1] == '\r' &&
           buffer[2] == '\n' && buffer[3] == 'b';
}
static_assert(expandsLfToCrlf());

// A short destination writes nothing at all: a half-expanded CRLF would be
// worse than a refused write, because the caller could not tell it happened.
constexpr bool refusesShortCrlfDestination()
{
    std::array<char, 3> buffer{};
    return expandClipboardTextToCrlf("a\nb", buffer) == 0 && buffer[0] == '\0';
}
static_assert(refusesShortCrlfDestination());

TEST(ClipboardTextNormalizationTest, CrlfPairIsNotSplitAcrossTruncation)
{
    std::array<char, 2> buffer{};
    const auto result = normalizeClipboardTextToLf("a\r\nb", buffer);
    EXPECT_EQ(result.bytesWritten, 2U);
    EXPECT_EQ(result.totalBytes, 3U);
    EXPECT_EQ(std::string_view(buffer.data(), result.bytesWritten), "a\n");
}

TEST(ClipboardTextNormalizationTest, RoundTripThroughCrlfPreservesText)
{
    constexpr std::string_view original = "line1\nline2\n中文\n";
    std::string expanded;
    expanded.resize(clipboardTextSizeWithCrlf(original));
    ASSERT_EQ(expandClipboardTextToCrlf(
                  original, std::span<char>{expanded.data(), expanded.size()}),
              expanded.size());

    std::string recovered;
    recovered.resize(normalizeClipboardTextToLf(expanded, {}).totalBytes);
    const auto result = normalizeClipboardTextToLf(
        expanded, std::span<char>{recovered.data(), recovered.size()});
    EXPECT_EQ(result.bytesWritten, recovered.size());
    EXPECT_EQ(result.bytesWritten, result.totalBytes);
    EXPECT_EQ(recovered, original);
}

// ProcessLocalClipboard is the implementation Headless returns and the one every
// clipboard test shares, so its own contract needs to hold.

TEST(ProcessLocalClipboardTest, NeverWrittenReportsNoText)
{
    Tina::Platform::ProcessLocalClipboard clipboard;
    std::array<char, 8> buffer{};
    const auto read = clipboard.readTextUtf8(buffer);
    ASSERT_TRUE(read.has_value());
    EXPECT_FALSE(read->hasText);
    EXPECT_EQ(read->totalBytes, 0U);
    EXPECT_FALSE(clipboard.hasText());
}

TEST(ProcessLocalClipboardTest, EmptyStringIsHeldTextNotAbsentText)
{
    Tina::Platform::ProcessLocalClipboard clipboard;
    ASSERT_TRUE(clipboard.writeTextUtf8("").has_value());
    std::array<char, 8> buffer{};
    const auto read = clipboard.readTextUtf8(buffer);
    ASSERT_TRUE(read.has_value());
    // Distinct from the never-written case above: there is text, it is empty.
    EXPECT_TRUE(read->hasText);
    EXPECT_EQ(read->totalBytes, 0U);
}

TEST(ProcessLocalClipboardTest, StoredTextIsLfNormalized)
{
    Tina::Platform::ProcessLocalClipboard clipboard;
    ASSERT_TRUE(clipboard.writeTextUtf8("a\r\nb\rc").has_value());
    std::array<char, 16> buffer{};
    const auto read = clipboard.readTextUtf8(buffer);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(std::string_view(buffer.data(), read->bytesWritten), "a\nb\nc");
}

TEST(ProcessLocalClipboardTest, RejectsTextThatIsNotStrictUtf8)
{
    Tina::Platform::ProcessLocalClipboard clipboard;
    const std::string invalid{"a\xC3", 2};
    EXPECT_FALSE(clipboard.writeTextUtf8(invalid).has_value());
    EXPECT_FALSE(clipboard.hasText());
}

TEST(ProcessLocalClipboardTest, TruncatedReadReportsRequiredSize)
{
    Tina::Platform::ProcessLocalClipboard clipboard;
    ASSERT_TRUE(clipboard.writeTextUtf8("abcdef").has_value());
    std::array<char, 3> buffer{};
    const auto read = clipboard.readTextUtf8(buffer);
    ASSERT_TRUE(read.has_value());
    EXPECT_TRUE(read->truncated());
    EXPECT_EQ(read->bytesWritten, 3U);
    EXPECT_EQ(read->totalBytes, 6U);

    // The reported size is enough to complete the read on a second attempt.
    std::string full;
    full.resize(read->totalBytes);
    const auto retry = clipboard.readTextUtf8(std::span<char>{full.data(), full.size()});
    ASSERT_TRUE(retry.has_value());
    EXPECT_FALSE(retry->truncated());
    EXPECT_EQ(full, "abcdef");
}

TEST(ProcessLocalClipboardTest, ClearReturnsToNeverWrittenState)
{
    Tina::Platform::ProcessLocalClipboard clipboard;
    ASSERT_TRUE(clipboard.writeTextUtf8("text").has_value());
    ASSERT_TRUE(clipboard.hasText());
    clipboard.clear();
    EXPECT_FALSE(clipboard.hasText());
    std::array<char, 8> buffer{};
    const auto read = clipboard.readTextUtf8(buffer);
    ASSERT_TRUE(read.has_value());
    EXPECT_FALSE(read->hasText);
}

} // namespace
