#include <gtest/gtest.h>

#include <tina/core/diagnostics/Diagnostics.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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
    capture->levels.push_back(record.level());
    capture->categories.emplace_back(record.category());
    capture->messages.emplace_back(record.message());

    if (capture->recursiveChannel != nullptr && capture->recursiveWrites == 0) {
        ++capture->recursiveWrites;
        capture->recursiveChannel->write(Core::Diagnostics::LogRecord::make(
            Core::Diagnostics::LogLevel::Error, "recursive", "must-not-recurse"));
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

    channel.write(LogRecord::make(LogLevel::Info, "test", "skipped"));
    channel.write(LogRecord::make(LogLevel::Warn, "test", "kept"));

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

    channel.write(LogRecord::make(LogLevel::Error, "test", "after-shutdown"));
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

    channel.write(LogRecord::make(LogLevel::Info, "test", "outer"));

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

    diagnostics.channel().write(LogRecord::make(LogLevel::Error, "test", "x"));
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

TEST(DiagnosticsTest, SynchronousByDefault)
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

    EXPECT_FALSE(diagnostics.isAsync());
    diagnostics.channel().write(LogRecord::make(LogLevel::Info, "test", "immediate"));

    // No flush: a synchronous write is already delivered when it returns.
    ASSERT_EQ(capture.messages.size(), 1U);
    EXPECT_TRUE(diagnostics.flush());
}

TEST(DiagnosticsTest, AsyncQueueDeliversAfterFlush)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
        .asyncQueueCapacity = 64,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;
    EXPECT_TRUE(diagnostics.isAsync());

    const DiagnosticChannel channel = diagnostics.channel();
    for (int index = 0; index < 32; ++index) {
        channel.write(LogRecord::make(LogLevel::Info, "test", "queued"));
    }

    ASSERT_TRUE(diagnostics.flush());
    EXPECT_EQ(diagnostics.writtenCount(), 32U);
    EXPECT_EQ(diagnostics.droppedByCapacityCount(), 0U);
    ASSERT_EQ(capture.messages.size(), 32U);
    EXPECT_EQ(capture.messages.front(), "queued");
}

TEST(DiagnosticsTest, AsyncQueuePreservesOrder)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
        .asyncQueueCapacity = 8,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;
    const DiagnosticChannel channel = diagnostics.channel();

    // More writes than slots, flushing between each so none are refused: this
    // exercises the ring wrapping rather than the drop path.
    for (int index = 0; index < 20; ++index) {
        const std::string message = "n" + std::to_string(index);
        channel.write(LogRecord::make(LogLevel::Info, "test", message));
        ASSERT_TRUE(diagnostics.flush());
    }

    ASSERT_EQ(capture.messages.size(), 20U);
    for (int index = 0; index < 20; ++index) {
        EXPECT_EQ(capture.messages[static_cast<std::size_t>(index)], "n" + std::to_string(index));
    }
    EXPECT_EQ(diagnostics.droppedByCapacityCount(), 0U);
}

TEST(DiagnosticsTest, ShutdownDrainsRecordsAlreadyAccepted)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
        .asyncQueueCapacity = 64,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;

    const DiagnosticChannel channel = diagnostics.channel();
    for (int index = 0; index < 16; ++index) {
        channel.write(LogRecord::make(LogLevel::Info, "test", "pending"));
    }

    // No flush first: shutdown itself must not lose accepted records.
    diagnostics.shutdown();
    EXPECT_EQ(capture.messages.size(), 16U);
    EXPECT_EQ(diagnostics.writtenCount(), 16U);
}

