#include <tina/ui/UIContext.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UIDirty.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <expected>
#include <limits>
#include <mutex>
#include <new>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::UI::Detail {

struct DeferredRoutedPointerListenerRelease final {
    u32 slot = 0;
    u32 generation = 0;
};

struct RoutedPointerListenerTokenState final {
    u32 generation = 0;
    bool active = false;
};

struct UIContextLifetimeControl final {
    UIContextLifetimeControl(
        std::thread::id threadId,
        usize rootCapacity,
        usize routedPointerListenerCapacity)
        : ownerThreadId(threadId),
          routedPointerListenerStates(routedPointerListenerCapacity)
    {
        deferredRootDestroys.reserve(rootCapacity);
        deferredRoutedPointerListenerReleases.reserve(routedPointerListenerCapacity);
    }

    std::mutex mutex;
    UIContext* context = nullptr;
    std::thread::id ownerThreadId{};
    std::vector<UINodeId> deferredRootDestroys;
    std::atomic_bool hasDeferredRootDestroys = false;
    std::vector<RoutedPointerListenerTokenState> routedPointerListenerStates;
    std::vector<DeferredRoutedPointerListenerRelease>
        deferredRoutedPointerListenerReleases;
    std::atomic_bool hasDeferredRoutedPointerListenerReleases = false;
};

} // namespace Tina::UI::Detail

