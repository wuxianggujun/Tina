#pragma once

// PRIVATE COM providers for Windows UIA fragment tree (UI-002 HWND bridge).

#include "UIUiaMapping.hpp"
#include "WindowsUiaAccessibilityProvider.hpp"

#include <UIAutomation.h>

#include <atomic>
#include <cstddef>
#include <vector>

namespace Tina::UI::UiaCom {

class HostBridgeRoot;

class NodeProvider final : public IRawElementProviderSimple, public IRawElementProviderFragment {
public:
    NodeProvider(HostBridgeRoot* root, Uia::UIUiaMappedNode mapped, RECT bounds) noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID patternId, IUnknown** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId, VARIANT* pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** pRetVal) override;

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** pRetVal) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** pRetVal) override;

    void setSiblingIndex(std::size_t index) noexcept { m_siblingIndex = index; }
    [[nodiscard]] const Uia::UIUiaMappedNode& mapped() const noexcept { return m_mapped; }

private:
    std::atomic<ULONG> m_ref{1};
    HostBridgeRoot* m_root = nullptr;
    Uia::UIUiaMappedNode m_mapped{};
    RECT m_bounds{};
    std::size_t m_siblingIndex = 0;
};

class HostBridgeRoot final : public IRawElementProviderSimple,
                             public IRawElementProviderFragment,
                             public IRawElementProviderFragmentRoot {
public:
    explicit HostBridgeRoot(HWND hwnd) noexcept;
    ~HostBridgeRoot() noexcept;

    void rebuildChildren(const WindowsUiaAccessibilityProvider& provider, HWND hwnd);
    void clearChildren() noexcept;

    [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd; }
    [[nodiscard]] std::size_t childCount() const noexcept { return m_children.size(); }
    [[nodiscard]] NodeProvider* childAt(std::size_t index) const noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID patternId, IUnknown** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId, VARIANT* pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** pRetVal) override;

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** pRetVal) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** pRetVal) override;

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** pRetVal) override;

private:
    std::atomic<ULONG> m_ref{1};
    HWND m_hwnd = nullptr;
    std::vector<NodeProvider*> m_children{};
};

} // namespace Tina::UI::UiaCom