TEST(DiagnosticsTest, FullQueueDropsNewestAndCounts)
{
    using namespace Core::Diagnostics;

    // A sink that blocks until released, so the queue is guaranteed to fill.
    struct BlockingSink final {
        std::mutex mutex;
        std::condition_variable released;
        bool release = false;
        int delivered = 0;
    };
    BlockingSink blocking;

    auto sink = [](void* userData, const Core::Diagnostics::LogRecord& /*record*/) {
        auto* state = static_cast<BlockingSink*>(userData);
        std::unique_lock lock(state->mutex);
        state->released.wait(lock, [state] { return state->release; });
        ++state->delivered;
    };

    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = sink,
        .sinkUserData = &blocking,
        .asyncQueueCapacity = 4,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;
    const DiagnosticChannel channel = diagnostics.channel();

    // 4 slots plus at most one in the sink; 32 writes cannot all be accepted.
    for (int index = 0; index < 32; ++index) {
        channel.write(LogRecord::make(LogLevel::Info, "test", "burst"));
    }
    EXPECT_GT(diagnostics.droppedByCapacityCount(), 0U);

    {
        const std::scoped_lock lock(blocking.mutex);
        blocking.release = true;
    }
    blocking.released.notify_all();

    diagnostics.shutdown();
    const u64 accepted = diagnostics.writtenCount();
    EXPECT_EQ(accepted + diagnostics.droppedByCapacityCount(), 32U);
}

