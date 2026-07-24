#pragma once

// Private Windows UIA accessibility provider (not Game SDK public).
// Consumes UIAccessibilityTree each publish; clears on root release / shutdown.

#include <tina/ui/UIAccessibility.hpp>

#include "UIUiaMapping.hpp"

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI {

class WindowsUiaAccessibilityProvider final : public IUIAccessibilityProvider {
public:
    explicit WindowsUiaAccessibilityProvider(std::pmr::memory_resource& resource) noexcept
        : m_tree(&resource)
        , m_mapped(&resource)
    {
    }

    Core::Status publish(const UIAccessibilityTree& tree) override;
    void clear() noexcept override;
    [[nodiscard]] bool hasPublishedTree() const noexcept override;

    [[nodiscard]] u64 publishCount() const noexcept { return m_publishCount; }
    [[nodiscard]] u64 clearCount() const noexcept { return m_clearCount; }
    [[nodiscard]] const UIAccessibilityTree& tree() const noexcept { return m_tree; }
    [[nodiscard]] std::span<const Uia::UIUiaMappedNode> mappedNodes() const noexcept;
    [[nodiscard]] Core::Result<Uia::UIUiaMappedNode> readMappedNode(UINodeId id) const;

private:
    UIAccessibilityTree m_tree{};
    std::pmr::vector<Uia::UIUiaMappedNode> m_mapped{std::pmr::get_default_resource()};
    bool m_hasTree = false;
    u64 m_publishCount = 0;
    u64 m_clearCount = 0;
};

} // namespace Tina::UI