namespace Tina::UI {
namespace {

using NodeStorageId = Core::GenerationId<Detail::UINodeRegistryTag>;
inline constexpr u32 InvalidNodeIndex = NodeStorageId::InvalidIndex;

struct NormalizedCapacityConfig final {
    usize nodeCapacity = 0;
    usize rootCapacity = 0;
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    usize hitSnapshotCapacity = 0;
    usize routePathCapacity = 0;
    usize routedPointerListenerCapacity = 0;
};

inline constexpr u32 InvalidRoutedPointerListenerIndex =
    (std::numeric_limits<u32>::max)();

struct RoutedPointerListenerRecord final {
    UINodeId node{};
    UIRoutedPointerEventKind kind = UIRoutedPointerEventKind::Move;
    UIEventPhaseMask phases = UIEventPhaseMask::None;
    UIRoutedPointerCallback callback{};
    u32 generation = 0;
    u32 previousNodeListenerIndex = InvalidRoutedPointerListenerIndex;
    u32 nextNodeListenerIndex = InvalidRoutedPointerListenerIndex;
    u32 nextFreeIndex = InvalidRoutedPointerListenerIndex;
    u64 registrationSerial = 0;
    bool active = false;
};

static_assert(std::is_nothrow_destructible_v<RoutedPointerListenerRecord>);

struct NodeRecord final {
    u32 parentIndex = InvalidNodeIndex;
    u32 firstChildIndex = InvalidNodeIndex;
    u32 lastChildIndex = InvalidNodeIndex;
    u32 previousSiblingIndex = InvalidNodeIndex;
    u32 nextSiblingIndex = InvalidNodeIndex;
    u32 rootIndex = InvalidNodeIndex;
    u32 depth = 0;
    UIWidgetKind kind = UIWidgetKind::Panel;
};

static_assert(sizeof(NodeRecord) <= 48);
static_assert(std::is_nothrow_destructible_v<NodeRecord>);

using NodePool = Core::GenerationPool<NodeRecord, Detail::UINodeRegistryTag>;

struct LayoutScratchState final {
    UILogicalSize measuredSize{};
    UILogicalRect localRect{};
    UILogicalRect worldRect{};
    UILogicalRect effectiveClip{};
    UIVisibility effectiveVisibility = UIVisibility::Visible;
    bool parentContentWidthDefinite = false;
    bool parentContentHeightDefinite = false;
    float parentContentWidth = 0.0F;
    float parentContentHeight = 0.0F;
    bool contentWidthDefinite = false;
    bool contentHeightDefinite = false;
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;
    u32 layoutOrdinal = 0;
    u32 paintOrdinal = 0;
};

struct LayoutPassStatistics final {
    usize passCount = 0;
    usize measuredNodeCount = 0;
    usize arrangedNodeCount = 0;
    usize percentMeasureFallbackCount = 0;
};

struct ResolvedLength final {
    bool hasValue = false;
    float value = 0.0F;
};

[[nodiscard]] Core::Error makeError(
    Core::ErrorCode code,
    std::string_view message,
    Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::Error{code, message, location};
}

[[nodiscard]] std::unexpected<Core::Error> fail(
    Core::ErrorCode code,
    std::string_view message,
    Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::failure(makeError(code, message, location));
}

[[nodiscard]] float normalizeFloat(float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] bool isFiniteNonNegative(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] Core::Status normalizeLayoutLength(
    UILayoutLength& length,
    bool allowAuto)
{
    switch (length.unit) {
    case UILayoutLengthUnit::Auto:
        if (!allowAuto) {
            return fail(UIErrorCode::InvalidLayout, "UI layout length cannot be Auto here");
        }
        length.value = 0.0F;
        return Core::success();
    case UILayoutLengthUnit::Px:
        if (!isFiniteNonNegative(length.value)) {
            return fail(
                UIErrorCode::InvalidLayout,
                "UI layout length must be finite and non-negative");
        }
        length.value = normalizeFloat(length.value);
        return Core::success();
    case UILayoutLengthUnit::Percent:
        if (!isFiniteNonNegative(length.value) || length.value > 100.0F) {
            return fail(
                UIErrorCode::InvalidLayout,
                "UI layout percent must be finite and within 0..100");
        }
        length.value = normalizeFloat(length.value);
        return Core::success();
    }

    return fail(UIErrorCode::InvalidLayout, "UI layout length unit is invalid");
}

[[nodiscard]] Core::Status normalizeSpacing(float& value)
{
    if (!isFiniteNonNegative(value)) {
        return fail(
            UIErrorCode::InvalidLayout,
            "UI layout spacing must be finite and non-negative");
    }
    value = normalizeFloat(value);
    return Core::success();
}

[[nodiscard]] Core::Status normalizeEdgeSpacing(UIEdgeSpacing& spacing)
{
    if (Core::Status status = normalizeSpacing(spacing.left); !status) {
        return status;
    }
    if (Core::Status status = normalizeSpacing(spacing.top); !status) {
        return status;
    }
    if (Core::Status status = normalizeSpacing(spacing.right); !status) {
        return status;
    }
    return normalizeSpacing(spacing.bottom);
}

[[nodiscard]] bool isValidFlexDirection(UIFlexDirection value) noexcept
{
    return value == UIFlexDirection::Row || value == UIFlexDirection::Column;
}

[[nodiscard]] bool isValidJustifyContent(UIJustifyContent value) noexcept
{
    return value == UIJustifyContent::Start
        || value == UIJustifyContent::Center
        || value == UIJustifyContent::End
        || value == UIJustifyContent::SpaceBetween;
}

[[nodiscard]] bool isValidAlignItems(UIAlignItems value) noexcept
{
    return value == UIAlignItems::Start
        || value == UIAlignItems::Center
        || value == UIAlignItems::End
        || value == UIAlignItems::Stretch;
}

[[nodiscard]] bool isValidPositionMode(UILayoutPositionMode value) noexcept
{
    return value == UILayoutPositionMode::InFlow
        || value == UILayoutPositionMode::AbsoluteOverlay;
}

[[nodiscard]] bool isValidVisibility(UIVisibility value) noexcept
{
    return value == UIVisibility::Visible
        || value == UIVisibility::Hidden
        || value == UIVisibility::Collapsed;
}

[[nodiscard]] Core::Result<UILayoutStyle> normalizeLayoutStyle(UILayoutStyle style)
{
    if (Core::Status status = normalizeLayoutLength(style.size.width, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.size.height, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.minMax.minWidth, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.minMax.minHeight, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.minMax.maxWidth, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.minMax.maxHeight, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeEdgeSpacing(style.margin); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeEdgeSpacing(style.padding); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.absoluteInset.left, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.absoluteInset.top, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.absoluteInset.right, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.absoluteInset.bottom, true); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeSpacing(style.flex.grow); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeSpacing(style.flex.gap.row); !status) {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeSpacing(style.flex.gap.column); !status) {
        return Core::failure(status.error());
    }
    if (!isValidFlexDirection(style.flex.direction)
        || !isValidJustifyContent(style.flex.justify)
        || !isValidAlignItems(style.flex.alignItems)
        || !isValidPositionMode(style.position)
        || !isValidVisibility(style.visibility)) {
        return fail(UIErrorCode::InvalidLayout, "UI layout enum value is invalid");
    }

    return style;
}

[[nodiscard]] ResolvedLength resolveLength(
    UILayoutLength length,
    bool basisDefinite,
    float basis,
    LayoutPassStatistics& statistics) noexcept
{
    if (length.unit == UILayoutLengthUnit::Px) {
        return ResolvedLength{.hasValue = true, .value = length.value};
    }
    if (length.unit == UILayoutLengthUnit::Percent) {
        if (basisDefinite && isFiniteNonNegative(basis)) {
            return ResolvedLength{
                .hasValue = true,
                .value = normalizeFloat(basis * (length.value * 0.01F)),
            };
        }
        ++statistics.percentMeasureFallbackCount;
    }
    return {};
}

[[nodiscard]] ResolvedLength resolveLengthNoFallbackCount(
    UILayoutLength length,
    bool basisDefinite,
    float basis) noexcept
{
    if (length.unit == UILayoutLengthUnit::Px) {
        return ResolvedLength{.hasValue = true, .value = length.value};
    }
    if (length.unit == UILayoutLengthUnit::Percent
        && basisDefinite
        && isFiniteNonNegative(basis)) {
        return ResolvedLength{
            .hasValue = true,
            .value = normalizeFloat(basis * (length.value * 0.01F)),
        };
    }
    return {};
}

[[nodiscard]] float clampWithMinMax(
    float value,
    UILayoutLength minLength,
    UILayoutLength maxLength,
    bool basisDefinite,
    float basis,
    LayoutPassStatistics& statistics) noexcept
{
    const ResolvedLength minValue =
        resolveLength(minLength, basisDefinite, basis, statistics);
    ResolvedLength maxValue =
        resolveLength(maxLength, basisDefinite, basis, statistics);
    if (minValue.hasValue && maxValue.hasValue && maxValue.value < minValue.value) {
        maxValue.value = minValue.value;
    }
    if (maxValue.hasValue) {
        value = (std::min)(value, maxValue.value);
    }
    if (minValue.hasValue) {
        value = (std::max)(value, minValue.value);
    }
    return normalizeFloat((std::max)(0.0F, value));
}

[[nodiscard]] float horizontalMargin(const UIEdgeSpacing& margin) noexcept
{
    return margin.left + margin.right;
}

[[nodiscard]] float verticalMargin(const UIEdgeSpacing& margin) noexcept
{
    return margin.top + margin.bottom;
}

[[nodiscard]] UILogicalRect intersectRects(UILogicalRect first, UILogicalRect second) noexcept
{
    const float left = (std::max)(first.x, second.x);
    const float top = (std::max)(first.y, second.y);
    const float right = (std::min)(first.right(), second.right());
    const float bottom = (std::min)(first.bottom(), second.bottom());
    return UILogicalRect{
        .x = normalizeFloat(left),
        .y = normalizeFloat(top),
        .width = normalizeFloat((std::max)(0.0F, right - left)),
        .height = normalizeFloat((std::max)(0.0F, bottom - top)),
    };
}

[[nodiscard]] UIVisibility combineVisibility(
    UIVisibility parent,
    UIVisibility local) noexcept
{
    if (parent == UIVisibility::Collapsed || local == UIVisibility::Collapsed) {
        return UIVisibility::Collapsed;
    }
    if (parent == UIVisibility::Hidden || local == UIVisibility::Hidden) {
        return UIVisibility::Hidden;
    }
    return UIVisibility::Visible;
}

[[nodiscard]] Core::Result<NormalizedCapacityConfig> normalizeCapacity(
    UIContextCapacityConfig config)
{
    if (Core::Status status = validateUIContextCapacityConfig(config); !status) {
        return Core::failure(status.error());
    }
    const usize dirtyQueueCapacity = config.dirtyQueueCapacity == 0
        ? config.nodeCapacity
        : config.dirtyQueueCapacity;
    const usize layoutSnapshotCapacity = config.layoutSnapshotCapacity == 0
        ? config.nodeCapacity
        : config.layoutSnapshotCapacity;
    const usize hitSnapshotCapacity = config.hitSnapshotCapacity == 0
        ? config.nodeCapacity
        : config.hitSnapshotCapacity;
    const usize routePathCapacity = config.routePathCapacity == 0
        ? config.nodeCapacity
        : config.routePathCapacity;
    const usize routedPointerListenerCapacity =
        config.routedPointerListenerCapacity == 0
        ? config.nodeCapacity
        : config.routedPointerListenerCapacity;
    return NormalizedCapacityConfig{
        .nodeCapacity = config.nodeCapacity,
        .rootCapacity = config.rootCapacity,
        .dirtyQueueCapacity = dirtyQueueCapacity,
        .layoutSnapshotCapacity = layoutSnapshotCapacity,
        .hitSnapshotCapacity = hitSnapshotCapacity,
        .routePathCapacity = routePathCapacity,
        .routedPointerListenerCapacity = routedPointerListenerCapacity,
    };
}

[[nodiscard]] bool sameNode(UINodeId left, UINodeId right) noexcept
{
    return left == right;
}

[[nodiscard]] bool isValidPointerHitPolicy(UIPointerHitPolicy policy) noexcept
{
    switch (policy) {
    case UIPointerHitPolicy::Ignore:
    case UIPointerHitPolicy::Targetable:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidRoutedPointerEventKind(
    UIRoutedPointerEventKind kind) noexcept
{
    switch (kind) {
    case UIRoutedPointerEventKind::Move:
    case UIRoutedPointerEventKind::ButtonDown:
    case UIRoutedPointerEventKind::ButtonUp:
    case UIRoutedPointerEventKind::Wheel:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidEventPhaseMask(UIEventPhaseMask phases) noexcept
{
    const auto bits = static_cast<u8>(phases);
    const auto allBits = static_cast<u8>(UIEventPhaseMask::All);
    return bits != 0 && (bits & static_cast<u8>(~allBits)) == 0;
}

[[nodiscard]] UIEventPhaseMask phaseMaskFor(UIEventPhase phase) noexcept
{
    switch (phase) {
    case UIEventPhase::Capture:
        return UIEventPhaseMask::Capture;
    case UIEventPhase::Target:
        return UIEventPhaseMask::Target;
    case UIEventPhase::Bubble:
        return UIEventPhaseMask::Bubble;
    }
    return UIEventPhaseMask::None;
}

[[nodiscard]] bool containsPointHalfOpen(
    UILogicalRect rect,
    UILogicalPoint point) noexcept
{
    return rect.width > 0.0F
        && rect.height > 0.0F
        && point.x >= rect.x
        && point.y >= rect.y
        && point.x < rect.right()
        && point.y < rect.bottom();
}

} // namespace

struct UIContext::Impl final {
    Platform::WindowId ownerWindow{};
    UIContextCapacityConfig capacityConfig{};
    std::thread::id ownerThreadId{};
    std::shared_ptr<Detail::UIContextLifetimeControl> lifetime;
    NodePool nodes;
    std::pmr::vector<UINodeId> idsByIndex;
    std::pmr::vector<UILayoutStyle> layoutStylesByIndex;
    std::pmr::vector<UIPointerHitPolicy> pointerHitPoliciesByIndex;
    std::pmr::vector<UIDirty> dirtyByIndex;
    std::pmr::vector<u8> dirtyQueuedByIndex;
    std::pmr::vector<UINodeId> dirtyQueue;
    std::pmr::vector<LayoutScratchState> layoutScratchByIndex;
    std::pmr::vector<u32> layoutOrderScratch;
    std::pmr::vector<u32> hitEntryIndexByNodeIndex;
    std::pmr::vector<u32> routedPointerListenerHeadByNodeIndex;
    std::pmr::vector<u32> routedPointerListenerTailByNodeIndex;
    std::pmr::vector<RoutedPointerListenerRecord> routedPointerListeners;
    std::pmr::vector<u32> routePathScratch;
    std::pmr::vector<u32> inactiveRoutedPointerListenerIndices;
    std::array<std::pmr::vector<UICommittedNodeEntry>, 2> committedBuffers;
    std::array<std::pmr::vector<UICommittedLayoutEntry>, 2> committedLayoutBuffers;
    std::array<std::pmr::vector<UICommittedHitEntry>, 2> committedHitBuffers;
    std::vector<UINodeId> deferredRootDestroyBuffer;
    std::vector<Detail::DeferredRoutedPointerListenerRelease>
        deferredRoutedPointerListenerReleaseBuffer;
    usize publishedBufferIndex = 0;
    usize publishedLayoutBufferIndex = 0;
    usize publishedHitBufferIndex = 0;
    u64 committedRevision = 0;
    u64 committedLayoutRevision = 0;
    u64 committedLayoutStructureRevision = 0;
    u64 committedHitRevision = 0;
    u64 committedHitStructureRevision = 0;
    u64 committedHitLayoutRevision = 0;
    u64 committedHitPaintOrderRevision = 0;
    UILogicalSize committedViewportSize{};
    bool hasCommittedViewport = false;
    usize liveRootCount = 0;
    u32 firstRootIndex = InvalidNodeIndex;
    u32 lastRootIndex = InvalidNodeIndex;
    bool structureDirty = false;
    bool layoutDirty = false;
    bool hitDirty = false;
    LayoutPassStatistics lastLayoutPass{};
    usize dirtyQueueHighWater = 0;
    usize committedHitTargetCount = 0;
    usize lastHitRebuildCount = 0;
    u32 freeRoutedPointerListenerHead = InvalidRoutedPointerListenerIndex;
    usize activeRoutedPointerListenerCount = 0;
    usize routedPointerListenerHighWater = 0;
    u64 routedPointerListenerRegistrationSerial = 0;
    usize routeDispatchDepth = 0;
    usize listenerCallbackCleanupDepth = 0;
    bool reclaimingInactiveRoutedPointerListeners = false;

    Impl(
        Platform::WindowId owner,
        UIContextCapacityConfig capacities,
        std::thread::id threadId,
        std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl,
        NodePool&& nodePool,
        std::pmr::memory_resource& resource)
        : ownerWindow(owner),
          capacityConfig(capacities),
          ownerThreadId(threadId),
          lifetime(std::move(lifetimeControl)),
          nodes(std::move(nodePool)),
          idsByIndex(&resource),
          layoutStylesByIndex(&resource),
          pointerHitPoliciesByIndex(&resource),
          dirtyByIndex(&resource),
          dirtyQueuedByIndex(&resource),
          dirtyQueue(&resource),
          layoutScratchByIndex(&resource),
          layoutOrderScratch(&resource),
          hitEntryIndexByNodeIndex(&resource),
          routedPointerListenerHeadByNodeIndex(&resource),
          routedPointerListenerTailByNodeIndex(&resource),
          routedPointerListeners(&resource),
          routePathScratch(&resource),
          inactiveRoutedPointerListenerIndices(&resource),
          committedBuffers{
              std::pmr::vector<UICommittedNodeEntry>(&resource),
              std::pmr::vector<UICommittedNodeEntry>(&resource)},
          committedLayoutBuffers{
              std::pmr::vector<UICommittedLayoutEntry>(&resource),
              std::pmr::vector<UICommittedLayoutEntry>(&resource)},
          committedHitBuffers{
              std::pmr::vector<UICommittedHitEntry>(&resource),
              std::pmr::vector<UICommittedHitEntry>(&resource)}
    {
    }

    [[nodiscard]] static Core::Result<std::unique_ptr<Impl>> Create(
        Platform::WindowId ownerWindow,
        NormalizedCapacityConfig normalized,
        std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl,
        std::pmr::memory_resource& resource)
    {
        auto poolResult = NodePool::Create(normalized.nodeCapacity, resource);
        if (!poolResult) {
            const Core::Error& error = poolResult.error();
            if (error.code == Core::CoreErrorCode::CapacityExceeded) {
                return fail(
                    UIErrorCode::CapacityExceeded,
                    "UI node pool capacity could not be reserved");
            }
            return Core::failure(error);
        }

        UIContextCapacityConfig capacities{
            .nodeCapacity = normalized.nodeCapacity,
            .rootCapacity = normalized.rootCapacity,
            .dirtyQueueCapacity = normalized.dirtyQueueCapacity,
            .layoutSnapshotCapacity = normalized.layoutSnapshotCapacity,
            .hitSnapshotCapacity = normalized.hitSnapshotCapacity,
            .routePathCapacity = normalized.routePathCapacity,
            .routedPointerListenerCapacity = normalized.routedPointerListenerCapacity,
        };

        auto impl = std::unique_ptr<Impl>(new Impl(
            ownerWindow,
            capacities,
            std::this_thread::get_id(),
            std::move(lifetimeControl),
            std::move(*poolResult),
            resource));
        impl->idsByIndex.resize(normalized.nodeCapacity);
        impl->layoutStylesByIndex.resize(normalized.nodeCapacity);
        impl->pointerHitPoliciesByIndex.resize(
            normalized.nodeCapacity,
            UIPointerHitPolicy::Ignore);
        impl->dirtyByIndex.resize(normalized.nodeCapacity, UIDirty::None);
        impl->dirtyQueuedByIndex.resize(normalized.nodeCapacity, 0);
        impl->dirtyQueue.reserve(normalized.dirtyQueueCapacity);
        impl->layoutScratchByIndex.resize(normalized.nodeCapacity);
        impl->layoutOrderScratch.reserve(normalized.nodeCapacity);
        impl->hitEntryIndexByNodeIndex.resize(
            normalized.nodeCapacity,
            InvalidUIHitEntryIndex);
        impl->routedPointerListenerHeadByNodeIndex.resize(
            normalized.nodeCapacity,
            InvalidRoutedPointerListenerIndex);
        impl->routedPointerListenerTailByNodeIndex.resize(
            normalized.nodeCapacity,
            InvalidRoutedPointerListenerIndex);
        impl->routedPointerListeners.resize(normalized.routedPointerListenerCapacity);
        for (usize listenerIndex = 0;
             listenerIndex < normalized.routedPointerListenerCapacity;
             ++listenerIndex) {
            RoutedPointerListenerRecord& listener =
                impl->routedPointerListeners[listenerIndex];
            listener.nextFreeIndex = listenerIndex + 1
                    < normalized.routedPointerListenerCapacity
                ? static_cast<u32>(listenerIndex + 1)
                : InvalidRoutedPointerListenerIndex;
        }
        impl->freeRoutedPointerListenerHead =
            normalized.routedPointerListenerCapacity == 0
            ? InvalidRoutedPointerListenerIndex
            : 0;
        impl->routePathScratch.reserve(normalized.routePathCapacity);
        impl->inactiveRoutedPointerListenerIndices.reserve(
            normalized.routedPointerListenerCapacity);
        impl->committedBuffers[0].reserve(normalized.nodeCapacity);
        impl->committedBuffers[1].reserve(normalized.nodeCapacity);
        impl->committedLayoutBuffers[0].reserve(normalized.layoutSnapshotCapacity);
        impl->committedLayoutBuffers[1].reserve(normalized.layoutSnapshotCapacity);
        impl->committedHitBuffers[0].reserve(normalized.hitSnapshotCapacity);
        impl->committedHitBuffers[1].reserve(normalized.hitSnapshotCapacity);
        impl->deferredRootDestroyBuffer.reserve(normalized.rootCapacity);
        impl->deferredRoutedPointerListenerReleaseBuffer.reserve(
            normalized.routedPointerListenerCapacity);
        return impl;
    }

    void detachLifetime(UIContext* context) noexcept
    {
        if (!lifetime) {
            return;
        }
        const std::scoped_lock lock(lifetime->mutex);
        if (lifetime->context == context) {
            lifetime->context = nullptr;
            lifetime->deferredRootDestroys.clear();
            lifetime->hasDeferredRootDestroys.store(false, std::memory_order_release);
            lifetime->deferredRoutedPointerListenerReleases.clear();
            lifetime->hasDeferredRoutedPointerListenerReleases.store(
                false,
                std::memory_order_release);
            for (Detail::RoutedPointerListenerTokenState& state
                 : lifetime->routedPointerListenerStates) {
                state.active = false;
            }
        }
    }

    void buildCommittedStructure(std::pmr::vector<UICommittedNodeEntry>& output) const noexcept
    {
        output.clear();
        u32 ordinal = 0;
        u32 rootIndex = firstRootIndex;
        while (rootIndex != InvalidNodeIndex) {
            const NodeRecord* root = recordByIndex(rootIndex);
            const u32 nextRootIndex = root == nullptr
                ? InvalidNodeIndex
                : root->nextSiblingIndex;
            appendCommittedTree(rootIndex, ordinal, output);
            rootIndex = nextRootIndex;
        }
    }

    void appendLayoutOrderTree(u32 index, std::pmr::vector<u32>& output) const noexcept
    {
        const u32 rootIndex = index;
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex) {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr) {
                return;
            }

            output.push_back(currentIndex);

            if (record->firstChildIndex != InvalidNodeIndex) {
                currentIndex = record->firstChildIndex;
                continue;
            }

            while (currentIndex != rootIndex) {
                record = recordByIndex(currentIndex);
                if (record == nullptr) {
                    return;
                }
                if (record->nextSiblingIndex != InvalidNodeIndex) {
                    currentIndex = record->nextSiblingIndex;
                    break;
                }
                currentIndex = record->parentIndex;
            }
            if (currentIndex == rootIndex) {
                currentIndex = InvalidNodeIndex;
            }
        }
    }

    void buildLayoutOrder(std::pmr::vector<u32>& output) const noexcept
    {
        output.clear();
        u32 rootIndex = firstRootIndex;
        while (rootIndex != InvalidNodeIndex) {
            const NodeRecord* root = recordByIndex(rootIndex);
            const u32 nextRootIndex = root == nullptr
                ? InvalidNodeIndex
                : root->nextSiblingIndex;
            appendLayoutOrderTree(rootIndex, output);
            rootIndex = nextRootIndex;
        }
    }

    void prepareLayoutState(
        UILogicalSize viewportSize,
        const std::pmr::vector<u32>& order) noexcept
    {
        for (const u32 index : order) {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr) {
                continue;
            }
            const UILayoutStyle& style = layoutStylesByIndex[index];
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            scratch = {};

            if (record->parentIndex == InvalidNodeIndex) {
                scratch.effectiveVisibility = style.visibility;
                scratch.parentContentWidthDefinite = true;
                scratch.parentContentHeightDefinite = true;
                scratch.parentContentWidth = viewportSize.width;
                scratch.parentContentHeight = viewportSize.height;
            } else {
                const LayoutScratchState& parentScratch =
                    layoutScratchByIndex[record->parentIndex];
                scratch.effectiveVisibility =
                    combineVisibility(parentScratch.effectiveVisibility, style.visibility);
                scratch.parentContentWidthDefinite = parentScratch.contentWidthDefinite;
                scratch.parentContentHeightDefinite = parentScratch.contentHeightDefinite;
                scratch.parentContentWidth = parentScratch.contentWidth;
                scratch.parentContentHeight = parentScratch.contentHeight;
            }

            const ResolvedLength width = resolveLengthNoFallbackCount(
                style.size.width,
                scratch.parentContentWidthDefinite,
                scratch.parentContentWidth);
            const ResolvedLength height = resolveLengthNoFallbackCount(
                style.size.height,
                scratch.parentContentHeightDefinite,
                scratch.parentContentHeight);
            const bool isRoot = record->parentIndex == InvalidNodeIndex;
            scratch.contentWidthDefinite = width.hasValue || isRoot;
            scratch.contentHeightDefinite = height.hasValue || isRoot;
            const float outerWidth = width.hasValue ? width.value : viewportSize.width;
            const float outerHeight = height.hasValue ? height.value : viewportSize.height;
            scratch.contentWidth = scratch.contentWidthDefinite
                ? (std::max)(0.0F, outerWidth - horizontalMargin(style.padding))
                : 0.0F;
            scratch.contentHeight = scratch.contentHeightDefinite
                ? (std::max)(0.0F, outerHeight - verticalMargin(style.padding))
                : 0.0F;
        }
    }

    [[nodiscard]] float resolvedWidth(
        const UILayoutStyle& style,
        const LayoutScratchState& scratch,
        LayoutPassStatistics& statistics) const noexcept
    {
        const ResolvedLength value = resolveLength(
            style.size.width,
            scratch.parentContentWidthDefinite,
            scratch.parentContentWidth,
            statistics);
        return value.hasValue ? value.value : -1.0F;
    }

    [[nodiscard]] float resolvedHeight(
        const UILayoutStyle& style,
        const LayoutScratchState& scratch,
        LayoutPassStatistics& statistics) const noexcept
    {
        const ResolvedLength value = resolveLength(
            style.size.height,
            scratch.parentContentHeightDefinite,
            scratch.parentContentHeight,
            statistics);
        return value.hasValue ? value.value : -1.0F;
    }

    [[nodiscard]] float clampWidth(
        float value,
        const UILayoutStyle& style,
        const LayoutScratchState& scratch,
        LayoutPassStatistics& statistics) const noexcept
    {
        return clampWithMinMax(
            value,
            style.minMax.minWidth,
            style.minMax.maxWidth,
            scratch.parentContentWidthDefinite,
            scratch.parentContentWidth,
            statistics);
    }

    [[nodiscard]] float clampHeight(
        float value,
        const UILayoutStyle& style,
        const LayoutScratchState& scratch,
        LayoutPassStatistics& statistics) const noexcept
    {
        return clampWithMinMax(
            value,
            style.minMax.minHeight,
            style.minMax.maxHeight,
            scratch.parentContentHeightDefinite,
            scratch.parentContentHeight,
            statistics);
    }

    void measureLayout(
        UILogicalSize viewportSize,
        const std::pmr::vector<u32>& order,
        LayoutPassStatistics& statistics) noexcept
    {
        for (usize reverseIndex = order.size(); reverseIndex > 0; --reverseIndex) {
            const u32 index = order[reverseIndex - 1];
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr) {
                continue;
            }
            const UILayoutStyle& style = layoutStylesByIndex[index];
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            ++statistics.measuredNodeCount;

            if (scratch.effectiveVisibility == UIVisibility::Collapsed) {
                scratch.measuredSize = {};
                continue;
            }

            float autoContentWidth = 0.0F;
            float autoContentHeight = 0.0F;
            usize flowChildCount = 0;

            u32 childIndex = record->firstChildIndex;
            while (childIndex != InvalidNodeIndex) {
                const NodeRecord* childRecord = recordByIndex(childIndex);
                if (childRecord == nullptr) {
                    break;
                }
                const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
                const LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
                if (childScratch.effectiveVisibility != UIVisibility::Collapsed
                    && childStyle.position == UILayoutPositionMode::InFlow) {
                    const float childOuterWidth =
                        childScratch.measuredSize.width + horizontalMargin(childStyle.margin);
                    const float childOuterHeight =
                        childScratch.measuredSize.height + verticalMargin(childStyle.margin);
                    if (style.flex.direction == UIFlexDirection::Row) {
                        if (flowChildCount > 0) {
                            autoContentWidth += style.flex.gap.column;
                        }
                        autoContentWidth += childOuterWidth;
                        autoContentHeight = (std::max)(autoContentHeight, childOuterHeight);
                    } else {
                        autoContentWidth = (std::max)(autoContentWidth, childOuterWidth);
                        if (flowChildCount > 0) {
                            autoContentHeight += style.flex.gap.row;
                        }
                        autoContentHeight += childOuterHeight;
                    }
                    ++flowChildCount;
                }
                childIndex = childRecord->nextSiblingIndex;
            }

            float outerWidth = resolvedWidth(style, scratch, statistics);
            float outerHeight = resolvedHeight(style, scratch, statistics);
            if (outerWidth < 0.0F) {
                outerWidth = record->parentIndex == InvalidNodeIndex
                    ? (std::max)(
                        autoContentWidth + horizontalMargin(style.padding),
                        viewportSize.width)
                    : autoContentWidth + horizontalMargin(style.padding);
            }
            if (outerHeight < 0.0F) {
                outerHeight = record->parentIndex == InvalidNodeIndex
                    ? (std::max)(
                        autoContentHeight + verticalMargin(style.padding),
                        viewportSize.height)
                    : autoContentHeight + verticalMargin(style.padding);
            }
            outerWidth = clampWidth(outerWidth, style, scratch, statistics);
            outerHeight = clampHeight(outerHeight, style, scratch, statistics);

            scratch.measuredSize = UILogicalSize{
                .width = normalizeFloat(outerWidth),
                .height = normalizeFloat(outerHeight),
            };
        }
    }

