#include "WindowsUiaComProviders.hpp"

#include <UIAutomationClient.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace Tina::UI::UiaCom {
namespace {

[[nodiscard]] RECT clientRelativeToScreen(HWND hwnd, const UI::UILogicalRect& logical) noexcept
{
    RECT client{};
    POINT origin{0, 0};
    if (hwnd != nullptr) {
        (void)::GetClientRect(hwnd, &client);
        (void)::ClientToScreen(hwnd, &origin);
    }
    RECT out{};
    out.left = origin.x + static_cast<LONG>(std::lround(logical.x));
    out.top = origin.y + static_cast<LONG>(std::lround(logical.y));
    out.right = out.left + static_cast<LONG>(std::lround(logical.width));
    out.bottom = out.top + static_cast<LONG>(std::lround(logical.height));
    return out;
}

[[nodiscard]] BSTR bstrFromUtf8(std::string_view utf8)
{
    if (utf8.empty()) {
        return ::SysAllocString(L"");
    }
    const int wideCount = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wideCount <= 0) {
        return ::SysAllocString(L"");
    }
    std::wstring wide(static_cast<std::size_t>(wideCount), L'\0');
    (void)::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), wideCount);
    return ::SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
}

void setBoolVariant(VARIANT* v, bool value) noexcept
{
    ::VariantInit(v);
    v->vt = VT_BOOL;
    v->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

void setI4Variant(VARIANT* v, LONG value) noexcept
{
    ::VariantInit(v);
    v->vt = VT_I4;
    v->lVal = value;
}

void setR8Variant(VARIANT* v, double value) noexcept
{
    ::VariantInit(v);
    v->vt = VT_R8;
    v->dblVal = value;
}

void setBstrVariant(VARIANT* v, std::string_view utf8)
{
    ::VariantInit(v);
    v->vt = VT_BSTR;
    v->bstrVal = bstrFromUtf8(utf8);
}

} // namespace

// --- NodeProvider ---

NodeProvider::NodeProvider(HostBridgeRoot* root, Uia::UIUiaMappedNode mapped, RECT bounds) noexcept
    : m_root(root)
    , m_mapped(std::move(mapped))
    , m_bounds(bounds)
{
}

