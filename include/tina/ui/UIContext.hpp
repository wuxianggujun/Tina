#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/Window.hpp>
#include <tina/ui/UICommittedHit.hpp>
#include <tina/ui/UICommittedLayout.hpp>
#include <tina/ui/UICommittedStructure.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIWidgetKind.hpp>

#include <memory>
#include <memory_resource>

namespace Tina::UI::Detail {

struct UIContextLifetimeControl;

} // namespace Tina::UI::Detail

namespace Tina::UI {

struct UIContextCapacityConfig final {
    static constexpr usize DefaultNodeCapacity = 4096;
    static constexpr usize DefaultRootCapacity = 64;
    static constexpr usize MaxNodeCapacity = 1'048'576;
    static constexpr usize MaxRootCapacity = 4096;

    usize nodeCapacity = DefaultNodeCapacity;
    usize rootCapacity = DefaultRootCapacity;
    // Zero derives from nodeCapacity. Non-zero values are fixed capacities and
    // do not grow at runtime.
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    // Counts every effectively visible route-ancestry entry, including nodes
    // whose pointer policy is Ignore; it is not a targetable-node capacity.
    usize hitSnapshotCapacity = 0;
};

struct UIContextStatistics final {
    usize nodeCapacity = 0;
    usize rootCapacity = 0;
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    usize hitSnapshotCapacity = 0;
    usize liveNodeCount = 0;
    usize liveRootCount = 0;
    usize committedNodeCount = 0;
    u64 committedRevision = 0;
    usize committedLayoutNodeCount = 0;
    u64 layoutRevision = 0;
    usize committedHitNodeCount = 0;
    usize committedHitTargetCount = 0;
    u64 hitRevision = 0;
    u64 paintOrderRevision = 0;
    bool dirty = false;       // Structure dirty kept for M7-C1a compatibility.
    bool layoutDirty = false; // Style/structure changes still requiring layout.
    bool hitDirty = false;
    usize lastLayoutPassCount = 0;
    usize lastLayoutMeasuredNodeCount = 0;
    usize lastLayoutArrangedNodeCount = 0;
    // Percent values skipped while an Auto axis lacked a definite Measure
    // basis. Arrange may still resolve them once against the final content box.
    usize lastLayoutPercentMeasureFallbackCount = 0;
    usize lastHitRebuildCount = 0;
    usize dirtyQueuePendingCount = 0;
    usize dirtyQueueHighWater = 0;
};

class UIContext;

// Move-only ownership of one retained root. Owner-thread destruction reclaims
// immediately; destruction on another thread enters a bounded preallocated
// release queue drained at the next owner-thread UI mutation/commit.
class UIRootOwner final {
public:
    UIRootOwner() noexcept = default;
    ~UIRootOwner() noexcept;

    UIRootOwner(const UIRootOwner&) = delete;
    UIRootOwner& operator=(const UIRootOwner&) = delete;

    UIRootOwner(UIRootOwner&& other) noexcept;
    UIRootOwner& operator=(UIRootOwner&& other) noexcept;

    void reset() noexcept;

    [[nodiscard]] UINodeId rootNodeId() const noexcept;
    [[nodiscard]] bool hasValue() const noexcept;
    explicit operator bool() const noexcept;

private:
    friend class UIContext;
    friend class UIRootBuilder;
    friend class UITreeUpdater;

    UIRootOwner(
        std::weak_ptr<Detail::UIContextLifetimeControl> lifetime,
        UINodeId root) noexcept;

    std::weak_ptr<Detail::UIContextLifetimeControl> m_lifetime{};
    UINodeId m_root{};
};

// Non-owning owner-thread view. It must not outlive the UIContext that created it.
class UIRootBuilder final {
public:
    UIRootBuilder() noexcept = default;

