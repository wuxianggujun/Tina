#pragma once

#include <tina/core/id/GenerationId.hpp>
#include <tina/platform/Window.hpp>

#include <compare>

namespace Tina::UI::Detail {

struct UINodeRegistryTag;

} // namespace Tina::UI::Detail

namespace Tina::UI {

class UIContext;
class UIRootBuilder;
class UITreeUpdater;

class UINodeId final {
public:
    constexpr UINodeId() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return m_ownerWindow.hasValue() && m_node.hasValue();
    }

    [[nodiscard]] constexpr Platform::WindowId ownerWindow() const noexcept
    {
        return m_ownerWindow;
    }

    [[nodiscard]] constexpr u32 index() const noexcept
    {
        return m_node.index();
    }

    [[nodiscard]] constexpr u32 generation() const noexcept
    {
        return m_node.generation();
    }

    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    auto operator<=>(const UINodeId&) const = default;

private:
    using StorageId = Core::GenerationId<Detail::UINodeRegistryTag>;

    friend class UIContext;
    friend class UIRootBuilder;
    friend class UITreeUpdater;

    [[nodiscard]] static constexpr UINodeId create(
        Platform::WindowId ownerWindow,
        StorageId node) noexcept
    {
        return UINodeId(ownerWindow, node);
    }

    [[nodiscard]] constexpr StorageId storageId() const noexcept
    {
        return m_node;
    }

    constexpr UINodeId(Platform::WindowId ownerWindow, StorageId node) noexcept
        : m_ownerWindow(ownerWindow), m_node(node)
    {
    }

    Platform::WindowId m_ownerWindow{};
    StorageId m_node{};
};

} // namespace Tina::UI
