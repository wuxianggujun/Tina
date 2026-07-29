#pragma once

// PRIVATE synchronous message contract between free-threaded UIA providers and
// the HWND/UIContext owner thread.

#include <tina/ui/UIAccessibility.hpp>

#include <Windows.h>

namespace Tina::UI::UiaCom {

struct WindowsUiaActionRequest final {
    UIAccessibilityAction action{};
    HRESULT result = E_FAIL;
    bool handled = false;
};

[[nodiscard]] inline UINT windowsUiaActionMessage() noexcept
{
    static const UINT message = ::RegisterWindowMessageW(L"Tina.UI.AccessibilityAction.v1");
    return message;
}

} // namespace Tina::UI::UiaCom
