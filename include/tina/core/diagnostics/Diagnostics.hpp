#pragma once

#include <tina/core/base/Compiler.hpp>
#include <tina/core/diagnostics/DiagnosticChannel.hpp>
#include <tina/core/diagnostics/LogLevel.hpp>
#include <tina/core/error/Result.hpp>

#include <memory>

namespace Tina::Core::Diagnostics {

// Optional test/custom sink. Must not call back into Diagnostics (recursion is
// suppressed; failures are counted only). userData is opaque. Exceptions are
// caught at the Diagnostics boundary and never re-enter logging.
using LogSinkFn = void (*)(void* userData, const LogRecord& record);

struct DiagnosticsConfig final {
    LogLevel minLevel = LogLevel::Info;
    LogSinkFn sink = nullptr;
    void* sinkUserData = nullptr;
};

// EngineHost-owned diagnostics root for the M-Diag-A0 slice: main-thread
// synchronous writes, private console sink by default, no background queue.
class Diagnostics final {
  public:
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&&) = delete;
    Diagnostics& operator=(Diagnostics&&) = delete;

    ~Diagnostics() noexcept;

    [[nodiscard]] static Result<std::unique_ptr<Diagnostics>> Create(const DiagnosticsConfig& config) noexcept;

    // Idempotent. After shutdown, all issued channels no-op.
    void shutdown() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] LogLevel minLevel() const noexcept;
    [[nodiscard]] u64 writtenCount() const noexcept;
    [[nodiscard]] u64 droppedByLevelCount() const noexcept;
    [[nodiscard]] u64 sinkFailureCount() const noexcept;
    [[nodiscard]] DiagnosticChannel channel() noexcept;

  private:
    friend class DiagnosticChannel;

    explicit Diagnostics(DiagnosticsConfig config) noexcept;

    void write(const LogRecord& record) noexcept;
    [[nodiscard]] bool isEnabled(LogLevel level) const noexcept;

    DiagnosticsConfig m_config{};
    bool m_open = true;
    bool m_inSink = false;
    u64 m_writtenCount = 0;
    u64 m_droppedByLevelCount = 0;
    u64 m_sinkFailureCount = 0;
};

} // namespace Tina::Core::Diagnostics