    [[nodiscard]] Core::Result<UIRootOwner> createRoot();
    [[nodiscard]] Core::Result<UINodeId> createPanel(UINodeId parent);
    [[nodiscard]] Core::Result<UINodeId> createLabel(UINodeId parent);
    [[nodiscard]] Core::Result<UINodeId> createButton(UINodeId parent);

private:
    friend class UIContext;

    explicit UIRootBuilder(UIContext& context) noexcept;

    UIContext* m_context = nullptr;
};

// Non-owning owner-thread view scoped to one live root. It must not outlive its
// UIContext; generation checks reject a root or child destroyed while it exists.
class UITreeUpdater final {
public:
    UITreeUpdater() noexcept = default;

    UITreeUpdater(const UITreeUpdater&) = delete;
    UITreeUpdater& operator=(const UITreeUpdater&) = delete;

    UITreeUpdater(UITreeUpdater&& other) noexcept;
    UITreeUpdater& operator=(UITreeUpdater&& other) noexcept;

    [[nodiscard]] bool isAlive(UINodeId node) const noexcept;
    [[nodiscard]] Core::Status setLayoutStyle(UINodeId node, const UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(
        UINodeId node,
        UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status destroy(UINodeId node);

private:
    friend class UIContext;

    UITreeUpdater(UIContext& context, UINodeId root) noexcept;

    UIContext* m_context = nullptr;
    UINodeId m_root{};
};

// Single-owner-thread retained-tree context for one WindowId. The supplied PMR
// resource backs bounded tree/id/style/dirty/layout/hit scratch and committed
// snapshot storage and must outlive the context. Small control-plane objects
// and cross-thread release queues allocate only during Create() from the process
// default heap.
class UIContext final {
public:
    [[nodiscard]] static Core::Result<std::unique_ptr<UIContext>> Create(
        Platform::WindowId ownerWindow,
        UIContextCapacityConfig capacityConfig = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~UIContext() noexcept;

    UIContext(const UIContext&) = delete;
    UIContext& operator=(const UIContext&) = delete;
    UIContext(UIContext&&) = delete;
    UIContext& operator=(UIContext&&) = delete;

    [[nodiscard]] Platform::WindowId ownerWindow() const noexcept;
    [[nodiscard]] bool contains(UINodeId node) const noexcept;

    [[nodiscard]] UIRootBuilder rootBuilder() noexcept;
    [[nodiscard]] Core::Result<UITreeUpdater> treeUpdater(UIRootOwner& rootOwner);

    // C1a diagnostic seam. Runtime frame publication uses commitLayout() so a
    // pending structure and its geometry become visible in one transaction.
    [[nodiscard]] Core::Status commitStructure();
    [[nodiscard]] UICommittedStructureView committedStructure() const noexcept;
    [[nodiscard]] Core::Status commitLayout(UILogicalSize viewportSize);
    [[nodiscard]] UICommittedLayoutView committedLayout() const noexcept;
    [[nodiscard]] UICommittedHitView committedHit() const noexcept;
    [[nodiscard]] UIContextStatistics statistics() const noexcept;
    [[nodiscard]] usize liveNodeCount() const noexcept;
    [[nodiscard]] usize liveRootCount() const noexcept;

private:
    friend class UIRootOwner;
    friend class UIRootBuilder;
    friend class UITreeUpdater;

    struct Impl;

    explicit UIContext(std::unique_ptr<Impl> impl) noexcept;

    [[nodiscard]] Core::Result<UIRootOwner> createRoot();
    [[nodiscard]] Core::Result<UINodeId> createChild(UINodeId parent, UIWidgetKind kind);
    [[nodiscard]] Core::Status setLayoutStyleFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        const UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicyFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status destroyNodeFromUpdater(UINodeId updaterRoot, UINodeId node);
    void destroyRootFromOwner(UINodeId root) noexcept;
    [[nodiscard]] bool isAliveInRoot(UINodeId updaterRoot, UINodeId node) const noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace Tina::UI
