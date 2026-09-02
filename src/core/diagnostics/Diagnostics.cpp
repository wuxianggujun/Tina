#include <tina/core/diagnostics/Diagnostics.hpp>

#include <tina/core/base/SourceLocation.hpp>
#include <tina/core/diagnostics/Log.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <new>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#endif

namespace Tina::Core::Diagnostics {
namespace {

// One layout for every stream sink, so a console line and a file line can be
// compared directly instead of being formatted twice with a drift between them.
// Returns bytes written, or 0 on failure.
usize writeRecordLine(std::FILE* const stream, const LogRecord& record) noexcept
{
    const LogLevel level = record.level();
    const std::string_view levelName = logLevelName(level);
    const std::string_view category = record.category().empty() ? std::string_view{"tina"} : record.category();
    const std::string_view message = record.message();
    const SourceLocation location = record.location();
    const char* const file = location.file_name() != nullptr ? location.file_name() : "";
    const char* const function = location.function_name() != nullptr ? location.function_name() : "";

    const int produced = std::fprintf(
        stream,
        "[%.*s] [%.*s] %.*s%s (%s:%u %s)\n",
        static_cast<int>(levelName.size()),
        levelName.data(),
        static_cast<int>(category.size()),
        category.data(),
        static_cast<int>(message.size()),
        message.data(),
        record.isTruncated() ? "[...]" : "",
        file,
        location.line(),
        function);

    return produced > 0 ? static_cast<usize>(produced) : 0;
}

void writeConsoleSink(const LogRecord& record) noexcept
{
    // Every level goes to stderr: stdout is the machine-readable evidence channel
    // that tools/bench/run_benchmark_gate.py parses as a single JSON line.
    (void) writeRecordLine(stderr, record);

    // Deliberately not flushed per line. A flush is one syscall per record and
    // was the dominant cost of the previous implementation; stderr is unbuffered
    // by default, so lines arrive without one anyway.
}

#if defined(_WIN32)
[[nodiscard]] bool debuggerIsAttached() noexcept
{
    return ::IsDebuggerPresent() != 0;
}

void writePlatformDebugSink(const LogRecord& record) noexcept
{
    // The record buffer is already NUL-terminated for exactly this reason, so no
    // scratch copy is needed for the message itself.
    ::OutputDebugStringA("[");
    ::OutputDebugStringA(std::string(logLevelName(record.level())).c_str());
    ::OutputDebugStringA("] ");
    ::OutputDebugStringA(record.message().data());
    ::OutputDebugStringA("\n");
}
#elif defined(__ANDROID__)
[[nodiscard]] bool debuggerIsAttached() noexcept
{
    // logcat is the only log a user can read on a device, so this sink is never
    // conditional on a debugger there.
    return true;
}

void writePlatformDebugSink(const LogRecord& record) noexcept
{
    int priority = ANDROID_LOG_INFO;
    switch (record.level()) {
    case LogLevel::Trace:
        priority = ANDROID_LOG_VERBOSE;
        break;
    case LogLevel::Debug:
        priority = ANDROID_LOG_DEBUG;
        break;
    case LogLevel::Info:
        priority = ANDROID_LOG_INFO;
        break;
    case LogLevel::Warn:
        priority = ANDROID_LOG_WARN;
        break;
    case LogLevel::Error:
        priority = ANDROID_LOG_ERROR;
        break;
    case LogLevel::Critical:
        priority = ANDROID_LOG_FATAL;
        break;
    case LogLevel::Off:
        return;
    }

    const std::string_view category = record.category().empty() ? std::string_view{"Tina"} : record.category();
    // __android_log_write needs a NUL-terminated tag; category is a literal in
    // practice but that is not guaranteed, so it is copied.
    const std::string tag(category);
    (void) __android_log_write(priority, tag.c_str(), record.message().data());
}
#else
[[nodiscard]] bool debuggerIsAttached() noexcept
{
    return false;
}

void writePlatformDebugSink(const LogRecord& /*record*/) noexcept
{
}
#endif

void reportSinkFailureOnce(const char* detail) noexcept
{
    // Controlled emergency path only — never re-enters Diagnostics::write.
    std::fprintf(stderr, "Tina diagnostics sink failure: %s\n", detail != nullptr ? detail : "unknown");
    std::fflush(stderr);
}

// Whether this target can spawn the drain thread at all. Emscripten without
// -pthread cannot: std::thread's constructor throws, and the project builds with
// -fno-exceptions, so the catch below would abort the process instead of degrading.
// A browser build asking for an async queue would therefore never reach its first
// frame. Synchronous logging on a single-threaded target is the honest fallback --
// there is no other thread for a sink to interfere with.
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
inline constexpr bool kDrainThreadSupported = false;
#else
inline constexpr bool kDrainThreadSupported = true;
#endif

// Per-thread, not per-object: a sink that logs is a defect whichever Diagnostics
// it targets, and the recursion it would cause is confined to the thread that
// entered the sink. Suppressing it globally per thread also covers a sink that
// reaches a second Diagnostics instance.
thread_local bool t_inSink = false;

class SinkGuard final {
  public:
    SinkGuard() noexcept
        : m_entered(!t_inSink)
    {
        if (m_entered) {
            t_inSink = true;
        }
    }
    ~SinkGuard() noexcept
    {
        if (m_entered) {
            t_inSink = false;
        }
    }
    SinkGuard(const SinkGuard&) = delete;
    SinkGuard& operator=(const SinkGuard&) = delete;
    SinkGuard(SinkGuard&&) = delete;
    SinkGuard& operator=(SinkGuard&&) = delete;