TEST(DiagnosticsTest, ConcurrentWritersAreAllAccountedFor)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
        .asyncQueueCapacity = 1024,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;

    constexpr int ThreadCount = 4;
    constexpr int PerThread = 500;

    std::vector<std::thread> writers;
    writers.reserve(ThreadCount);
    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex) {
        writers.emplace_back([&diagnostics] {
            const DiagnosticChannel channel = diagnostics.channel();
            for (int index = 0; index < PerThread; ++index) {
                channel.write(LogRecord::make(LogLevel::Info, "test", "concurrent"));
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }

    diagnostics.shutdown();

    // Every record is either delivered or explicitly counted as dropped; none
    // may vanish, and none may be counted twice.
    EXPECT_EQ(diagnostics.writtenCount() + diagnostics.droppedByCapacityCount(),
              static_cast<u64>(ThreadCount) * PerThread);
    EXPECT_EQ(diagnostics.sinkFailureCount(), 0U);
    EXPECT_EQ(capture.messages.size(), diagnostics.writtenCount());
}

// Marks each delivered sequence number in a bitmap so a slot delivered twice is
// visible afterwards. Deliberately as cheap as a null sink: with a sink that
// formats, the drain thread never catches up to the writers, and the window this
// exercises -- shutdown draining what the join left behind -- is never reached.
constexpr int SequenceLimit = 1 << 22;

struct SequenceSink final {
    std::vector<bool> seen = std::vector<bool>(SequenceLimit, false);
    int delivered = 0;
    int duplicates = 0;
    int outOfRange = 0;
};

void sequenceSink(void* userData, const Core::Diagnostics::LogRecord& record)
{
    // No mutex: deliver serialises every sink call under m_sinkMutex, and the
    // counters are only read once the drain thread has been joined.
    auto* state = static_cast<SequenceSink*>(userData);
    const std::string_view message = record.message();
    ++state->delivered;
    if (message.size() != sizeof(int)) {
        ++state->outOfRange;
        return;
    }
    int sequence = 0;
    std::memcpy(&sequence, message.data(), sizeof(sequence));
    if (sequence < 0 || sequence >= SequenceLimit) {
        ++state->outOfRange;
        return;
    }
    if (state->seen[static_cast<std::size_t>(sequence)]) {
        ++state->duplicates;
        return;
    }
    state->seen[static_cast<std::size_t>(sequence)] = true;
}

TEST(DiagnosticsTest, ShutdownInterleavedWithWritersDeliversEachRecordExactlyOnce)
{
    using namespace Core::Diagnostics;

    // The sibling test above joins its writers before shutting down, so it never
    // reaches the window this one targets: shutdown draining the ring while a
    // writer is inside tryEnqueue. Instrumenting that residual loop showed this
    // shape reaches it in most runs and the bounded-writer shape in none.
    //
    // What this does not do: fail on the unlocked-ring defect it was written for.
    // The residual loop holds one record when it runs, and a lost update there
    // costs exactly that record -- indistinguishable from a write legitimately
    // refused by the close. Verified: the defect passes this test 6/6. It guards
    // against double and phantom delivery, not against a dropped record.
    constexpr int Rounds = 8;
    constexpr int WriterCount = 4;

    for (int round = 0; round < Rounds; ++round) {
        SequenceSink recorded;
        auto created = Diagnostics::Create(DiagnosticsConfig{
            .minLevel = LogLevel::Trace,
            .sink = &sequenceSink,
            .sinkUserData = &recorded,
            .asyncQueueCapacity = 64,
        });
        ASSERT_TRUE(created.has_value());
        auto& diagnostics = **created;

        // Unbounded on purpose. A writer loop with a fixed count has usually
        // finished by the time shutdown joins, and the residual drain is then
        // never entered at all -- instrumenting it under a bounded writer printed
        // zero hits, so a bounded version of this test passes on a broken ring.
        std::atomic<bool> stop{false};
        std::atomic<int> issued{0};
        std::vector<std::thread> writers;
        writers.reserve(WriterCount);
        for (int writerIndex = 0; writerIndex < WriterCount; ++writerIndex) {
            writers.emplace_back([&diagnostics, &stop, &issued] {
                const DiagnosticChannel channel = diagnostics.channel();
                while (!stop.load(std::memory_order_relaxed)) {
                    // Raw bytes, not text: make copies by explicit length, and
                    // formatting here would throttle the writers.
                    const int sequence = issued.fetch_add(1, std::memory_order_relaxed);
                    char message[sizeof(int)] = {};
                    std::memcpy(message, &sequence, sizeof(sequence));
                    channel.write(
                        LogRecord::make(LogLevel::Info, "test", std::string_view{message, sizeof(message)}));
                }
            });
        }

        // Let the writers reach a steady rate so the ring is populated when the
        // close lands, rather than shutting down into an empty queue.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        diagnostics.shutdown();
        stop.store(true, std::memory_order_relaxed);
        for (auto& writer : writers) {
            writer.join();
        }

        EXPECT_FALSE(diagnostics.isOpen());
        EXPECT_EQ(diagnostics.sinkFailureCount(), 0U);
        ASSERT_LT(issued.load(std::memory_order_relaxed), SequenceLimit)
            << "raise SequenceLimit: the writers outran the bitmap";

        // No total is assertable: a write racing the close is legitimately refused.
        // What must hold is that no slot was delivered twice and none was invented
        // -- an unlocked ring index repeats or skips, and both surface here.
        EXPECT_EQ(static_cast<u64>(recorded.delivered), diagnostics.writtenCount());
        EXPECT_EQ(recorded.duplicates, 0) << "a sequence number was delivered twice";
        EXPECT_EQ(recorded.outOfRange, 0) << "a slot was delivered that no writer wrote";

        // A second shutdown must not fault on the emptied ring or re-deliver.
        const int deliveredCount = recorded.delivered;
        diagnostics.shutdown();
        EXPECT_EQ(recorded.delivered, deliveredCount);
    }
}

TEST(DiagnosticsTest, FlushFromInsideASinkIsRefusedInsteadOfDeadlocking)
{
    using namespace Core::Diagnostics;

    struct FlushingSink final {
        Diagnostics* diagnostics = nullptr;
        int calls = 0;
        bool flushReturn = true;
    };

    FlushingSink sink;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = [](void* userData, const LogRecord&) {
            auto* state = static_cast<FlushingSink*>(userData);
            ++state->calls;
            // Would self-deadlock on m_sinkMutex before the t_inSink early return.
            state->flushReturn = state->diagnostics->flush(50);
        },
        .sinkUserData = &sink,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;
    sink.diagnostics = &diagnostics;

    diagnostics.channel().write(LogRecord::make(LogLevel::Info, "test", "flushing"));

    EXPECT_EQ(sink.calls, 1);
    EXPECT_FALSE(sink.flushReturn);
    EXPECT_EQ(diagnostics.writtenCount(), 1U);
    diagnostics.shutdown();
}

TEST(DiagnosticsTest, ErrorIsDeliveredWithoutAnExplicitFlush)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
        .asyncQueueCapacity = 64,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;

    // Stands in for the ~150 sites that log and then call std::terminate: the
    // record must be delivered before write returns, or it dies with the process.
    diagnostics.channel().write(LogRecord::make(LogLevel::Error, "test", "about-to-terminate"));

    ASSERT_EQ(capture.messages.size(), 1U);
    EXPECT_EQ(capture.messages[0], "about-to-terminate");
}

TEST(DiagnosticsTest, CriticalIsDeliveredWithoutAnExplicitFlush)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
        .asyncQueueCapacity = 64,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;
    const DiagnosticChannel channel = diagnostics.channel();

    channel.write(LogRecord::make(LogLevel::Info, "test", "before"));
    channel.write(LogRecord::make(LogLevel::Critical, "test", "fatal"));

    // Critical flushes itself, which also drains what preceded it, so ordering
    // is preserved rather than jumping the queue.
    ASSERT_EQ(capture.messages.size(), 2U);
    EXPECT_EQ(capture.messages[0], "before");
    EXPECT_EQ(capture.messages[1], "fatal");
}

