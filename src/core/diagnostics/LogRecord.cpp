#include <tina/core/diagnostics/LogRecord.hpp>

#include <cstring>

namespace Tina::Core::Diagnostics {

LogRecord LogRecord::make(
    const LogLevel level,
    const std::string_view category,
    const std::string_view message,
    const SourceLocation location) noexcept
{
    LogRecord record;
    record.m_level = level;
    record.m_category = category;
    record.m_location = location;

    // One byte is reserved so the buffer is always NUL-terminated: sinks that
    // hand the text to a C API (OutputDebugStringA, __android_log_write) need a
    // terminator, and message() alone cannot provide one.
    constexpr usize maximum = MessageCapacity - 1;
    usize copied = message.size();
    if (copied > maximum) {
        copied = maximum;
        record.m_truncated = true;
    }
    if (copied > 0) {
        std::memcpy(record.m_message, message.data(), copied);
    }
    record.m_message[copied] = '\0';
    record.m_length = static_cast<u16>(copied);
    return record;
}

void LogRecord::setMessageLength(const usize length, const bool truncated) noexcept
{
    constexpr usize maximum = MessageCapacity - 1;
    const usize clamped = length > maximum ? maximum : length;
    m_message[clamped] = '\0';
    m_length = static_cast<u16>(clamped);
    m_truncated = truncated || clamped != length;
}

} // namespace Tina::Core::Diagnostics