    [[nodiscard]] bool isCrossAxisAuto(
        const UILayoutStyle& style,
        UIFlexDirection direction) const noexcept
    {
        return direction == UIFlexDirection::Row
            ? style.size.height.isAuto()
            : style.size.width.isAuto();
    }

    void assignLayoutRect(
        u32 index,
        UILogicalRect worldRect,
        UILogicalRect parentWorldRect,
        UILogicalRect viewportRect,
        u32 ordinal) noexcept
    {
        LayoutScratchState& scratch = layoutScratchByIndex[index];
        const UILayoutStyle& style = layoutStylesByIndex[index];
        if (scratch.effectiveVisibility == UIVisibility::Collapsed) {
            worldRect.width = 0.0F;
            worldRect.height = 0.0F;
        }
        scratch.worldRect = worldRect;
        scratch.localRect = UILogicalRect{
            .x = normalizeFloat(worldRect.x - parentWorldRect.x),
            .y = normalizeFloat(worldRect.y - parentWorldRect.y),
            .width = normalizeFloat(worldRect.width),
            .height = normalizeFloat(worldRect.height),
        };
        scratch.effectiveClip = scratch.effectiveVisibility == UIVisibility::Collapsed
            ? UILogicalRect{}
            : intersectRects(viewportRect, worldRect);
        scratch.contentWidthDefinite = true;
        scratch.contentHeightDefinite = true;
        scratch.contentWidth = normalizeFloat(
            (std::max)(0.0F, worldRect.width - horizontalMargin(style.padding)));
        scratch.contentHeight = normalizeFloat(
            (std::max)(0.0F, worldRect.height - verticalMargin(style.padding)));
        scratch.layoutOrdinal = ordinal;
        scratch.paintOrdinal = ordinal;
    }

    void refreshMeasuredSizeForParentContent(
        u32 childIndex,
        UILogicalRect parentContentRect,
        LayoutPassStatistics& statistics) noexcept
    {
        const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
        LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
        childScratch.parentContentWidthDefinite = true;
        childScratch.parentContentHeightDefinite = true;
        childScratch.parentContentWidth = parentContentRect.width;
        childScratch.parentContentHeight = parentContentRect.height;

        const float resolvedOuterWidth = resolvedWidth(childStyle, childScratch, statistics);
        const float resolvedOuterHeight = resolvedHeight(childStyle, childScratch, statistics);
        const float outerWidth = resolvedOuterWidth >= 0.0F
            ? resolvedOuterWidth
            : childScratch.measuredSize.width;
        const float outerHeight = resolvedOuterHeight >= 0.0F
            ? resolvedOuterHeight
            : childScratch.measuredSize.height;
        childScratch.measuredSize = UILogicalSize{
            .width = clampWidth(outerWidth, childStyle, childScratch, statistics),
            .height = clampHeight(outerHeight, childStyle, childScratch, statistics),
        };
    }

    [[nodiscard]] float resolveInset(
        UILayoutLength length,
        float basis,
        LayoutPassStatistics& statistics) const noexcept
    {
        const ResolvedLength resolved = resolveLength(length, true, basis, statistics);
        return resolved.hasValue ? resolved.value : -1.0F;
    }

    void arrangeAbsoluteChild(
        u32 childIndex,
        UILogicalRect parentContentRect,
        UILogicalRect parentWorldRect,
        UILogicalRect viewportRect,
        LayoutPassStatistics& statistics) noexcept
    {
        const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
        LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
        refreshMeasuredSizeForParentContent(childIndex, parentContentRect, statistics);
        const float left = resolveInset(
            childStyle.absoluteInset.left,
            parentContentRect.width,
            statistics);
        const float right = resolveInset(
            childStyle.absoluteInset.right,
            parentContentRect.width,
            statistics);
        const float top = resolveInset(
            childStyle.absoluteInset.top,
            parentContentRect.height,
            statistics);
        const float bottom = resolveInset(
            childStyle.absoluteInset.bottom,
            parentContentRect.height,
            statistics);

        float width = childScratch.measuredSize.width;
        float height = childScratch.measuredSize.height;
        if (childStyle.size.width.isAuto() && left >= 0.0F && right >= 0.0F) {
            width = (std::max)(0.0F, parentContentRect.width - left - right);
        }
        if (childStyle.size.height.isAuto() && top >= 0.0F && bottom >= 0.0F) {
            height = (std::max)(0.0F, parentContentRect.height - top - bottom);
        }
        width = clampWidth(width, childStyle, childScratch, statistics);
        height = clampHeight(height, childStyle, childScratch, statistics);

        const float x = left >= 0.0F
            ? parentContentRect.x + left
            : (right >= 0.0F ? parentContentRect.right() - right - width : parentContentRect.x);
        const float y = top >= 0.0F
            ? parentContentRect.y + top
            : (bottom >= 0.0F ? parentContentRect.bottom() - bottom - height : parentContentRect.y);

        assignLayoutRect(
            childIndex,
            UILogicalRect{
                .x = normalizeFloat(x),
                .y = normalizeFloat(y),
                .width = normalizeFloat((std::max)(0.0F, width)),
                .height = normalizeFloat((std::max)(0.0F, height)),
            },
            parentWorldRect,
            viewportRect,
            0);
    }

    void arrangeChildren(
        u32 parentIndex,
        UILogicalRect viewportRect,
        LayoutPassStatistics& statistics) noexcept
    {
        const NodeRecord* parentRecord = recordByIndex(parentIndex);
        if (parentRecord == nullptr) {
            return;
        }
        const UILayoutStyle& parentStyle = layoutStylesByIndex[parentIndex];
        const LayoutScratchState& parentScratch = layoutScratchByIndex[parentIndex];
        const UILogicalRect parentWorldRect = parentScratch.worldRect;
        const UILogicalRect parentContentRect{
            .x = normalizeFloat(parentWorldRect.x + parentStyle.padding.left),
            .y = normalizeFloat(parentWorldRect.y + parentStyle.padding.top),
            .width = normalizeFloat(
                (std::max)(0.0F, parentWorldRect.width - horizontalMargin(parentStyle.padding))),
            .height = normalizeFloat(
                (std::max)(0.0F, parentWorldRect.height - verticalMargin(parentStyle.padding))),
        };

        if (parentScratch.effectiveVisibility == UIVisibility::Collapsed) {
            u32 collapsedChild = parentRecord->firstChildIndex;
            while (collapsedChild != InvalidNodeIndex) {
                const NodeRecord* childRecord = recordByIndex(collapsedChild);
                if (childRecord == nullptr) {
                    break;
                }
                assignLayoutRect(collapsedChild, parentContentRect, parentWorldRect, viewportRect, 0);
                collapsedChild = childRecord->nextSiblingIndex;
            }
            return;
        }

        const bool row = parentStyle.flex.direction == UIFlexDirection::Row;
        const float contentMain = row ? parentContentRect.width : parentContentRect.height;
        const float contentCross = row ? parentContentRect.height : parentContentRect.width;
        const float configuredGap = row ? parentStyle.flex.gap.column : parentStyle.flex.gap.row;

        usize flowChildCount = 0;
        float totalMain = 0.0F;
        double totalGrow = 0.0;
        u32 childIndex = parentRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex) {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr) {
                break;
            }
            const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
            LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
            if (childScratch.effectiveVisibility != UIVisibility::Collapsed
                && childStyle.position == UILayoutPositionMode::InFlow) {
                refreshMeasuredSizeForParentContent(
                    childIndex,
                    parentContentRect,
                    statistics);
                if (flowChildCount > 0) {
                    totalMain += configuredGap;
                }
                totalMain += row
                    ? childScratch.measuredSize.width + horizontalMargin(childStyle.margin)
                    : childScratch.measuredSize.height + verticalMargin(childStyle.margin);
                totalGrow += static_cast<double>(childStyle.flex.grow);
                ++flowChildCount;
            }
            childIndex = childRecord->nextSiblingIndex;
        }

