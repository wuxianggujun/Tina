#include <gtest/gtest.h>

#include <tina/core/diagnostics/LogFormat.hpp>
#include <tina/core/diagnostics/LogRecord.hpp>

#include <array>
#include <string>
#include <string_view>

namespace Tina::Tests {
namespace {

using Core::Diagnostics::Format::Argument;
using Core::Diagnostics::Format::Result;

// Mirrors the real call path: format into a record-sized buffer minus the NUL.
struct Formatted final {
    std::string text;
    bool truncated = false;
};

template <typename... Values>
Formatted run(const std::string_view pattern, Values... values)
{
    std::array<char, Core::Diagnostics::LogRecord::MessageCapacity> buffer{};
    Formatted formatted;
    if constexpr (sizeof...(Values) == 0) {
        const Result result = Core::Diagnostics::Format::format(buffer.data(), buffer.size() - 1, pattern, nullptr, 0);
        formatted.text.assign(buffer.data(), result.length);
        formatted.truncated = result.truncated;
    } else {
        const Argument arguments[]{Argument{values}...};
        const Result result = Core::Diagnostics::Format::format(
            buffer.data(), buffer.size() - 1, pattern, arguments, sizeof...(Values));
        formatted.text.assign(buffer.data(), result.length);
        formatted.truncated = result.truncated;
    }
    return formatted;
}

TEST(LogFormatTest, PlainPatternIsCopiedVerbatim)
{
    const Formatted formatted = run("no placeholders here");
    EXPECT_EQ(formatted.text, "no placeholders here");
    EXPECT_FALSE(formatted.truncated);
}

TEST(LogFormatTest, SubstitutesIntegersSignedAndUnsigned)
{
    EXPECT_EQ(run("count={}", 42).text, "count=42");
    EXPECT_EQ(run("delta={}", -7).text, "delta=-7");
    EXPECT_EQ(run("size={}", static_cast<Core::u64>(18446744073709551615ULL)).text, "size=18446744073709551615");
    EXPECT_EQ(run("small={}", static_cast<Core::u8>(255)).text, "small=255");
}

TEST(LogFormatTest, SubstitutesTextBoolCharAndPointer)
{
    EXPECT_EQ(run("name={}", std::string_view{"tina"}).text, "name=tina");
    EXPECT_EQ(run("literal={}", "raw").text, "literal=raw");
    EXPECT_EQ(run("flag={}", true).text, "flag=true");
    EXPECT_EQ(run("flag={}", false).text, "flag=false");
    EXPECT_EQ(run("ch={}", 'x').text, "ch=x");
    EXPECT_EQ(run("ptr={}", static_cast<const void*>(nullptr)).text, "ptr=nullptr");
}

// A null const char* must not dereference; it is a plausible argument from a
// C API and the formatter runs on paths that cannot afford a crash.
TEST(LogFormatTest, NullCharPointerIsReportedNotDereferenced)
{
    const char* const missing = nullptr;
    EXPECT_EQ(run("value={}", missing).text, "value=(null)");
}

TEST(LogFormatTest, SubstitutesFloatingPoint)
{
    EXPECT_EQ(run("ratio={}", 1.5).text, "ratio=1.5");
    EXPECT_EQ(run("ratio={}", 0.0).text, "ratio=0");
    EXPECT_EQ(run("ratio={}", -2.25F).text, "ratio=-2.25");
}

TEST(LogFormatTest, DoubledBraceEmitsLiteralBrace)
{
    EXPECT_EQ(run("{{}").text, "{}");
    EXPECT_EQ(run("{{{}}}", 5).text, "{5}");
}

TEST(LogFormatTest, MultiplePlaceholdersFillInOrder)
{
    EXPECT_EQ(run("{} of {} ({})", 3, 10, std::string_view{"partial"}).text, "3 of 10 (partial)");
}

// Both mismatch directions stay visible in the output: a wrong argument count is
// a call-site defect, and a silently shorter line would hide it.
TEST(LogFormatTest, MissingArgumentIsMarkedNotDropped)
{
    EXPECT_EQ(run("a={} b={}", 1).text, "a=1 b={?}");
}

TEST(LogFormatTest, SurplusArgumentIsReported)
{
    EXPECT_EQ(run("a={}", 1, 2, 3).text, "a=1 {extra:2}");
}

TEST(LogFormatTest, OverlongMessageTruncatesAndFlags)
{
    const std::string longText(Core::Diagnostics::LogRecord::MessageCapacity * 2, 'x');
    const Formatted formatted = run("{}", std::string_view{longText});
    EXPECT_TRUE(formatted.truncated);
    EXPECT_EQ(formatted.text.size(), Core::Diagnostics::LogRecord::MessageCapacity - 1);
}

// A value that starts inside the buffer but does not fit must still report
// truncation rather than appearing complete.
TEST(LogFormatTest, PartiallyFittingValueFlagsTruncation)
{
    std::array<char, 8> buffer{};
    const Argument arguments[]{Argument{std::string_view{"abcdefghij"}}};
    const Result result = Core::Diagnostics::Format::format(buffer.data(), buffer.size(), "{}", arguments, 1);
    EXPECT_TRUE(result.truncated);
    EXPECT_EQ(result.length, buffer.size());
}

TEST(LogFormatTest, ZeroCapacityIsSafe)
{
    const Result result = Core::Diagnostics::Format::format(nullptr, 0, "text", nullptr, 0);
    EXPECT_EQ(result.length, 0U);
    EXPECT_TRUE(result.truncated);
}

TEST(LogRecordTest, MakeCopiesMessageAndTerminates)
{
    const auto record = Core::Diagnostics::LogRecord::make(
        Core::Diagnostics::LogLevel::Warn, "cat", "hello");
    EXPECT_EQ(record.level(), Core::Diagnostics::LogLevel::Warn);
    EXPECT_EQ(record.category(), std::string_view{"cat"});
    EXPECT_EQ(record.message(), std::string_view{"hello"});
    EXPECT_FALSE(record.isTruncated());
    // The buffer must be usable as a C string by platform sinks.
    EXPECT_STREQ(record.message().data(), "hello");
}

TEST(LogRecordTest, MakeTruncatesOverlongMessage)
{
    const std::string longText(Core::Diagnostics::LogRecord::MessageCapacity * 2, 'y');
    const auto record = Core::Diagnostics::LogRecord::make(
        Core::Diagnostics::LogLevel::Info, "cat", longText);
    EXPECT_TRUE(record.isTruncated());
    EXPECT_EQ(record.message().size(), Core::Diagnostics::LogRecord::MessageCapacity - 1);
}

} // namespace
} // namespace Tina::Tests