TEST(DiagnosticsTest, AsyncRecordOutlivesItsProducerStackFrame)
{
    using namespace Core::Diagnostics;

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
        .asyncQueueCapacity = 64,
    });
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;

    {
        // The source buffer is gone before the drain thread runs. A borrowed
        // view would read freed memory here; the record owns its bytes.
        std::string transient(120, 'z');
        diagnostics.channel().write(LogRecord::make(LogLevel::Info, "test", transient));
        transient.assign(120, 'q');
    }

    ASSERT_TRUE(diagnostics.flush());
    ASSERT_EQ(capture.messages.size(), 1U);
    EXPECT_EQ(capture.messages[0], std::string(120, 'z'));
}

// Unique per test so a parallel run cannot have two Diagnostics appending to one
// file, which would make every size assertion racy.
[[nodiscard]] std::filesystem::path makeLogPath(const std::string_view stem)
{
    static std::atomic<int> counter{0};
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "tina_diagnostics_tests";
    std::filesystem::create_directories(directory);
    return directory / (std::string(stem) + std::to_string(counter.fetch_add(1)) + ".log");
}

[[nodiscard]] std::string readWholeFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

TEST(DiagnosticsFileSinkTest, WritesRecordsToTheFile)
{
    using namespace Core::Diagnostics;

    const std::filesystem::path path = makeLogPath("basic");
    std::filesystem::remove(path);

    {
        CaptureSink capture;
        auto created = Diagnostics::Create(DiagnosticsConfig{
            .minLevel = LogLevel::Trace,
            .sink = &captureSink,
            .sinkUserData = &capture,
            .filePath = path.string(),
            .platformDebugSink = false,
        });
        ASSERT_TRUE(created.has_value());
        auto& diagnostics = **created;
        ASSERT_TRUE(diagnostics.isFileSinkOpen());

        diagnostics.channel().write(LogRecord::make(LogLevel::Warn, "file.test", "written-to-disk"));

        // The custom sink still ran: the file sink is additive, not a replacement.
        EXPECT_EQ(capture.messages.size(), 1U);
        diagnostics.shutdown();
        EXPECT_FALSE(diagnostics.isFileSinkOpen());
    }

    const std::string contents = readWholeFile(path);
    EXPECT_NE(contents.find("written-to-disk"), std::string::npos);
    EXPECT_NE(contents.find("[Warn]"), std::string::npos);
    EXPECT_NE(contents.find("[file.test]"), std::string::npos);
    std::filesystem::remove(path);
}

TEST(DiagnosticsFileSinkTest, AppendsRatherThanTruncating)
{
    using namespace Core::Diagnostics;

    const std::filesystem::path path = makeLogPath("append");
    std::filesystem::remove(path);

    for (int run = 0; run < 2; ++run) {
        CaptureSink capture;
        auto created = Diagnostics::Create(DiagnosticsConfig{
            .minLevel = LogLevel::Trace,
            .sink = &captureSink,
            .sinkUserData = &capture,
            .filePath = path.string(),
            .platformDebugSink = false,
        });
        ASSERT_TRUE(created.has_value());
        (*created)->channel().write(
            LogRecord::make(LogLevel::Info, "file.test", run == 0 ? "first-run" : "second-run"));
        (*created)->shutdown();
    }

    // A restart must not erase the run that explains why it restarted.
    const std::string contents = readWholeFile(path);
    EXPECT_NE(contents.find("first-run"), std::string::npos);
    EXPECT_NE(contents.find("second-run"), std::string::npos);
    std::filesystem::remove(path);
}