    [[nodiscard]] bool entered() const noexcept { return m_entered; }

  private:
    bool m_entered;
};

// Explicitly assigned by the owner; never lazily constructed. Relaxed ordering
// would be enough for the pointer itself, but acquire/release keeps the
// Diagnostics state it points at visible to a thread that just observed it.
std::atomic<Diagnostics*> g_defaultDiagnostics{nullptr};

} // namespace

void setDefaultDiagnostics(Diagnostics* const diagnostics) noexcept
{
    g_defaultDiagnostics.store(diagnostics, std::memory_order_release);
}

DiagnosticChannel defaultChannel() noexcept
{
    Diagnostics* const diagnostics = g_defaultDiagnostics.load(std::memory_order_acquire);
    if (diagnostics == nullptr) {
        return DiagnosticChannel{};
    }
    return diagnostics->channel();
}

DiagnosticChannel::DiagnosticChannel(Diagnostics* owner) noexcept
    : m_owner(owner)
{
}

bool DiagnosticChannel::isOpen() const noexcept
{
    return m_owner != nullptr && m_owner->isOpen();
}

bool DiagnosticChannel::isEnabled(LogLevel level) const noexcept
{
    return m_owner != nullptr && m_owner->isEnabled(level);
}

void DiagnosticChannel::write(const LogRecord& record) const noexcept
{
    if (m_owner == nullptr) {
        return;
    }
    m_owner->write(record);
}

Diagnostics::Diagnostics(DiagnosticsConfig config) noexcept
    : m_config(config)
{
    if (m_config.minLevel > LogLevel::Off) {
        m_config.minLevel = LogLevel::Off;
    }
}

Diagnostics::~Diagnostics() noexcept
{
    shutdown();
}

Result<std::unique_ptr<Diagnostics>> Diagnostics::Create(const DiagnosticsConfig& config) noexcept
{
    try {
        if (config.minLevel > LogLevel::Off) {
            return failure(CoreErrorCode::InvalidArgument, "DiagnosticsConfig.minLevel is out of range");
        }
        auto diagnostics = std::unique_ptr<Diagnostics>(new Diagnostics(config));

        // Console keeps its original rule: it is the fallback when no custom sink
        // was installed, so a test that captures records still sees nothing else.
        // File and platform are additive and independent of it.
        diagnostics->m_useConsole = config.sink == nullptr;
        diagnostics->m_usePlatformDebug = config.platformDebugSink && debuggerIsAttached();

        if (!config.filePath.empty()) {
            // The config's view may point at a temporary; the path is needed again
            // on every rotation, so it is copied rather than borrowed.
            diagnostics->m_filePath.assign(config.filePath);
            diagnostics->m_config.filePath = diagnostics->m_filePath;

            // userApplicationFilePath only composes a path; the per-user directory
            // will not exist on a first run. Errors are ignored -- fopen below
            // reports the outcome that actually matters.
            const std::filesystem::path parent =
                std::filesystem::path{diagnostics->m_filePath}.parent_path();
            if (!parent.empty()) {
                std::error_code directoryError;
                std::filesystem::create_directories(parent, directoryError);
            }

            // Append, never truncate: a previous run's log is evidence, and a
            // crash-restart loop would otherwise erase the run that explains it.
            diagnostics->m_fileHandle = std::fopen(diagnostics->m_filePath.c_str(), "ab");
            if (diagnostics->m_fileHandle == nullptr) {
                // Not a Create failure. A product that cannot open its log file
                // still has a console and a platform sink, and refusing to start
                // over a log file would be a worse outcome than logging elsewhere.
                reportSinkFailureOnce("log file could not be opened");
            } else {
                diagnostics->m_fileOpen.store(true, std::memory_order_release);
                if (std::fseek(diagnostics->m_fileHandle, 0, SEEK_END) == 0) {
                    const long position = std::ftell(diagnostics->m_fileHandle);
                    if (position > 0) {
                        diagnostics->m_fileBytes = static_cast<usize>(position);
                    }
                }
            }
        }

        if (config.asyncQueueCapacity > 0 && !kDrainThreadSupported) {
            // Downgraded to synchronous rather than failed, for the same reason as the
            // log file above: refusing to start because logging cannot be asynchronous
            // would be a worse outcome than logging synchronously. isAsync() reports
            // false afterwards, so a caller that cares can still tell.
            reportSinkFailureOnce("async logging unavailable on this target; using synchronous writes");
        } else if (config.asyncQueueCapacity > 0) {
            // Allocated here rather than through MemoryTracker: Diagnostics is
            // created before the tracker exists, and a logger that needs another
            // subsystem to start would invert the dependency it is there to serve.
            diagnostics->m_queue = std::make_unique<LogRecord[]>(config.asyncQueueCapacity);
            diagnostics->m_queueCapacity = config.asyncQueueCapacity;
            diagnostics->m_drainThread = std::thread([raw = diagnostics.get()] { raw->drainLoop(); });
        }
        return diagnostics;
    } catch (const std::system_error& error) {
        return failure(
            CoreErrorCode::Internal,
            error.what() != nullptr ? error.what() : "Diagnostics queue thread creation failed");
    } catch (const std::bad_alloc&) {
        return failure(CoreErrorCode::OutOfMemory, "Diagnostics::Create allocation failed");
    } catch (const std::exception& exception) {
        return failure(CoreErrorCode::Internal, exception.what() != nullptr ? exception.what() : "Diagnostics::Create");
    } catch (...) {
        return failure(CoreErrorCode::Internal, "Diagnostics::Create unknown exception");
    }
}

void Diagnostics::shutdown() noexcept
{
    // Closing first stops new records from being accepted, so the queue is
    // guaranteed to reach empty rather than being refilled under the drain.
    m_open.store(false, std::memory_order_release);

    if (m_drainThread.joinable()) {
        if (m_drainThread.get_id() == std::this_thread::get_id()) {
            // A sink called shutdown from the drain thread. Joining would
            // deadlock on self; the loop exits on its own once it sees the flag.
            m_stopping.store(true, std::memory_order_release);
            return;
        }
        {
            const std::scoped_lock lock(m_queueMutex);
            m_stopping.store(true, std::memory_order_release);
        }
        m_queueFilled.notify_all();
        m_drainThread.join();
    }

    // Records accepted before the close still deserve delivery. Each is taken
    // under m_queueMutex because a writer that passed isEnabled() before the
    // store above can still be inside tryEnqueue, and delivered outside it
    // because deliver takes m_sinkMutex while flush takes them in that order.
    while (true) {
        LogRecord record;
        {
            const std::scoped_lock lock(m_queueMutex);
            if (m_queueSize == 0) {
                break;
            }
            record = m_queue[m_queueHead];
            m_queueHead = (m_queueHead + 1) % m_queueCapacity;
            --m_queueSize;
        }
        deliver(record);
    }

    (void) flush();

    // Last thing: a record delivered above still needs its file line.
    const std::scoped_lock lock(m_sinkMutex);
    closeFileLocked();
}

bool Diagnostics::flush(const u32 timeoutMilliseconds) noexcept
{
    // Reached from inside the sink, which deliver runs under m_sinkMutex: taking
    // it again would self-deadlock, and false says nothing was flushed.
    if (t_inSink) {
        return false;
    }

    bool complete = true;
    // isAsync instead of m_drainThread.joinable(): reading the thread object races
    // with the join in shutdown, and after it m_stopping ends the wait at once.
    if (isAsync()) {
        std::unique_lock lock(m_queueMutex);
        complete = m_queueDrained.wait_for(lock, std::chrono::milliseconds(timeoutMilliseconds), [this] {
            return (m_queueSize == 0 && !m_draining) || m_stopping.load(std::memory_order_relaxed);
        });
    }

    // stdout is not flushed: no log line goes there, and flushing it would touch
    // the evidence channel a benchmark or gate process owns.
    std::fflush(stderr);
    {
        // Under the sink lock so a concurrent write cannot be flushed halfway.
        const std::scoped_lock lock(m_sinkMutex);
        if (m_fileHandle != nullptr) {
            (void) std::fflush(m_fileHandle);
        }
    }
    return complete;
}

bool Diagnostics::isOpen() const noexcept
{
    return m_open.load(std::memory_order_acquire);
}

bool Diagnostics::isAsync() const noexcept
{
    return m_queueCapacity > 0;
}

LogLevel Diagnostics::minLevel() const noexcept
{
    return m_config.minLevel;
}

u64 Diagnostics::writtenCount() const noexcept
{
    return m_writtenCount.load(std::memory_order_relaxed);
}

u64 Diagnostics::droppedByLevelCount() const noexcept
{
    return m_droppedByLevelCount.load(std::memory_order_relaxed);
}

u64 Diagnostics::droppedByCapacityCount() const noexcept
{
    return m_droppedByCapacityCount.load(std::memory_order_relaxed);
}

u64 Diagnostics::sinkFailureCount() const noexcept
{
    return m_sinkFailureCount.load(std::memory_order_relaxed);
}

bool Diagnostics::isFileSinkOpen() const noexcept
{
    return m_fileOpen.load(std::memory_order_acquire);
}

u64 Diagnostics::fileRotationCount() const noexcept
{
    return m_fileRotationCount.load(std::memory_order_relaxed);
}

DiagnosticChannel Diagnostics::channel() noexcept
{
    return DiagnosticChannel{this};
}

bool Diagnostics::isEnabled(LogLevel level) const noexcept
{
    return isOpen() && isLogLevelEnabled(m_config.minLevel, level);
}

void Diagnostics::deliver(const LogRecord& record) noexcept
{
    const SinkGuard guard;
    if (!guard.entered()) {
        // Reached from inside the sink. Counted, never delivered.
        m_sinkFailureCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const std::scoped_lock lock(m_sinkMutex);

    // The stream sinks run first and cannot throw. Only the caller-supplied sink
    // can, and a throw there must not cost the record its file and platform copy.
    if (m_useConsole) {
        writeConsoleSink(record);
    }
    if (m_fileHandle != nullptr) {
        writeToFileLocked(record);
    }
    if (m_usePlatformDebug) {
        writePlatformDebugSink(record);
    }

    try {
        if (m_config.sink != nullptr) {
            m_config.sink(m_config.sinkUserData, record);
        }
        m_writtenCount.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        m_sinkFailureCount.fetch_add(1, std::memory_order_relaxed);
        reportSinkFailureOnce("exception escaped LogSinkFn");
    }
}

void Diagnostics::writeToFileLocked(const LogRecord& record) noexcept
{
    if (m_config.fileRotateBytes > 0 && m_fileBytes >= m_config.fileRotateBytes) {
        rotateFileLocked();
        if (m_fileHandle == nullptr) {
            return;
        }
    }

    const usize written = writeRecordLine(m_fileHandle, record);
    if (written == 0) {
        // A failed write means the handle is no longer usable -- a full disk or a
        // removed volume. Closing it stops every later line from retrying.
        reportSinkFailureOnce("log file write failed");
        closeFileLocked();
        return;
    }
    m_fileBytes += written;
}

void Diagnostics::rotateFileLocked() noexcept
{
    closeFileLocked();

    // One backup only. A ring of N would need N renames per rotation and answers
    // a question nobody asked of a log this size.
    const std::string backupPath = m_filePath + ".1";
    (void) std::remove(backupPath.c_str());
    if (std::rename(m_filePath.c_str(), backupPath.c_str()) != 0) {
        reportSinkFailureOnce("log file rotation failed");
        return;
    }

    m_fileHandle = std::fopen(m_filePath.c_str(), "ab");
    if (m_fileHandle == nullptr) {
        reportSinkFailureOnce("log file could not be reopened after rotation");
        return;
    }
    m_fileOpen.store(true, std::memory_order_release);
    m_fileBytes = 0;
    m_fileRotationCount.fetch_add(1, std::memory_order_relaxed);
}

void Diagnostics::closeFileLocked() noexcept
{
    if (m_fileHandle == nullptr) {
        return;
    }
    (void) std::fclose(m_fileHandle);
    m_fileHandle = nullptr;
    m_fileBytes = 0;
    m_fileOpen.store(false, std::memory_order_release);
}

bool Diagnostics::tryEnqueue(const LogRecord& record) noexcept
{
    {
        const std::scoped_lock lock(m_queueMutex);
        // Rechecked under the lock: shutdown clears m_open before it takes this
        // mutex, so a slot claimed here is either drained by it or refused.
        if (!isOpen()) {
            return false;
        }
        if (m_queueSize == m_queueCapacity) {
            // Drop the newest rather than overwrite the oldest: the record that
            // explains how the burst started is worth more than the one that
            // arrived while it was already known to be overloaded.
            m_droppedByCapacityCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        m_queue[(m_queueHead + m_queueSize) % m_queueCapacity] = record;
        ++m_queueSize;
    }
    m_queueFilled.notify_one();
    return true;
}

void Diagnostics::drainLoop() noexcept
{
    while (true) {
        LogRecord record;
        {
            std::unique_lock lock(m_queueMutex);
            m_queueFilled.wait(
                lock, [this] { return m_stopping.load(std::memory_order_relaxed) || m_queueSize > 0; });
            if (m_queueSize == 0) {
                // Only reachable when stopping: shutdown joins this thread and
                // delivers whatever is left, so nothing is lost by leaving now.
                return;
            }
            record = m_queue[m_queueHead];
            m_queueHead = (m_queueHead + 1) % m_queueCapacity;
            --m_queueSize;

            // Set before the lock is released so flush cannot observe an empty
            // queue while this record is still on its way to the sink.
            m_draining = true;
        }

        deliver(record);

        {
            const std::scoped_lock lock(m_queueMutex);
            m_draining = false;
        }
        m_queueDrained.notify_all();
    }
}

void Diagnostics::write(const LogRecord& record) noexcept
{
    if (!isEnabled(record.level())) {
        if (isOpen()) {
            m_droppedByLevelCount.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    // A record produced by the sink itself must not be queued either: it would
    // be delivered later instead of recursing, which hides the defect rather
    // than reporting it.
    if (m_queueCapacity == 0 || t_inSink) {
        deliver(record);
        return;
    }

    if (!tryEnqueue(record)) {
        return;
    }

    // Error and above flush before returning. Tina has ~150 sites that log and
    // then call std::terminate, and a queued record dies with the process -- the
    // line explaining the termination is exactly the one that must survive it.
    //
    // Queued first, then waited on, so it stays in order behind the records that
    // set up its context instead of jumping ahead of them.
    if (static_cast<u8>(record.level()) >= static_cast<u8>(LogLevel::Error)) {
        (void) flush();
    }
}

} // namespace Tina::Core::Diagnostics