        const float freeSpace = contentMain - totalMain;
        const float growSpace = totalGrow > 0.0 ? (std::max)(0.0F, freeSpace) : 0.0F;
        float mainOffset = 0.0F;
        float gap = configuredGap;
        if (totalGrow == 0.0 && freeSpace > 0.0F) {
            switch (parentStyle.flex.justify) {
            case UIJustifyContent::Center:
                mainOffset = freeSpace * 0.5F;
                break;
            case UIJustifyContent::End:
                mainOffset = freeSpace;
                break;
            case UIJustifyContent::SpaceBetween:
                if (flowChildCount > 1) {
                    gap += freeSpace / static_cast<float>(flowChildCount - 1);
                }
                break;
            case UIJustifyContent::Start:
                break;
            }
        }

        childIndex = parentRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex) {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr) {
                break;
            }
            const u32 currentChild = childIndex;
            childIndex = childRecord->nextSiblingIndex;
            const UILayoutStyle& childStyle = layoutStylesByIndex[currentChild];
            LayoutScratchState& childScratch = layoutScratchByIndex[currentChild];

            if (childScratch.effectiveVisibility == UIVisibility::Collapsed) {
                assignLayoutRect(currentChild, parentContentRect, parentWorldRect, viewportRect, 0);
                continue;
            }
            if (childStyle.position == UILayoutPositionMode::AbsoluteOverlay) {
                arrangeAbsoluteChild(
                    currentChild,
                    parentContentRect,
                    parentWorldRect,
                    viewportRect,
                    statistics);
                continue;
            }

            float width = childScratch.measuredSize.width;
            float height = childScratch.measuredSize.height;
            if (totalGrow > 0.0 && childStyle.flex.grow > 0.0F) {
                const double growRatio =
                    static_cast<double>(childStyle.flex.grow) / totalGrow;
                const float share = growSpace * static_cast<float>(growRatio);
                if (row) {
                    width += share;
                } else {
                    height += share;
                }
            }

            const float crossAvailable = (std::max)(
                0.0F,
                contentCross - (row ? verticalMargin(childStyle.margin) : horizontalMargin(childStyle.margin)));
            if (parentStyle.flex.alignItems == UIAlignItems::Stretch
                && isCrossAxisAuto(childStyle, parentStyle.flex.direction)) {
                if (row) {
                    height = crossAvailable;
                } else {
                    width = crossAvailable;
                }
            }
            width = clampWidth(width, childStyle, childScratch, statistics);
            height = clampHeight(height, childStyle, childScratch, statistics);

            const float childMainSize = row ? width : height;
            const float childCrossSize = row ? height : width;
            const float mainBefore = row ? childStyle.margin.left : childStyle.margin.top;
            const float mainAfter = row ? childStyle.margin.right : childStyle.margin.bottom;
            const float crossBefore = row ? childStyle.margin.top : childStyle.margin.left;
            const float crossAfter = row ? childStyle.margin.bottom : childStyle.margin.right;

            float crossOffset = crossBefore;
            const float crossFree =
                contentCross - childCrossSize - crossBefore - crossAfter;
            if (crossFree > 0.0F) {
                switch (parentStyle.flex.alignItems) {
                case UIAlignItems::Center:
                    crossOffset = crossBefore + crossFree * 0.5F;
                    break;
                case UIAlignItems::End:
                    crossOffset = crossBefore + crossFree;
                    break;
                case UIAlignItems::Stretch:
                case UIAlignItems::Start:
                    break;
                }
            }

            const float x = row
                ? parentContentRect.x + mainOffset + mainBefore
                : parentContentRect.x + crossOffset;
            const float y = row
                ? parentContentRect.y + crossOffset
                : parentContentRect.y + mainOffset + mainBefore;
            assignLayoutRect(
                currentChild,
                UILogicalRect{
                    .x = normalizeFloat(x),
                    .y = normalizeFloat(y),
                    .width = normalizeFloat((std::max)(0.0F, width)),
                    .height = normalizeFloat((std::max)(0.0F, height)),
                },
                parentWorldRect,
                viewportRect,
                0);

            mainOffset += childMainSize + mainBefore + mainAfter + gap;
        }
    }

    void arrangeLayout(
        UILogicalSize viewportSize,
        const std::pmr::vector<u32>& order,
        LayoutPassStatistics& statistics) noexcept
    {
        const UILogicalRect viewportRect{
            .x = 0.0F,
            .y = 0.0F,
            .width = viewportSize.width,
            .height = viewportSize.height,
        };

        u32 ordinal = 0;
        for (const u32 index : order) {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr) {
                continue;
            }
            ++statistics.arrangedNodeCount;
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            if (record->parentIndex == InvalidNodeIndex) {
                assignLayoutRect(
                    index,
                    UILogicalRect{
                        .x = 0.0F,
                        .y = 0.0F,
                        .width = scratch.measuredSize.width,
                        .height = scratch.measuredSize.height,
                    },
                    viewportRect,
                    viewportRect,
                    ordinal);
            }
            scratch.layoutOrdinal = ordinal;
            scratch.paintOrdinal = ordinal;
            arrangeChildren(index, viewportRect, statistics);
            ++ordinal;
        }
    }

    void buildCommittedLayout(
        std::pmr::vector<UICommittedLayoutEntry>& output,
        const std::pmr::vector<u32>& order) const noexcept
    {
        output.clear();
        u32 ordinal = 0;
        for (const u32 index : order) {
            const LayoutScratchState& scratch = layoutScratchByIndex[index];
            output.push_back(UICommittedLayoutEntry{
                .node = idForIndex(index),
                .localRect = scratch.localRect,
                .worldRect = scratch.worldRect,
                .effectiveClip = scratch.effectiveClip,
                .effectiveVisibility = scratch.effectiveVisibility,
                .layoutOrdinal = ordinal,
                .paintOrdinal = ordinal,
            });
            ++ordinal;
        }
    }

    [[nodiscard]] Core::Result<usize> buildCommittedHit(
        std::pmr::vector<UICommittedHitEntry>& output,
        std::span<const UICommittedLayoutEntry> layoutEntries)
    {
        usize visibleEntryCount = 0;
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries) {
            if (layoutEntry.effectiveVisibility == UIVisibility::Visible) {
                ++visibleEntryCount;
            }
        }
        if (visibleEntryCount > capacityConfig.hitSnapshotCapacity) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI committed hit snapshot capacity has been exhausted");
        }

        output.clear();
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries) {
            hitEntryIndexByNodeIndex[layoutEntry.node.index()] = InvalidUIHitEntryIndex;
        }
        usize targetCount = 0;

        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries) {
            if (layoutEntry.effectiveVisibility != UIVisibility::Visible) {
                continue;
            }
            if (!contains(layoutEntry.node)) {
                return fail(
                    UIErrorCode::InvalidNode,
                    "UI hit snapshot layout references a stale node");
            }

            const u32 nodeIndex = layoutEntry.node.index();
            const NodeRecord* record = recordByIndex(nodeIndex);
            if (record == nullptr) {
                return fail(
                    UIErrorCode::InvalidNode,
                    "UI hit snapshot node record is unavailable");
            }

            const UIPointerHitPolicy policy = pointerHitPoliciesByIndex[nodeIndex];
            if (!isValidPointerHitPolicy(policy)) {
                return fail(
                    UIErrorCode::InvalidPointerPolicy,
                    "UI hit snapshot contains an invalid pointer policy");
            }

            const u32 entryIndex = static_cast<u32>(output.size());
            u32 parentEntryIndex = InvalidUIHitEntryIndex;
            u32 rootEntryIndex = entryIndex;
            if (record->parentIndex != InvalidNodeIndex) {
                parentEntryIndex = hitEntryIndexByNodeIndex[record->parentIndex];
                rootEntryIndex = hitEntryIndexByNodeIndex[record->rootIndex];
                if (parentEntryIndex == InvalidUIHitEntryIndex
                    || rootEntryIndex == InvalidUIHitEntryIndex) {
                    return fail(
                        UIErrorCode::InvalidNode,
                        "UI hit snapshot visible ancestry is incomplete");
                }
            }

            output.push_back(UICommittedHitEntry{
                .node = layoutEntry.node,
                .parentEntryIndex = parentEntryIndex,
                .rootEntryIndex = rootEntryIndex,
                .worldRect = layoutEntry.worldRect,
                .effectiveClip = layoutEntry.effectiveClip,
                .paintOrdinal = layoutEntry.paintOrdinal,
                .policy = policy,
            });
            hitEntryIndexByNodeIndex[nodeIndex] = entryIndex;
            if (policy == UIPointerHitPolicy::Targetable) {
                ++targetCount;
            }
        }
        return targetCount;
    }

    [[nodiscard]] static bool isFiniteLayoutRect(UILogicalRect rect) noexcept
    {
        return std::isfinite(rect.x)
            && std::isfinite(rect.y)
            && isFiniteNonNegative(rect.width)
            && isFiniteNonNegative(rect.height)
            && std::isfinite(rect.right())
            && std::isfinite(rect.bottom());
    }

    [[nodiscard]] Core::Status validateLayoutCandidate(
        const std::pmr::vector<u32>& order) const
    {
        for (const u32 index : order) {
            const LayoutScratchState& scratch = layoutScratchByIndex[index];
            if (!isFiniteNonNegative(scratch.measuredSize.width)
                || !isFiniteNonNegative(scratch.measuredSize.height)
                || !isFiniteLayoutRect(scratch.localRect)
                || !isFiniteLayoutRect(scratch.worldRect)
                || !isFiniteLayoutRect(scratch.effectiveClip)) {
                return fail(
                    UIErrorCode::InvalidLayout,
                    "UI layout arithmetic produced non-finite geometry");
            }
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status publishStructureIfDirty()
    {
        if (!structureDirty) {
            return Core::success();
        }

        const usize writeBufferIndex = 1 - publishedBufferIndex;
        buildCommittedStructure(committedBuffers[writeBufferIndex]);
        publishedBufferIndex = writeBufferIndex;
        ++committedRevision;
        structureDirty = false;
        return Core::success();
    }

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == ownerThreadId;
    }

    [[nodiscard]] Core::Status ensureOwnerThread() const
    {
        if (!isOwnerThread()) {
            return fail(
                UIErrorCode::WrongOwnerThread,
                "UI context was accessed from a non-owner thread");
        }
        return Core::success();
    }

    void publishRoutedPointerListenerTokenState(
        u32 slot,
        u32 generation,
        bool active) noexcept
    {
        if (!lifetime) {
            return;
        }
        const std::scoped_lock lock(lifetime->mutex);
        if (slot >= lifetime->routedPointerListenerStates.size()) {
            return;
        }
        lifetime->routedPointerListenerStates[slot] =
            Detail::RoutedPointerListenerTokenState{
                .generation = generation,
                .active = active,
            };
    }

    void unlinkRoutedPointerListener(u32 listenerIndex) noexcept
    {
        if (listenerIndex >= routedPointerListeners.size()) {
            return;
        }
        RoutedPointerListenerRecord& listener = routedPointerListeners[listenerIndex];
        const u32 nodeIndex = listener.node.index();
        if (nodeIndex >= routedPointerListenerHeadByNodeIndex.size()) {
            return;
        }

        if (listener.previousNodeListenerIndex != InvalidRoutedPointerListenerIndex) {
            routedPointerListeners[listener.previousNodeListenerIndex]
                .nextNodeListenerIndex = listener.nextNodeListenerIndex;
        } else if (routedPointerListenerHeadByNodeIndex[nodeIndex] == listenerIndex) {
            routedPointerListenerHeadByNodeIndex[nodeIndex] =
                listener.nextNodeListenerIndex;
        }
        if (listener.nextNodeListenerIndex != InvalidRoutedPointerListenerIndex) {
            routedPointerListeners[listener.nextNodeListenerIndex]
                .previousNodeListenerIndex = listener.previousNodeListenerIndex;
        } else if (routedPointerListenerTailByNodeIndex[nodeIndex] == listenerIndex) {
            routedPointerListenerTailByNodeIndex[nodeIndex] =
                listener.previousNodeListenerIndex;
        }
        listener.previousNodeListenerIndex = InvalidRoutedPointerListenerIndex;
        listener.nextNodeListenerIndex = InvalidRoutedPointerListenerIndex;
    }

    void recycleRoutedPointerListener(u32 listenerIndex) noexcept
    {
        if (listenerIndex >= routedPointerListeners.size()) {
            return;
        }
        RoutedPointerListenerRecord& listener = routedPointerListeners[listenerIndex];
        unlinkRoutedPointerListener(listenerIndex);

        // Stabilize every intrusive/free-list field before invoking a user
        // callable's move/destructor. Those operations are noexcept but may
        // release UI owners or tokens and therefore re-enter the context.
        listener.node = {};
        listener.kind = UIRoutedPointerEventKind::Move;
        listener.phases = UIEventPhaseMask::None;
        listener.registrationSerial = 0;
        listener.active = false;
        listener.previousNodeListenerIndex = InvalidRoutedPointerListenerIndex;
        listener.nextNodeListenerIndex = InvalidRoutedPointerListenerIndex;
        listener.nextFreeIndex = InvalidRoutedPointerListenerIndex;

        ++listenerCallbackCleanupDepth;
        auto cleanupDepth = Core::makeScopeExit([this]() noexcept {
            --listenerCallbackCleanupDepth;
        });
        UIRoutedPointerCallback detachedCallback(std::move(listener.callback));

        listener.nextFreeIndex = freeRoutedPointerListenerHead;
        freeRoutedPointerListenerHead = listenerIndex;
        detachedCallback.reset();
    }

    void reclaimInactiveRoutedPointerListeners() noexcept
    {
        if (routeDispatchDepth != 0
            || reclaimingInactiveRoutedPointerListeners) {
            return;
        }

        reclaimingInactiveRoutedPointerListeners = true;
        auto reclaimGuard = Core::makeScopeExit([this]() noexcept {
            reclaimingInactiveRoutedPointerListeners = false;
        });
        while (!inactiveRoutedPointerListenerIndices.empty()) {
            const u32 listenerIndex =
                inactiveRoutedPointerListenerIndices.back();
            inactiveRoutedPointerListenerIndices.pop_back();
            if (listenerIndex < routedPointerListeners.size()
                && !routedPointerListeners[listenerIndex].active
                && routedPointerListeners[listenerIndex].node.hasValue()) {
                recycleRoutedPointerListener(listenerIndex);
            }
        }
    }

    void deactivateRoutedPointerListener(
        u32 listenerIndex,
        u32 generation,
        bool publishTokenState) noexcept
    {
        if (listenerIndex >= routedPointerListeners.size()) {
            return;
        }
        RoutedPointerListenerRecord& listener = routedPointerListeners[listenerIndex];
        if (!listener.active || listener.generation != generation) {
            return;
        }

        listener.active = false;
        if (activeRoutedPointerListenerCount > 0) {
            --activeRoutedPointerListenerCount;
        }
        if (publishTokenState) {
            publishRoutedPointerListenerTokenState(listenerIndex, generation, false);
        }
        if (routeDispatchDepth != 0
            || listenerCallbackCleanupDepth != 0
            || reclaimingInactiveRoutedPointerListeners) {
            inactiveRoutedPointerListenerIndices.push_back(listenerIndex);
            return;
        }
        recycleRoutedPointerListener(listenerIndex);
        reclaimInactiveRoutedPointerListeners();
    }

    void deactivateAllRoutedPointerListenersForNode(u32 nodeIndex) noexcept
    {
        if (nodeIndex >= routedPointerListenerHeadByNodeIndex.size()) {
            return;
        }
        u32 listenerIndex = routedPointerListenerHeadByNodeIndex[nodeIndex];
        while (listenerIndex != InvalidRoutedPointerListenerIndex) {
            RoutedPointerListenerRecord& listener = routedPointerListeners[listenerIndex];
            const u32 nextListenerIndex = listener.nextNodeListenerIndex;
            deactivateRoutedPointerListener(
                listenerIndex,
                listener.generation,
                true);
            listenerIndex = nextListenerIndex;
        }
        if (routeDispatchDepth == 0) {
            routedPointerListenerHeadByNodeIndex[nodeIndex] =
                InvalidRoutedPointerListenerIndex;
            routedPointerListenerTailByNodeIndex[nodeIndex] =
                InvalidRoutedPointerListenerIndex;
        }
    }

    void drainDeferredRoutedPointerListenerReleases() noexcept
    {
        if (!isOwnerThread() || !lifetime) {
            return;
        }
        if (!lifetime->hasDeferredRoutedPointerListenerReleases.load(
                std::memory_order_acquire)) {
            reclaimInactiveRoutedPointerListeners();
            return;
        }
        deferredRoutedPointerListenerReleaseBuffer.clear();
        {
            const std::scoped_lock lock(lifetime->mutex);
            deferredRoutedPointerListenerReleaseBuffer.swap(
                lifetime->deferredRoutedPointerListenerReleases);
            lifetime->hasDeferredRoutedPointerListenerReleases.store(
                false,
                std::memory_order_release);
        }
        for (const Detail::DeferredRoutedPointerListenerRelease release
             : deferredRoutedPointerListenerReleaseBuffer) {
            deactivateRoutedPointerListener(
                release.slot,
                release.generation,
                false);
        }
        deferredRoutedPointerListenerReleaseBuffer.clear();
        reclaimInactiveRoutedPointerListeners();
    }

    void drainDeferredRootDestroys() noexcept
    {
        if (!isOwnerThread() || !lifetime) {
            return;
        }

        if (lifetime->hasDeferredRootDestroys.load(std::memory_order_acquire)) {
            deferredRootDestroyBuffer.clear();
            {
                const std::scoped_lock lock(lifetime->mutex);
                deferredRootDestroyBuffer.swap(lifetime->deferredRootDestroys);
                lifetime->hasDeferredRootDestroys.store(
                    false,
                    std::memory_order_release);
            }

            for (const UINodeId root : deferredRootDestroyBuffer) {
                destroyRootImmediately(root);
            }
            deferredRootDestroyBuffer.clear();
        }
        drainDeferredRoutedPointerListenerReleases();
    }

    [[nodiscard]] UINodeId idForIndex(u32 index) const noexcept
    {
        if (index == InvalidNodeIndex || index >= idsByIndex.size()) {
            return {};
        }
        return idsByIndex[index];
    }

    [[nodiscard]] NodeRecord* recordByIndex(u32 index) noexcept
    {
        return nodes.tryGet(idForIndex(index).storageId());
    }

    [[nodiscard]] const NodeRecord* recordByIndex(u32 index) const noexcept
    {
        return nodes.tryGet(idForIndex(index).storageId());
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveNode(UINodeId node)
    {
        if (!node.hasValue()) {
            return fail(UIErrorCode::InvalidNode, "UI node id is empty");
        }
        if (node.ownerWindow() != ownerWindow) {
            return fail(
                UIErrorCode::WrongOwnerWindow,
                "UI node belongs to another owner window");
        }
        if (node.storageId().owner() != nodes.owner()) {
            return fail(UIErrorCode::WrongContext, "UI node belongs to another context");
        }
        NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr) {
            return fail(UIErrorCode::InvalidNode, "UI node is stale or out of range");
        }
        return record;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveParent(UINodeId parent)
    {
        if (!parent.hasValue()) {
            return fail(UIErrorCode::InvalidParent, "UI parent id is empty");
        }
        if (parent.ownerWindow() != ownerWindow) {
            return fail(
                UIErrorCode::WrongOwnerWindow,
                "UI parent belongs to another owner window");
        }
        if (parent.storageId().owner() != nodes.owner()) {
            return fail(UIErrorCode::WrongContext, "UI parent belongs to another context");
        }
        NodeRecord* record = nodes.tryGet(parent.storageId());
        if (record == nullptr) {
            return fail(UIErrorCode::InvalidParent, "UI parent is stale or out of range");
        }
        return record;
    }

    [[nodiscard]] bool contains(UINodeId node) const noexcept
    {
        return node.hasValue()
            && node.ownerWindow() == ownerWindow
            && node.storageId().owner() == nodes.owner()
            && nodes.contains(node.storageId());
    }

    void resetNodeSideData(u32 index) noexcept
    {
        if (index >= layoutStylesByIndex.size()) {
            return;
        }
        layoutStylesByIndex[index] = {};
        pointerHitPoliciesByIndex[index] = UIPointerHitPolicy::Ignore;
        dirtyByIndex[index] = UIDirty::None;
        dirtyQueuedByIndex[index] = 0;
        layoutScratchByIndex[index] = {};
        routedPointerListenerHeadByNodeIndex[index] =
            InvalidRoutedPointerListenerIndex;
        routedPointerListenerTailByNodeIndex[index] =
            InvalidRoutedPointerListenerIndex;
    }

    void markStructureChanged() noexcept
    {
        structureDirty = true;
        layoutDirty = true;
        hitDirty = true;
    }

    void compactDirtyQueue() noexcept
    {
        usize writeIndex = 0;
        for (const UINodeId queued : dirtyQueue) {
            if (!queued.hasValue() || queued.index() >= dirtyQueuedByIndex.size()) {
                continue;
            }
            const u32 index = queued.index();
            if (idForIndex(index) != queued) {
                // A stale generation may share its slot with a newly queued node.
                // Never clear the current generation's side-state from the stale entry.
                continue;
            }
            if (!contains(queued)
                || dirtyQueuedByIndex[index] == 0
                || !anyDirty(dirtyByIndex[index])) {
                dirtyQueuedByIndex[index] = 0;
                continue;
            }
            dirtyQueue[writeIndex++] = queued;
        }
        dirtyQueue.resize(writeIndex);
    }

    [[nodiscard]] usize validDirtyQueueCount() const noexcept
    {
        usize count = 0;
        for (const UINodeId queued : dirtyQueue) {
            if (!queued.hasValue() || queued.index() >= dirtyQueuedByIndex.size()) {
                continue;
            }
            const u32 index = queued.index();
            if (idForIndex(index) == queued
                && contains(queued)
                && dirtyQueuedByIndex[index] != 0
                && anyDirty(dirtyByIndex[index])) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] Core::Status markLayoutStyleDirty(UINodeId node)
    {
        if (!contains(node) || node.index() >= dirtyByIndex.size()) {
            return fail(UIErrorCode::InvalidNode, "UI dirty node is invalid");
        }

        layoutOrderScratch.clear();
        u32 index = node.index();
        while (index != InvalidNodeIndex) {
            layoutOrderScratch.push_back(index);
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr) {
                return fail(UIErrorCode::InvalidNode, "UI dirty ancestry is invalid");
            }
            index = record->parentIndex;
        }

        usize requiredQueueEntries = 0;
        for (const u32 dirtyIndex : layoutOrderScratch) {
            if (dirtyQueuedByIndex[dirtyIndex] == 0) {
                ++requiredQueueEntries;
            }
        }
        if (dirtyQueue.size() > capacityConfig.dirtyQueueCapacity
            || requiredQueueEntries
                > capacityConfig.dirtyQueueCapacity - dirtyQueue.size()) {
            compactDirtyQueue();
        }
        if (dirtyQueue.size() > capacityConfig.dirtyQueueCapacity
            || requiredQueueEntries
                > capacityConfig.dirtyQueueCapacity - dirtyQueue.size()) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI dirty queue capacity has been exhausted");
        }

        constexpr UIDirty ChangedNodeDirty =
            UIDirty::Style
            | UIDirty::Measure
            | UIDirty::Arrange
            | UIDirty::Composite
            | UIDirty::HitTest
            | UIDirty::Semantics;
        constexpr UIDirty AncestorDirty =
            UIDirty::Measure
            | UIDirty::Arrange
            | UIDirty::Composite
            | UIDirty::HitTest;
        for (usize pathIndex = 0; pathIndex < layoutOrderScratch.size(); ++pathIndex) {
            const u32 dirtyIndex = layoutOrderScratch[pathIndex];
            if (dirtyQueuedByIndex[dirtyIndex] == 0) {
                dirtyQueue.push_back(idForIndex(dirtyIndex));
                dirtyQueuedByIndex[dirtyIndex] = 1;
            }
            dirtyByIndex[dirtyIndex] |= pathIndex == 0 ? ChangedNodeDirty : AncestorDirty;
        }
        dirtyQueueHighWater = (std::max)(dirtyQueueHighWater, dirtyQueue.size());
        layoutDirty = true;
        hitDirty = true;
        return Core::success();
    }

    [[nodiscard]] Core::Status markHitTestDirty(UINodeId node)
    {
        if (!contains(node) || node.index() >= dirtyByIndex.size()) {
            return fail(UIErrorCode::InvalidNode, "UI hit-test dirty node is invalid");
        }

        const u32 index = node.index();
        if (dirtyQueuedByIndex[index] == 0) {
            if (dirtyQueue.size() >= capacityConfig.dirtyQueueCapacity) {
                compactDirtyQueue();
            }
            if (dirtyQueue.size() >= capacityConfig.dirtyQueueCapacity) {
                return fail(
                    UIErrorCode::CapacityExceeded,
                    "UI dirty queue capacity has been exhausted");
            }
            dirtyQueue.push_back(node);
            dirtyQueuedByIndex[index] = 1;
        }
        dirtyByIndex[index] |= UIDirty::HitTest;
        dirtyQueueHighWater = (std::max)(dirtyQueueHighWater, dirtyQueue.size());
        hitDirty = true;
        return Core::success();
    }

    void clearDirtyState() noexcept
    {
        std::fill(dirtyByIndex.begin(), dirtyByIndex.end(), UIDirty::None);
        std::fill(dirtyQueuedByIndex.begin(), dirtyQueuedByIndex.end(), 0);
        dirtyQueue.clear();
        layoutDirty = false;
        hitDirty = false;
    }

    [[nodiscard]] bool isNodeWithinRoot(UINodeId root, UINodeId node) const noexcept
    {
        if (!contains(root) || !contains(node)) {
            return false;
        }

        const NodeRecord* nodeRecord = nodes.tryGet(node.storageId());
        if (nodeRecord == nullptr) {
            return false;
        }
        return nodeRecord->rootIndex == root.index();
    }

    [[nodiscard]] Core::Result<UINodeId> createNode(UIWidgetKind kind)
    {
        auto idResult = nodes.tryEmplace();
        if (!idResult) {
            const Core::Error& error = idResult.error();
            if (error.code == Core::CoreErrorCode::CapacityExceeded) {
                return fail(UIErrorCode::CapacityExceeded, "UI node capacity has been exhausted");
            }
            return Core::failure(error);
        }

        const UINodeId node = UINodeId::create(ownerWindow, *idResult);
        idsByIndex[node.index()] = node;
        resetNodeSideData(node.index());
        NodeRecord* record = nodes.tryGet(node.storageId());
        record->kind = kind;
        record->rootIndex = node.index();
        return node;
    }

    [[nodiscard]] Core::Result<UIRootOwner> createRoot(UIContext& context)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (liveRootCount >= capacityConfig.rootCapacity) {
            return fail(UIErrorCode::CapacityExceeded, "UI root capacity has been exhausted");
        }

        auto nodeResult = createNode(UIWidgetKind::Root);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }

        const UINodeId root = *nodeResult;
        NodeRecord* rootRecord = nodes.tryGet(root.storageId());
        rootRecord->parentIndex = InvalidNodeIndex;
        rootRecord->previousSiblingIndex = lastRootIndex;
        rootRecord->nextSiblingIndex = InvalidNodeIndex;
        rootRecord->depth = 0;

        if (lastRootIndex != InvalidNodeIndex) {
            recordByIndex(lastRootIndex)->nextSiblingIndex = root.index();
        } else {
            firstRootIndex = root.index();
        }
        lastRootIndex = root.index();
        ++liveRootCount;
        markStructureChanged();
        return UIRootOwner(context.m_impl->lifetime, root);
    }

    [[nodiscard]] Core::Result<UINodeId> createChild(UINodeId parent, UIWidgetKind kind)
    {
        if (kind == UIWidgetKind::Root) {
            return fail(UIErrorCode::InvalidParent, "Root nodes cannot be created as children");
        }
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();

        auto parentResult = resolveParent(parent);
        if (!parentResult) {
            return Core::failure(parentResult.error());
        }
        NodeRecord& parentRecord = **parentResult;
        if (parentRecord.depth == (std::numeric_limits<u32>::max)()) {
            return fail(UIErrorCode::InvalidParent, "UI parent depth cannot be represented");
        }

        auto nodeResult = createNode(kind);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }

        const UINodeId node = *nodeResult;
        NodeRecord* childRecord = nodes.tryGet(node.storageId());
        childRecord->parentIndex = parent.index();
        childRecord->previousSiblingIndex = parentRecord.lastChildIndex;
        childRecord->nextSiblingIndex = InvalidNodeIndex;
        childRecord->rootIndex = parentRecord.rootIndex;
        childRecord->depth = parentRecord.depth + 1;

        if (parentRecord.lastChildIndex != InvalidNodeIndex) {
            recordByIndex(parentRecord.lastChildIndex)->nextSiblingIndex = node.index();
        } else {
            parentRecord.firstChildIndex = node.index();
        }
        parentRecord.lastChildIndex = node.index();
        markStructureChanged();
        return node;
    }

    void unlinkFromTree(u32 index, NodeRecord& record) noexcept
    {
        if (record.parentIndex != InvalidNodeIndex) {
            NodeRecord* parent = recordByIndex(record.parentIndex);
            if (parent != nullptr) {
                if (parent->firstChildIndex == index) {
                    parent->firstChildIndex = record.nextSiblingIndex;
                }
                if (parent->lastChildIndex == index) {
                    parent->lastChildIndex = record.previousSiblingIndex;
                }
            }
        } else {
            if (firstRootIndex == index) {
                firstRootIndex = record.nextSiblingIndex;
            }
            if (lastRootIndex == index) {
                lastRootIndex = record.previousSiblingIndex;
            }
        }

        if (record.previousSiblingIndex != InvalidNodeIndex) {
            if (NodeRecord* previous = recordByIndex(record.previousSiblingIndex);
                previous != nullptr) {
                previous->nextSiblingIndex = record.nextSiblingIndex;
            }
        }
        if (record.nextSiblingIndex != InvalidNodeIndex) {
            if (NodeRecord* next = recordByIndex(record.nextSiblingIndex); next != nullptr) {
                next->previousSiblingIndex = record.previousSiblingIndex;
            }
        }
    }

    void eraseDetachedSubtree(u32 index) noexcept
    {
        u32 currentIndex = index;
        while (currentIndex != InvalidNodeIndex) {
            NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr) {
                return;
            }

            if (record->firstChildIndex != InvalidNodeIndex) {
                currentIndex = record->firstChildIndex;
                continue;
            }

            const u32 parentIndex = record->parentIndex;
            const u32 nextSiblingIndex = record->nextSiblingIndex;
            if (currentIndex != index) {
                unlinkFromTree(currentIndex, *record);
            }

            const UINodeId node = idForIndex(currentIndex);
            deactivateAllRoutedPointerListenersForNode(currentIndex);
            idsByIndex[currentIndex] = {};
            resetNodeSideData(currentIndex);
            static_cast<void>(nodes.erase(node.storageId()));

            if (currentIndex == index) {
                return;
            }
            currentIndex = nextSiblingIndex != InvalidNodeIndex
                ? nextSiblingIndex
                : parentIndex;
        }
    }

    [[nodiscard]] Core::Status destroySubtree(UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }

        auto nodeResult = resolveNode(node);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }

        NodeRecord& record = **nodeResult;
        const bool wasRoot = record.kind == UIWidgetKind::Root;
        unlinkFromTree(node.index(), record);
        eraseDetachedSubtree(node.index());
        if (wasRoot && liveRootCount > 0) {
            --liveRootCount;
        }
        markStructureChanged();
        return Core::success();
    }

    void destroyRootImmediately(UINodeId root) noexcept
    {
        if (!isOwnerThread() || !contains(root)) {
            return;
        }

        NodeRecord* rootRecord = nodes.tryGet(root.storageId());
        if (rootRecord == nullptr || rootRecord->kind != UIWidgetKind::Root) {
            return;
        }

        unlinkFromTree(root.index(), *rootRecord);
        eraseDetachedSubtree(root.index());
        if (liveRootCount > 0) {
            --liveRootCount;
        }
        markStructureChanged();
    }

    [[nodiscard]] Core::Status destroyFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue()) {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot)) {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        if (!contains(node)) {
            auto nodeResult = resolveNode(node);
            return nodeResult ? Core::success() : Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node)) {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }

        if (sameNode(updaterRoot, node)) {
            return fail(
                UIErrorCode::RootRequired,
                "Destroying a root node requires UIRootOwner::reset");
        }
        return destroySubtree(node);
    }

    [[nodiscard]] Core::Status setLayoutStyleFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        const UILayoutStyle& style)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue()) {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot)) {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto normalizedStyle = normalizeLayoutStyle(style);
        if (!normalizedStyle) {
            return Core::failure(normalizedStyle.error());
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node)) {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }

        UILayoutStyle& currentStyle = layoutStylesByIndex[node.index()];
        if (currentStyle == *normalizedStyle) {
            return Core::success();
        }

        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus) {
            return dirtyStatus;
        }
        currentStyle = *normalizedStyle;
        return Core::success();
    }

    [[nodiscard]] Core::Status setPointerHitPolicyFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        UIPointerHitPolicy policy)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue()) {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot)) {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node)) {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        if (!isValidPointerHitPolicy(policy)) {
            return fail(
                UIErrorCode::InvalidPointerPolicy,
                "UI pointer hit policy is not recognized");
        }

        UIPointerHitPolicy& currentPolicy = pointerHitPoliciesByIndex[node.index()];
        if (currentPolicy == policy) {
            return Core::success();
        }
        if (Core::Status dirtyStatus = markHitTestDirty(node); !dirtyStatus) {
            return dirtyStatus;
        }
        currentPolicy = policy;
        return Core::success();
    }

    void appendCommittedTree(
        u32 index,
        u32& ordinal,
        std::pmr::vector<UICommittedNodeEntry>& output) const noexcept
    {
        const u32 rootIndex = index;
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex) {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr) {
                return;
            }

            const u32 currentOrdinal = ordinal++;
            output.push_back(UICommittedNodeEntry{
                .node = idForIndex(currentIndex),
                .parent = idForIndex(record->parentIndex),
                .depth = record->depth,
                .preorder = currentOrdinal,
                .paintOrdinal = currentOrdinal,
                .kind = record->kind,
            });

            if (record->firstChildIndex != InvalidNodeIndex) {
                currentIndex = record->firstChildIndex;
                continue;
            }

            while (currentIndex != rootIndex) {
                record = recordByIndex(currentIndex);
                if (record == nullptr) {
                    return;
                }
                if (record->nextSiblingIndex != InvalidNodeIndex) {
                    currentIndex = record->nextSiblingIndex;
                    break;
                }
                currentIndex = record->parentIndex;
            }
            if (currentIndex == rootIndex) {
                currentIndex = InvalidNodeIndex;
            }
        }
    }

    [[nodiscard]] Core::Status commitStructure()
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        if (routeDispatchDepth != 0) {
            return fail(
                UIErrorCode::PointerRouteAlreadyInProgress,
                "UI structure cannot be committed during pointer routing");
        }
        drainDeferredRootDestroys();
        return publishStructureIfDirty();
    }

    [[nodiscard]] Core::Status validateViewport(UILogicalSize viewportSize) const
    {
        if (!isFiniteNonNegative(viewportSize.width)
            || !isFiniteNonNegative(viewportSize.height)) {
            return fail(
                UIErrorCode::InvalidLayout,
                "UI layout viewport must be finite and non-negative");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status commitLayout(UILogicalSize viewportSize)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        if (routeDispatchDepth != 0) {
            return fail(
                UIErrorCode::PointerRouteAlreadyInProgress,
                "UI layout cannot be committed during pointer routing");
        }
        drainDeferredRootDestroys();
        if (Core::Status viewportStatus = validateViewport(viewportSize); !viewportStatus) {
            return viewportStatus;
        }
        viewportSize.width = normalizeFloat(viewportSize.width);
        viewportSize.height = normalizeFloat(viewportSize.height);
        const bool viewportChanged = !hasCommittedViewport
            || viewportSize != committedViewportSize;
        const bool layoutNeedsCommit = structureDirty || layoutDirty || viewportChanged;
        const bool hitNeedsCommit = hitDirty || layoutNeedsCommit || committedHitRevision == 0;

        if (!layoutNeedsCommit && !hitNeedsCommit) {
            lastLayoutPass = {};
            lastHitRebuildCount = 0;
            return Core::success();
        }
        if (layoutNeedsCommit
            && nodes.activeCount() > capacityConfig.layoutSnapshotCapacity) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI committed layout snapshot capacity has been exhausted");
        }

        const usize writeStructureBufferIndex = 1 - publishedBufferIndex;
        if (structureDirty) {
            buildCommittedStructure(committedBuffers[writeStructureBufferIndex]);
        }

        usize writeLayoutBufferIndex = publishedLayoutBufferIndex;
        LayoutPassStatistics pass{};
        std::span<const UICommittedLayoutEntry> candidateLayoutEntries{};
        if (layoutNeedsCommit) {
            writeLayoutBufferIndex = 1 - publishedLayoutBufferIndex;
            std::pmr::vector<UICommittedLayoutEntry>& writeLayout =
                committedLayoutBuffers[writeLayoutBufferIndex];
            writeLayout.clear();

            buildLayoutOrder(layoutOrderScratch);
            pass.passCount = layoutOrderScratch.empty() ? 0 : 1;
            prepareLayoutState(viewportSize, layoutOrderScratch);
            measureLayout(viewportSize, layoutOrderScratch, pass);
            arrangeLayout(viewportSize, layoutOrderScratch, pass);
            if (Core::Status candidateStatus = validateLayoutCandidate(layoutOrderScratch);
                !candidateStatus) {
                return candidateStatus;
            }
            buildCommittedLayout(writeLayout, layoutOrderScratch);
            candidateLayoutEntries = std::span<const UICommittedLayoutEntry>(
                writeLayout.data(),
                writeLayout.size());
        } else {
            const std::pmr::vector<UICommittedLayoutEntry>& currentLayout =
                committedLayoutBuffers[publishedLayoutBufferIndex];
            candidateLayoutEntries = std::span<const UICommittedLayoutEntry>(
                currentLayout.data(),
                currentLayout.size());
        }

        const u64 candidateStructureRevision = committedRevision + (structureDirty ? 1u : 0u);
        const u64 candidateLayoutRevision = committedLayoutRevision + (layoutNeedsCommit ? 1u : 0u);
        // C1c-a derives paint order solely from committed tree preorder. A
        // future independent Order mutation must advance this stamp without
        // conflating it with hit-only policy changes.
        const u64 candidatePaintOrderRevision = candidateStructureRevision;
        usize writeHitBufferIndex = publishedHitBufferIndex;
        usize candidateHitTargetCount = committedHitTargetCount;
        if (hitNeedsCommit) {
            writeHitBufferIndex = 1 - publishedHitBufferIndex;
            auto hitResult = buildCommittedHit(
                committedHitBuffers[writeHitBufferIndex],
                candidateLayoutEntries);
            if (!hitResult) {
                return Core::failure(hitResult.error());
            }
            candidateHitTargetCount = *hitResult;
        }

        if (structureDirty) {
            publishedBufferIndex = writeStructureBufferIndex;
            ++committedRevision;
            structureDirty = false;
        }
        if (layoutNeedsCommit) {
            publishedLayoutBufferIndex = writeLayoutBufferIndex;
            ++committedLayoutRevision;
            committedLayoutStructureRevision = candidateStructureRevision;
            committedViewportSize = viewportSize;
            hasCommittedViewport = true;
        }
        if (hitNeedsCommit) {
            publishedHitBufferIndex = writeHitBufferIndex;
            ++committedHitRevision;
            committedHitStructureRevision = candidateStructureRevision;
            committedHitLayoutRevision = candidateLayoutRevision;
            committedHitPaintOrderRevision = candidatePaintOrderRevision;
            committedHitTargetCount = candidateHitTargetCount;
        }
        lastLayoutPass = layoutNeedsCommit ? pass : LayoutPassStatistics{};
        lastHitRebuildCount = hitNeedsCommit ? 1 : 0;
        clearDirtyState();
        return Core::success();
    }

    [[nodiscard]] UICommittedStructureView committedStructure() const noexcept
    {
        const std::pmr::vector<UICommittedNodeEntry>& entries =
            committedBuffers[publishedBufferIndex];
        return UICommittedStructureView{
            std::span<const UICommittedNodeEntry>(entries.data(), entries.size()),
            committedRevision,
        };
    }

    [[nodiscard]] UICommittedLayoutView committedLayout() const noexcept
    {
        const std::pmr::vector<UICommittedLayoutEntry>& entries =
            committedLayoutBuffers[publishedLayoutBufferIndex];
        return UICommittedLayoutView{
            std::span<const UICommittedLayoutEntry>(entries.data(), entries.size()),
            committedLayoutStructureRevision,
            committedLayoutRevision,
        };
    }

    [[nodiscard]] UICommittedHitView committedHit() const noexcept
    {
        const std::pmr::vector<UICommittedHitEntry>& entries =
            committedHitBuffers[publishedHitBufferIndex];
        return UICommittedHitView{
            std::span<const UICommittedHitEntry>(entries.data(), entries.size()),
            committedHitStructureRevision,
            committedHitLayoutRevision,
            committedHitPaintOrderRevision,
            committedHitRevision,
        };
    }

    [[nodiscard]] UIPointerHitQueryResult queryPointerHit(
        UILogicalPoint point) const noexcept
    {
        const UICommittedHitView hit = committedHit();
        UIPointerHitQueryResult result{
            .structureRevision = hit.structureRevision(),
            .layoutRevision = hit.layoutRevision(),
            .paintOrderRevision = hit.paintOrderRevision(),
            .hitRevision = hit.hitRevision(),
        };
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return result;
        }

        const std::span<const UICommittedHitEntry> entries = hit.entries();
        for (usize reverseIndex = entries.size(); reverseIndex > 0; --reverseIndex) {
            ++result.visitedEntryCount;
            const usize entryIndex = reverseIndex - 1;
            const UICommittedHitEntry& entry = entries[entryIndex];
            if (entry.policy != UIPointerHitPolicy::Targetable
                || !containsPointHalfOpen(entry.worldRect, point)
                || !containsPointHalfOpen(entry.effectiveClip, point)
                || entry.rootEntryIndex >= entries.size()) {
                continue;
            }

            const UICommittedHitEntry& root = entries[entry.rootEntryIndex];
            result.target = UIPointerHitTarget{
                .node = entry.node,
                .rootNode = root.node,
                .hitEntryIndex = static_cast<u32>(entryIndex),
                .rootEntryIndex = entry.rootEntryIndex,
                .worldRect = entry.worldRect,
                .effectiveClip = entry.effectiveClip,
                .paintOrdinal = entry.paintOrdinal,
            };
            return result;
        }
        return result;
    }

    [[nodiscard]] Core::Result<std::pair<u32, u32>>
    addRoutedPointerListener(
        UIRoutedPointerListenerDesc descriptor,
        UIRoutedPointerCallback callback)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        auto nodeResult = resolveNode(descriptor.node);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }
        if (!isValidRoutedPointerEventKind(descriptor.kind)
            || !isValidEventPhaseMask(descriptor.phases)
            || !callback.hasValue()) {
            return fail(
                UIErrorCode::InvalidRoutedPointerListener,
                "UI routed pointer listener descriptor or callback is invalid");
        }
        if (freeRoutedPointerListenerHead == InvalidRoutedPointerListenerIndex) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI routed pointer listener capacity has been exhausted");
        }
        if (routedPointerListenerRegistrationSerial
            == (std::numeric_limits<u64>::max)()) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI routed pointer listener registration serial is exhausted");
        }

        const u32 listenerIndex = freeRoutedPointerListenerHead;
        RoutedPointerListenerRecord& listener =
            routedPointerListeners[listenerIndex];
        freeRoutedPointerListenerHead = listener.nextFreeIndex;

        ++listener.generation;
        if (listener.generation == 0) {
            ++listener.generation;
        }
        listener.node = descriptor.node;
        listener.kind = descriptor.kind;
        listener.phases = descriptor.phases;
        listener.callback = std::move(callback);
        listener.previousNodeListenerIndex =
            routedPointerListenerTailByNodeIndex[descriptor.node.index()];
        listener.nextNodeListenerIndex = InvalidRoutedPointerListenerIndex;
        listener.nextFreeIndex = InvalidRoutedPointerListenerIndex;
        listener.registrationSerial = ++routedPointerListenerRegistrationSerial;
        listener.active = true;

        if (listener.previousNodeListenerIndex != InvalidRoutedPointerListenerIndex) {
            routedPointerListeners[listener.previousNodeListenerIndex]
                .nextNodeListenerIndex = listenerIndex;
        } else {
            routedPointerListenerHeadByNodeIndex[descriptor.node.index()] =
                listenerIndex;
        }
        routedPointerListenerTailByNodeIndex[descriptor.node.index()] =
            listenerIndex;
        ++activeRoutedPointerListenerCount;
        routedPointerListenerHighWater = (std::max)(
            routedPointerListenerHighWater,
            activeRoutedPointerListenerCount);
        publishRoutedPointerListenerTokenState(
            listenerIndex,
            listener.generation,
            true);
        return std::pair<u32, u32>{listenerIndex, listener.generation};
    }

    [[nodiscard]] Core::Status validatePointerInput(
        const UIPointerInputEvent& input) const
    {
        if (!input.platformFrame.hasValue() || input.sourceSequence == 0) {
            return fail(
                UIErrorCode::InvalidPointerInput,
                "UI pointer input requires a platform frame and source sequence");
        }
        if (!input.window.hasValue()) {
            return fail(
                UIErrorCode::InvalidPointerInput,
                "UI pointer input owner window is empty");
        }
        if (input.window != ownerWindow) {
            return fail(
                UIErrorCode::WrongOwnerWindow,
                "UI pointer input belongs to another owner window");
        }
        if (input.pointer != Platform::PrimaryPointerId
            || !isValidRoutedPointerEventKind(input.kind)
            || !std::isfinite(input.position.x)
            || !std::isfinite(input.position.y)
            || !std::isfinite(input.delta.x)
            || !std::isfinite(input.delta.y)) {
            return fail(
                UIErrorCode::InvalidPointerInput,
                "UI pointer input kind, identity, position, or delta is invalid");
        }
        if ((input.kind == UIRoutedPointerEventKind::ButtonDown
             || input.kind == UIRoutedPointerEventKind::ButtonUp)
            && input.button >= Platform::PointerButton::Count) {
            return fail(
                UIErrorCode::InvalidPointerInput,
                "UI pointer button is invalid");
        }
        return Core::success();
    }

    void dispatchRoutedPointerListeners(
        UINodeId node,
        UIEventPhase phase,
        UIRoutedPointerEventKind kind,
        u64 registrationSerialBoundary,
        UIRoutedPointerEvent& event,
        UIPointerRouteResult& result) noexcept
    {
        if (!contains(node) || node.index() >= routedPointerListenerHeadByNodeIndex.size()) {
            return;
        }
        Detail::UIRoutedPointerEventAccess::setRouteState(
            event,
            phase,
            node,
            result.pointQuery.target.node,
            result.pointQuery.target.rootNode);
        const UIEventPhaseMask requiredPhase = phaseMaskFor(phase);
        u32 listenerIndex = routedPointerListenerHeadByNodeIndex[node.index()];
        while (listenerIndex != InvalidRoutedPointerListenerIndex) {
            if (listenerIndex >= routedPointerListeners.size()) {
                return;
            }
            RoutedPointerListenerRecord& listener =
                routedPointerListeners[listenerIndex];
            const u32 nextListenerIndex = listener.nextNodeListenerIndex;
            if (listener.active
                && listener.node == node
                && listener.kind == kind
                && listener.registrationSerial <= registrationSerialBoundary
                && hasEventPhase(listener.phases, requiredPhase)) {
                ++result.listenerInvocationCount;
                listener.callback(event);
                if (event.isImmediatePropagationStopped()) {
                    return;
                }
            }
            listenerIndex = nextListenerIndex;
        }
    }

    [[nodiscard]] Core::Result<UIPointerRouteResult> routePointerInput(
        const UIPointerInputEvent& input)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0) {
            return fail(
                UIErrorCode::PointerRouteAlreadyInProgress,
                "UI pointer routing is already in progress");
        }
        drainDeferredRootDestroys();
        if (Core::Status inputStatus = validatePointerInput(input); !inputStatus) {
            return Core::failure(inputStatus.error());
        }

        UIPointerRouteResult result{
            .pointQuery = queryPointerHit(input.position),
        };
        if (!result.pointQuery.hasTarget()) {
            return result;
        }
        if (!contains(result.pointQuery.target.node)) {
            result.targetInvalidated = true;
            return result;
        }

        const UICommittedHitView hit = committedHit();
        const std::span<const UICommittedHitEntry> entries = hit.entries();
        routePathScratch.clear();
        u32 entryIndex = result.pointQuery.target.hitEntryIndex;
        while (entryIndex != InvalidUIHitEntryIndex) {
            if (entryIndex >= entries.size()) {
                return fail(
                    Core::CoreErrorCode::Internal,
                    "UI committed pointer route entry index is invalid");
            }
            if (routePathScratch.size() >= capacityConfig.routePathCapacity) {
                routePathScratch.clear();
                return fail(
                    UIErrorCode::CapacityExceeded,
                    "UI pointer route path capacity has been exhausted");
            }
            routePathScratch.push_back(entryIndex);
            if (entryIndex == result.pointQuery.target.rootEntryIndex) {
                break;
            }
            entryIndex = entries[entryIndex].parentEntryIndex;
            if (routePathScratch.size() > entries.size()) {
                routePathScratch.clear();
                return fail(
                    Core::CoreErrorCode::Internal,
                    "UI committed pointer route ancestry contains a cycle");
            }
        }
        if (routePathScratch.empty()
            || routePathScratch.back() != result.pointQuery.target.rootEntryIndex
            || entries[routePathScratch.back()].node
                != result.pointQuery.target.rootNode) {
            routePathScratch.clear();
            return fail(
                Core::CoreErrorCode::Internal,
                "UI committed pointer route root is invalid");
        }

        result.routeDepth = routePathScratch.size();
        const u64 registrationSerialBoundary =
            routedPointerListenerRegistrationSerial;
        UIRoutedPointerEvent routedEvent =
            Detail::UIRoutedPointerEventAccess::Create(input);
        routeDispatchDepth = 1;
        auto dispatchCleanup = Core::makeScopeExit([this]() noexcept {
            routeDispatchDepth = 0;
            drainDeferredRoutedPointerListenerReleases();
            reclaimInactiveRoutedPointerListeners();
        });

        const UINodeId targetNode = result.pointQuery.target.node;
        for (usize reversePathIndex = routePathScratch.size();
             reversePathIndex > 1;
             --reversePathIndex) {
            if (!contains(targetNode)) {
                result.targetInvalidated = true;
                break;
            }
            const UINodeId currentNode =
                entries[routePathScratch[reversePathIndex - 1]].node;
            if (contains(currentNode)) {
                dispatchRoutedPointerListeners(
                    currentNode,
                    UIEventPhase::Capture,
                    input.kind,
                    registrationSerialBoundary,
                    routedEvent,
                    result);
            }
            if (routedEvent.isPropagationStopped()) {
                break;
            }
        }

        if (!routedEvent.isPropagationStopped() && !result.targetInvalidated) {
            if (!contains(targetNode)) {
                result.targetInvalidated = true;
            } else {
                dispatchRoutedPointerListeners(
                    targetNode,
                    UIEventPhase::Target,
                    input.kind,
                    registrationSerialBoundary,
                    routedEvent,
                    result);
            }
        }

        if (!routedEvent.isPropagationStopped() && !result.targetInvalidated) {
            for (usize pathIndex = 1;
                 pathIndex < routePathScratch.size();
                 ++pathIndex) {
                if (!contains(targetNode)) {
                    result.targetInvalidated = true;
                    break;
                }
                const UINodeId currentNode =
                    entries[routePathScratch[pathIndex]].node;
                if (contains(currentNode)) {
                    dispatchRoutedPointerListeners(
                        currentNode,
                        UIEventPhase::Bubble,
                        input.kind,
                        registrationSerialBoundary,
                        routedEvent,
                        result);
                }
                if (routedEvent.isPropagationStopped()) {
                    break;
                }
            }
        }

        if (!contains(targetNode)) {
            result.targetInvalidated = true;
        }
        result.consumed = routedEvent.isInputTransitionConsumed();
        result.stopped = routedEvent.isPropagationStopped();
        return result;
    }

    [[nodiscard]] UIContextStatistics statistics() const noexcept
    {
        return UIContextStatistics{
            .nodeCapacity = capacityConfig.nodeCapacity,
            .rootCapacity = capacityConfig.rootCapacity,
            .dirtyQueueCapacity = capacityConfig.dirtyQueueCapacity,
            .layoutSnapshotCapacity = capacityConfig.layoutSnapshotCapacity,
            .hitSnapshotCapacity = capacityConfig.hitSnapshotCapacity,
            .routePathCapacity = capacityConfig.routePathCapacity,
            .routedPointerListenerCapacity =
                capacityConfig.routedPointerListenerCapacity,
            .activeRoutedPointerListenerCount =
                activeRoutedPointerListenerCount,
            .routedPointerListenerHighWater = routedPointerListenerHighWater,
            .liveNodeCount = nodes.activeCount(),
            .liveRootCount = liveRootCount,
            .committedNodeCount = committedBuffers[publishedBufferIndex].size(),
            .committedRevision = committedRevision,
            .committedLayoutNodeCount =
                committedLayoutBuffers[publishedLayoutBufferIndex].size(),
            .layoutRevision = committedLayoutRevision,
            .committedHitNodeCount = committedHitBuffers[publishedHitBufferIndex].size(),
            .committedHitTargetCount = committedHitTargetCount,
            .hitRevision = committedHitRevision,
            .paintOrderRevision = committedHitPaintOrderRevision,
            .dirty = structureDirty,
            .layoutDirty = layoutDirty,
            .hitDirty = hitDirty,
            .lastLayoutPassCount = lastLayoutPass.passCount,
            .lastLayoutMeasuredNodeCount = lastLayoutPass.measuredNodeCount,
            .lastLayoutArrangedNodeCount = lastLayoutPass.arrangedNodeCount,
            .lastLayoutPercentMeasureFallbackCount =
                lastLayoutPass.percentMeasureFallbackCount,
            .lastHitRebuildCount = lastHitRebuildCount,
            .dirtyQueuePendingCount = validDirtyQueueCount(),
            .dirtyQueueHighWater = dirtyQueueHighWater,
        };
    }
};

