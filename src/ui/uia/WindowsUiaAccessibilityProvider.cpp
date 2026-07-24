#include "WindowsUiaAccessibilityProvider.hpp"

#include <tina/ui/WindowsUiaAccessibilityProviderFactory.hpp>

#include <memory>
#include <utility>

// ControlType / ToggleState numeric values in UIUiaMapping.hpp match UI Automation
// (UIAutomationClient.h). This TU intentionally does not include COM headers so the
// private adapter compiles without dragging IUnknown into the rest of the UI graph.
// HWND / IRawElementProviderSimple registration is a later product slice.

namespace Tina::UI {

Core::Status WindowsUiaAccessibilityProvider::publish(const UIAccessibilityTree& tree)
{
    m_tree = tree;
    m_mapped.clear();
    m_mapped.reserve(m_tree.size());
    for (const UIAccessibilityNode& node : m_tree.nodes()) {
        m_mapped.push_back(Uia::mapAccessibilityNode(node));
    }
    m_hasTree = true;
    ++m_publishCount;
    return Core::success();
}

void WindowsUiaAccessibilityProvider::clear() noexcept
{
    m_tree = UIAccessibilityTree{};
    m_mapped.clear();
    m_hasTree = false;
    ++m_clearCount;
}

bool WindowsUiaAccessibilityProvider::hasPublishedTree() const noexcept
{
    return m_hasTree;
}

std::span<const Uia::UIUiaMappedNode> WindowsUiaAccessibilityProvider::mappedNodes() const noexcept
{
    return std::span<const Uia::UIUiaMappedNode>(m_mapped.data(), m_mapped.size());
}

Core::Result<Uia::UIUiaMappedNode> WindowsUiaAccessibilityProvider::readMappedNode(UINodeId id) const
{
    if (!m_hasTree) {
        return Core::failure(UIErrorCode::AccessibilityTreeMissing, "no UIA accessibility tree published");
    }
    for (const Uia::UIUiaMappedNode& node : m_mapped) {
        if (node.node == id) {
            return node;
        }
    }
    return Core::failure(UIErrorCode::AccessibilityNodeStale, "UIA accessibility node is missing or stale");
}

Core::Result<std::unique_ptr<IUIAccessibilityProvider>> createWindowsUiaAccessibilityProvider(
    std::pmr::memory_resource& resource)
{
    return std::unique_ptr<IUIAccessibilityProvider>(
        std::make_unique<WindowsUiaAccessibilityProvider>(resource));
}

bool windowsUiaAccessibilityProviderAvailable() noexcept
{
    return true;
}

} // namespace Tina::UI
