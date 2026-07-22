#include <gtest/gtest.h>

#include <tina/core/diagnostics/Diagnostics.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Tests {
namespace {

struct CaptureSink final {
    std::vector<std::string> messages;
    std::vector<Core::Diagnostics::LogLevel> levels;
    std::vector<std::string> categories;
    int recursiveWrites = 0;
    Core::Diagnostics::DiagnosticChannel* recursiveChannel = nullptr;
};

void captureSink(void* userData, const Core::Diagnostics::LogRecord& record)
{
    auto* capture = static_cast<CaptureSink*>(userData);
    if (capture == nullptr) {
        return;
    }
    capture->levels.push_back(record.level);
    capture->categories.emplace_back(record.category);
    capture->messages.emplace_back(record.message);

    if (capture->recursiveChannel != nullptr && capture->recursiveWrites == 0) {
        ++capture->recursiveWrites;
        capture->recursiveChannel->write(Core::Diagnostics::LogRecord{
            .level = Core::Diagnostics::LogLevel::Error,
            .category = "recursive",
            .message = "must-not-recurse",
        });
    }
}

void throwingSink(void* /*userData*/, const Core::Diagnostics::LogRecord& /*record*/)
{
    throw std::runtime_error("sink boom");
}

TEST(DiagnosticsTest, LevelFilterDropsBelowMinimumWithoutSinkCall)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Warn,
        .sink = &captureSink,
        .sinkUserData = &capture,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;

    const DiagnosticChannel channel = diagnostics.channel();
    EXPECT_TRUE(channel.isOpen());
    EXPECT_FALSE(channel.isEnabled(LogLevel::Info));
    EXPECT_TRUE(channel.isEnabled(LogLevel::Warn));

    channel.write(LogRecord{.level = LogLevel::Info, .category = "test", .message = "skipped"});
    channel.write(LogRecord{.level = LogLevel::Warn, .category = "test", .message = "kept"});

    EXPECT_EQ(diagnostics.droppedByLevelCount(), 1U);
    EXPECT_EQ(diagnostics.writtenCount(), 1U);
    ASSERT_EQ(capture.messages.size(), 1U);
    EXPECT_EQ(capture.messages[0], "kept");
    EXPECT_EQ(capture.levels[0], LogLevel::Warn);
}

TEST(DiagnosticsTest, ChannelNoOpsAfterShutdown)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;
    const DiagnosticChannel channel = diagnostics.channel();

    diagnostics.shutdown();
    EXPECT_FALSE(diagnostics.isOpen());
    EXPECT_FALSE(channel.isOpen());
    EXPECT_FALSE(channel.isEnabled(LogLevel::Error));

    channel.write(LogRecord{.level = LogLevel::Error, .category = "test", .message = "after-shutdown"});
    EXPECT_TRUE(capture.messages.empty());
    EXPECT_EQ(diagnostics.writtenCount(), 0U);
    EXPECT_EQ(diagnostics.droppedByLevelCount(), 0U);
}

TEST(DiagnosticsTest, SinkFailureDoesNotRecurse)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;
    DiagnosticChannel channel = diagnostics.channel();
    capture.recursiveChannel = &channel;

    channel.write(LogRecord{.level = LogLevel::Info, .category = "test", .message = "outer"});

    EXPECT_EQ(capture.recursiveWrites, 1);
    ASSERT_EQ(capture.messages.size(), 1U);
    EXPECT_EQ(capture.messages[0], "outer");
    EXPECT_EQ(diagnostics.writtenCount(), 1U);
    EXPECT_EQ(diagnostics.sinkFailureCount(), 1U);
}

TEST(DiagnosticsTest, ExceptionFromSinkIsCountedAndDoesNotPropagate)
{
    using namespace Core::Diagnostics;

    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &throwingSink,
        .sinkUserData = nullptr,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;

    diagnostics.channel().write(LogRecord{.level = LogLevel::Error, .category = "test", .message = "x"});
    EXPECT_EQ(diagnostics.writtenCount(), 0U);
    EXPECT_EQ(diagnostics.sinkFailureCount(), 1U);
}

TEST(DiagnosticsTest, LogLevelHelpers)
{
    using namespace Core::Diagnostics;
    EXPECT_TRUE(isLogLevelEnabled(LogLevel::Info, LogLevel::Error));
    EXPECT_FALSE(isLogLevelEnabled(LogLevel::Info, LogLevel::Debug));
    EXPECT_FALSE(isLogLevelEnabled(LogLevel::Off, LogLevel::Critical));
    EXPECT_EQ(logLevelName(LogLevel::Warn), std::string_view{"Warn"});
}

} // namespace
} // namespace Tina::Tests