UIRoutedPointerListenerToken::UIRoutedPointerListenerToken(
    std::weak_ptr<Detail::UIContextLifetimeControl> lifetime,
    u32 slot,
    u32 generation) noexcept
    : m_lifetime(std::move(lifetime)),
      m_slot(slot),
      m_generation(generation)
{
}

UIRoutedPointerListenerToken::~UIRoutedPointerListenerToken() noexcept
{
    reset();
}

UIRoutedPointerListenerToken::UIRoutedPointerListenerToken(
    UIRoutedPointerListenerToken&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)),
      m_slot(std::exchange(other.m_slot, 0)),
      m_generation(std::exchange(other.m_generation, 0))
{
}

UIRoutedPointerListenerToken& UIRoutedPointerListenerToken::operator=(
    UIRoutedPointerListenerToken&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_slot = std::exchange(other.m_slot, 0);
    m_generation = std::exchange(other.m_generation, 0);
    return *this;
}

void UIRoutedPointerListenerToken::reset() noexcept
{
    const u32 generation = m_generation;
    if (generation == 0) {
        m_lifetime.reset();
        m_slot = 0;
        return;
    }

    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime =
        m_lifetime.lock();
    UIContext* immediateContext = nullptr;
    if (lifetime) {
        const std::scoped_lock lock(lifetime->mutex);
        if (m_slot < lifetime->routedPointerListenerStates.size()) {
            Detail::RoutedPointerListenerTokenState& state =
                lifetime->routedPointerListenerStates[m_slot];
            if (state.active && state.generation == generation) {
                state.active = false;
                if (lifetime->context != nullptr) {
                    if (std::this_thread::get_id() == lifetime->ownerThreadId) {
                        immediateContext = lifetime->context;
                    } else {
                        if (lifetime->deferredRoutedPointerListenerReleases.size()
                            == lifetime->deferredRoutedPointerListenerReleases.capacity()) {
                            std::terminate();
                        }
                        lifetime->deferredRoutedPointerListenerReleases.push_back(
                            Detail::DeferredRoutedPointerListenerRelease{
                                .slot = m_slot,
                                .generation = generation,
                            });
                        lifetime->hasDeferredRoutedPointerListenerReleases.store(
                            true,
                            std::memory_order_release);
                    }
                }
            }
        }
    }

    if (immediateContext != nullptr) {
        immediateContext->releaseRoutedPointerListenerFromToken(
            m_slot,
            generation);
    }
    m_lifetime.reset();
    m_slot = 0;
    m_generation = 0;
}

