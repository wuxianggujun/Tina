#pragma once

// Backend-neutral composition session used by the private Win32 IMM32 host.
// Public Platform headers never include IMM32 types. This type only owns UTF-8
// preedit bytes and emits TextCompositionStage transitions.

#include <tina/core/error/Result.hpp>
#include <tina/platform/Input.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace Tina::Platform::Detail {

inline constexpr usize DefaultImePreeditByteCapacity = 512;

struct Imm32CompositionEvent final {
    TextCompositionStage stage = TextCompositionStage::Updated;
    std::string_view preeditUtf8{};
    u32 cursorCodepoint = 0;
    // When stage is Ended, optional committed text for a following TextInput.
    std::string_view committedUtf8{};
};

// Fixed-capacity preedit session. Create-time capacity only; no heap growth.
class Imm32CompositionSession final {
  public:
    explicit Imm32CompositionSession(
        usize preeditByteCapacity = DefaultImePreeditByteCapacity) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::string_view preeditUtf8() const noexcept;
    [[nodiscard]] u32 cursorCodepoint() const noexcept { return cursorCodepoint_; }

    // Returns Started (first) or Updated. Rejects oversized preedit.
    [[nodiscard]] Core::Result<Imm32CompositionEvent> updatePreedit(
        std::string_view utf8,
        u32 cursorCodepoint) noexcept;

    // Ends an active composition and optionally carries a commit payload.
    // A non-empty direct commit is emitted even when the IME skipped preedit.
    [[nodiscard]] std::optional<Imm32CompositionEvent> end(
        std::string_view committedUtf8 = {}) noexcept;

    // Cancels an active composition. Without an active session, nullopt.
    [[nodiscard]] std::optional<Imm32CompositionEvent> cancel() noexcept;

    // Focus lost / window destroy: cancel if active.
    [[nodiscard]] std::optional<Imm32CompositionEvent> onFocusLost() noexcept;

  private:
    [[nodiscard]] Core::Status storePreedit(std::string_view utf8) noexcept;

    std::array<char, DefaultImePreeditByteCapacity> preeditBytes_{};
    usize preeditByteCapacity_ = DefaultImePreeditByteCapacity;
    usize preeditSize_ = 0;
    u32 cursorCodepoint_ = 0;
    bool active_ = false;
};

// UTF-16 (Windows WCHAR composition strings) -> UTF-8 into a fixed out buffer.
// Returns byte count written excluding NUL, or nullopt on invalid/overflow.
[[nodiscard]] std::optional<usize> encodeUtf16ToUtf8(
    std::span<const char16_t> utf16,
    std::span<char> outUtf8) noexcept;

} // namespace Tina::Platform::Detail
