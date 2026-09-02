#pragma once

#include <tina/core/base/Compiler.hpp>
#include <tina/core/diagnostics/DiagnosticChannel.hpp>
#include <tina/core/diagnostics/LogLevel.hpp>
#include <tina/core/error/Result.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace Tina::Core::Diagnostics {

// Optional test/custom sink. Must not call back into Diagnostics. What that is
// held to: a write is refused and counted as a sink failure, and a flush returns
// false without flushing -- both are dropped diagnostics, not deadlocks. shutdown
// from the sink is honoured but leaves the drain thread to exit on its own.
// userData is opaque. Exceptions are caught at the Diagnostics boundary and never
// re-enter logging.
using LogSinkFn = void (*)(void* userData, const LogRecord& record);

struct DiagnosticsConfig final {
    LogLevel minLevel = LogLevel::Info;
    LogSinkFn sink = nullptr;
    void* sinkUserData = nullptr;

    // 0 keeps writes synchronous on the calling thread. Any other value spawns
    // one drain thread and a queue of that many records, so a producer pays only
    // the copy into a slot.
    //
    // Default is synchronous because a test that writes then reads a counter must
    // see the result without an intervening flush. EngineHost opts in.
    //
    // Silently downgraded to synchronous on targets that cannot spawn the drain
    // thread (Emscripten built without -pthread). Create still succeeds; isAsync()
    // reports false.
    usize asyncQueueCapacity = 0;

    // Non-empty opens a file sink. Copied during Create, so a temporary is fine.
    //
    // Additive: it runs alongside whichever of sink/console is active rather than
    // replacing it. Missing parent directories are created, matching what
    // Core::writeFile already does, so a path from Core::userApplicationFilePath
    // works without the caller preparing anything.
    std::string_view filePath{};

    // Rotates once the live file has reached this size, keeping one backup at
    // "<filePath>.1". 0 disables rotation and lets the file grow.
    //
    // Checked before a line is written, not after, so the file may exceed this by
    // the length of one line. Truncating a line mid-record would be worse.
    usize fileRotateBytes = 0;

    // OutputDebugStringA on Windows, __android_log_write on Android, nothing
    // elsewhere. On Android this is the only output a user can actually read, so
    // it defaults on.
    //
    // On Windows it is skipped entirely unless a debugger was attached when
    // Create ran: the call is a syscall per line and does nothing without one.
    // A debugger attached later is not picked up -- re-checking per line is the
    // cost this avoids.
    bool platformDebugSink = true;
};

// EngineHost-owned diagnostics root. Every member is safe to call from any
// thread. Writes are synchronous by default and asynchronous when
// DiagnosticsConfig::asyncQueueCapacity is non-zero.
class Diagnostics final {
  public:
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&&) = delete;
    Diagnostics& operator=(Diagnostics&&) = delete;

    ~Diagnostics() noexcept;

    [[nodiscard]] static Result<std::unique_ptr<Diagnostics>> Create(const DiagnosticsConfig& config) noexcept;

    // Idempotent. Drains and joins the queue thread, so records already accepted
    // still reach the sink. After it returns, all issued channels no-op.
    void shutdown() noexcept;

    // Blocks until every accepted record has reached the sink, then flushes the
    // console streams. Returns false on timeout, which means a sink is stuck.
    // A no-op returning true in synchronous mode -- the write already happened.
    [[nodiscard]] bool flush(u32 timeoutMilliseconds = 2000) noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool isAsync() const noexcept;
    [[nodiscard]] LogLevel minLevel() const noexcept;
    [[nodiscard]] u64 writtenCount() const noexcept;
    // Records that reached write() and were rejected by minLevel.
    //
    // Counts only what got that far. The TINA_LOG_* macros test isEnabled() first
    // and never build a record when it fails, so a filtered macro call is invisible
    // here -- skipping the record is the point, and an atomic increment would cost
    // more than the check it is meant to be cheaper than. This counter therefore
    // measures callers that bypass the macros, not total log volume.
    [[nodiscard]] u64 droppedByLevelCount() const noexcept;

    // Records the queue refused because it was full. Non-zero means the sink is
    // slower than the producers; it is a load signal, not an error.
    [[nodiscard]] u64 droppedByCapacityCount() const noexcept;
    [[nodiscard]] u64 sinkFailureCount() const noexcept;

    // False when filePath was set but the file could not be opened or a write
    // failed. Logging continues through the other sinks either way: losing the
    // file is not a reason to lose the diagnostic.
    [[nodiscard]] bool isFileSinkOpen() const noexcept;
    [[nodiscard]] u64 fileRotationCount() const noexcept;
    [[nodiscard]] DiagnosticChannel channel() noexcept;

  private:
    friend class DiagnosticChannel;

    explicit Diagnostics(DiagnosticsConfig config) noexcept;

    void write(const LogRecord& record) noexcept;
    [[nodiscard]] bool isEnabled(LogLevel level) const noexcept;

    // Runs the sink under m_sinkMutex with the recursion guard held.
    void deliver(const LogRecord& record) noexcept;
    [[nodiscard]] bool tryEnqueue(const LogRecord& record) noexcept;
    void drainLoop() noexcept;

    // All three require m_sinkMutex, which deliver already holds.
    void writeToFileLocked(const LogRecord& record) noexcept;
    void rotateFileLocked() noexcept;
    void closeFileLocked() noexcept;

    DiagnosticsConfig m_config{};

    // Owns the bytes m_config.filePath only borrows.
    std::string m_filePath;
    std::FILE* m_fileHandle = nullptr;
    usize m_fileBytes = 0;
    std::atomic<bool> m_fileOpen{false};
    std::atomic<u64> m_fileRotationCount{0};
    bool m_useConsole = false;
    bool m_usePlatformDebug = false;

    std::atomic<bool> m_open{true};
    std::atomic<u64> m_writtenCount{0};
    std::atomic<u64> m_droppedByLevelCount{0};
    std::atomic<u64> m_droppedByCapacityCount{0};
    std::atomic<u64> m_sinkFailureCount{0};

    // Serialises sink invocation. A LogSinkFn is written by the consumer and
    // cannot be assumed reentrant, so exactly one thread is inside it at a time.
    std::mutex m_sinkMutex;

    // Ring buffer, only touched under m_queueMutex.
    std::unique_ptr<LogRecord[]> m_queue;
    usize m_queueCapacity = 0;
    usize m_queueHead = 0;
    usize m_queueSize = 0;
    bool m_draining = false;
    std::mutex m_queueMutex;
    std::condition_variable m_queueFilled;
    std::condition_variable m_queueDrained;
    std::atomic<bool> m_stopping{false};
    std::thread m_drainThread;
};

} // namespace Tina::Core::Diagnostics