bool UIRoutedPointerListenerToken::isActive() const noexcept
{
    if (m_generation == 0) {
        return false;
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime =
        m_lifetime.lock();
    if (!lifetime) {
        return false;
    }
    const std::scoped_lock lock(lifetime->mutex);
    if (lifetime->context == nullptr
        || m_slot >= lifetime->routedPointerListenerStates.size()) {
        return false;
    }
    const Detail::RoutedPointerListenerTokenState& state =
        lifetime->routedPointerListenerStates[m_slot];
    return state.active && state.generation == m_generation;
}

UIRoutedPointerListenerToken::operator bool() const noexcept
{
    return isActive();
}

UIRootOwner::UIRootOwner(
    std::weak_ptr<Detail::UIContextLifetimeControl> lifetime,
    UINodeId root) noexcept
    : m_lifetime(std::move(lifetime)), m_root(root)
{
}

UIRootOwner::~UIRootOwner() noexcept
{
    reset();
}

UIRootOwner::UIRootOwner(UIRootOwner&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)), m_root(other.m_root)
{
    other.m_root = {};
}

UIRootOwner& UIRootOwner::operator=(UIRootOwner&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_root = other.m_root;
    other.m_root = {};
    return *this;
}

void UIRootOwner::reset() noexcept
{
    const UINodeId root = m_root;
    if (!root.hasValue()) {
        m_lifetime.reset();
        return;
    }

    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime) {
        m_root = {};
        m_lifetime.reset();
        return;
    }

    UIContext* context = nullptr;
    {
        const std::scoped_lock lock(lifetime->mutex);
        context = lifetime->context;
        if (context != nullptr
            && std::this_thread::get_id() != lifetime->ownerThreadId) {
            // One move-only owner exists per live root, so the queue reserved to
            // rootCapacity cannot fill before the owner thread drains it.
            if (lifetime->deferredRootDestroys.size()
                == lifetime->deferredRootDestroys.capacity()) {
                std::terminate();
            }
            lifetime->deferredRootDestroys.push_back(root);
            lifetime->hasDeferredRootDestroys.store(
                true,
                std::memory_order_release);
            context = nullptr;
        }
    }

    if (context != nullptr) {
        context->destroyRootFromOwner(root);
    }
    m_root = {};
    m_lifetime.reset();
}

