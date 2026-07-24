#include "WindowsUiaHostBridge.hpp"

#include "WindowsUiaComProviders.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/ui/UIErrors.hpp>

#include <UIAutomation.h>
#include <commctrl.h>

#include <memory_resource>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "UIAutomationCore.lib")
#pragma comment(lib, "oleaut32.lib")

namespace Tina::UI {
namespace {

constexpr UINT_PTR kSubclassId = 0x54494E41u; // 'TINA'

} // namespace

LRESULT CALLBACK WindowsUiaHostBridge::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                    UINT_PTR /*subclassId*/, DWORD_PTR refData)
{
    auto* bridge = reinterpret_cast<WindowsUiaHostBridge*>(refData);
    if (msg == WM_GETOBJECT && bridge != nullptr) {
        if (static_cast<LONG>(lParam) == static_cast<LONG>(UiaRootObjectId)) {
            bridge->noteWmGetObject();
            if (IRawElementProviderSimple* root = bridge->rootProviderNoAddRef(); root != nullptr) {
                // UiaReturnRawElementProvider consumes one reference on success.
                root->AddRef();
                const LRESULT lr = ::UiaReturnRawElementProvider(hwnd, wParam, lParam, root);
                if (lr == 0) {
                    // Documentation: non-zero on success path via LresultFromObject; if zero, drop ref.
                    root->Release();
                }
                return lr;
            }
        }
    }
    if (msg == WM_NCDESTROY && bridge != nullptr) {
        // Avoid re-entrancy issues: only clear providers; detach removes subclass from dtor/user.
        bridge->clear();
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

WindowsUiaHostBridge::~WindowsUiaHostBridge() noexcept
{
    detach();
}

Core::Status WindowsUiaHostBridge::attach(HWND hwnd)
{
    if (hwnd == nullptr || !::IsWindow(hwnd)) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "WindowsUiaHostBridge requires a valid HWND");
    }
    if (m_hwnd != nullptr) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "WindowsUiaHostBridge already attached");
    }

    m_provider = std::make_unique<WindowsUiaAccessibilityProvider>(*std::pmr::get_default_resource());
    auto* root = new UiaCom::HostBridgeRoot(hwnd);
    m_rootProvider = static_cast<IRawElementProviderSimple*>(root);

    if (!::SetWindowSubclass(hwnd, &WindowsUiaHostBridge::subclassProc, kSubclassId,
                             reinterpret_cast<DWORD_PTR>(this))) {
        m_rootProvider->Release();
        m_rootProvider = nullptr;
        m_provider.reset();
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "SetWindowSubclass failed for UIA host bridge");
    }

    m_hwnd = hwnd;
    m_wmGetObjectCount = 0;
    return Core::success();
}

void WindowsUiaHostBridge::detach() noexcept
{
    if (m_hwnd != nullptr) {
        (void)::RemoveWindowSubclass(m_hwnd, &WindowsUiaHostBridge::subclassProc, kSubclassId);
        m_hwnd = nullptr;
    }
    if (m_rootProvider != nullptr) {
        if (::UiaClientsAreListening()) {
            (void)::UiaDisconnectProvider(m_rootProvider);
        }
        static_cast<UiaCom::HostBridgeRoot*>(m_rootProvider)->clearChildren();
        m_rootProvider->Release();
        m_rootProvider = nullptr;
    }
    if (m_provider) {
        m_provider->clear();
        m_provider.reset();
    }
    m_wmGetObjectCount = 0;
}

Core::Status WindowsUiaHostBridge::publish(const UIAccessibilityTree& tree)
{
    if (m_hwnd == nullptr || m_provider == nullptr || m_rootProvider == nullptr) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "WindowsUiaHostBridge is not attached");
    }
    if (auto status = m_provider->publish(tree); !status) {
        return status;
    }
    auto* root = static_cast<UiaCom::HostBridgeRoot*>(m_rootProvider);
    root->rebuildChildren(*m_provider, m_hwnd);
    if (::UiaClientsAreListening()) {
        (void)::UiaRaiseStructureChangedEvent(m_rootProvider, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }
    return Core::success();
}

void WindowsUiaHostBridge::clear() noexcept
{
    if (m_provider) {
        m_provider->clear();
    }
    if (m_rootProvider != nullptr) {
        static_cast<UiaCom::HostBridgeRoot*>(m_rootProvider)->clearChildren();
    }
}

bool WindowsUiaHostBridge::hasPublishedTree() const noexcept
{
    return m_provider && m_provider->hasPublishedTree();
}

u64 WindowsUiaHostBridge::publishCount() const noexcept
{
    return m_provider ? m_provider->publishCount() : 0;
}

u64 WindowsUiaHostBridge::clearCount() const noexcept
{
    return m_provider ? m_provider->clearCount() : 0;
}

IRawElementProviderSimple* WindowsUiaHostBridge::acquireRootProvider() const noexcept
{
    if (m_rootProvider == nullptr) {
        return nullptr;
    }
    m_rootProvider->AddRef();
    return m_rootProvider;
}

Core::Result<std::unique_ptr<WindowsUiaHostBridge>> createWindowsUiaHostBridge(std::pmr::memory_resource&)
{
    return std::make_unique<WindowsUiaHostBridge>();
}

} // namespace Tina::UI
