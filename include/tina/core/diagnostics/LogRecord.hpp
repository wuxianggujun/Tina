#pragma once

#include <tina/core/base/SourceLocation.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/diagnostics/LogLevel.hpp>

#include <cstddef>
#include <string_view>

namespace Tina::Core::Diagnostics {

// Owning log payload. The message is copied into a fixed inline buffer rather
// than borrowed: a queued record outlives the frame that produced it, so a
// string_view here would dangle by construction once writes stop being
// synchronous.
//
// Category must be a stable low-cardinality string literal and is still
// borrowed -- literals have static storage, and copying them would waste most
// of the buffer. Callers must not pass a dynamic category.
class LogRecord final {
  public:
    // Sized so the record stays cache-friendly at 1024 queue slots (~256KB
    // total). Longer messages are truncated and flagged rather than allocated.
    static constexpr usize MessageCapacity = 256;

    LogRecord() noexcept = default;

    // Copies at most MessageCapacity - 1 bytes of message. UTF-8 is not
    // validated; a truncation may split a multi-byte sequence, which is why
    // isTruncated() exists rather than being inferred from the text.
    [[nodiscard]] static LogRecord make(
        LogLevel level,
        std::string_view category,
        std::string_view message,
        SourceLocation location = SourceLocation::current()) noexcept;

    [[nodiscard]] LogLevel level() const noexcept
    {
        return m_level;
    }

    [[nodiscard]] std::string_view category() const noexcept
    {
        return m_category;
    }

    [[nodiscard]] std::string_view message() const noexcept
    {
        return std::string_view{m_message, m_length};
    }

    [[nodiscard]] SourceLocation location() const noexcept
    {
        return m_location;
    }

    // True when the message did not fit. The stored text is a prefix.
    [[nodiscard]] bool isTruncated() const noexcept
    {
        return m_truncated;
    }

    // Writable span for in-place formatting, so a formatted message is built
    // once in the record instead of in a scratch buffer and then copied.
    [[nodiscard]] char* messageBuffer() noexcept
    {
        return m_message;
    }

    void setMessageLength(usize length, bool truncated) noexcept;
    void setLevel(LogLevel level) noexcept
    {
        m_level = level;
    }
    void setCategory(std::string_view category) noexcept
    {
        m_category = category;
    }
    void setLocation(SourceLocation location) noexcept
    {
        m_location = location;
    }

  private:
    LogLevel m_level = LogLevel::Info;
    bool m_truncated = false;
    u16 m_length = 0;
    std::string_view m_category{};
    SourceLocation m_location = SourceLocation::current();
    char m_message[MessageCapacity]{};
};

} // namespace Tina::Core::Diagnostics