UINodeId UIRootOwner::rootNodeId() const noexcept
{
    return m_root;
}

bool UIRootOwner::hasValue() const noexcept
{
    return m_root.hasValue();
}

UIRootOwner::operator bool() const noexcept
{
    return hasValue();
}

UIRootBuilder::UIRootBuilder(UIContext& context) noexcept
    : m_context(&context)
{
}

Core::Result<UIRootOwner> UIRootBuilder::createRoot()
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createRoot();
}

Core::Result<UINodeId> UIRootBuilder::createPanel(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Panel);
}

Core::Result<UINodeId> UIRootBuilder::createLabel(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Label);
}

Core::Result<UINodeId> UIRootBuilder::createButton(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Button);
}

UITreeUpdater::UITreeUpdater(UIContext& context, UINodeId root) noexcept
    : m_context(&context), m_root(root)
{
}

UITreeUpdater::UITreeUpdater(UITreeUpdater&& other) noexcept
    : m_context(std::exchange(other.m_context, nullptr)),
      m_root(std::exchange(other.m_root, {}))
{
}

UITreeUpdater& UITreeUpdater::operator=(UITreeUpdater&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    m_context = std::exchange(other.m_context, nullptr);
    m_root = std::exchange(other.m_root, {});
    return *this;
}

bool UITreeUpdater::isAlive(UINodeId node) const noexcept
{
    return m_context != nullptr
        && m_context->isAliveInRoot(m_root, node);
}

