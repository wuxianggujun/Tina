#pragma once

// Public factory for the optional Windows UIA accessibility adapter.
// The header stays free of COM / UIAutomation types; implementation lives in
// tina_ui_uia and is only linked when TINA_BUILD_UI_UIA=ON (Windows-only).

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIAccessibility.hpp>

#include <memory>
#include <memory_resource>

namespace Tina::UI {

// Creates an IUIAccessibilityProvider that maps UIAccessibilityTree onto
// UIA-shaped properties (ControlType, Name, IsEnabled, focus, RangeValue,
// ToggleState, Value). When TINA_BUILD_UI_UIA is on, EngineHost also attaches
// private WindowsUiaHostBridge to the primary Win32 HWND (WM_GETOBJECT). This
// factory stays COM-free for Game SDK headers.
[[nodiscard]] Core::Result<std::unique_ptr<IUIAccessibilityProvider>> createWindowsUiaAccessibilityProvider(
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

// True when this binary was linked with the Windows UIA private adapter.
[[nodiscard]] bool windowsUiaAccessibilityProviderAvailable() noexcept;

} // namespace Tina::UI
