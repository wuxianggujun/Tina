#pragma once

#include <tina/core/base/EnumFlags.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UISemantics.hpp>

#include <cstring>
#include <limits>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::UI {

// Platform-neutral accessibility states derived from committed semantics (UI-002).
// Real UIA/AT-SPI backends map these onto platform properties; they are not the platform APIs.
enum class UIAccessibilityState : u32 {
    None = 0,
    Enabled = 1u << 0,
    Disabled = 1u << 1,
    Focused = 1u << 2,
    Checked = 1u << 3,
    Unchecked = 1u << 4,
    HasRange = 1u << 5,
    ReadOnly = 1u << 6,
    Selected = 1u << 7,
};

TINA_ENUM_FLAG_OPERATORS(UIAccessibilityState);

[[nodiscard]] constexpr bool hasState(UIAccessibilityState set, UIAccessibilityState flag) noexcept
{
    return hasAnyFlag(set, flag);
}

// Stable, owner-thread node published for assistive tech adapters.
// Text views point into UIAccessibilityTree private storage (not the live UIContext).
struct UIAccessibilityNode final {
    UINodeId node{};
    UINodeId parent{};
    UISemanticsRole role = UISemanticsRole::Group;
    UISemanticsAction actions = UISemanticsAction::None;
    UISemanticsLiveSetting liveSetting = UISemanticsLiveSetting::Off;
    UILogicalRect worldRect{};
    std::string_view name{};
    std::string_view description{};
    std::string_view valueText{};
    float value = 0.0F;
    float minValue = 0.0F;
    float maxValue = 0.0F;
    UIListViewItemKey virtualItemKey = InvalidUIListViewItemKey;
    u64 virtualItemIndex = 0;
    u32 level = 0;
    bool expandable = false;
    bool expanded = false;
    UIAccessibilityState states = UIAccessibilityState::None;
};

enum class UIAccessibilityActionKind : u8 {
    Focus,
    Invoke,
    Toggle,
    SetRangeValue,
    SetTextValue,
};

// Platform-neutral synchronous action. Platform adapters marshal this onto the
// UIContext owner thread; textValue is borrowed only for the duration of the call.
struct UIAccessibilityAction final {
    UIAccessibilityActionKind kind = UIAccessibilityActionKind::Focus;
    UINodeId node{};
    double rangeValue = 0.0;
    std::string_view textValue{};
};

[[nodiscard]] constexpr UIAccessibilityState statesFromSemantics(const UISemanticsEntry& entry) noexcept
{
    UIAccessibilityState states = UIAccessibilityState::None;
    if (entry.enabled) {
        states = states | UIAccessibilityState::Enabled;
    } else {
        states = states | UIAccessibilityState::Disabled;
    }
    if (entry.focused) {
        states = states | UIAccessibilityState::Focused;
    }
    if (entry.role == UISemanticsRole::Checkbox || entry.role == UISemanticsRole::Switch ||
        entry.role == UISemanticsRole::RadioButton ||
        (entry.role == UISemanticsRole::MenuItem &&
         hasSemanticsAction(entry.actions, UISemanticsAction::Toggle))) {
        states = states | (entry.checked ? UIAccessibilityState::Checked : UIAccessibilityState::Unchecked);
    }
    if (entry.hasRange) {
        states = states | UIAccessibilityState::HasRange;
    }
    if (entry.selected) {
        states = states | UIAccessibilityState::Selected;
    }
    if (entry.readOnly) {
        states = states | UIAccessibilityState::ReadOnly;
    }
    return states;
}

// Owner-thread accessibility snapshot built from committedSemantics().
// Outlives one commit only for as long as the tree object is kept; rebuild after each commitLayout.
// Platform UIA/AT-SPI providers should hold this tree (or re-query each AT poll), never raw UINodeId
// across destroy without checking node generation via findNode().
class UIAccessibilityTree final {
public:
    UIAccessibilityTree() = default;

    explicit UIAccessibilityTree(std::pmr::memory_resource* resource) noexcept
        : m_nodes(resource != nullptr ? resource : std::pmr::get_default_resource())
        , m_text(resource != nullptr ? resource : std::pmr::get_default_resource())
    {
    }

    UIAccessibilityTree(const UIAccessibilityTree& other)
        : m_nodes(other.m_nodes.get_allocator())
        , m_text(other.m_text.get_allocator())
        , m_semanticsRevision(other.m_semanticsRevision)
        , m_structureRevision(other.m_structureRevision)
        , m_layoutRevision(other.m_layoutRevision)
        , m_viewportSize(other.m_viewportSize)
    {
        (void)copyFrom(other);
    }

    UIAccessibilityTree& operator=(const UIAccessibilityTree& other)
    {
        if (this != &other) {
            m_semanticsRevision = other.m_semanticsRevision;
            m_structureRevision = other.m_structureRevision;
            m_layoutRevision = other.m_layoutRevision;
            m_viewportSize = other.m_viewportSize;
            (void)copyFrom(other);
        }
        return *this;
    }

