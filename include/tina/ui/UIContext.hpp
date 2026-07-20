#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/Window.hpp>
#include <tina/ui/UICommittedHit.hpp>
#include <tina/ui/UICommittedLayout.hpp>
#include <tina/ui/UICommittedPaint.hpp>
#include <tina/ui/UICommittedStructure.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UIContextConfig.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/UIEventRouting.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>
#include <tina/ui/UIWidgetKind.hpp>

#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>

namespace Tina::UI::Detail {

struct UIContextLifetimeControl;

} // namespace Tina::UI::Detail

namespace Tina::UI {

struct UIContextStatistics final {
    usize nodeCapacity = 0;
    usize rootCapacity = 0;
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    usize hitSnapshotCapacity = 0;
    usize paintSnapshotCapacity = 0;
    usize routePathCapacity = 0;
    usize routedPointerListenerCapacity = 0;
    usize activeRoutedPointerListenerCount = 0;
    usize routedPointerListenerHighWater = 0;
    usize buttonActionCapacity = 0;
    usize activeButtonActionCount = 0;
    usize buttonActionHighWater = 0;
    usize textByteCapacity = 0;
    usize textByteUsed = 0;
    usize textByteHighWater = 0;
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
    usize committedPaintNodeCount = 0;
    u64 paintRevision = 0;
    bool dirty = false;       // Structure dirty kept for M7-C1a compatibility.
    bool layoutDirty = false; // Style/structure changes still requiring layout.
    bool hitDirty = false;
    bool paintDirty = false;
    usize lastLayoutPassCount = 0;
    usize lastLayoutMeasuredNodeCount = 0;
    usize lastLayoutArrangedNodeCount = 0;
    // Percent values skipped while an Auto axis lacked a definite Measure
    // basis. Arrange may still resolve them once against the final content box.
    usize lastLayoutPercentMeasureFallbackCount = 0;
    usize lastHitRebuildCount = 0;
    usize lastPaintCacheRebuildCount = 0;
    usize lastPaintSnapshotRebuildCount = 0;
    usize dirtyQueuePendingCount = 0;
    usize dirtyQueueHighWater = 0;
};

class UIContext;

// Move-only ownership of one routed-pointer listener registration. Owner-thread
// reset takes effect immediately, including during dispatch. Off-thread reset is
// queued in bounded storage and takes effect before the next owner-thread UI
// mutation or route. Reset remains safe after the UIContext is destroyed.
class UIRoutedPointerListenerToken final {
public:
    UIRoutedPointerListenerToken() noexcept = default;
    ~UIRoutedPointerListenerToken() noexcept;

    UIRoutedPointerListenerToken(const UIRoutedPointerListenerToken&) = delete;
    UIRoutedPointerListenerToken& operator=(const UIRoutedPointerListenerToken&) = delete;

    UIRoutedPointerListenerToken(UIRoutedPointerListenerToken&& other) noexcept;
    UIRoutedPointerListenerToken& operator=(UIRoutedPointerListenerToken&& other) noexcept;

    void reset() noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    explicit operator bool() const noexcept;

private:
    friend class UIContext;

    UIRoutedPointerListenerToken(
        std::weak_ptr<Detail::UIContextLifetimeControl> lifetime,
        u32 slot,
        u32 generation) noexcept;

    std::weak_ptr<Detail::UIContextLifetimeControl> m_lifetime{};
    u32 m_slot = 0;
    u32 m_generation = 0;
};

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

