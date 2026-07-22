#pragma once

#include <tina/core/diagnostics/LogRecord.hpp>

namespace Tina::Core::Diagnostics {

class Diagnostics;

// Non-owning narrow write surface. Valid while the owning Diagnostics is open.
// After Diagnostics::shutdown(), writes become no-ops and isEnabled returns false.
class DiagnosticChannel final {
  public:
    DiagnosticChannel() noexcept = default;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool isEnabled(LogLevel level) const noexcept;

    // Level-filters before any sink call. Does not allocate or format.
    void write(const LogRecord& record) const noexcept;

  private:
    friend class Diagnostics;

    explicit DiagnosticChannel(Diagnostics* owner) noexcept;

    Diagnostics* m_owner = nullptr;
};

} // namespace Tina::Core::Diagnostics