    UIAccessibilityTree(UIAccessibilityTree&&) noexcept = default;
    UIAccessibilityTree& operator=(UIAccessibilityTree&&) noexcept = default;

    [[nodiscard]] u64 semanticsRevision() const noexcept { return m_semanticsRevision; }
    [[nodiscard]] u64 structureRevision() const noexcept { return m_structureRevision; }
    [[nodiscard]] u64 layoutRevision() const noexcept { return m_layoutRevision; }
    [[nodiscard]] UILogicalSize viewportSize() const noexcept { return m_viewportSize; }
    [[nodiscard]] usize size() const noexcept { return m_nodes.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_nodes.empty(); }
    [[nodiscard]] std::span<const UIAccessibilityNode> nodes() const noexcept
    {
        return std::span<const UIAccessibilityNode>(m_nodes.data(), m_nodes.size());
    }

    [[nodiscard]] const UIAccessibilityNode* at(usize index) const noexcept
    {
        return index < m_nodes.size() ? &m_nodes[index] : nullptr;
    }

    // Returns nullptr when the node is missing or generation does not match (stale).
    [[nodiscard]] const UIAccessibilityNode* findNode(UINodeId id) const noexcept
    {
        if (!id.hasValue()) {
            return nullptr;
        }
        for (const UIAccessibilityNode& node : m_nodes) {
            if (node.node == id) {
                return &node;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const UIAccessibilityNode* findByRole(UISemanticsRole role) const noexcept
    {
        for (const UIAccessibilityNode& node : m_nodes) {
            if (node.role == role) {
                return &node;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const UIAccessibilityNode* findByName(std::string_view name) const noexcept
    {
        for (const UIAccessibilityNode& node : m_nodes) {
            if (node.name == name) {
                return &node;
            }
        }
        return nullptr;
    }

    // Rebuild from a committed semantics view. Copies text into private storage.
    // Text is appended into a single pre-reserved buffer so string_views never dangle.
    [[nodiscard]] Core::Status rebuildFrom(const UICommittedSemanticsView& semantics)
    {
        m_nodes.clear();
        m_text.clear();
        m_semanticsRevision = semantics.semanticsRevision();
        m_structureRevision = semantics.structureRevision();
        m_layoutRevision = semantics.layoutRevision();
        m_viewportSize = semantics.viewportSize();

        usize textBytes = 0;
        for (const UISemanticsEntry& entry : semantics.entries()) {
            textBytes += entry.name.size() + entry.description.size() + entry.valueText.size();
        }
        m_text.reserve(textBytes);
        m_nodes.reserve(semantics.size());
        for (const UISemanticsEntry& entry : semantics.entries()) {
            UIAccessibilityNode node{
                .node = entry.node,
                .parent = entry.parent,
                .role = entry.role,
                .actions = entry.actions,
                .liveSetting = entry.liveSetting,
                .worldRect = entry.worldRect,
                .value = entry.value,
                .minValue = entry.minValue,
                .maxValue = entry.maxValue,
                .virtualItemKey = entry.virtualItemKey,
                .virtualItemIndex = entry.virtualItemIndex,
                .level = entry.level,
                .expandable = entry.expandable,
                .expanded = entry.expanded,
                .states = statesFromSemantics(entry),
            };
            const usize nameOff = m_text.size();
            if (auto status = appendText(entry.name); !status) {
                return status;
            }
            const usize nameLen = entry.name.size();
            const usize descOff = m_text.size();
            if (auto status = appendText(entry.description); !status) {
                return status;
            }
            const usize descLen = entry.description.size();
            const usize valueOff = m_text.size();
            if (auto status = appendText(entry.valueText); !status) {
                return status;
            }
            const usize valueLen = entry.valueText.size();
            m_nodes.push_back(node);
            rebindNodeText(m_nodes.back(), nameOff, nameLen, descOff, descLen, valueOff, valueLen);
        }
        return Core::success();
    }

    // True when tree still matches the given semantics revision (caller re-queries after commit).
    [[nodiscard]] bool matchesRevision(u64 semanticsRevision) const noexcept
    {
        return m_semanticsRevision != 0 && m_semanticsRevision == semanticsRevision;
    }

private:
    [[nodiscard]] Core::Status appendText(std::string_view source)
    {
        if (source.empty()) {
            return Core::success();
        }
        const usize offset = m_text.size();
        if (offset > (std::numeric_limits<usize>::max)() - source.size()) {
            return Core::failure(UIErrorCode::CapacityExceeded, "UI accessibility text capacity overflow");
        }
        m_text.resize(offset + source.size());
        std::memcpy(m_text.data() + offset, source.data(), source.size());
        return Core::success();
    }

    void rebindNodeText(UIAccessibilityNode& node, usize nameOff, usize nameLen, usize descOff, usize descLen,
                        usize valueOff, usize valueLen) noexcept
    {
        node.name = nameLen == 0 ? std::string_view{} : std::string_view(m_text.data() + nameOff, nameLen);
        node.description = descLen == 0 ? std::string_view{} : std::string_view(m_text.data() + descOff, descLen);
        node.valueText = valueLen == 0 ? std::string_view{} : std::string_view(m_text.data() + valueOff, valueLen);
    }

    // Deep-copy nodes while rebinding string_views into this tree's text storage.
    [[nodiscard]] Core::Status copyFrom(const UIAccessibilityTree& other)
    {
        m_nodes.clear();
        m_text.clear();
        usize textBytes = 0;
        for (const UIAccessibilityNode& source : other.m_nodes) {
            textBytes += source.name.size() + source.description.size() + source.valueText.size();
        }
        m_text.reserve(textBytes);
        m_nodes.reserve(other.m_nodes.size());
        for (const UIAccessibilityNode& source : other.m_nodes) {
            UIAccessibilityNode node{
                .node = source.node,
                .parent = source.parent,
                .role = source.role,
                .actions = source.actions,
                .liveSetting = source.liveSetting,
                .worldRect = source.worldRect,
                .value = source.value,
                .minValue = source.minValue,
                .maxValue = source.maxValue,
                .virtualItemKey = source.virtualItemKey,
                .virtualItemIndex = source.virtualItemIndex,
                .level = source.level,
                .expandable = source.expandable,
                .expanded = source.expanded,
                .states = source.states,
            };
            const usize nameOff = m_text.size();
            if (auto status = appendText(source.name); !status) {
                return status;
            }
            const usize nameLen = source.name.size();
            const usize descOff = m_text.size();
            if (auto status = appendText(source.description); !status) {
                return status;
            }
            const usize descLen = source.description.size();
            const usize valueOff = m_text.size();
            if (auto status = appendText(source.valueText); !status) {
                return status;
            }
            const usize valueLen = source.valueText.size();
            m_nodes.push_back(node);
            rebindNodeText(m_nodes.back(), nameOff, nameLen, descOff, descLen, valueOff, valueLen);
        }
        return Core::success();
    }

    std::pmr::vector<UIAccessibilityNode> m_nodes{std::pmr::get_default_resource()};
    std::pmr::vector<char> m_text{std::pmr::get_default_resource()};
    u64 m_semanticsRevision = 0;
    u64 m_structureRevision = 0;
    u64 m_layoutRevision = 0;
    UILogicalSize m_viewportSize{};
};

// Narrow SPI for platform backends (UIA/AT-SPI). First slice: tree is built by callers.
// Future Windows/Linux adapters implement this by reading UIAccessibilityTree each poll.
class IUIAccessibilityProvider {
public:
    virtual ~IUIAccessibilityProvider() noexcept = default;

    // Publish or replace the current accessibility tree. Must be owner-thread.
    virtual Core::Status publish(const UIAccessibilityTree& tree) = 0;

    // Clear published tree (context destroy / root release).
    virtual void clear() noexcept = 0;

    [[nodiscard]] virtual bool hasPublishedTree() const noexcept = 0;
};

// In-process probe provider for tests and product smoke (not a real screen reader).
class UIAccessibilityProbeProvider final : public IUIAccessibilityProvider {
public:
    Core::Status publish(const UIAccessibilityTree& tree) override
    {
        m_tree = tree;
        m_hasTree = true;
        ++m_publishCount;
        return Core::success();
    }

    void clear() noexcept override
    {
        m_tree = UIAccessibilityTree{};
        m_hasTree = false;
        ++m_clearCount;
    }

    [[nodiscard]] bool hasPublishedTree() const noexcept override { return m_hasTree; }
    [[nodiscard]] const UIAccessibilityTree& tree() const noexcept { return m_tree; }
    [[nodiscard]] u64 publishCount() const noexcept { return m_publishCount; }
    [[nodiscard]] u64 clearCount() const noexcept { return m_clearCount; }

    // Simulate assistive tech reading a node; rejects stale ids.
    [[nodiscard]] Core::Result<UIAccessibilityNode> readNode(UINodeId id) const
    {
        if (!m_hasTree) {
            return Core::failure(UIErrorCode::AccessibilityTreeMissing, "no accessibility tree published");
        }
        const UIAccessibilityNode* node = m_tree.findNode(id);
        if (node == nullptr) {
            return Core::failure(UIErrorCode::AccessibilityNodeStale, "accessibility node is missing or stale");
        }
        return *node;
    }

private:
    UIAccessibilityTree m_tree{};
    bool m_hasTree = false;
    u64 m_publishCount = 0;
    u64 m_clearCount = 0;
};

} // namespace Tina::UI