HRESULT STDMETHODCALLTYPE NodeProvider::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
        *ppvObject = static_cast<IRawElementProviderSimple*>(this);
    } else if (riid == IID_IRawElementProviderFragment) {
        *ppvObject = static_cast<IRawElementProviderFragment*>(this);
    } else {
        *ppvObject = nullptr;
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

HRESULT STDMETHODCALLTYPE NodeProvider::GetPatternProvider(PATTERNID, IUnknown** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    // Patterns are mirrored via GetPropertyValue in this slice.
    *pRetVal = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    ::VariantInit(pRetVal);
    switch (propertyId) {
    case UIA_NamePropertyId:
        setBstrVariant(pRetVal, m_mapped.name);
        return S_OK;
    case UIA_ControlTypePropertyId:
        setI4Variant(pRetVal, static_cast<LONG>(m_mapped.controlTypeId));
        return S_OK;
    case UIA_IsEnabledPropertyId:
        setBoolVariant(pRetVal, m_mapped.isEnabled);
        return S_OK;
    case UIA_IsKeyboardFocusablePropertyId:
        setBoolVariant(pRetVal, m_mapped.isKeyboardFocusable);
        return S_OK;
    case UIA_HasKeyboardFocusPropertyId:
        setBoolVariant(pRetVal, m_mapped.hasKeyboardFocus);
        return S_OK;
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        setBoolVariant(pRetVal, true);
        return S_OK;
    case UIA_ProviderDescriptionPropertyId:
        setBstrVariant(pRetVal, "Tina.UI.WindowsUia.NodeProvider");
        return S_OK;
    case UIA_AutomationIdPropertyId: {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "tina-ui-node-%u-%u", m_mapped.node.index(), m_mapped.node.generation());
        setBstrVariant(pRetVal, buffer);
        return S_OK;
    }
    case UIA_ValueValuePropertyId:
        if (m_mapped.value.has_value()) {
            setBstrVariant(pRetVal, m_mapped.value->value);
            return S_OK;
        }
        break;
    case UIA_RangeValueValuePropertyId:
        if (m_mapped.rangeValue.has_value()) {
            setR8Variant(pRetVal, m_mapped.rangeValue->value);
            return S_OK;
        }
        break;
    case UIA_RangeValueMinimumPropertyId:
        if (m_mapped.rangeValue.has_value()) {
            setR8Variant(pRetVal, m_mapped.rangeValue->minimum);
            return S_OK;
        }
        break;
    case UIA_RangeValueMaximumPropertyId:
        if (m_mapped.rangeValue.has_value()) {
            setR8Variant(pRetVal, m_mapped.rangeValue->maximum);
            return S_OK;
        }
        break;
    case UIA_ToggleToggleStatePropertyId:
        if (m_mapped.toggleState.has_value()) {
            setI4Variant(pRetVal, static_cast<LONG>(*m_mapped.toggleState));
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
        return S_OK;
    }
    switch (direction) {
    case NavigateDirection_Parent:
        return m_root->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(pRetVal));
    case NavigateDirection_NextSibling:
        if (NodeProvider* next = m_root->childAt(m_siblingIndex + 1); next != nullptr) {
            return next->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(pRetVal));
        }
        break;
    case NavigateDirection_PreviousSibling:
        if (m_siblingIndex > 0) {
            if (NodeProvider* prev = m_root->childAt(m_siblingIndex - 1); prev != nullptr) {
                return prev->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(pRetVal));
            }
        }
        break;
    case NavigateDirection_FirstChild:
    case NavigateDirection_LastChild:
        break;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::GetRuntimeId(SAFEARRAY** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    // UiaAppendRuntimeId, hwnd-derived uniqueness, node index, generation
    LONG ids[4] = {
        static_cast<LONG>(UiaAppendRuntimeId),
        static_cast<LONG>(reinterpret_cast<uintptr_t>(m_root != nullptr ? m_root->hwnd() : nullptr) & 0x7fffffff),
        static_cast<LONG>(m_mapped.node.index()),
        static_cast<LONG>(m_mapped.node.generation()),
    };
    SAFEARRAY* array = ::SafeArrayCreateVector(VT_I4, 0, 4);
    if (array == nullptr) {
        return E_OUTOFMEMORY;
    }
    for (LONG i = 0; i < 4; ++i) {
        (void)::SafeArrayPutElement(array, &i, &ids[i]);
    }
    *pRetVal = array;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_BoundingRectangle(UiaRect* pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    pRetVal->left = static_cast<double>(m_bounds.left);
    pRetVal->top = static_cast<double>(m_bounds.top);
    pRetVal->width = static_cast<double>(m_bounds.right - m_bounds.left);
    pRetVal->height = static_cast<double>(m_bounds.bottom - m_bounds.top);
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
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NodeProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (m_root == nullptr) {
        *pRetVal = nullptr;
        return S_OK;
    }
    return m_root->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(pRetVal));
}

// --- HostBridgeRoot ---

HostBridgeRoot::HostBridgeRoot(HWND hwnd) noexcept
    : m_hwnd(hwnd)
{
}

HostBridgeRoot::~HostBridgeRoot() noexcept
{
    clearChildren();
}

void HostBridgeRoot::clearChildren() noexcept
{
    for (NodeProvider* child : m_children) {
        if (child != nullptr) {
            child->Release();
        }
    }
    m_children.clear();
}