    [[nodiscard]] Core::Result<UINodeId> createPanel(UINodeId parent);
    [[nodiscard]] Core::Result<UINodeId> createLabel(UINodeId parent);
    [[nodiscard]] Core::Result<UINodeId> createButton(UINodeId parent);
    [[nodiscard]] bool isAlive(UINodeId node) const noexcept;
    [[nodiscard]] Core::Status setLayoutStyle(UINodeId node, const UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(
        UINodeId node,
        UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setBoxPaint(UINodeId node, const UIBoxPaint& paint);
    // Label/Button only. Stores strict UTF-8 without NUL into the fixed text
    // byte budget and dirties Measure for Auto-sized intrinsic placeholders.
    // Glyph raster and FreeType remain out of this API.
    [[nodiscard]] Core::Status setText(UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyle(UINodeId node, const UITextStyle& style);
    [[nodiscard]] Core::Result<std::string_view> text(UINodeId node);
    [[nodiscard]] Core::Result<UITextStyle> textStyle(UINodeId node);
    [[nodiscard]] Core::Status setButtonAction(
        UINodeId button,
        UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearButtonAction(UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressed(UINodeId button) const;
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListener(
        UIRoutedPointerListenerDesc descriptor,
        UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Status destroy(UINodeId node);

private:
    friend class UIContext;

    UITreeUpdater(UIContext& context, UINodeId root) noexcept;

    UIContext* m_context = nullptr;
    UINodeId m_root{};
};

// Single-owner-thread retained-tree context for one WindowId. The supplied PMR
// resource backs bounded tree/id/style/paint/dirty/layout/hit scratch and
// committed snapshot storage and must outlive the context. Small control-plane
// objects and cross-thread release queues allocate only during Create() from the process
// default heap.
class UIContext final {
public:
    // Default Create wires createPlaceholderTextRasterizer, opens its built-in
    // empty face, and owns a fixed-capacity UIGlyphAtlas for paint-time
    // placements. Glyph DisplayList emission depends on successful atlas insert.
    [[nodiscard]] static Core::Result<std::unique_ptr<UIContext>> Create(
        Platform::WindowId ownerWindow,
        UIContextCapacityConfig capacityConfig = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    // Takes ownership of textRasterizer. For the placeholder rasterizer, empty
    // font bytes open the built-in face. FreeType adapters must open a real face
    // before setText can measure (or measure fails with InvalidFont).
    [[nodiscard]] static Core::Result<std::unique_ptr<UIContext>> Create(
        Platform::WindowId ownerWindow,
        UIContextCapacityConfig capacityConfig,
        std::unique_ptr<IUITextRasterizer> textRasterizer,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    // Destruction is an owner-thread, phase-boundary operation. Destroying a
    // context from one of its routed callbacks/callback destructors, or from a
    // non-owner thread, violates the lifetime contract and terminates rather
    // than continuing with a use-after-free.
    ~UIContext() noexcept;

    UIContext(const UIContext&) = delete;
    UIContext& operator=(const UIContext&) = delete;
    UIContext(UIContext&&) = delete;
    UIContext& operator=(UIContext&&) = delete;

    [[nodiscard]] Platform::WindowId ownerWindow() const noexcept;
    [[nodiscard]] bool contains(UINodeId node) const noexcept;

    // Opens (or replaces) the text face used by measure/paint. Closes the previous
    // face, clears the glyph atlas, and dirties layout/paint for nodes with text.
    // FreeType requires non-empty font bytes; placeholder rejects non-empty bytes.
    [[nodiscard]] Core::Status openTextFont(
        std::span<const std::byte> fontBytes,
        i32 faceIndex = 0);

    [[nodiscard]] UIRootBuilder rootBuilder() noexcept;
    [[nodiscard]] Core::Result<UITreeUpdater> treeUpdater(UIRootOwner& rootOwner);

    // C1a diagnostic seam. Runtime frame publication uses commitLayout() so a
    // pending structure and its geometry become visible in one transaction.
    [[nodiscard]] Core::Status commitStructure();
    [[nodiscard]] UICommittedStructureView committedStructure() const noexcept;
    [[nodiscard]] Core::Status commitLayout(UILogicalSize viewportSize);
    [[nodiscard]] UICommittedLayoutView committedLayout() const noexcept;
    [[nodiscard]] UICommittedHitView committedHit() const noexcept;
    [[nodiscard]] UICommittedPaintView committedPaint() const noexcept;
    // Borrow of the context-owned R8 glyph atlas page after paint publication.
    // Valid until the next paint that inserts/clears the atlas, or Context
    // destruction. Empty when no atlas is allocated.
    [[nodiscard]] std::span<const u8> glyphAtlasPixels() const noexcept;
    [[nodiscard]] u32 glyphAtlasWidth() const noexcept;
    [[nodiscard]] u32 glyphAtlasHeight() const noexcept;
    // Pure query over the last committed hit snapshot. It never commits
    // layout, rebuilds hit data, dispatches an event, or allocates storage.
    [[nodiscard]] UIPointerHitQueryResult queryPointerHit(
        UILogicalPoint point) const noexcept;
    // Registers an owning, fixed-inline callback in stable per-node order.
    // Registration is bounded; dispatch never grows or heap-falls back.
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListener(
        UIRoutedPointerListenerDesc descriptor,
        UIRoutedPointerCallback callback);
    // Routes one normalized pointer input over the last committed hit snapshot.
    // It performs at most one point query and never commits layout or hit data.
    [[nodiscard]] Core::Result<UIPointerRouteResult> routePointerInput(
        const UIPointerInputEvent& input);
    // Clears retained Primary Pointer interaction state for the matching
    // Window without synthesizing an Up event or invoking a Button action.
    [[nodiscard]] Core::Status cancelPointerInteraction(
        Platform::WindowId routedWindow);
    [[nodiscard]] UIContextStatistics statistics() const noexcept;
    [[nodiscard]] usize liveNodeCount() const noexcept;
    [[nodiscard]] usize liveRootCount() const noexcept;

private:
    friend class UIRootOwner;
    friend class UIRootBuilder;
    friend class UITreeUpdater;
    friend class UIRoutedPointerListenerToken;

    struct Impl;

    explicit UIContext(std::unique_ptr<Impl> impl) noexcept;

    [[nodiscard]] Core::Result<UIRootOwner> createRoot();
    [[nodiscard]] Core::Result<UINodeId> createChild(UINodeId parent, UIWidgetKind kind);
    [[nodiscard]] Core::Result<UINodeId> createChildFromUpdater(
        UINodeId updaterRoot,
        UINodeId parent,
        UIWidgetKind kind);
    [[nodiscard]] Core::Status setLayoutStyleFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        const UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicyFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setBoxPaintFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        const UIBoxPaint& paint);
    [[nodiscard]] Core::Status setTextFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyleFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        const UITextStyle& style);
    [[nodiscard]] Core::Result<std::string_view> textFromUpdater(
        UINodeId updaterRoot,
        UINodeId node);
    [[nodiscard]] Core::Result<UITextStyle> textStyleFromUpdater(
        UINodeId updaterRoot,
        UINodeId node);
    [[nodiscard]] Core::Status setButtonActionFromUpdater(
        UINodeId updaterRoot,
        UINodeId button,
        UIButtonActionCallback&& callback);
    [[nodiscard]] Core::Status clearButtonActionFromUpdater(
        UINodeId updaterRoot,
        UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressedFromUpdater(
        UINodeId updaterRoot,
        UINodeId button);
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListenerFromUpdater(
        UINodeId updaterRoot,
        UIRoutedPointerListenerDesc descriptor,
        UIRoutedPointerCallback&& callback);
    [[nodiscard]] Core::Status destroyNodeFromUpdater(UINodeId updaterRoot, UINodeId node);
    void destroyRootFromOwner(UINodeId root) noexcept;
    [[nodiscard]] bool isAliveInRoot(UINodeId updaterRoot, UINodeId node) const noexcept;
    void releaseRoutedPointerListenerFromToken(u32 slot, u32 generation) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace Tina::UI