TEST(DiagnosticsFileSinkTest, RotatesOnceCapacityIsReached)
{
    using namespace Core::Diagnostics;

    const std::filesystem::path path = makeLogPath("rotate");
    const std::filesystem::path backup = std::filesystem::path{path}.concat(".1");
    std::filesystem::remove(path);
    std::filesystem::remove(backup);

    {
        CaptureSink capture;
        auto created = Diagnostics::Create(DiagnosticsConfig{
            .minLevel = LogLevel::Trace,
            .sink = &captureSink,
            .sinkUserData = &capture,
            .filePath = path.string(),
            .fileRotateBytes = 512,
            .platformDebugSink = false,
        });
        ASSERT_TRUE(created.has_value());
        auto& diagnostics = **created;
        const DiagnosticChannel channel = diagnostics.channel();

        for (int index = 0; index < 80; ++index) {
            channel.write(LogRecord::make(LogLevel::Info, "file.test", "rotate-me-please"));
        }
        EXPECT_GT(diagnostics.fileRotationCount(), 0U);
        EXPECT_TRUE(diagnostics.isFileSinkOpen());
        diagnostics.shutdown();
    }

    ASSERT_TRUE(std::filesystem::exists(backup));
    ASSERT_TRUE(std::filesystem::exists(path));

    // The live file restarted at zero, so it holds at most one rotation's worth
    // plus the line that crossed the threshold.
    EXPECT_LT(std::filesystem::file_size(path), 512U + 512U);
    std::filesystem::remove(path);
    std::filesystem::remove(backup);
}

TEST(DiagnosticsFileSinkTest, UnopenableFileStillLeavesLoggingUsable)
{
    using namespace Core::Diagnostics;

    // A directory as the target: fopen cannot open it for writing on any platform.
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "tina_diagnostics_tests";
    std::filesystem::create_directories(directory);

    CaptureSink capture;
    auto created = Diagnostics::Create(DiagnosticsConfig{
        .minLevel = LogLevel::Trace,
        .sink = &captureSink,
        .sinkUserData = &capture,
        .filePath = directory.string(),
        .platformDebugSink = false,
    });

    // Create must succeed: refusing to start over a log file would be worse than
    // logging without one.
    ASSERT_TRUE(created.has_value());
    auto& diagnostics = **created;
    EXPECT_FALSE(diagnostics.isFileSinkOpen());

    diagnostics.channel().write(LogRecord::make(LogLevel::Error, "file.test", "still-delivered"));
    ASSERT_EQ(capture.messages.size(), 1U);
    EXPECT_EQ(capture.messages[0], "still-delivered");
    EXPECT_EQ(diagnostics.writtenCount(), 1U);
}

TEST(DiagnosticsFileSinkTest, AsyncWritesReachTheFileAfterFlush)
{
    using namespace Core::Diagnostics;

    const std::filesystem::path path = makeLogPath("async");
    std::filesystem::remove(path);

    {
        CaptureSink capture;
        auto created = Diagnostics::Create(DiagnosticsConfig{
            .minLevel = LogLevel::Trace,
            .sink = &captureSink,
            .sinkUserData = &capture,
            .asyncQueueCapacity = 128,
            .filePath = path.string(),
            .platformDebugSink = false,
        });
        ASSERT_TRUE(created.has_value());
        auto& diagnostics = **created;

        diagnostics.channel().write(LogRecord::make(LogLevel::Info, "file.test", "queued-then-written"));
        ASSERT_TRUE(diagnostics.flush());

        // flush covers the file handle too, so the bytes are readable without
        // waiting for shutdown.
        EXPECT_NE(readWholeFile(path).find("queued-then-written"), std::string::npos);
        diagnostics.shutdown();
    }
    std::filesystem::remove(path);
}

} // namespace
} // namespace Tina::Tests
