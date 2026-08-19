#include "WindowsUiaComProviders.hpp"

#include "WindowsUiaActionDispatch.hpp"

#include <UIAutomationClient.h>

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <map>
#include <new>
#include <string>
#include <utility>

namespace Tina::UI::UiaCom {
namespace {

constexpr UINT kActionTimeoutMs = 5'000;
constexpr double kDefaultWindowsDpi = 96.0;

[[nodiscard]] RECT clientRelativeToScreen(HWND hwnd, const UI::UILogicalRect& logical) noexcept
{
    POINT origin{0, 0};
    double contentScale = 1.0;
    if (hwnd != nullptr) {
        (void)::ClientToScreen(hwnd, &origin);
        const UINT dpi = ::GetDpiForWindow(hwnd);
        if (dpi != 0) {
            contentScale = static_cast<double>(dpi) / kDefaultWindowsDpi;
        }
    }
    const auto project = [contentScale](float logicalCoordinate) noexcept {
        return static_cast<LONG>(std::lround(static_cast<double>(logicalCoordinate) *
                                             contentScale));
    };
    RECT out{};
    out.left = origin.x + project(logical.x);
    out.top = origin.y + project(logical.y);
    out.right = origin.x + project(logical.x + logical.width);
    out.bottom = origin.y + project(logical.y + logical.height);
    return out;
}

[[nodiscard]] HRESULT bstrFromUtf8(std::string_view utf8, BSTR* output) noexcept
{
    if (output == nullptr) {
        return E_POINTER;
    }
    *output = nullptr;
    if (utf8.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return E_INVALIDARG;
    }
    if (utf8.empty()) {
        *output = ::SysAllocStringLen(nullptr, 0);
        return *output != nullptr ? S_OK : E_OUTOFMEMORY;
    }
    const int sourceLength = static_cast<int>(utf8.size());
    const int wideCount =
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), sourceLength, nullptr, 0);
    if (wideCount <= 0) {
        return E_INVALIDARG;
    }
    try {
        std::wstring wide(static_cast<std::size_t>(wideCount), L'\0');
        if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), sourceLength,
                                  wide.data(), wideCount) != wideCount) {
            return E_INVALIDARG;
        }
        *output = ::SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
        return *output != nullptr ? S_OK : E_OUTOFMEMORY;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}

[[nodiscard]] HRESULT utf8FromWide(LPCWSTR wide, std::string& output) noexcept
{
    if (wide == nullptr) {
        return E_INVALIDARG;
    }
    const std::size_t wideLength = std::wcslen(wide);
    if (wideLength > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return E_INVALIDARG;
    }
    if (wideLength == 0) {
        output.clear();
        return S_OK;
    }
    const int sourceLength = static_cast<int>(wideLength);
    const int utf8Count = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, sourceLength,
                                                nullptr, 0, nullptr, nullptr);
    if (utf8Count <= 0) {
        return E_INVALIDARG;
    }
    try {
        output.resize(static_cast<std::size_t>(utf8Count));
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, sourceLength,
                                  output.data(), utf8Count, nullptr, nullptr) != utf8Count) {
            output.clear();
            return E_INVALIDARG;
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        output.clear();
        return E_OUTOFMEMORY;
    }
}

void setBoolVariant(VARIANT* value, bool enabled) noexcept
{
    ::VariantInit(value);
    value->vt = VT_BOOL;
    value->boolVal = enabled ? VARIANT_TRUE : VARIANT_FALSE;
}

void setI4Variant(VARIANT* value, LONG number) noexcept
{
    ::VariantInit(value);
    value->vt = VT_I4;
    value->lVal = number;
}

void setR8Variant(VARIANT* value, double number) noexcept
{
    ::VariantInit(value);
    value->vt = VT_R8;
    value->dblVal = number;
}

[[nodiscard]] HRESULT setBstrVariant(VARIANT* value, std::string_view utf8) noexcept
{
    ::VariantInit(value);
    BSTR result = nullptr;
    const HRESULT status = bstrFromUtf8(utf8, &result);
    if (FAILED(status)) {
        return status;
    }
    value->vt = VT_BSTR;
    value->bstrVal = result;
    return S_OK;
}

[[nodiscard]] bool pointInside(const RECT& rect, double x, double y) noexcept
{
    return x >= static_cast<double>(rect.left) && x < static_cast<double>(rect.right) &&
           y >= static_cast<double>(rect.top) && y < static_cast<double>(rect.bottom);
}

} // namespace

