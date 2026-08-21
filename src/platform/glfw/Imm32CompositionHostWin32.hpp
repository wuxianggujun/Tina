#pragma once

// Private Win32 IMM32 host for the GLFW platform backend. Translates IMM
// composition messages into ordered Imm32CompositionEvent values stored in a
// bounded poll-local queue. Public Platform headers never include IMM32.

#include "Imm32CompositionSession.hpp"
#include "GlfwTextInputPlacement.hpp"

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
    // Stores the latest committed caret placement and applies it to the active
    // IMM context when one exists. IMM32 may not expose a context until
    // WM_IME_STARTCOMPOSITION; the stored value is reapplied at that boundary.
    void setTextInputPlacement(
        std::optional<GlfwTextInputPlacementPixels> placement) noexcept;

    [[nodiscard]] bool attached() const noexcept { return hwnd_ != nullptr; }
    [[nodiscard]] Imm32CompositionSession& session() noexcept { return session_; }

    // Drain one pending composition event produced by the subclass during
    // glfwPollEvents / DefWindowProc. May also yield a commit string.
    struct Pending final {
        Pending() = default;
        Pending(const Pending&) = delete;
        Pending& operator=(const Pending&) = delete;
        Pending(Pending&& other) noexcept;
        Pending& operator=(Pending&& other) noexcept;

        void assign(Imm32CompositionEvent event, std::string preedit,
                    std::string commit) noexcept;

        Imm32CompositionEvent composition{};
        // Owned copies keep the borrowed event views valid across FIFO moves.
        std::string preeditStorage{};
        std::string commitStorage{};

      private:
        void rebindViews() noexcept;
    };
    [[nodiscard]] std::optional<Pending> takePending() noexcept;

    [[nodiscard]] std::optional<Imm32CompositionEvent> onFocusLost() noexcept;

    // Called from the subclass WndProc in this TU only.
    LRESULT subclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

  private:
    static constexpr usize MaximumPendingEvents = 32;

    void publish(Imm32CompositionEvent event, std::string preedit, std::string commit) noexcept;
    void clearPending() noexcept;
    [[nodiscard]] bool readImmString(
        HIMC context,
        DWORD index,
        std::string& outUtf8) noexcept;

    HWND hwnd_ = nullptr;
    WNDPROC previousProc_ = nullptr;
    Imm32CompositionSession session_{};
    std::array<Pending, MaximumPendingEvents> pendingEvents_{};
    usize pendingEventCount_ = 0;
    std::array<wchar_t, DefaultImePreeditByteCapacity> wideScratch_{};
    std::optional<GlfwTextInputPlacementPixels> textInputPlacement_{};

    void applyTextInputPlacement(HIMC context) noexcept;
};

LRESULT CALLBACK imm32CompositionSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);

} // namespace Tina::Platform::Detail