Core::Status UITreeUpdater::setLayoutStyle(UINodeId node, const UILayoutStyle& style)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setLayoutStyleFromUpdater(m_root, node, style);
}

Core::Status UITreeUpdater::setPointerHitPolicy(
    UINodeId node,
    UIPointerHitPolicy policy)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setPointerHitPolicyFromUpdater(m_root, node, policy);
}

Core::Status UITreeUpdater::destroy(UINodeId node)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->destroyNodeFromUpdater(m_root, node);
}

Core::Result<std::unique_ptr<UIContext>> UIContext::Create(
    Platform::WindowId ownerWindow,
    UIContextCapacityConfig capacityConfig,
    std::pmr::memory_resource& resource)
{
    if (!ownerWindow.hasValue()) {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }

    auto normalizedResult = normalizeCapacity(capacityConfig);
    if (!normalizedResult) {
        return Core::failure(normalizedResult.error());
    }

    try {
        const std::thread::id ownerThreadId = std::this_thread::get_id();
        auto lifetime = std::make_shared<Detail::UIContextLifetimeControl>(
            ownerThreadId,
            normalizedResult->rootCapacity,
            normalizedResult->routedPointerListenerCapacity);
        auto implResult =
            Impl::Create(ownerWindow, *normalizedResult, lifetime, resource);
        if (!implResult) {
            return Core::failure(implResult.error());
        }

        auto context = std::unique_ptr<UIContext>(new UIContext(std::move(*implResult)));
        lifetime->context = context.get();
        return context;
    } catch (const std::bad_alloc&) {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI context allocation failed");
    } catch (const std::exception& exception) {
        return fail(Core::CoreErrorCode::Internal, std::string_view(exception.what()));
    } catch (...) {
        return fail(Core::CoreErrorCode::Internal, "UI context allocation failed");
    }
}

UIContext::UIContext(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

UIContext::~UIContext() noexcept
{
    if (m_impl) {
        if (!m_impl->isOwnerThread()
            || m_impl->routeDispatchDepth != 0
            || m_impl->listenerCallbackCleanupDepth != 0) {
            std::terminate();
        }
        m_impl->detachLifetime(this);
    }
}

Platform::WindowId UIContext::ownerWindow() const noexcept
{
    return m_impl->ownerWindow;
}

bool UIContext::contains(UINodeId node) const noexcept
{
    return m_impl->isOwnerThread() && m_impl->contains(node);
}

UIRootBuilder UIContext::rootBuilder() noexcept
{
    return UIRootBuilder(*this);
}

Core::Result<UITreeUpdater> UIContext::treeUpdater(UIRootOwner& rootOwner)
{
    if (Core::Status ownerThread = m_impl->ensureOwnerThread(); !ownerThread) {
        return Core::failure(ownerThread.error());
    }
    m_impl->drainDeferredRootDestroys();
    if (!rootOwner.hasValue()) {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a root owner");
    }
    if (rootOwner.rootNodeId().ownerWindow() != m_impl->ownerWindow) {
        return fail(
            UIErrorCode::WrongOwnerWindow,
            "UI root owner belongs to another owner window");
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime =
        rootOwner.m_lifetime.lock();
    if (!lifetime || lifetime->context == nullptr) {
        return fail(UIErrorCode::RootRequired, "UI root owner is detached");
    }
    if (lifetime->context != this) {
        return fail(UIErrorCode::WrongContext, "UI root owner belongs to another context");
    }
    if (!m_impl->contains(rootOwner.rootNodeId())) {
        return fail(UIErrorCode::RootRequired, "UI root owner is no longer alive");
    }
    return UITreeUpdater(*this, rootOwner.rootNodeId());
}

Core::Status UIContext::commitStructure()
{
    return m_impl->commitStructure();
}

UICommittedStructureView UIContext::committedStructure() const noexcept
{
    return m_impl->committedStructure();
}

Core::Status UIContext::commitLayout(UILogicalSize viewportSize)
{
    return m_impl->commitLayout(viewportSize);
}

UICommittedLayoutView UIContext::committedLayout() const noexcept
{
    return m_impl->committedLayout();
}

UICommittedHitView UIContext::committedHit() const noexcept
{
    return m_impl->committedHit();
}

UIPointerHitQueryResult UIContext::queryPointerHit(UILogicalPoint point) const noexcept
{
    return m_impl->queryPointerHit(point);
}

Core::Result<UIRoutedPointerListenerToken> UIContext::addRoutedPointerListener(
    UIRoutedPointerListenerDesc descriptor,
    UIRoutedPointerCallback callback)
{
    auto registration = m_impl->addRoutedPointerListener(
        descriptor,
        std::move(callback));
    if (!registration) {
        return Core::failure(registration.error());
    }
    return UIRoutedPointerListenerToken{
        m_impl->lifetime,
        registration->first,
        registration->second,
    };
}

Core::Result<UIPointerRouteResult> UIContext::routePointerInput(
    const UIPointerInputEvent& input)
{
    return m_impl->routePointerInput(input);
}

UIContextStatistics UIContext::statistics() const noexcept
{
    return m_impl->statistics();
}

usize UIContext::liveNodeCount() const noexcept
{
    return m_impl->nodes.activeCount();
}

usize UIContext::liveRootCount() const noexcept
{
    return m_impl->liveRootCount;
}

Core::Result<UIRootOwner> UIContext::createRoot()
{
    return m_impl->createRoot(*this);
}

Core::Result<UINodeId> UIContext::createChild(UINodeId parent, UIWidgetKind kind)
{
    return m_impl->createChild(parent, kind);
}

Core::Status UIContext::setLayoutStyleFromUpdater(
    UINodeId updaterRoot,
    UINodeId node,
    const UILayoutStyle& style)
{
    return m_impl->setLayoutStyleFromUpdater(updaterRoot, node, style);
}

Core::Status UIContext::setPointerHitPolicyFromUpdater(
    UINodeId updaterRoot,
    UINodeId node,
    UIPointerHitPolicy policy)
{
    return m_impl->setPointerHitPolicyFromUpdater(updaterRoot, node, policy);
}

Core::Status UIContext::destroyNodeFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->destroyFromUpdater(updaterRoot, node);
}

void UIContext::destroyRootFromOwner(UINodeId root) noexcept
{
    if (!m_impl->isOwnerThread()) {
        return;
    }
    m_impl->drainDeferredRootDestroys();
    m_impl->destroyRootImmediately(root);
}

bool UIContext::isAliveInRoot(UINodeId updaterRoot, UINodeId node) const noexcept
{
    if (!m_impl->isOwnerThread() || !updaterRoot.hasValue()) {
        return false;
    }
    return m_impl->isNodeWithinRoot(updaterRoot, node);
}

void UIContext::releaseRoutedPointerListenerFromToken(
    u32 slot,
    u32 generation) noexcept
{
    if (m_impl && m_impl->isOwnerThread()) {
        m_impl->deactivateRoutedPointerListener(slot, generation, false);
    }
}

} // namespace Tina::UI
