#pragma once

// PRIVATE COM providers for the Windows UIA fragment tree (UI-002 HWND bridge).

#include "UIUiaMapping.hpp"
#include "WindowsUiaAccessibilityProvider.hpp"

#include <UIAutomation.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace Tina::UI::UiaCom {

inline constexpr std::size_t InvalidProviderNodeIndex = (std::numeric_limits<std::size_t>::max)();

struct ProviderSnapshotNode final {
    Uia::UIUiaMappedNode mapped{};
    RECT bounds{};
    std::size_t parent = InvalidProviderNodeIndex;
    std::size_t previousSibling = InvalidProviderNodeIndex;
    std::size_t nextSibling = InvalidProviderNodeIndex;
    std::vector<std::size_t> children{};
};

struct ProviderSnapshot final {
    std::vector<ProviderSnapshotNode> nodes{};
    std::vector<std::size_t> children{};
    std::uintptr_t hwndIdentity = 0;
};

class HostBridgeRoot;

class NodeProvider final : public IRawElementProviderSimple,
                           public IRawElementProviderFragment,
                           public IInvokeProvider,
                           public IToggleProvider,
                           public IRangeValueProvider,
                           public IValueProvider {
public:
    NodeProvider(HostBridgeRoot& root, std::shared_ptr<const ProviderSnapshot> snapshot,
                 std::size_t nodeIndex) noexcept;
    ~NodeProvider() noexcept;

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

    HRESULT STDMETHODCALLTYPE Invoke() override;
    HRESULT STDMETHODCALLTYPE Toggle() override;
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* pRetVal) override;
    HRESULT STDMETHODCALLTYPE SetValue(double value) override;
    HRESULT STDMETHODCALLTYPE get_Value(double* pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_Maximum(double* pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_Minimum(double* pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* pRetVal) override;
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* pRetVal) override;

    [[nodiscard]] const ProviderSnapshotNode& node() const noexcept;

private:
    [[nodiscard]] bool supportsInvoke() const noexcept;
    [[nodiscard]] bool supportsToggle() const noexcept;
    [[nodiscard]] bool supportsRangeValue() const noexcept;
    [[nodiscard]] bool supportsValue() const noexcept;
    [[nodiscard]] HRESULT ensureActionable(bool readOnly = false) const noexcept;

    std::atomic<ULONG> m_ref{1};
    HostBridgeRoot* m_root = nullptr;
    std::shared_ptr<const ProviderSnapshot> m_snapshot{};
    std::size_t m_nodeIndex = InvalidProviderNodeIndex;
};

class HostBridgeRoot final : public IRawElementProviderSimple,
                             public IRawElementProviderFragment,
                             public IRawElementProviderFragmentRoot {
public:
    explicit HostBridgeRoot(HWND hwnd) noexcept;
    ~HostBridgeRoot() noexcept = default;

    [[nodiscard]] bool rebuildSnapshot(const WindowsUiaAccessibilityProvider& provider, HWND hwnd) noexcept;
    void clearSnapshot() noexcept;
    void disconnect() noexcept;

    [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd.load(std::memory_order_acquire); }
    [[nodiscard]] HRESULT performAction(const UIAccessibilityAction& action) const noexcept;
    [[nodiscard]] HRESULT createNodeProvider(const std::shared_ptr<const ProviderSnapshot>& snapshot,
                                             std::size_t nodeIndex,
                                             IRawElementProviderFragment** pRetVal) noexcept;
    [[nodiscard]] HRESULT raiseLiveRegionChanged(UINodeId node) noexcept;

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
    [[nodiscard]] std::shared_ptr<const ProviderSnapshot> snapshot() const noexcept;

    std::atomic<ULONG> m_ref{1};
    std::atomic<HWND> m_hwnd{nullptr};
    std::atomic<std::shared_ptr<const ProviderSnapshot>> m_snapshot{};
};

} // namespace Tina::UI::UiaCom
