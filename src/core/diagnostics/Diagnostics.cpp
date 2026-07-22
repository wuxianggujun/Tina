#include <tina/core/diagnostics/Diagnostics.hpp>

#include <cstdio>
#include <exception>
#include <new>
#include <utility>

namespace Tina::Core::Diagnostics {
namespace {

void writeConsoleSink(const LogRecord& record) noexcept
{
    FILE* const stream =
        (record.level == LogLevel::Warn || record.level == LogLevel::Error || record.level == LogLevel::Critical)
            ? stderr
            : stdout;

    const std::string_view levelName = logLevelName(record.level);
    const std::string_view category = record.category.empty() ? std::string_view{"tina"} : record.category;
    const char* const file = record.location.file_name() != nullptr ? record.location.file_name() : "";
    const char* const function = record.location.function_name() != nullptr ? record.location.function_name() : "";

    std::fprintf(
        stream,
        "[%.*s] [%.*s] %.*s (%s:%u %s)\n",
        static_cast<int>(levelName.size()),
        levelName.data(),
        static_cast<int>(category.size()),
        category.data(),
        static_cast<int>(record.message.size()),
        record.message.data(),
        file,
        record.location.line(),
        function);
    std::fflush(stream);
}

void reportSinkFailureOnce(const char* detail) noexcept
{
    // Controlled emergency path only — never re-enters Diagnostics::write.
    std::fprintf(stderr, "Tina diagnostics sink failure: %s\n", detail != nullptr ? detail : "unknown");
    std::fflush(stderr);
}

} // namespace

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
        return std::unique_ptr<Diagnostics>(new Diagnostics(config));
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
    m_open = false;
}

bool Diagnostics::isOpen() const noexcept
{
    return m_open;
}

LogLevel Diagnostics::minLevel() const noexcept
{
    return m_config.minLevel;
}

u64 Diagnostics::writtenCount() const noexcept
{
    return m_writtenCount;
}

u64 Diagnostics::droppedByLevelCount() const noexcept
{
    return m_droppedByLevelCount;
}

u64 Diagnostics::sinkFailureCount() const noexcept
{
    return m_sinkFailureCount;
}

DiagnosticChannel Diagnostics::channel() noexcept
{
    return DiagnosticChannel{this};
}

bool Diagnostics::isEnabled(LogLevel level) const noexcept
{
    return m_open && isLogLevelEnabled(m_config.minLevel, level);
}

void Diagnostics::write(const LogRecord& record) noexcept
{
    if (!isEnabled(record.level)) {
        if (m_open) {
            ++m_droppedByLevelCount;
        }
        return;
    }

    // Sink failure must not recurse into logging.
    if (m_inSink) {
        ++m_sinkFailureCount;
        return;
    }

    m_inSink = true;
    try {
        if (m_config.sink != nullptr) {
            m_config.sink(m_config.sinkUserData, record);
        } else {
            writeConsoleSink(record);
        }
        ++m_writtenCount;
    } catch (...) {
        ++m_sinkFailureCount;
        reportSinkFailureOnce("exception escaped LogSinkFn");
    }
    m_inSink = false;
}

} // namespace Tina::Core::Diagnostics