void HostBridgeRoot::rebuildChildren(const WindowsUiaAccessibilityProvider& provider, HWND hwnd)
{
    clearChildren();
    m_hwnd = hwnd;
    const auto mapped = provider.mappedNodes();
    const auto nodes = provider.tree().nodes();
    m_children.reserve(mapped.size());
    for (std::size_t i = 0; i < mapped.size(); ++i) {
        RECT bounds{};
        if (i < nodes.size()) {
            bounds = clientRelativeToScreen(hwnd, nodes[i].worldRect);
        }
        auto* child = new NodeProvider(this, mapped[i], bounds);
        child->setSiblingIndex(i);
        m_children.push_back(child);
    }
}

NodeProvider* HostBridgeRoot::childAt(std::size_t index) const noexcept
{
    return index < m_children.size() ? m_children[index] : nullptr;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
        *ppvObject = static_cast<IRawElementProviderSimple*>(this);
    } else if (riid == IID_IRawElementProviderFragment) {
        *ppvObject = static_cast<IRawElementProviderFragment*>(this);
    } else if (riid == IID_IRawElementProviderFragmentRoot) {
        *ppvObject = static_cast<IRawElementProviderFragmentRoot*>(this);
    } else {
        *ppvObject = nullptr;
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
        setBstrVariant(pRetVal, "Tina UI Root");
        return S_OK;
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
        setBstrVariant(pRetVal, "Tina.UI.WindowsUia.HostBridgeRoot");
        return S_OK;
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        setBoolVariant(pRetVal, true);
        return S_OK;
    default:
        break;
    }
    pRetVal->vt = VT_EMPTY;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::get_HostRawElementProvider(IRawElementProviderSimple** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    if (m_hwnd == nullptr) {
        *pRetVal = nullptr;
        return S_OK;
    }
    return ::UiaHostProviderFromHwnd(m_hwnd, pRetVal);
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::Navigate(NavigateDirection direction, IRawElementProviderFragment** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    if (m_children.empty()) {
        return S_OK;
    }
    switch (direction) {
    case NavigateDirection_FirstChild:
        return m_children.front()->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(pRetVal));
    case NavigateDirection_LastChild:
        return m_children.back()->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(pRetVal));
    default:
        break;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::GetRuntimeId(SAFEARRAY** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    LONG ids[2] = {
        static_cast<LONG>(UiaAppendRuntimeId),
        static_cast<LONG>(reinterpret_cast<uintptr_t>(m_hwnd) & 0x7fffffff),
    };
    SAFEARRAY* array = ::SafeArrayCreateVector(VT_I4, 0, 2);
    if (array == nullptr) {
        return E_OUTOFMEMORY;
    }
    for (LONG i = 0; i < 2; ++i) {
        (void)::SafeArrayPutElement(array, &i, &ids[i]);
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
    if (m_hwnd != nullptr) {
        (void)::GetClientRect(m_hwnd, &rect);
        POINT origin{0, 0};
        (void)::ClientToScreen(m_hwnd, &origin);
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
    if (m_hwnd != nullptr) {
        (void)::SetFocus(m_hwnd);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::get_FragmentRoot(IRawElementProviderFragmentRoot** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    return QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(pRetVal));
}

HRESULT STDMETHODCALLTYPE HostBridgeRoot::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** pRetVal)
{
    if (pRetVal == nullptr) {
        return E_POINTER;
    }
    *pRetVal = nullptr;
    const LONG px = static_cast<LONG>(x);
    const LONG py = static_cast<LONG>(y);
    for (NodeProvider* child : m_children) {
        if (child == nullptr) {
            continue;
        }
        UiaRect rect{};
        if (FAILED(child->get_BoundingRectangle(&rect))) {
            continue;
        }
        if (px >= rect.left && px < rect.left + rect.width && py >= rect.top && py < rect.top + rect.height) {
            return child->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(pRetVal));
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
    for (NodeProvider* child : m_children) {
        if (child != nullptr && child->mapped().hasKeyboardFocus) {
            return child->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(pRetVal));
        }
    }
    return S_OK;
}

} // namespace Tina::UI::UiaCom
