#pragma once

// PRIVATE: HWND + IRawElementProviderSimple bridge for Windows UIA (UI-002).

#include "WindowsUiaAccessibilityProvider.hpp"

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIAccessibility.hpp>

#include <Windows.h>

#include <memory>
#include <memory_resource>

struct IRawElementProviderSimple;

namespace Tina::UI {

class WindowsUiaHostBridge final {
public:
    WindowsUiaHostBridge() = default;
    WindowsUiaHostBridge(const WindowsUiaHostBridge&) = delete;
    WindowsUiaHostBridge& operator=(const WindowsUiaHostBridge&) = delete;
    ~WindowsUiaHostBridge() noexcept;

    [[nodiscard]] Core::Status attach(HWND hwnd);
    void detach() noexcept;

    [[nodiscard]] bool isAttached() const noexcept { return m_hwnd != nullptr; }
    [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd; }

    [[nodiscard]] Core::Status publish(const UIAccessibilityTree& tree);
    void clear() noexcept;

    [[nodiscard]] bool hasPublishedTree() const noexcept;
    [[nodiscard]] u64 publishCount() const noexcept;
    [[nodiscard]] u64 clearCount() const noexcept;
    [[nodiscard]] u64 wmGetObjectCount() const noexcept { return m_wmGetObjectCount; }

    // AddRef'd; caller must Release(). Tests only.
    [[nodiscard]] IRawElementProviderSimple* acquireRootProvider() const noexcept;

    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                         DWORD_PTR refData);

private:
    void noteWmGetObject() noexcept { ++m_wmGetObjectCount; }
    [[nodiscard]] IRawElementProviderSimple* rootProviderNoAddRef() const noexcept { return m_rootProvider; }

    HWND m_hwnd = nullptr;
    std::unique_ptr<WindowsUiaAccessibilityProvider> m_provider{};
    IRawElementProviderSimple* m_rootProvider = nullptr;
    u64 m_wmGetObjectCount = 0;
};

[[nodiscard]] Core::Result<std::unique_ptr<WindowsUiaHostBridge>> createWindowsUiaHostBridge(
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

} // namespace Tina::UI
