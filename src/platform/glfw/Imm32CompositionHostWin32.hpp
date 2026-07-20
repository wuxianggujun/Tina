#pragma once

// Private Win32 IMM32 host for the GLFW platform backend. Translates IMM
// composition messages into Imm32CompositionEvent values stored in a
// poll-local pending slot. Public Platform headers never include IMM32.

#include "Imm32CompositionSession.hpp"

#include <windows.h>
#include <imm.h>

#include <array>
#include <optional>
#include <string>

namespace Tina::Platform::Detail {

class Imm32CompositionHostWin32 final {
  public:
    Imm32CompositionHostWin32() = default;
    ~Imm32CompositionHostWin32() noexcept { detach(); }

    Imm32CompositionHostWin32(const Imm32CompositionHostWin32&) = delete;
    Imm32CompositionHostWin32& operator=(const Imm32CompositionHostWin32&) = delete;

    [[nodiscard]] Core::Status attach(HWND hwnd) noexcept;
    void detach() noexcept;

    [[nodiscard]] bool attached() const noexcept { return hwnd_ != nullptr; }
    [[nodiscard]] Imm32CompositionSession& session() noexcept { return session_; }

    // Drain at most one pending composition event produced by the subclass
    // during glfwPollEvents / DefWindowProc. May also yield a commit string.
    struct Pending final {
        Imm32CompositionEvent composition{};
        // Owned copy so the event's string_view remains valid until takePending.
        std::string preeditStorage{};
        std::string commitStorage{};
    };
    [[nodiscard]] std::optional<Pending> takePending() noexcept;

    [[nodiscard]] std::optional<Imm32CompositionEvent> onFocusLost() noexcept;

    // Called from the subclass WndProc in this TU only.
    LRESULT subclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

  private:
    void publish(Imm32CompositionEvent event, std::string preedit, std::string commit) noexcept;
    [[nodiscard]] bool readImmString(
        HIMC context,
        DWORD index,
        std::string& outUtf8) noexcept;

    HWND hwnd_ = nullptr;
    WNDPROC previousProc_ = nullptr;
    Imm32CompositionSession session_{};
    bool hasPending_ = false;
    Pending pending_{};
    std::array<wchar_t, DefaultImePreeditByteCapacity> wideScratch_{};
};

LRESULT CALLBACK imm32CompositionSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);

} // namespace Tina::Platform::Detail
