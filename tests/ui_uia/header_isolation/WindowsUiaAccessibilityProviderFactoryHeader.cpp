#include <tina/ui/WindowsUiaAccessibilityProviderFactory.hpp>

namespace {
[[maybe_unused]] constexpr auto* kFactory = &Tina::UI::createWindowsUiaAccessibilityProvider;
[[maybe_unused]] constexpr auto* kAvailable = &Tina::UI::windowsUiaAccessibilityProviderAvailable;
} // namespace