NodeProvider::NodeProvider(HostBridgeRoot& root, std::shared_ptr<const ProviderSnapshot> snapshot,
                           std::size_t nodeIndex) noexcept
    : m_root(&root)
    , m_snapshot(std::move(snapshot))
    , m_nodeIndex(nodeIndex)
{
    m_root->AddRef();
}

NodeProvider::~NodeProvider() noexcept
{
    if (m_root != nullptr) {
        m_root->Release();
    }
}

const ProviderSnapshotNode& NodeProvider::node() const noexcept
{
    return m_snapshot->nodes[m_nodeIndex];
}

bool NodeProvider::supportsInvoke() const noexcept
{
    return node().mapped.invokeSupported;
}

bool NodeProvider::supportsToggle() const noexcept
{
    return node().mapped.toggleState.has_value();
}

bool NodeProvider::supportsRangeValue() const noexcept
{
    return node().mapped.rangeValue.has_value();
}

bool NodeProvider::supportsValue() const noexcept
{
    const u32 controlType = node().mapped.controlTypeId;
    return node().mapped.value.has_value() &&
           (controlType == Uia::kControlTypeEdit || controlType == Uia::kControlTypeComboBox);
}

HRESULT NodeProvider::ensureActionable(bool readOnly) const noexcept
{
    if (m_root == nullptr || m_root->hwnd() == nullptr) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!node().mapped.isEnabled) {
        return UIA_E_ELEMENTNOTENABLED;
    }
    if (readOnly) {
        return UIA_E_NOTSUPPORTED;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr) {
        return E_POINTER;
    }
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
        *ppvObject = static_cast<IRawElementProviderSimple*>(this);
    } else if (riid == IID_IRawElementProviderFragment) {
        *ppvObject = static_cast<IRawElementProviderFragment*>(this);
    } else if (riid == IID_IInvokeProvider && supportsInvoke()) {
        *ppvObject = static_cast<IInvokeProvider*>(this);
    } else if (riid == IID_IToggleProvider && supportsToggle()) {
        *ppvObject = static_cast<IToggleProvider*>(this);
    } else if (riid == IID_IRangeValueProvider && supportsRangeValue()) {
        *ppvObject = static_cast<IRangeValueProvider*>(this);
    } else if (riid == IID_IValueProvider && supportsValue()) {
        *ppvObject = static_cast<IValueProvider*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE NodeProvider::AddRef()
{
    return m_ref.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE NodeProvider::Release()
{
    const ULONG value = m_ref.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (value == 0) {
        delete this;
    }
    return value;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_ProviderOptions(ProviderOptions* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::GetPatternProvider(PATTERNID patternId, IUnknown** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    if (patternId == UIA_InvokePatternId && supportsInvoke()) {
        return QueryInterface(IID_IInvokeProvider, reinterpret_cast<void**>(pRetVal));
    }
    if (patternId == UIA_TogglePatternId && supportsToggle()) {
        return QueryInterface(IID_IToggleProvider, reinterpret_cast<void**>(pRetVal));
    }
    if (patternId == UIA_RangeValuePatternId && supportsRangeValue()) {
        return QueryInterface(IID_IRangeValueProvider, reinterpret_cast<void**>(pRetVal));
    }
    if (patternId == UIA_ValuePatternId && supportsValue()) {
        return QueryInterface(IID_IValueProvider, reinterpret_cast<void**>(pRetVal));
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    ::VariantInit(pRetVal);
    const auto& mapped = node().mapped;
    switch (propertyId) {
    case UIA_NamePropertyId:
        return setBstrVariant(pRetVal, mapped.name);
    case UIA_HelpTextPropertyId:
        return setBstrVariant(pRetVal, mapped.description);
    case UIA_ControlTypePropertyId:
        setI4Variant(pRetVal, static_cast<LONG>(mapped.controlTypeId));
        return S_OK;
    case UIA_IsEnabledPropertyId:
        setBoolVariant(pRetVal, mapped.isEnabled);
        return S_OK;
    case UIA_IsKeyboardFocusablePropertyId:
        setBoolVariant(pRetVal, mapped.isKeyboardFocusable);
        return S_OK;
    case UIA_HasKeyboardFocusPropertyId:
        setBoolVariant(pRetVal, mapped.hasKeyboardFocus);
        return S_OK;
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        setBoolVariant(pRetVal, true);
        return S_OK;
    case UIA_ProviderDescriptionPropertyId:
        return setBstrVariant(pRetVal, "Tina.UI.WindowsUia.NodeProvider");
    case UIA_FrameworkIdPropertyId:
        return setBstrVariant(pRetVal, "Tina");
    case UIA_AutomationIdPropertyId: {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "tina-ui-node-%u-%u", mapped.node.index(), mapped.node.generation());
        return setBstrVariant(pRetVal, buffer);
    }
    case UIA_ValueValuePropertyId:
        if (mapped.value.has_value()) {
            return setBstrVariant(pRetVal, mapped.value->value);
        }
        break;
    case UIA_ValueIsReadOnlyPropertyId:
        if (mapped.value.has_value()) {
            setBoolVariant(pRetVal, mapped.value->isReadOnly);
            return S_OK;
        }
        break;
    case UIA_RangeValueValuePropertyId:
        if (mapped.rangeValue.has_value()) {
            setR8Variant(pRetVal, mapped.rangeValue->value);
            return S_OK;
        }
        break;
    case UIA_RangeValueMinimumPropertyId:
        if (mapped.rangeValue.has_value()) {
            setR8Variant(pRetVal, mapped.rangeValue->minimum);
            return S_OK;
        }
        break;
    case UIA_RangeValueMaximumPropertyId:
        if (mapped.rangeValue.has_value()) {
            setR8Variant(pRetVal, mapped.rangeValue->maximum);
            return S_OK;
        }
        break;
    case UIA_RangeValueIsReadOnlyPropertyId:
        if (mapped.rangeValue.has_value()) {
            setBoolVariant(pRetVal, mapped.rangeValue->isReadOnly);
            return S_OK;
        }
        break;
    case UIA_RangeValueLargeChangePropertyId:
    case UIA_RangeValueSmallChangePropertyId:
        if (mapped.rangeValue.has_value()) {
            setR8Variant(pRetVal, (std::numeric_limits<double>::quiet_NaN)());
            return S_OK;
        }
        break;
    case UIA_ToggleToggleStatePropertyId:
        if (mapped.toggleState.has_value()) {
            setI4Variant(pRetVal, static_cast<LONG>(*mapped.toggleState));
            return S_OK;
        }
        break;
    default:
        break;
    }
    pRetVal->vt = VT_EMPTY;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_HostRawElementProvider(IRawElementProviderSimple** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    if (m_root == nullptr) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const ProviderSnapshotNode& current = node();
    switch (direction) {
    case NavigateDirection_Parent:
        if (current.parent == InvalidProviderNodeIndex) {
            return m_root->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(pRetVal));
        }
        return m_root->createNodeProvider(m_snapshot, current.parent, pRetVal);
    case NavigateDirection_NextSibling:
        return current.nextSibling == InvalidProviderNodeIndex
                   ? S_OK
                   : m_root->createNodeProvider(m_snapshot, current.nextSibling, pRetVal);
    case NavigateDirection_PreviousSibling:
        return current.previousSibling == InvalidProviderNodeIndex
                   ? S_OK
                   : m_root->createNodeProvider(m_snapshot, current.previousSibling, pRetVal);
    case NavigateDirection_FirstChild:
        return current.children.empty() ? S_OK : m_root->createNodeProvider(m_snapshot, current.children.front(), pRetVal);
    case NavigateDirection_LastChild:
        return current.children.empty() ? S_OK : m_root->createNodeProvider(m_snapshot, current.children.back(), pRetVal);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::GetRuntimeId(SAFEARRAY** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    const auto& mapped = node().mapped;
    LONG ids[4] = {
        static_cast<LONG>(UiaAppendRuntimeId),
        static_cast<LONG>(m_snapshot->hwndIdentity & 0x7fffffffU),
        static_cast<LONG>(mapped.node.index()),
        static_cast<LONG>(mapped.node.generation()),
    };
    SAFEARRAY* array = ::SafeArrayCreateVector(VT_I4, 0, 4);
    if (array == nullptr) {
        return E_OUTOFMEMORY;
    }
    for (LONG index = 0; index < 4; ++index) {
        if (FAILED(::SafeArrayPutElement(array, &index, &ids[index]))) {
            (void)::SafeArrayDestroy(array);
            return E_FAIL;
        }
    }
    *pRetVal = array;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_BoundingRectangle(UiaRect* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    const RECT& bounds = node().bounds;
    pRetVal->left = static_cast<double>(bounds.left);
    pRetVal->top = static_cast<double>(bounds.top);
    pRetVal->width = static_cast<double>(bounds.right - bounds.left);
    pRetVal->height = static_cast<double>(bounds.bottom - bounds.top);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::GetEmbeddedFragmentRoots(SAFEARRAY** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::SetFocus()
{
    if (!node().mapped.isKeyboardFocusable) {
        return UIA_E_NOTSUPPORTED;
    }
    if (const HRESULT status = ensureActionable(); FAILED(status)) {
        return status;
    }
    return m_root->performAction({.kind = UIAccessibilityActionKind::Focus, .node = node().mapped.node});
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (m_root == nullptr) {
        *pRetVal = nullptr;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return m_root->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(pRetVal));
}

HRESULT STDMETHODCALLTYPE NodeProvider::Invoke()
{
    if (!supportsInvoke()) {
        return UIA_E_NOTSUPPORTED;
    }
    if (const HRESULT status = ensureActionable(); FAILED(status)) {
        return status;
    }
    return m_root->performAction({.kind = UIAccessibilityActionKind::Invoke, .node = node().mapped.node});
}

HRESULT STDMETHODCALLTYPE NodeProvider::Toggle()
{
    if (!supportsToggle()) {
        return UIA_E_NOTSUPPORTED;
    }
    if (const HRESULT status = ensureActionable(); FAILED(status)) {
        return status;
    }
    return m_root->performAction({.kind = UIAccessibilityActionKind::Toggle, .node = node().mapped.node});
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_ToggleState(ToggleState* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (!supportsToggle()) {
        return UIA_E_NOTSUPPORTED;
    }
    *pRetVal = static_cast<ToggleState>(*node().mapped.toggleState);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::SetValue(double value)
{
    if (!supportsRangeValue()) {
        return UIA_E_NOTSUPPORTED;
    }
    const bool readOnly = node().mapped.rangeValue->isReadOnly;
    if (const HRESULT status = ensureActionable(readOnly); FAILED(status)) {
        return status;
    }
    const auto& range = *node().mapped.rangeValue;
    if (!std::isfinite(value) || value < range.minimum || value > range.maximum) {
        return E_INVALIDARG;
    }
    return m_root->performAction({
        .kind = UIAccessibilityActionKind::SetRangeValue,
        .node = node().mapped.node,
        .rangeValue = value,
    });
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_Value(double* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (!supportsRangeValue()) {
        return UIA_E_NOTSUPPORTED;
    }
    *pRetVal = node().mapped.rangeValue->value;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_IsReadOnly(BOOL* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (supportsRangeValue()) {
        *pRetVal = node().mapped.rangeValue->isReadOnly ? TRUE : FALSE;
        return S_OK;
    }
    if (supportsValue()) {
        *pRetVal = node().mapped.value->isReadOnly ? TRUE : FALSE;
        return S_OK;
    }
    return UIA_E_NOTSUPPORTED;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_Maximum(double* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (!supportsRangeValue()) {
        return UIA_E_NOTSUPPORTED;
    }
    *pRetVal = node().mapped.rangeValue->maximum;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_Minimum(double* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (!supportsRangeValue()) {
        return UIA_E_NOTSUPPORTED;
    }
    *pRetVal = node().mapped.rangeValue->minimum;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_LargeChange(double* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (!supportsRangeValue()) {
        return UIA_E_NOTSUPPORTED;
    }
    *pRetVal = (std::numeric_limits<double>::quiet_NaN)();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_SmallChange(double* pRetVal)
{
    return get_LargeChange(pRetVal);
}

HRESULT STDMETHODCALLTYPE NodeProvider::SetValue(LPCWSTR value)
{
    if (!supportsValue()) {
        return UIA_E_NOTSUPPORTED;
    }
    if (const HRESULT status = ensureActionable(node().mapped.value->isReadOnly); FAILED(status)) {
        return status;
    }
    std::string utf8;
    if (const HRESULT status = utf8FromWide(value, utf8); FAILED(status)) {
        return status;
    }
    return m_root->performAction({
        .kind = UIAccessibilityActionKind::SetTextValue,
        .node = node().mapped.node,
        .textValue = utf8,
    });
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_Value(BSTR* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (!supportsValue()) {
        *pRetVal = nullptr;
        return UIA_E_NOTSUPPORTED;
    }
    return bstrFromUtf8(node().mapped.value->value, pRetVal);
}

HostBridgeRoot::HostBridgeRoot(HWND hwnd) noexcept
    : m_hwnd(hwnd)
{
}

bool HostBridgeRoot::rebuildSnapshot(const WindowsUiaAccessibilityProvider& provider, HWND hwnd) noexcept
{
    try {
        auto next = std::make_shared<ProviderSnapshot>();
        const auto mapped = provider.mappedNodes();
        const auto sourceNodes = provider.tree().nodes();
        next->hwndIdentity = reinterpret_cast<std::uintptr_t>(hwnd);
        next->nodes.reserve(mapped.size());

        std::map<UINodeId, std::size_t> indices;
        for (std::size_t index = 0; index < mapped.size(); ++index) {
            RECT bounds{};
            if (index < sourceNodes.size()) {
                bounds = clientRelativeToScreen(hwnd, sourceNodes[index].worldRect);
            }
            next->nodes.push_back(ProviderSnapshotNode{
                .mapped = mapped[index],
                .bounds = bounds,
            });
            indices.emplace(mapped[index].node, index);
        }

        next->children.reserve(next->nodes.size());
        for (std::size_t index = 0; index < next->nodes.size(); ++index) {
            ProviderSnapshotNode& current = next->nodes[index];
            const auto parent = indices.find(current.mapped.parent);
            std::vector<std::size_t>* siblings = &next->children;
            if (parent != indices.end() && parent->second != index) {
                current.parent = parent->second;
                siblings = &next->nodes[parent->second].children;
            }
            if (!siblings->empty()) {
                current.previousSibling = siblings->back();
                next->nodes[siblings->back()].nextSibling = index;
            }
            siblings->push_back(index);
        }

        m_hwnd.store(hwnd, std::memory_order_release);
        m_snapshot.store(std::shared_ptr<const ProviderSnapshot>(std::move(next)), std::memory_order_release);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

void HostBridgeRoot::clearSnapshot() noexcept
{
    m_snapshot.store({}, std::memory_order_release);
}

void HostBridgeRoot::disconnect() noexcept
{
    clearSnapshot();
    m_hwnd.store(nullptr, std::memory_order_release);
}

std::shared_ptr<const ProviderSnapshot> HostBridgeRoot::snapshot() const noexcept
{
    return m_snapshot.load(std::memory_order_acquire);
}

HRESULT HostBridgeRoot::performAction(const UIAccessibilityAction& action) const noexcept
{
    const HWND target = hwnd();
    const UINT message = windowsUiaActionMessage();
    if (target == nullptr || message == 0 || !::IsWindow(target)) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    WindowsUiaActionRequest request{.action = action};
    DWORD_PTR messageResult = 0;
    const LRESULT sent = ::SendMessageTimeoutW(target, message, 0, reinterpret_cast<LPARAM>(&request),
                                               SMTO_ABORTIFHUNG | SMTO_BLOCK, kActionTimeoutMs, &messageResult);
    if (sent == 0 || !request.handled) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return request.result;
}

HRESULT HostBridgeRoot::createNodeProvider(const std::shared_ptr<const ProviderSnapshot>& current,
                                           std::size_t nodeIndex,
                                           IRawElementProviderFragment** pRetVal) noexcept
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    if (!current || nodeIndex >= current->nodes.size()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    auto* provider = new (std::nothrow) NodeProvider(*this, current, nodeIndex);
    if (provider == nullptr) {
        return E_OUTOFMEMORY;
    }
    *pRetVal = static_cast<IRawElementProviderFragment*>(provider);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr) {
        return E_POINTER;
    }
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
        *ppvObject = static_cast<IRawElementProviderSimple*>(this);
    } else if (riid == IID_IRawElementProviderFragment) {
        *ppvObject = static_cast<IRawElementProviderFragment*>(this);
    } else if (riid == IID_IRawElementProviderFragmentRoot) {
        *ppvObject = static_cast<IRawElementProviderFragmentRoot*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE HostBridgeRoot::AddRef()
{
    return m_ref.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE HostBridgeRoot::Release()
{
    const ULONG value = m_ref.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (value == 0) {
        delete this;
    }
    return value;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::get_ProviderOptions(ProviderOptions* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::GetPatternProvider(PATTERNID, IUnknown** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::GetPropertyValue(PROPERTYID propertyId, VARIANT* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    ::VariantInit(pRetVal);
    switch (propertyId) {
    case UIA_NamePropertyId:
        return setBstrVariant(pRetVal, "Tina UI Root");
    case UIA_ControlTypePropertyId:
        setI4Variant(pRetVal, static_cast<LONG>(Uia::kControlTypePane));
        return S_OK;
    case UIA_IsEnabledPropertyId:
        setBoolVariant(pRetVal, true);
        return S_OK;
    case UIA_IsKeyboardFocusablePropertyId:
        setBoolVariant(pRetVal, false);
        return S_OK;
    case UIA_ProviderDescriptionPropertyId:
        return setBstrVariant(pRetVal, "Tina.UI.WindowsUia.HostBridgeRoot");
    case UIA_FrameworkIdPropertyId:
        return setBstrVariant(pRetVal, "Tina");
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        setBoolVariant(pRetVal, true);
        return S_OK;
    default:
        pRetVal->vt = VT_EMPTY;
        return S_OK;
    }
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::get_HostRawElementProvider(IRawElementProviderSimple** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    const HWND target = hwnd();
    if (target == nullptr) {
        *pRetVal = nullptr;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return ::UiaHostProviderFromHwnd(target, pRetVal);
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::Navigate(NavigateDirection direction, IRawElementProviderFragment** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    const auto current = snapshot();
    if (!current || current->children.empty()) {
        return S_OK;
    }
    if (direction == NavigateDirection_FirstChild) {
        return createNodeProvider(current, current->children.front(), pRetVal);
    }
    if (direction == NavigateDirection_LastChild) {
        return createNodeProvider(current, current->children.back(), pRetVal);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::GetRuntimeId(SAFEARRAY** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    LONG ids[2] = {
        static_cast<LONG>(UiaAppendRuntimeId),
        static_cast<LONG>(reinterpret_cast<std::uintptr_t>(hwnd()) & 0x7fffffffU),
    };
    SAFEARRAY* array = ::SafeArrayCreateVector(VT_I4, 0, 2);
    if (array == nullptr) {
        return E_OUTOFMEMORY;
    }
    for (LONG index = 0; index < 2; ++index) {
        if (FAILED(::SafeArrayPutElement(array, &index, &ids[index]))) {
            (void)::SafeArrayDestroy(array);
            return E_FAIL;
        }
    }
    *pRetVal = array;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::get_BoundingRectangle(UiaRect* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    RECT rect{};
    const HWND target = hwnd();
    if (target != nullptr) {
        (void)::GetClientRect(target, &rect);
        POINT origin{0, 0};
        (void)::ClientToScreen(target, &origin);
        rect.left += origin.x;
        rect.right += origin.x;
        rect.top += origin.y;
        rect.bottom += origin.y;
    }
    pRetVal->left = static_cast<double>(rect.left);
    pRetVal->top = static_cast<double>(rect.top);
    pRetVal->width = static_cast<double>(rect.right - rect.left);
    pRetVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::GetEmbeddedFragmentRoots(SAFEARRAY** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::SetFocus()
{
    return UIA_E_NOTSUPPORTED;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::get_FragmentRoot(IRawElementProviderFragmentRoot** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    return QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(pRetVal));
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::ElementProviderFromPoint(double x, double y,
                                                                   IRawElementProviderFragment** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    const auto current = snapshot();
    if (current && std::isfinite(x) && std::isfinite(y)) {
        for (std::size_t index = current->nodes.size(); index > 0; --index) {
            if (pointInside(current->nodes[index - 1].bounds, x, y)) {
                return createNodeProvider(current, index - 1, pRetVal);
            }
        }
    }
    return QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(pRetVal));
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::GetFocus(IRawElementProviderFragment** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    const auto current = snapshot();
    if (!current) {
        return S_OK;
    }
    for (std::size_t index = 0; index < current->nodes.size(); ++index) {
        if (current->nodes[index].mapped.hasKeyboardFocus) {
            return createNodeProvider(current, index, pRetVal);
        }
    }
    return S_OK;
}

} // namespace Tina::UI::UiaCom
