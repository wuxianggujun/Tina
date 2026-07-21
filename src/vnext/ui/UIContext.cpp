#include <tina/ui/UIContext.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIDirty.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/text/UIGlyphAtlas.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
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
    usize paintSnapshotCapacity = 0;
    usize routePathCapacity = 0;
    usize routedPointerListenerCapacity = 0;
    usize buttonActionCapacity = 0;
    usize textByteCapacity = 0;
};

struct TextByteAllocation final {
    u32 offset = 0;
    u32 capacity = 0;
};

struct WidgetTextState final {
    TextByteAllocation allocation{};
    u32 length = 0;
    UITextStyle style{};
    UITextMetrics metrics{};
    bool hasContent = false;
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

inline constexpr u32 InvalidButtonActionIndex =
    (std::numeric_limits<u32>::max)();

struct ButtonActionRecord final {
    UINodeId node{};
    UIButtonActionCallback callback{};
    u32 generation = 0;
    u32 nextFreeIndex = InvalidButtonActionIndex;
    u64 registrationSerial = 0;
    bool active = false;
    bool queuedForReclaim = false;
    bool invoking = false;
};

static_assert(std::is_nothrow_destructible_v<ButtonActionRecord>);

struct ButtonActionInvocationCandidate final {
    UINodeId button{};
    u32 actionIndex = InvalidButtonActionIndex;
    u32 generation = 0;

    [[nodiscard]] bool hasValue() const noexcept
    {
        return button.hasValue() && actionIndex != InvalidButtonActionIndex
            && generation != 0;
    }
};

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

struct LayoutPreparedInputs final {
    UIVisibility effectiveVisibility = UIVisibility::Visible;
    bool parentContentWidthDefinite = false;
    bool parentContentHeightDefinite = false;
    float parentContentWidth = 0.0F;
    float parentContentHeight = 0.0F;
    bool contentWidthDefinite = false;
    bool contentHeightDefinite = false;
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;

    auto operator<=>(const LayoutPreparedInputs&) const = default;
};

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
    // Prepare inputs remain stable after Arrange. The corresponding working
    // fields above are intentionally updated to final geometry during Arrange.
    LayoutPreparedInputs preparedInputs{};
};

struct LayoutPassStatistics final {
    usize passCount = 0;
    usize measuredNodeCount = 0;
    usize arrangedNodeCount = 0;
    usize percentMeasureFallbackCount = 0;
};

inline constexpr u8 LayoutWorkMeasure = 1u << 0;
inline constexpr u8 LayoutWorkArrange = 1u << 1;
inline constexpr u8 LayoutWorkMeasureComplete = 1u << 2;
inline constexpr u8 LayoutWorkArrangeComplete = 1u << 3;

[[nodiscard]] constexpr bool hasLayoutWork(u8 work, u8 flag) noexcept
{
    return (work & flag) != 0;
}

[[nodiscard]] constexpr u8 layoutSubtreeCompletionMask(u8 work) noexcept
{
    u8 mask = 0;
    if ((work & LayoutWorkMeasure) != 0) {
        mask |= LayoutWorkMeasureComplete;
    }
    if ((work & LayoutWorkArrange) != 0) {
        mask |= LayoutWorkArrangeComplete;
    }
    return mask;
}

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

[[nodiscard]] UIBoxPaint normalizeBoxPaint(UIBoxPaint paint) noexcept
{
    if (paint.solidFill.has_value()
        && paint.solidFill->color.alpha == 0) {
        paint.solidFill.reset();
    }
    return paint;
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
    const usize paintSnapshotCapacity = config.paintSnapshotCapacity == 0
        ? config.nodeCapacity
        : config.paintSnapshotCapacity;
    const usize routePathCapacity = config.routePathCapacity == 0
        ? config.nodeCapacity
        : config.routePathCapacity;
    const usize routedPointerListenerCapacity =
        config.routedPointerListenerCapacity == 0
        ? config.nodeCapacity
        : config.routedPointerListenerCapacity;
    const usize buttonActionCapacity = config.buttonActionCapacity == 0
        ? config.nodeCapacity
        : config.buttonActionCapacity;
    const usize textByteCapacity = config.textByteCapacity == 0
        ? UIContextCapacityConfig::DefaultTextByteCapacity
        : config.textByteCapacity;
    return NormalizedCapacityConfig{
        .nodeCapacity = config.nodeCapacity,
        .rootCapacity = config.rootCapacity,
        .dirtyQueueCapacity = dirtyQueueCapacity,
        .layoutSnapshotCapacity = layoutSnapshotCapacity,
        .hitSnapshotCapacity = hitSnapshotCapacity,
        .paintSnapshotCapacity = paintSnapshotCapacity,
        .routePathCapacity = routePathCapacity,
        .routedPointerListenerCapacity = routedPointerListenerCapacity,
        .buttonActionCapacity = buttonActionCapacity,
        .textByteCapacity = textByteCapacity,
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
    std::pmr::vector<UIBoxPaint> boxPaintsByIndex;
    std::pmr::vector<UIPremultipliedRgba8Color> localSolidFillCacheByIndex;
    std::pmr::vector<UIPremultipliedRgba8Color> localTextColorCacheByIndex;
    std::pmr::vector<WidgetTextState> textStatesByIndex;
    std::pmr::vector<char> textBytes;
    std::pmr::vector<TextByteAllocation> freeTextAllocations;
    usize textByteUsed = 0;
    usize textByteHighWater = 0;
    std::unique_ptr<IUITextRasterizer> textRasterizer;
    UIFontFaceId textFace{};
    std::unique_ptr<UIGlyphAtlas> glyphAtlas;
    // Paint-local scratch: copies glyph advances out of a borrow-local raster
    // batch so paint can walk UTF-8 (including newlines) without use-after-free.
    std::pmr::vector<float> textPaintAdvanceScratch;
    std::pmr::vector<UIDirty> dirtyByIndex;
    std::pmr::vector<u8> dirtyQueuedByIndex;
    std::pmr::vector<UINodeId> dirtyQueue;
    std::pmr::vector<LayoutScratchState> layoutScratchByIndex;
    std::pmr::vector<u8> layoutWorkByIndex;
    std::pmr::vector<u32> layoutOrderScratch;
    std::pmr::vector<u32> hitEntryIndexByNodeIndex;
    std::pmr::vector<u32> routedPointerListenerHeadByNodeIndex;
    std::pmr::vector<u32> routedPointerListenerTailByNodeIndex;
    std::pmr::vector<RoutedPointerListenerRecord> routedPointerListeners;
    std::pmr::vector<u32> routePathScratch;
    std::pmr::vector<u32> inactiveRoutedPointerListenerIndices;
    std::pmr::vector<u32> buttonActionIndexByNodeIndex;
    std::pmr::vector<u64> buttonActionClearRouteSerialByNodeIndex;
    // M11-C0: Checkbox checked bit (index-aligned with nodes; false for non-Checkbox).
    std::pmr::vector<u8> checkboxCheckedByNodeIndex;
    std::pmr::vector<ButtonActionRecord> buttonActions;
    std::pmr::vector<u32> inactiveButtonActionIndices;
    std::array<std::pmr::vector<UICommittedNodeEntry>, 2> committedBuffers;
    std::array<std::pmr::vector<UICommittedLayoutEntry>, 2> committedLayoutBuffers;
    std::array<std::pmr::vector<UICommittedHitEntry>, 2> committedHitBuffers;
    std::array<std::pmr::vector<UICommittedPaintEntry>, 2> committedPaintBuffers;
    std::vector<UINodeId> deferredRootDestroyBuffer;
    std::vector<Detail::DeferredRoutedPointerListenerRelease>
        deferredRoutedPointerListenerReleaseBuffer;
    usize publishedBufferIndex = 0;
    usize publishedLayoutBufferIndex = 0;
    usize publishedHitBufferIndex = 0;
    usize publishedPaintBufferIndex = 0;
    u64 committedRevision = 0;
    u64 committedLayoutRevision = 0;
    u64 committedLayoutStructureRevision = 0;
    u64 committedHitRevision = 0;
    u64 committedHitStructureRevision = 0;
    u64 committedHitLayoutRevision = 0;
    u64 committedHitPaintOrderRevision = 0;
    u64 committedPaintRevision = 0;
    u64 committedPaintStructureRevision = 0;
    u64 committedPaintLayoutRevision = 0;
    u64 committedPaintOrderRevision = 0;
    UILogicalSize committedPaintViewportSize{};
    UILogicalSize committedViewportSize{};
    bool hasCommittedViewport = false;
    usize liveRootCount = 0;
    u32 firstRootIndex = InvalidNodeIndex;
    u32 lastRootIndex = InvalidNodeIndex;
    bool structureDirty = false;
    bool layoutDirty = false;
    bool hitDirty = false;
    bool paintDirty = false;
    // A failed candidate may have partially mutated layout scratch. The next
    // layout attempt must rebuild from scratch before reuse is enabled again.
    bool layoutReuseCacheValid = false;
    bool layoutReuseInProgress = false;
    LayoutPassStatistics lastLayoutPass{};
    usize dirtyQueueHighWater = 0;
    usize committedHitTargetCount = 0;
    usize lastHitRebuildCount = 0;
    usize lastPaintCacheRebuildCount = 0;
    usize lastPaintSnapshotRebuildCount = 0;
    u32 freeRoutedPointerListenerHead = InvalidRoutedPointerListenerIndex;
    usize activeRoutedPointerListenerCount = 0;
    usize routedPointerListenerHighWater = 0;
    u64 routedPointerListenerRegistrationSerial = 0;
    usize routeDispatchDepth = 0;
    usize listenerCallbackOperationDepth = 0;
    bool reclaimingInactiveRoutedPointerListeners = false;
    u32 freeButtonActionHead = InvalidButtonActionIndex;
    usize activeButtonActionCount = 0;
    usize buttonActionHighWater = 0;
    u64 buttonActionRegistrationSerial = 0;
    u64 buttonRouteSerial = 0;
    usize buttonActionCallbackOperationDepth = 0;
    bool reclaimingInactiveButtonActions = false;
    UINodeId armedPrimaryButton{};
    bool armedPrimaryButtonPressed = false;
    // Last Button that received Primary Pointer arm. Keyboard/Gamepad Accept
    // activates this node without requiring a live pointer press.
    UINodeId defaultActionFocusButton{};
    // Last Label that received Primary Pointer Down (IME/text target).
    UINodeId imeFocusLabel{};
    static constexpr usize MaxImePreeditBytes = 512;
    std::array<char, MaxImePreeditBytes> imePreeditBytes_{};
    usize imePreeditSize_ = 0;
    u32 imePreeditCursor_ = 0;
    bool imeCompositionActive_ = false;

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
          boxPaintsByIndex(&resource),
          localSolidFillCacheByIndex(&resource),
          localTextColorCacheByIndex(&resource),
          textStatesByIndex(&resource),
          textBytes(&resource),
          freeTextAllocations(&resource),
          textPaintAdvanceScratch(&resource),
          dirtyByIndex(&resource),
          dirtyQueuedByIndex(&resource),
          dirtyQueue(&resource),
          layoutScratchByIndex(&resource),
          layoutWorkByIndex(&resource),
          layoutOrderScratch(&resource),
          hitEntryIndexByNodeIndex(&resource),
          routedPointerListenerHeadByNodeIndex(&resource),
          routedPointerListenerTailByNodeIndex(&resource),
          routedPointerListeners(&resource),
          routePathScratch(&resource),
          inactiveRoutedPointerListenerIndices(&resource),
          buttonActionIndexByNodeIndex(&resource),
          checkboxCheckedByNodeIndex(&resource),
          buttonActionClearRouteSerialByNodeIndex(&resource),
          buttonActions(&resource),
          inactiveButtonActionIndices(&resource),
          committedBuffers{
              std::pmr::vector<UICommittedNodeEntry>(&resource),
              std::pmr::vector<UICommittedNodeEntry>(&resource)},
          committedLayoutBuffers{
              std::pmr::vector<UICommittedLayoutEntry>(&resource),
              std::pmr::vector<UICommittedLayoutEntry>(&resource)},
          committedHitBuffers{
              std::pmr::vector<UICommittedHitEntry>(&resource),
              std::pmr::vector<UICommittedHitEntry>(&resource)},
          committedPaintBuffers{
              std::pmr::vector<UICommittedPaintEntry>(&resource),
              std::pmr::vector<UICommittedPaintEntry>(&resource)}
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
            .paintSnapshotCapacity = normalized.paintSnapshotCapacity,
            .routePathCapacity = normalized.routePathCapacity,
            .routedPointerListenerCapacity = normalized.routedPointerListenerCapacity,
            .buttonActionCapacity = normalized.buttonActionCapacity,
            .textByteCapacity = normalized.textByteCapacity,
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
        impl->boxPaintsByIndex.resize(normalized.nodeCapacity);
        impl->localSolidFillCacheByIndex.resize(normalized.nodeCapacity);
        impl->localTextColorCacheByIndex.resize(normalized.nodeCapacity);
        impl->textStatesByIndex.resize(normalized.nodeCapacity);
        impl->textBytes.resize(normalized.textByteCapacity, '\0');
        impl->freeTextAllocations.reserve(normalized.nodeCapacity);
        impl->dirtyByIndex.resize(normalized.nodeCapacity, UIDirty::None);
        impl->dirtyQueuedByIndex.resize(normalized.nodeCapacity, 0);
        impl->dirtyQueue.reserve(normalized.dirtyQueueCapacity);
        impl->layoutScratchByIndex.resize(normalized.nodeCapacity);
        impl->layoutWorkByIndex.resize(normalized.nodeCapacity, 0);
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
        impl->buttonActionIndexByNodeIndex.resize(
            normalized.nodeCapacity,
            InvalidButtonActionIndex);
        impl->buttonActionClearRouteSerialByNodeIndex.resize(
            normalized.nodeCapacity,
            0);
        impl->checkboxCheckedByNodeIndex.resize(normalized.nodeCapacity, 0);
        const usize buttonActionStorageCapacity = normalized.buttonActionCapacity + 1;
        impl->buttonActions.resize(buttonActionStorageCapacity);
        for (usize actionIndex = 0;
             actionIndex < buttonActionStorageCapacity;
             ++actionIndex) {
            ButtonActionRecord& action = impl->buttonActions[actionIndex];
            action.nextFreeIndex = actionIndex + 1 < buttonActionStorageCapacity
                ? static_cast<u32>(actionIndex + 1)
                : InvalidButtonActionIndex;
        }
        impl->freeButtonActionHead = buttonActionStorageCapacity == 0
            ? InvalidButtonActionIndex
            : 0;
        impl->inactiveButtonActionIndices.reserve(buttonActionStorageCapacity);
        impl->committedBuffers[0].reserve(normalized.nodeCapacity);
        impl->committedBuffers[1].reserve(normalized.nodeCapacity);
        impl->committedLayoutBuffers[0].reserve(normalized.layoutSnapshotCapacity);
        impl->committedLayoutBuffers[1].reserve(normalized.layoutSnapshotCapacity);
        impl->committedHitBuffers[0].reserve(normalized.hitSnapshotCapacity);
        impl->committedHitBuffers[1].reserve(normalized.hitSnapshotCapacity);
        impl->committedPaintBuffers[0].reserve(normalized.paintSnapshotCapacity);
        impl->committedPaintBuffers[1].reserve(normalized.paintSnapshotCapacity);
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

    void markLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept
    {
        const u8 completion = layoutSubtreeCompletionMask(work);
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex) {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr) {
                return;
            }
            layoutWorkByIndex[currentIndex] |= work | completion;

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

    void ensureLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept
    {
        if (rootIndex >= layoutWorkByIndex.size()) {
            return;
        }
        const u8 requiredCompletion = layoutSubtreeCompletionMask(work);
        if ((layoutWorkByIndex[rootIndex] & requiredCompletion)
            == requiredCompletion) {
            return;
        }
        markLayoutSubtreeWork(rootIndex, work);
    }

    void markLayoutAncestorsWork(u32 nodeIndex, u8 work) noexcept
    {
        u32 currentIndex = nodeIndex;
        while (currentIndex != InvalidNodeIndex) {
            layoutWorkByIndex[currentIndex] |= work;
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr) {
                return;
            }
            currentIndex = record->parentIndex;
        }
    }

    void initializeLayoutWork(
        const std::pmr::vector<u32>& order,
        bool allowReuse) noexcept
    {
        for (const u32 index : order) {
            layoutWorkByIndex[index] = 0;
        }

        if (!allowReuse) {
            for (const u32 index : order) {
                layoutWorkByIndex[index] = LayoutWorkMeasure | LayoutWorkArrange;
            }
            return;
        }

        for (const u32 index : order) {
            const UIDirty dirty = dirtyByIndex[index];
            if (hasDirty(dirty, UIDirty::Measure)) {
                layoutWorkByIndex[index] |= LayoutWorkMeasure | LayoutWorkArrange;
            } else if (hasDirty(dirty, UIDirty::Arrange)) {
                layoutWorkByIndex[index] |= LayoutWorkArrange;
            }
            if (hasDirty(dirty, UIDirty::Style)) {
                // A direct style change can alter the containing basis or
                // effective visibility of any descendant.
                ensureLayoutSubtreeWork(
                    index,
                    LayoutWorkMeasure | LayoutWorkArrange);
                markLayoutAncestorsWork(index, LayoutWorkArrange);
            }
        }
    }

    [[nodiscard]] static bool samePreparedLayoutInputs(
        const LayoutPreparedInputs& previous,
        const LayoutPreparedInputs& current) noexcept
    {
        return previous == current;
    }

    void prepareLayoutState(
        UILogicalSize viewportSize,
        const std::pmr::vector<u32>& order,
        bool allowReuse) noexcept
    {
        initializeLayoutWork(order, allowReuse);
        for (const u32 index : order) {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr) {
                continue;
            }
            const UILayoutStyle& style = layoutStylesByIndex[index];
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            const LayoutPreparedInputs previous = scratch.preparedInputs;
            if (!allowReuse) {
                scratch = {};
            }

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

            const LayoutPreparedInputs currentInputs{
                .effectiveVisibility = scratch.effectiveVisibility,
                .parentContentWidthDefinite = scratch.parentContentWidthDefinite,
                .parentContentHeightDefinite = scratch.parentContentHeightDefinite,
                .parentContentWidth = scratch.parentContentWidth,
                .parentContentHeight = scratch.parentContentHeight,
                .contentWidthDefinite = scratch.contentWidthDefinite,
                .contentHeightDefinite = scratch.contentHeightDefinite,
                .contentWidth = scratch.contentWidth,
                .contentHeight = scratch.contentHeight,
            };
            scratch.preparedInputs = currentInputs;

            if (allowReuse && !samePreparedLayoutInputs(previous, currentInputs)) {
                // Parent constraint or effective visibility changes can
                // invalidate every descendant even when only an ancestor was
                // explicitly queued dirty.
                ensureLayoutSubtreeWork(
                    index,
                    LayoutWorkMeasure | LayoutWorkArrange);
                markLayoutAncestorsWork(index, LayoutWorkArrange);
            }
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
            if (!hasLayoutWork(layoutWorkByIndex[index], LayoutWorkMeasure)) {
                continue;
            }
            const UILayoutStyle& style = layoutStylesByIndex[index];
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            const UILogicalSize previousMeasuredSize = scratch.measuredSize;
            ++statistics.measuredNodeCount;

            if (scratch.effectiveVisibility == UIVisibility::Collapsed) {
                scratch.measuredSize = {};
                if (scratch.measuredSize != previousMeasuredSize) {
                    ensureLayoutSubtreeWork(index, LayoutWorkArrange);
                }
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

            if (flowChildCount == 0
                && (record->kind == UIWidgetKind::Label
                    || record->kind == UIWidgetKind::Button)
                && index < textStatesByIndex.size()
                && textStatesByIndex[index].hasContent) {
                const UILogicalSize textSize =
                    textStatesByIndex[index].metrics.measuredSize;
                autoContentWidth = textSize.width;
                autoContentHeight = textSize.height;
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
            if (scratch.measuredSize != previousMeasuredSize) {
                ensureLayoutSubtreeWork(index, LayoutWorkArrange);
            }
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
        const UILogicalRect previousWorldRect = scratch.worldRect;
        const UILogicalRect previousLocalRect = scratch.localRect;
        const UILogicalRect previousEffectiveClip = scratch.effectiveClip;
        const UIVisibility previousVisibility = scratch.effectiveVisibility;
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

        if (layoutReuseInProgress
            && (previousWorldRect != scratch.worldRect
                || previousLocalRect != scratch.localRect
                || previousEffectiveClip != scratch.effectiveClip
                || previousVisibility != scratch.effectiveVisibility)) {
            ensureLayoutSubtreeWork(index, LayoutWorkArrange);
        }
    }

    void refreshMeasuredSizeForParentContent(
        u32 childIndex,
        UILogicalRect parentContentRect,
        LayoutPassStatistics& statistics) noexcept
    {
        const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
        LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
        const UILogicalSize previousMeasuredSize = childScratch.measuredSize;
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
        if (layoutReuseInProgress
            && childScratch.measuredSize != previousMeasuredSize) {
            ensureLayoutSubtreeWork(childIndex, LayoutWorkArrange);
        }
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
            if (!hasLayoutWork(layoutWorkByIndex[index], LayoutWorkArrange)) {
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

    [[nodiscard]] static usize countDrawableTextCodepoints(
        std::string_view utf8) noexcept
    {
        usize count = 0;
        usize index = 0;
        while (index < utf8.size()) {
            const auto first = static_cast<unsigned char>(utf8[index]);
            usize unitLength = 1;
            if (first <= 0x7FU) {
                unitLength = 1;
            } else if ((first & 0xE0U) == 0xC0U) {
                unitLength = 2;
            } else if ((first & 0xF0U) == 0xE0U) {
                unitLength = 3;
            } else {
                unitLength = 4;
            }
            if (unitLength > utf8.size() - index) {
                break;
            }
            if (!(unitLength == 1 && first == '\n')) {
                ++count;
            }
            index += unitLength;
        }
        return count;
    }

    [[nodiscard]] Core::Result<usize> validatePaintCandidateCapacity(
        std::span<const UICommittedLayoutEntry> layoutEntries) const
    {
        usize paintEntryCount = 0;
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries) {
            if (layoutEntry.effectiveVisibility != UIVisibility::Visible) {
                continue;
            }
            if (!contains(layoutEntry.node)) {
                return fail(
                    UIErrorCode::InvalidNode,
                    "UI paint snapshot layout references a stale node");
            }
            const u32 nodeIndex = layoutEntry.node.index();
            const UIBoxPaint& paint = boxPaintsByIndex[nodeIndex];
            if (paint.solidFill.has_value()
                && paint.solidFill->color.alpha != 0) {
                ++paintEntryCount;
            }
            if (nodeIndex < textStatesByIndex.size()
                && textStatesByIndex[nodeIndex].hasContent
                && textStatesByIndex[nodeIndex].style.color.alpha != 0) {
                paintEntryCount += countDrawableTextCodepoints(
                    textViewFor(nodeIndex));
            }
            // Active IME preedit is painted after the committed Label text.
            if (imeCompositionActive_
                && imeFocusLabel.hasValue()
                && layoutEntry.node == imeFocusLabel
                && imePreeditSize_ > 0) {
                paintEntryCount += countDrawableTextCodepoints(
                    std::string_view(imePreeditBytes_.data(), imePreeditSize_));
            }
            // M7-E8: one caret solid for the IME-focused Label.
            if (imeFocusLabel.hasValue()
                && layoutEntry.node == imeFocusLabel
                && isLiveLabel(imeFocusLabel)) {
                ++paintEntryCount;
            }
        }
        if (paintEntryCount > capacityConfig.paintSnapshotCapacity) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI committed paint snapshot capacity has been exhausted");
        }
        return paintEntryCount;
    }

    [[nodiscard]] usize rebuildDirtyPaintCaches(
        std::span<const UICommittedLayoutEntry> layoutEntries) noexcept
    {
        usize rebuildCount = 0;
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries) {
            const u32 nodeIndex = layoutEntry.node.index();
            if (nodeIndex >= dirtyByIndex.size()
                || !hasDirty(dirtyByIndex[nodeIndex], UIDirty::Paint)) {
                continue;
            }

            const UIBoxPaint& paint = boxPaintsByIndex[nodeIndex];
            localSolidFillCacheByIndex[nodeIndex] = paint.solidFill.has_value()
                ? premultiply(paint.solidFill->color)
                : UIPremultipliedRgba8Color{};
            localTextColorCacheByIndex[nodeIndex] =
                nodeIndex < textStatesByIndex.size()
                    && textStatesByIndex[nodeIndex].hasContent
                ? premultiply(textStatesByIndex[nodeIndex].style.color)
                : UIPremultipliedRgba8Color{};
            ++rebuildCount;
        }
        return rebuildCount;
    }

    // Shared UTF-8 → glyph/solid paint emitter. Returns final cursor after the
    // last drawable codepoint (for chaining IME preedit after committed text).
    struct TextPaintCursor final {
        float x = 0.0F;
        float y = 0.0F;
        float lineHeight = 0.0F;
        float baseX = 0.0F;
    };

    void appendUtf8TextPaints(
        std::pmr::vector<UICommittedPaintEntry>& output,
        const UICommittedLayoutEntry& layoutEntry,
        u32& nextPaintOrdinal,
        std::string_view utf8,
        const UITextStyle& style,
        UIPremultipliedRgba8Color color,
        float startX,
        float startY,
        TextPaintCursor* outCursor) noexcept
    {
        if (outCursor != nullptr) {
            *outCursor = TextPaintCursor{
                .x = startX,
                .y = startY,
                .lineHeight = style.logicalSize * style.lineHeightScale,
                .baseX = layoutEntry.worldRect.x,
            };
        }
        if (utf8.empty() || color.isTransparent()) {
            return;
        }
        const float fallbackAdvance = style.logicalSize * style.advanceScale;
        const float lineHeight = style.logicalSize * style.lineHeightScale;
        if (!(std::isfinite(fallbackAdvance) && fallbackAdvance > 0.0F
              && std::isfinite(lineHeight) && lineHeight > 0.0F)) {
            return;
        }
        const u32 pixelSize = static_cast<u32>(
            (std::max)(1.0F, std::floor(style.logicalSize)));

        if (textRasterizer && textFace.hasValue() && glyphAtlas) {
            auto batch = textRasterizer->raster(textFace, utf8, style);
            if (batch) {
                const usize outputBase = output.size();
                const u32 ordinalBase = nextPaintOrdinal;
                float cursorX = startX;
                float cursorY = startY;
                usize glyphIndex = 0;
                usize index = 0;
                bool usedAtlasPath = true;
                while (index < utf8.size()) {
                    const auto first = static_cast<unsigned char>(utf8[index]);
                    usize unitLength = 1;
                    if (first <= 0x7FU) {
                        unitLength = 1;
                    } else if ((first & 0xE0U) == 0xC0U) {
                        unitLength = 2;
                    } else if ((first & 0xF0U) == 0xE0U) {
                        unitLength = 3;
                    } else {
                        unitLength = 4;
                    }
                    if (unitLength > utf8.size() - index) {
                        usedAtlasPath = false;
                        break;
                    }
                    if (unitLength == 1 && first == '\n') {
                        cursorX = layoutEntry.worldRect.x;
                        cursorY = normalizeFloat(cursorY + lineHeight);
                        index += unitLength;
                        continue;
                    }
                    if (glyphIndex >= batch->glyphs.size()) {
                        usedAtlasPath = false;
                        break;
                    }
                    const UITextGlyphRaster& glyph = batch->glyphs[glyphIndex];
                    ++glyphIndex;
                    float advance = glyph.advance;
                    if (!(std::isfinite(advance) && advance > 0.0F)) {
                        advance = fallbackAdvance;
                    }

                    std::span<const u8> coverage{};
                    if (glyph.width > 0 && glyph.height > 0) {
                        const usize coverageBytes =
                            static_cast<usize>(glyph.width) * glyph.height;
                        if (glyph.coverageOffset + coverageBytes
                            > batch->coverage.size()) {
                            usedAtlasPath = false;
                            break;
                        }
                        coverage = std::span<const u8>(
                            batch->coverage.data() + glyph.coverageOffset,
                            coverageBytes);
                    }
                    auto placed = glyphAtlas->insert(
                        UIGlyphKey{
                            .face = textFace,
                            .codepoint = glyph.codepoint,
                            .pixelSize = pixelSize,
                        },
                        glyph,
                        coverage);
                    if (!placed) {
                        usedAtlasPath = false;
                        break;
                    }

                    const float drawX = normalizeFloat(cursorX + glyph.bearingX);
                    const float drawY = normalizeFloat(
                        cursorY + (lineHeight - glyph.bearingY));
                    const float drawW = placed->width > 0
                        ? static_cast<float>(placed->width)
                        : advance;
                    const float drawH = placed->height > 0
                        ? static_cast<float>(placed->height)
                        : lineHeight;

                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect =
                            UILogicalRect{
                                .x = drawX,
                                .y = drawY,
                                .width = normalizeFloat((std::max)(0.0F, drawW)),
                                .height = normalizeFloat((std::max)(0.0F, drawH)),
                            },
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal,
                        .solidFill = color,
                        .isGlyph = placed->width > 0 && placed->height > 0,
                        .atlasX = placed->atlasX,
                        .atlasY = placed->atlasY,
                        .atlasWidth = placed->width,
                        .atlasHeight = placed->height,
                        .atlasPage = 0,
                    });
                    ++nextPaintOrdinal;
                    cursorX = normalizeFloat(cursorX + advance);
                    index += unitLength;
                }
                if (usedAtlasPath) {
                    if (outCursor != nullptr) {
                        outCursor->x = cursorX;
                        outCursor->y = cursorY;
                        outCursor->lineHeight = lineHeight;
                        outCursor->baseX = layoutEntry.worldRect.x;
                    }
                    return;
                }
                while (output.size() > outputBase) {
                    output.pop_back();
                }
                nextPaintOrdinal = ordinalBase;
            }
        }

        float cursorX = startX;
        float cursorY = startY;
        usize index = 0;
        while (index < utf8.size()) {
            const auto first = static_cast<unsigned char>(utf8[index]);
            usize unitLength = 1;
            if (first <= 0x7FU) {
                unitLength = 1;
            } else if ((first & 0xE0U) == 0xC0U) {
                unitLength = 2;
            } else if ((first & 0xF0U) == 0xE0U) {
                unitLength = 3;
            } else {
                unitLength = 4;
            }
            if (unitLength > utf8.size() - index) {
                break;
            }
            if (unitLength == 1 && first == '\n') {
                cursorX = layoutEntry.worldRect.x;
                cursorY = normalizeFloat(cursorY + lineHeight);
                index += unitLength;
                continue;
            }
            output.push_back(UICommittedPaintEntry{
                .node = layoutEntry.node,
                .worldRect =
                    UILogicalRect{
                        .x = normalizeFloat(cursorX),
                        .y = normalizeFloat(cursorY),
                        .width = normalizeFloat(fallbackAdvance),
                        .height = normalizeFloat(lineHeight),
                    },
                .effectiveClip = layoutEntry.effectiveClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = color,
                .isGlyph = false,
            });
            ++nextPaintOrdinal;
            cursorX = normalizeFloat(cursorX + fallbackAdvance);
            index += unitLength;
        }
        if (outCursor != nullptr) {
            outCursor->x = cursorX;
            outCursor->y = cursorY;
            outCursor->lineHeight = lineHeight;
            outCursor->baseX = layoutEntry.worldRect.x;
        }
    }

    void appendTextGlyphPaints(
        std::pmr::vector<UICommittedPaintEntry>& output,
        const UICommittedLayoutEntry& layoutEntry,
        u32& nextPaintOrdinal) noexcept
    {
        const u32 nodeIndex = layoutEntry.node.index();
        TextPaintCursor cursor{
            .x = layoutEntry.worldRect.x,
            .y = layoutEntry.worldRect.y,
            .lineHeight = 0.0F,
            .baseX = layoutEntry.worldRect.x,
        };

        if (nodeIndex < textStatesByIndex.size()
            && textStatesByIndex[nodeIndex].hasContent) {
            const UIPremultipliedRgba8Color color =
                localTextColorCacheByIndex[nodeIndex];
            if (!color.isTransparent()) {
                const WidgetTextState& state = textStatesByIndex[nodeIndex];
                appendUtf8TextPaints(
                    output,
                    layoutEntry,
                    nextPaintOrdinal,
                    textViewFor(nodeIndex),
                    state.style,
                    color,
                    layoutEntry.worldRect.x,
                    layoutEntry.worldRect.y,
                    &cursor);
            }
        }

        // M7-E7: paint active IME preedit after committed text on the focus Label.
        if (imeCompositionActive_
            && imeFocusLabel.hasValue()
            && layoutEntry.node == imeFocusLabel
            && imePreeditSize_ > 0) {
            UITextStyle style{};
            if (nodeIndex < textStatesByIndex.size()) {
                style = textStatesByIndex[nodeIndex].style;
            }
            // Distinct cyan-ish preedit tint (premultiplied).
            const UIPremultipliedRgba8Color preeditColor =
                premultiply(UIStraightSrgba8Color{
                    .red = 0,
                    .green = 180,
                    .blue = 255,
                    .alpha = 255,
                });
            const float startX = (nodeIndex < textStatesByIndex.size()
                                  && textStatesByIndex[nodeIndex].hasContent)
                ? cursor.x
                : layoutEntry.worldRect.x;
            const float startY = (nodeIndex < textStatesByIndex.size()
                                  && textStatesByIndex[nodeIndex].hasContent)
                ? cursor.y
                : layoutEntry.worldRect.y;
            appendUtf8TextPaints(
                output,
                layoutEntry,
                nextPaintOrdinal,
                std::string_view(imePreeditBytes_.data(), imePreeditSize_),
                style,
                preeditColor,
                startX,
                startY,
                &cursor);
        }

        // M7-E8: caret after committed text (+ preedit when composing).
        if (imeFocusLabel.hasValue()
            && layoutEntry.node == imeFocusLabel
            && isLiveLabel(imeFocusLabel)) {
            float lineHeight = cursor.lineHeight;
            if (!(std::isfinite(lineHeight) && lineHeight > 0.0F)) {
                if (nodeIndex < textStatesByIndex.size()) {
                    const UITextStyle& style = textStatesByIndex[nodeIndex].style;
                    lineHeight = style.logicalSize * style.lineHeightScale;
                } else {
                    lineHeight = 16.0F * 1.2F;
                }
            }
            if (!(std::isfinite(lineHeight) && lineHeight > 0.0F)) {
                lineHeight = 19.2F;
            }
            constexpr float CaretWidth = 2.0F;
            const UIPremultipliedRgba8Color caretColor =
                premultiply(UIStraightSrgba8Color{
                    .red = 255,
                    .green = 255,
                    .blue = 255,
                    .alpha = 255,
                });
            output.push_back(UICommittedPaintEntry{
                .node = layoutEntry.node,
                .worldRect =
                    UILogicalRect{
                        .x = normalizeFloat(cursor.x),
                        .y = normalizeFloat(cursor.y),
                        .width = CaretWidth,
                        .height = normalizeFloat(lineHeight),
                    },
                .effectiveClip = layoutEntry.effectiveClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = caretColor,
                .isGlyph = false,
            });
            ++nextPaintOrdinal;
        }
    }

    void buildCommittedPaint(
        std::pmr::vector<UICommittedPaintEntry>& output,
        std::span<const UICommittedLayoutEntry> layoutEntries) noexcept
    {
        output.clear();
        u32 nextPaintOrdinal = 1;
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries) {
            if (layoutEntry.effectiveVisibility != UIVisibility::Visible) {
                continue;
            }
            const u32 nodeIndex = layoutEntry.node.index();
            const UIPremultipliedRgba8Color fill =
                localSolidFillCacheByIndex[nodeIndex];
            if (!fill.isTransparent()) {
                output.push_back(UICommittedPaintEntry{
                    .node = layoutEntry.node,
                    .worldRect = layoutEntry.worldRect,
                    .effectiveClip = layoutEntry.effectiveClip,
                    .paintOrdinal = nextPaintOrdinal,
                    .solidFill = fill,
                });
                ++nextPaintOrdinal;
            }
            appendTextGlyphPaints(output, layoutEntry, nextPaintOrdinal);
        }
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

        ++listenerCallbackOperationDepth;
        auto callbackOperation = Core::makeScopeExit([this]() noexcept {
            --listenerCallbackOperationDepth;
        });
        UIRoutedPointerCallback detachedCallback(std::move(listener.callback));

        listener.nextFreeIndex = freeRoutedPointerListenerHead;
        freeRoutedPointerListenerHead = listenerIndex;
        detachedCallback.reset();
    }

    void reclaimInactiveRoutedPointerListeners() noexcept
    {
        if (routeDispatchDepth != 0
            || listenerCallbackOperationDepth != 0
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

    [[nodiscard]] Core::Result<std::pair<u32, u32>>
    rollbackRoutedPointerListenerRegistration(
        u32 listenerIndex,
        Core::Error error)
    {
        recycleRoutedPointerListener(listenerIndex);
        reclaimInactiveRoutedPointerListeners();
        return Core::failure(std::move(error));
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
            || listenerCallbackOperationDepth != 0
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

    [[nodiscard]] Core::Result<NodeRecord*> resolveButton(UINodeId button)
    {
        auto nodeResult = resolveNode(button);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::Button
            && (*nodeResult)->kind != UIWidgetKind::Checkbox) {
            return fail(
                UIErrorCode::InvalidButtonAction,
                "UI Button action requires a Button or Checkbox node");
        }
        return *nodeResult;
    }

    [[nodiscard]] static bool isDefaultActivatableKind(UIWidgetKind kind) noexcept
    {
        return kind == UIWidgetKind::Button || kind == UIWidgetKind::Checkbox;
    }

    void clearArmedPrimaryButton() noexcept
    {
        armedPrimaryButton = {};
        armedPrimaryButtonPressed = false;
    }

    void clearDefaultActionFocus() noexcept
    {
        defaultActionFocusButton = {};
    }

    void clearImeComposition() noexcept
    {
        const bool wasActive = imeCompositionActive_;
        const UINodeId focus = imeFocusLabel;
        imeCompositionActive_ = false;
        imePreeditSize_ = 0;
        imePreeditCursor_ = 0;
        if (wasActive && focus.hasValue() && contains(focus)) {
            static_cast<void>(markPaintDirty(focus));
        }
    }

    void clearImeFocus() noexcept
    {
        clearImeComposition();
        imeFocusLabel = {};
    }

    [[nodiscard]] bool isLiveLabel(UINodeId node) const noexcept
    {
        if (!node.hasValue() || !contains(node)) {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        return record != nullptr && record->kind == UIWidgetKind::Label;
    }

    void recycleButtonAction(u32 actionIndex) noexcept
    {
        if (actionIndex >= buttonActions.size()) {
            return;
        }
        ButtonActionRecord& action = buttonActions[actionIndex];
        if (action.node.hasValue()
            && action.node.index() < buttonActionIndexByNodeIndex.size()
            && buttonActionIndexByNodeIndex[action.node.index()] == actionIndex) {
            buttonActionIndexByNodeIndex[action.node.index()] = InvalidButtonActionIndex;
        }

        action.node = {};
        action.registrationSerial = 0;
        action.active = false;
        action.queuedForReclaim = false;
        action.invoking = false;
        action.nextFreeIndex = InvalidButtonActionIndex;

        ++buttonActionCallbackOperationDepth;
        auto callbackOperation = Core::makeScopeExit([this]() noexcept {
            --buttonActionCallbackOperationDepth;
        });
        UIButtonActionCallback detachedCallback(std::move(action.callback));

        action.nextFreeIndex = freeButtonActionHead;
        freeButtonActionHead = actionIndex;
        detachedCallback.reset();
    }

    void reclaimInactiveButtonActions() noexcept
    {
        if (routeDispatchDepth != 0
            || buttonActionCallbackOperationDepth != 0
            || reclaimingInactiveButtonActions) {
            return;
        }

        reclaimingInactiveButtonActions = true;
        auto reclaimGuard = Core::makeScopeExit([this]() noexcept {
            reclaimingInactiveButtonActions = false;
        });
        while (!inactiveButtonActionIndices.empty()) {
            const u32 actionIndex = inactiveButtonActionIndices.back();
            inactiveButtonActionIndices.pop_back();
            if (actionIndex >= buttonActions.size()) {
                continue;
            }
            ButtonActionRecord& action = buttonActions[actionIndex];
            action.queuedForReclaim = false;
            if (!action.active && !action.invoking && action.node.hasValue()) {
                recycleButtonAction(actionIndex);
            }
        }
    }

    void deactivateButtonAction(u32 actionIndex) noexcept
    {
        if (actionIndex >= buttonActions.size()) {
            return;
        }
        ButtonActionRecord& action = buttonActions[actionIndex];
        if (!action.node.hasValue()) {
            return;
        }
        if (action.node.index() < buttonActionIndexByNodeIndex.size()
            && buttonActionIndexByNodeIndex[action.node.index()] == actionIndex) {
            buttonActionIndexByNodeIndex[action.node.index()] = InvalidButtonActionIndex;
        }
        if (action.active) {
            action.active = false;
            if (activeButtonActionCount > 0) {
                --activeButtonActionCount;
            }
        }
        if (routeDispatchDepth != 0
            || buttonActionCallbackOperationDepth != 0
            || action.invoking
            || reclaimingInactiveButtonActions) {
            if (!action.queuedForReclaim) {
                action.queuedForReclaim = true;
                inactiveButtonActionIndices.push_back(actionIndex);
            }
            return;
        }
        recycleButtonAction(actionIndex);
        reclaimInactiveButtonActions();
    }

    void deactivateButtonActionForNode(u32 nodeIndex) noexcept
    {
        if (nodeIndex >= buttonActionIndexByNodeIndex.size()) {
            return;
        }
        const UINodeId node = idForIndex(nodeIndex);
        if (armedPrimaryButton == node) {
            clearArmedPrimaryButton();
        }
        if (defaultActionFocusButton == node) {
            clearDefaultActionFocus();
        }
        if (imeFocusLabel == node) {
            clearImeFocus();
        }
        const u32 actionIndex = buttonActionIndexByNodeIndex[nodeIndex];
        buttonActionIndexByNodeIndex[nodeIndex] = InvalidButtonActionIndex;
        if (actionIndex != InvalidButtonActionIndex) {
            deactivateButtonAction(actionIndex);
        }
    }

    [[nodiscard]] Core::Status rollbackButtonActionRegistration(
        u32 actionIndex,
        Core::Error error)
    {
        deactivateButtonAction(actionIndex);
        reclaimInactiveButtonActions();
        return Core::failure(std::move(error));
    }

    [[nodiscard]] ButtonActionInvocationCandidate captureButtonAction(
        UINodeId button,
        u64 registrationSerialBoundary) const noexcept
    {
        if (!contains(button) || button.index() >= buttonActionIndexByNodeIndex.size()) {
            return {};
        }
        const u32 actionIndex = buttonActionIndexByNodeIndex[button.index()];
        if (actionIndex >= buttonActions.size()) {
            return {};
        }
        const ButtonActionRecord& action = buttonActions[actionIndex];
        if (!action.active
            || action.node != button
            || action.registrationSerial == 0
            || action.registrationSerial > registrationSerialBoundary
            || !action.callback.hasValue()) {
            return {};
        }
        return ButtonActionInvocationCandidate{
            .button = button,
            .actionIndex = actionIndex,
            .generation = action.generation,
        };
    }

    void invokeButtonAction(
        ButtonActionInvocationCandidate candidate,
        const UIButtonActionEvent& event,
        u64 routeSerial) noexcept
    {
        if (!candidate.hasValue()
            || !contains(candidate.button)
            || candidate.button.index() >= buttonActionClearRouteSerialByNodeIndex.size()
            || buttonActionClearRouteSerialByNodeIndex[candidate.button.index()] == routeSerial
            || candidate.actionIndex >= buttonActions.size()) {
            return;
        }
        const NodeRecord* buttonRecord = nodes.tryGet(candidate.button.storageId());
        ButtonActionRecord& action = buttonActions[candidate.actionIndex];
        if (buttonRecord == nullptr
            || !isDefaultActivatableKind(buttonRecord->kind)
            || action.generation != candidate.generation
            || action.node != candidate.button
            || !action.callback.hasValue()) {
            return;
        }

        action.invoking = true;
        ++buttonActionCallbackOperationDepth;
        action.callback(event);
        --buttonActionCallbackOperationDepth;
        if (candidate.actionIndex < buttonActions.size()) {
            ButtonActionRecord& current = buttonActions[candidate.actionIndex];
            if (current.generation == candidate.generation) {
                current.invoking = false;
            }
        }
    }

    void releaseTextAllocation(TextByteAllocation allocation) noexcept
    {
        if (allocation.capacity == 0) {
            return;
        }
        freeTextAllocations.push_back(allocation);
        if (textByteUsed >= allocation.capacity) {
            textByteUsed -= allocation.capacity;
        } else {
            textByteUsed = 0;
        }
    }

    [[nodiscard]] Core::Result<TextByteAllocation> allocateTextBytes(u32 byteCount)
    {
        if (byteCount == 0) {
            return TextByteAllocation{};
        }
        for (usize freeIndex = 0; freeIndex < freeTextAllocations.size(); ++freeIndex) {
            TextByteAllocation& candidate = freeTextAllocations[freeIndex];
            if (candidate.capacity < byteCount) {
                continue;
            }
            const TextByteAllocation allocated = candidate;
            freeTextAllocations[freeIndex] = freeTextAllocations.back();
            freeTextAllocations.pop_back();
            textByteUsed += allocated.capacity;
            textByteHighWater = (std::max)(textByteHighWater, textByteUsed);
            return allocated;
        }
        if (textByteUsed > capacityConfig.textByteCapacity
            || static_cast<usize>(byteCount)
                > capacityConfig.textByteCapacity - textByteUsed) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI text byte capacity has been exhausted");
        }
        const TextByteAllocation allocated{
            .offset = static_cast<u32>(textByteUsed),
            .capacity = byteCount,
        };
        textByteUsed += byteCount;
        textByteHighWater = (std::max)(textByteHighWater, textByteUsed);
        return allocated;
    }

    void clearTextState(u32 index) noexcept
    {
        if (index >= textStatesByIndex.size()) {
            return;
        }
        WidgetTextState& state = textStatesByIndex[index];
        releaseTextAllocation(state.allocation);
        state = {};
    }

    [[nodiscard]] std::string_view textViewFor(u32 index) const noexcept
    {
        if (index >= textStatesByIndex.size()) {
            return {};
        }
        const WidgetTextState& state = textStatesByIndex[index];
        if (!state.hasContent || state.length == 0 || state.allocation.capacity == 0) {
            return {};
        }
        return std::string_view(
            textBytes.data() + state.allocation.offset,
            state.length);
    }

    [[nodiscard]] Core::Result<UITextMetrics> measureWidgetText(
        std::string_view utf8,
        const UITextStyle& style)
    {
        if (textRasterizer && textFace.hasValue()) {
            return textRasterizer->measure(textFace, utf8, style);
        }
        // Fallback keeps measure available when a custom FreeType rasterizer is
        // injected before any face is opened.
        return measurePlaceholderText(utf8, style);
    }

    [[nodiscard]] static bool supportsWidgetText(UIWidgetKind kind) noexcept
    {
        return kind == UIWidgetKind::Label || kind == UIWidgetKind::Button;
    }

    void resetNodeSideData(u32 index) noexcept
    {
        if (index >= layoutStylesByIndex.size()) {
            return;
        }
        layoutStylesByIndex[index] = {};
        pointerHitPoliciesByIndex[index] = UIPointerHitPolicy::Ignore;
        boxPaintsByIndex[index] = {};
        localSolidFillCacheByIndex[index] = {};
        localTextColorCacheByIndex[index] = {};
        clearTextState(index);
        dirtyByIndex[index] = UIDirty::None;
        dirtyQueuedByIndex[index] = 0;
        layoutScratchByIndex[index] = {};
        layoutWorkByIndex[index] = 0;
        routedPointerListenerHeadByNodeIndex[index] =
            InvalidRoutedPointerListenerIndex;
        routedPointerListenerTailByNodeIndex[index] =
            InvalidRoutedPointerListenerIndex;
        buttonActionIndexByNodeIndex[index] = InvalidButtonActionIndex;
        buttonActionClearRouteSerialByNodeIndex[index] = 0;
        if (index < checkboxCheckedByNodeIndex.size()) {
            checkboxCheckedByNodeIndex[index] = 0;
        }
    }

    void markStructureChanged() noexcept
    {
        structureDirty = true;
        layoutDirty = true;
        hitDirty = true;
        layoutReuseCacheValid = false;
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

    [[nodiscard]] Core::Status markPaintDirty(UINodeId node)
    {
        if (!contains(node) || node.index() >= dirtyByIndex.size()) {
            return fail(UIErrorCode::InvalidNode, "UI paint dirty node is invalid");
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
        dirtyByIndex[index] |= UIDirty::Paint;
        dirtyQueueHighWater = (std::max)(dirtyQueueHighWater, dirtyQueue.size());
        paintDirty = true;
        return Core::success();
    }

    void clearDirtyState() noexcept
    {
        std::fill(dirtyByIndex.begin(), dirtyByIndex.end(), UIDirty::None);
        std::fill(dirtyQueuedByIndex.begin(), dirtyQueuedByIndex.end(), 0);
        dirtyQueue.clear();
        layoutDirty = false;
        hitDirty = false;
        paintDirty = false;
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
        // Button/Checkbox and Label are Targetable: Label is the IME/text target
        // surface (M7-E6). Root/Panel remain Ignore.
        pointerHitPoliciesByIndex[node.index()] =
            (kind == UIWidgetKind::Button || kind == UIWidgetKind::Checkbox
             || kind == UIWidgetKind::Label)
            ? UIPointerHitPolicy::Targetable
            : UIPointerHitPolicy::Ignore;
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

    [[nodiscard]] Core::Result<UINodeId> createChildFromUpdater(
        UINodeId updaterRoot,
        UINodeId parent,
        UIWidgetKind kind)
    {
        if (kind == UIWidgetKind::Root) {
            return fail(UIErrorCode::InvalidParent, "Root nodes cannot be created as children");
        }
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue()) {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot)) {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto parentResult = resolveParent(parent);
        if (!parentResult) {
            return Core::failure(parentResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, parent)) {
            return fail(UIErrorCode::InvalidNode, "UI parent is not owned by the updater root");
        }

        return createChild(parent, kind);
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
            deactivateButtonActionForNode(currentIndex);
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

    [[nodiscard]] Core::Status setBoxPaintFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        const UIBoxPaint& paint)
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

        const UIBoxPaint normalizedPaint = normalizeBoxPaint(paint);
        UIBoxPaint& currentPaint = boxPaintsByIndex[node.index()];
        if (currentPaint == normalizedPaint) {
            return Core::success();
        }
        if (Core::Status dirtyStatus = markPaintDirty(node); !dirtyStatus) {
            return dirtyStatus;
        }
        currentPaint = normalizedPaint;
        return Core::success();
    }

    [[nodiscard]] Core::Status setTextFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        std::string_view utf8)
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
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind)) {
            return fail(
                UIErrorCode::InvalidText,
                "UI text is only supported on Label and Button nodes");
        }
        if (utf8.size() > (std::numeric_limits<u32>::max)()) {
            return fail(UIErrorCode::CapacityExceeded, "UI text payload is too large");
        }

        auto metrics = measureWidgetText(
            utf8,
            textStatesByIndex[node.index()].style);
        if (!metrics) {
            return Core::failure(metrics.error());
        }

        WidgetTextState& state = textStatesByIndex[node.index()];
        const std::string_view current = textViewFor(node.index());
        if (state.hasContent == !utf8.empty()
            && current == utf8
            && state.metrics == *metrics) {
            return Core::success();
        }

        TextByteAllocation reservedAllocation{};
        bool reservedNewAllocation = false;
        if (!utf8.empty() && state.allocation.capacity < utf8.size()) {
            auto allocation = allocateTextBytes(static_cast<u32>(utf8.size()));
            if (!allocation) {
                return Core::failure(allocation.error());
            }
            reservedAllocation = *allocation;
            reservedNewAllocation = true;
        }

        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus) {
            if (reservedNewAllocation) {
                releaseTextAllocation(reservedAllocation);
            }
            return dirtyStatus;
        }
        if (Core::Status paintStatus = markPaintDirty(node); !paintStatus) {
            if (reservedNewAllocation) {
                releaseTextAllocation(reservedAllocation);
            }
            return paintStatus;
        }

        if (utf8.empty()) {
            releaseTextAllocation(state.allocation);
            state.allocation = {};
            state.length = 0;
            state.metrics = {};
            state.hasContent = false;
            return Core::success();
        }

        if (reservedNewAllocation) {
            releaseTextAllocation(state.allocation);
            state.allocation = reservedAllocation;
        }
        std::memcpy(
            textBytes.data() + state.allocation.offset,
            utf8.data(),
            utf8.size());
        state.length = static_cast<u32>(utf8.size());
        state.metrics = *metrics;
        state.hasContent = true;
        return Core::success();
    }

    [[nodiscard]] Core::Status setTextStyleFromUpdater(
        UINodeId updaterRoot,
        UINodeId node,
        const UITextStyle& style)
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
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind)) {
            return fail(
                UIErrorCode::InvalidText,
                "UI text style is only supported on Label and Button nodes");
        }

        WidgetTextState& state = textStatesByIndex[node.index()];
        if (state.style == style) {
            return Core::success();
        }

        UITextMetrics metrics{};
        if (state.hasContent) {
            auto measured = measureWidgetText(textViewFor(node.index()), style);
            if (!measured) {
                return Core::failure(measured.error());
            }
            metrics = *measured;
        }

        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus) {
            return dirtyStatus;
        }
        if (Core::Status paintStatus = markPaintDirty(node); !paintStatus) {
            return paintStatus;
        }
        state.style = style;
        state.metrics = metrics;
        return Core::success();
    }

    [[nodiscard]] Core::Result<std::string_view> textFromUpdater(
        UINodeId updaterRoot,
        UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
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
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind)) {
            return fail(
                UIErrorCode::InvalidText,
                "UI text is only supported on Label and Button nodes");
        }
        return textViewFor(node.index());
    }

    [[nodiscard]] Core::Result<UITextStyle> textStyleFromUpdater(
        UINodeId updaterRoot,
        UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
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
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind)) {
            return fail(
                UIErrorCode::InvalidText,
                "UI text style is only supported on Label and Button nodes");
        }
        return textStatesByIndex[node.index()].style;
    }

    [[nodiscard]] Core::Status setButtonActionFromUpdater(
        UINodeId updaterRoot,
        UINodeId button,
        UIButtonActionCallback&& callback)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot)) {
            return fail(
                UIErrorCode::RootRequired,
                "UI tree updater requires a live root owner");
        }
        auto buttonResult = resolveButton(button);
        if (!buttonResult) {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button)) {
            return fail(
                UIErrorCode::InvalidNode,
                "UI Button is not owned by the updater root");
        }
        if (!callback.hasValue()) {
            return fail(
                UIErrorCode::InvalidButtonAction,
                "UI Button action callback is empty");
        }

        const u32 previousActionIndex =
            buttonActionIndexByNodeIndex[button.index()];
        const bool replacing = previousActionIndex < buttonActions.size()
            && buttonActions[previousActionIndex].active
            && buttonActions[previousActionIndex].node == button;
        if (previousActionIndex != InvalidButtonActionIndex && !replacing) {
            return fail(
                Core::CoreErrorCode::Internal,
                "UI Button action mapping is inconsistent");
        }
        if (!replacing
            && activeButtonActionCount >= capacityConfig.buttonActionCapacity) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI Button action capacity has been exhausted");
        }
        if (freeButtonActionHead == InvalidButtonActionIndex) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI Button action transaction storage has been exhausted");
        }
        if (buttonActionRegistrationSerial
            == (std::numeric_limits<u64>::max)()) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI Button action registration serial is exhausted");
        }

        const u32 actionIndex = freeButtonActionHead;
        ButtonActionRecord& action = buttonActions[actionIndex];
        freeButtonActionHead = action.nextFreeIndex;
        ++action.generation;
        if (action.generation == 0) {
            ++action.generation;
        }
        action.node = button;
        action.nextFreeIndex = InvalidButtonActionIndex;
        action.registrationSerial = 0;
        action.active = false;
        action.queuedForReclaim = false;
        action.invoking = false;
        {
            ++buttonActionCallbackOperationDepth;
            auto callbackOperation = Core::makeScopeExit([this]() noexcept {
                --buttonActionCallbackOperationDepth;
            });
            action.callback = std::move(callback);
        }
        reclaimInactiveButtonActions();

        if (!contains(updaterRoot)) {
            return rollbackButtonActionRegistration(
                actionIndex,
                makeError(
                    UIErrorCode::RootRequired,
                    "UI tree updater root was released while setting a Button action"));
        }
        auto liveButtonResult = resolveButton(button);
        if (!liveButtonResult) {
            return rollbackButtonActionRegistration(
                actionIndex,
                liveButtonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button)) {
            return rollbackButtonActionRegistration(
                actionIndex,
                makeError(
                    UIErrorCode::InvalidNode,
                    "UI Button left the updater root while setting its action"));
        }
        if (buttonActionIndexByNodeIndex[button.index()] != previousActionIndex) {
            return rollbackButtonActionRegistration(
                actionIndex,
                makeError(
                    UIErrorCode::InvalidButtonAction,
                    "UI Button action changed during callback transfer"));
        }
        if (buttonActionRegistrationSerial
            == (std::numeric_limits<u64>::max)()) {
            return rollbackButtonActionRegistration(
                actionIndex,
                makeError(
                    UIErrorCode::CapacityExceeded,
                    "UI Button action registration serial is exhausted"));
        }

        action.registrationSerial = ++buttonActionRegistrationSerial;
        action.active = true;
        buttonActionIndexByNodeIndex[button.index()] = actionIndex;
        ++activeButtonActionCount;
        if (replacing) {
            deactivateButtonAction(previousActionIndex);
        }
        buttonActionHighWater = (std::max)(
            buttonActionHighWater,
            activeButtonActionCount);
        return Core::success();
    }

    [[nodiscard]] Core::Status clearButtonActionFromUpdater(
        UINodeId updaterRoot,
        UINodeId button)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot)) {
            return fail(
                UIErrorCode::RootRequired,
                "UI tree updater requires a live root owner");
        }
        auto buttonResult = resolveButton(button);
        if (!buttonResult) {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button)) {
            return fail(
                UIErrorCode::InvalidNode,
                "UI Button is not owned by the updater root");
        }

        if (routeDispatchDepth != 0) {
            buttonActionClearRouteSerialByNodeIndex[button.index()] =
                buttonRouteSerial;
        }
        const u32 actionIndex = buttonActionIndexByNodeIndex[button.index()];
        buttonActionIndexByNodeIndex[button.index()] = InvalidButtonActionIndex;
        if (actionIndex != InvalidButtonActionIndex) {
            deactivateButtonAction(actionIndex);
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isButtonPressedFromUpdater(
        UINodeId updaterRoot,
        UINodeId button)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot)) {
            return fail(
                UIErrorCode::RootRequired,
                "UI tree updater requires a live root owner");
        }
        auto buttonResult = resolveButton(button);
        if (!buttonResult) {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button)) {
            return fail(
                UIErrorCode::InvalidNode,
                "UI Button is not owned by the updater root");
        }
        return armedPrimaryButton == button && armedPrimaryButtonPressed;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveCheckbox(UINodeId checkbox)
    {
        auto nodeResult = resolveNode(checkbox);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::Checkbox) {
            return fail(
                UIErrorCode::InvalidButtonAction,
                "UI Checkbox API requires a Checkbox node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status setCheckboxActionFromUpdater(
        UINodeId updaterRoot,
        UINodeId checkbox,
        UIButtonActionCallback&& callback)
    {
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult) {
            return Core::failure(checkboxResult.error());
        }
        return setButtonActionFromUpdater(updaterRoot, checkbox, std::move(callback));
    }

    [[nodiscard]] Core::Status clearCheckboxActionFromUpdater(
        UINodeId updaterRoot,
        UINodeId checkbox)
    {
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult) {
            return Core::failure(checkboxResult.error());
        }
        return clearButtonActionFromUpdater(updaterRoot, checkbox);
    }

    [[nodiscard]] Core::Status setCheckedFromUpdater(
        UINodeId updaterRoot,
        UINodeId checkbox,
        bool checked)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot)) {
            return fail(
                UIErrorCode::RootRequired,
                "UI tree updater requires a live root owner");
        }
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult) {
            return Core::failure(checkboxResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox)) {
            return fail(
                UIErrorCode::InvalidNode,
                "UI Checkbox is not owned by the updater root");
        }
        if (checkbox.index() >= checkboxCheckedByNodeIndex.size()) {
            return fail(Core::CoreErrorCode::Internal, "UI Checkbox index out of range");
        }
        const u8 next = checked ? 1 : 0;
        if (checkboxCheckedByNodeIndex[checkbox.index()] != next) {
            checkboxCheckedByNodeIndex[checkbox.index()] = next;
            static_cast<void>(markPaintDirty(checkbox));
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isCheckedFromUpdater(
        UINodeId updaterRoot,
        UINodeId checkbox) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot)) {
            return fail(
                UIErrorCode::RootRequired,
                "UI tree updater requires a live root owner");
        }
        // const_cast: resolveCheckbox is non-const only for API reuse of resolveNode.
        auto checkboxResult = const_cast<Impl*>(this)->resolveCheckbox(checkbox);
        if (!checkboxResult) {
            return Core::failure(checkboxResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox)) {
            return fail(
                UIErrorCode::InvalidNode,
                "UI Checkbox is not owned by the updater root");
        }
        if (checkbox.index() >= checkboxCheckedByNodeIndex.size()) {
            return fail(Core::CoreErrorCode::Internal, "UI Checkbox index out of range");
        }
        return checkboxCheckedByNodeIndex[checkbox.index()] != 0;
    }

    [[nodiscard]] Core::Result<bool> isCheckboxPressedFromUpdater(
        UINodeId updaterRoot,
        UINodeId checkbox)
    {
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult) {
            return Core::failure(checkboxResult.error());
        }
        return isButtonPressedFromUpdater(updaterRoot, checkbox);
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
        const bool paintNeedsCommit =
            paintDirty || layoutNeedsCommit || committedPaintRevision == 0;

        if (!layoutNeedsCommit && !hitNeedsCommit && !paintNeedsCommit) {
            lastLayoutPass = {};
            lastHitRebuildCount = 0;
            lastPaintCacheRebuildCount = 0;
            lastPaintSnapshotRebuildCount = 0;
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
            const bool allowLayoutReuse =
                !structureDirty && !viewportChanged && layoutReuseCacheValid;
            layoutReuseCacheValid = false;
            layoutReuseInProgress = allowLayoutReuse;
            auto layoutReuseGuard = Core::makeScopeExit([this]() noexcept {
                layoutReuseInProgress = false;
            });
            writeLayoutBufferIndex = 1 - publishedLayoutBufferIndex;
            std::pmr::vector<UICommittedLayoutEntry>& writeLayout =
                committedLayoutBuffers[writeLayoutBufferIndex];
            writeLayout.clear();

            buildLayoutOrder(layoutOrderScratch);
            pass.passCount = layoutOrderScratch.empty() ? 0 : 1;
            prepareLayoutState(viewportSize, layoutOrderScratch, allowLayoutReuse);
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

        usize writePaintBufferIndex = publishedPaintBufferIndex;
        usize candidatePaintCacheRebuildCount = 0;
        if (paintNeedsCommit) {
            auto paintCapacity = validatePaintCandidateCapacity(candidateLayoutEntries);
            if (!paintCapacity) {
                return Core::failure(paintCapacity.error());
            }
            writePaintBufferIndex = 1 - publishedPaintBufferIndex;
            candidatePaintCacheRebuildCount =
                rebuildDirtyPaintCaches(candidateLayoutEntries);
            buildCommittedPaint(
                committedPaintBuffers[writePaintBufferIndex],
                candidateLayoutEntries);
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
        if (paintNeedsCommit) {
            publishedPaintBufferIndex = writePaintBufferIndex;
            ++committedPaintRevision;
            committedPaintStructureRevision = candidateStructureRevision;
            committedPaintLayoutRevision = candidateLayoutRevision;
            committedPaintOrderRevision = candidatePaintOrderRevision;
            committedPaintViewportSize = viewportSize;
        }
        lastLayoutPass = layoutNeedsCommit ? pass : LayoutPassStatistics{};
        lastHitRebuildCount = hitNeedsCommit ? 1 : 0;
        lastPaintCacheRebuildCount = candidatePaintCacheRebuildCount;
        lastPaintSnapshotRebuildCount = paintNeedsCommit ? 1 : 0;
        if (layoutNeedsCommit) {
            layoutReuseCacheValid = true;
        }
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

    [[nodiscard]] UICommittedPaintView committedPaint() const noexcept
    {
        const std::pmr::vector<UICommittedPaintEntry>& entries =
            committedPaintBuffers[publishedPaintBufferIndex];
        return UICommittedPaintView{
            std::span<const UICommittedPaintEntry>(entries.data(), entries.size()),
            committedPaintViewportSize,
            committedPaintStructureRevision,
            committedPaintLayoutRevision,
            committedPaintOrderRevision,
            committedPaintRevision,
        };
    }

    [[nodiscard]] std::span<const u8> glyphAtlasPixels() const noexcept
    {
        if (!glyphAtlas) {
            return {};
        }
        return glyphAtlas->pagePixels();
    }

    [[nodiscard]] u32 glyphAtlasWidth() const noexcept
    {
        return glyphAtlas ? glyphAtlas->capacity().width : 0U;
    }

    [[nodiscard]] u32 glyphAtlasHeight() const noexcept
    {
        return glyphAtlas ? glyphAtlas->capacity().height : 0U;
    }

    [[nodiscard]] Core::Status openTextFont(
        std::span<const std::byte> fontBytes,
        i32 faceIndex)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!textRasterizer) {
            return fail(UIErrorCode::InvalidFont, "UI context has no text rasterizer");
        }

        auto newFace = textRasterizer->openFace(fontBytes, faceIndex);
        if (!newFace) {
            return Core::failure(newFace.error());
        }

        if (textFace.hasValue()) {
            static_cast<void>(textRasterizer->closeFace(textFace));
            textFace = {};
        }
        textFace = *newFace;
        if (glyphAtlas) {
            glyphAtlas->clear();
        }

        // Remeasure retained text and dirty layout/paint for all text nodes.
        for (u32 index = 0; index < static_cast<u32>(textStatesByIndex.size()); ++index) {
            WidgetTextState& state = textStatesByIndex[index];
            if (!state.hasContent) {
                continue;
            }
            const UINodeId node = idForIndex(index);
            if (!node.hasValue() || !contains(node)) {
                continue;
            }
            auto metrics = measureWidgetText(textViewFor(index), state.style);
            if (!metrics) {
                return Core::failure(metrics.error());
            }
            state.metrics = *metrics;
            if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus) {
                return dirtyStatus;
            }
            if (Core::Status paintStatus = markPaintDirty(node); !paintStatus) {
                return paintStatus;
            }
        }
        return Core::success();
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
        UIRoutedPointerCallback&& callback,
        UINodeId updaterRoot)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (updaterRoot.hasValue() && !contains(updaterRoot)) {
            return fail(
                UIErrorCode::RootRequired,
                "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(descriptor.node);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }
        if (updaterRoot.hasValue()
            && !isNodeWithinRoot(updaterRoot, descriptor.node)) {
            return fail(
                UIErrorCode::InvalidNode,
                "UI routed pointer listener node is not owned by the updater root");
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
        listener.previousNodeListenerIndex = InvalidRoutedPointerListenerIndex;
        listener.nextNodeListenerIndex = InvalidRoutedPointerListenerIndex;
        listener.nextFreeIndex = InvalidRoutedPointerListenerIndex;
        listener.registrationSerial = 0;
        listener.active = false;
        {
            ++listenerCallbackOperationDepth;
            auto callbackOperation = Core::makeScopeExit([this]() noexcept {
                --listenerCallbackOperationDepth;
            });
            listener.callback = std::move(callback);
        }
        reclaimInactiveRoutedPointerListeners();

        // Moving a valid fixed-inline callable may execute user move/destructor
        // code. Revalidate generation ownership before publishing the slot.
        if (updaterRoot.hasValue() && !contains(updaterRoot)) {
            return rollbackRoutedPointerListenerRegistration(
                listenerIndex,
                makeError(
                    UIErrorCode::RootRequired,
                    "UI tree updater root was released while registering a routed pointer listener"));
        }
        auto liveNodeResult = resolveNode(descriptor.node);
        if (!liveNodeResult) {
            return rollbackRoutedPointerListenerRegistration(
                listenerIndex,
                liveNodeResult.error());
        }
        if (updaterRoot.hasValue()
            && !isNodeWithinRoot(updaterRoot, descriptor.node)) {
            return rollbackRoutedPointerListenerRegistration(
                listenerIndex,
                makeError(
                    UIErrorCode::InvalidNode,
                    "UI routed pointer listener node left the updater root during registration"));
        }
        if (routedPointerListenerRegistrationSerial
            == (std::numeric_limits<u64>::max)()) {
            return rollbackRoutedPointerListenerRegistration(
                listenerIndex,
                makeError(
                    UIErrorCode::CapacityExceeded,
                    "UI routed pointer listener registration serial is exhausted"));
        }

        listener.previousNodeListenerIndex =
            routedPointerListenerTailByNodeIndex[descriptor.node.index()];
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

    [[nodiscard]] Core::Result<std::pair<u32, u32>>
    addRoutedPointerListenerFromUpdater(
        UINodeId updaterRoot,
        UIRoutedPointerListenerDesc descriptor,
        UIRoutedPointerCallback&& callback)
    {
        if (!updaterRoot.hasValue()) {
            return fail(
                UIErrorCode::RootRequired,
                "UI tree updater requires a live root owner");
        }
        return addRoutedPointerListener(
            descriptor,
            std::move(callback),
            updaterRoot);
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
        const UICommittedHitView hit = committedHit();
        const std::span<const UICommittedHitEntry> entries = hit.entries();
        routePathScratch.clear();
        if (result.pointQuery.hasTarget()) {
            if (!contains(result.pointQuery.target.node)) {
                result.targetInvalidated = true;
            } else {
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
                    || routePathScratch.back()
                        != result.pointQuery.target.rootEntryIndex
                    || entries[routePathScratch.back()].node
                        != result.pointQuery.target.rootNode) {
                    routePathScratch.clear();
                    return fail(
                        Core::CoreErrorCode::Internal,
                        "UI committed pointer route root is invalid");
                }
                result.routeDepth = routePathScratch.size();
            }
        }

        if (buttonRouteSerial == (std::numeric_limits<u64>::max)()) {
            routePathScratch.clear();
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI Button route serial is exhausted");
        }

        UINodeId nearestButton{};
        bool pointWithinArmedButton = false;
        for (const u32 routeEntryIndex : routePathScratch) {
            if (routeEntryIndex >= entries.size()) {
                routePathScratch.clear();
                return fail(
                    Core::CoreErrorCode::Internal,
                    "UI committed Button route entry index is invalid");
            }
            const UINodeId routeNode = entries[routeEntryIndex].node;
            if (routeNode == armedPrimaryButton) {
                pointWithinArmedButton = true;
            }
            if (!nearestButton.hasValue() && contains(routeNode)) {
                const NodeRecord* routeRecord =
                    nodes.tryGet(routeNode.storageId());
                if (routeRecord != nullptr
                    && isDefaultActivatableKind(routeRecord->kind)) {
                    nearestButton = routeNode;
                }
            }
        }

        const UINodeId armedButtonAtRouteStart = armedPrimaryButton;
        const bool hadArmedInteraction = armedButtonAtRouteStart.hasValue();
        const bool primaryButtonDown =
            input.kind == UIRoutedPointerEventKind::ButtonDown
            && input.button == Platform::PointerButton::Primary;
        const bool primaryButtonUp =
            input.kind == UIRoutedPointerEventKind::ButtonUp
            && input.button == Platform::PointerButton::Primary;
        const u64 actionRegistrationSerialBoundary =
            buttonActionRegistrationSerial;
        const ButtonActionInvocationCandidate actionCandidate =
            primaryButtonUp && hadArmedInteraction && pointWithinArmedButton
            ? captureButtonAction(
                armedButtonAtRouteStart,
                actionRegistrationSerialBoundary)
            : ButtonActionInvocationCandidate{};
        const u64 currentButtonRouteSerial = ++buttonRouteSerial;
        const u64 registrationSerialBoundary =
            routedPointerListenerRegistrationSerial;
        UIRoutedPointerEvent routedEvent =
            Detail::UIRoutedPointerEventAccess::Create(input);
        routeDispatchDepth = 1;
        auto dispatchCleanup = Core::makeScopeExit([this]() noexcept {
            routeDispatchDepth = 0;
            drainDeferredRoutedPointerListenerReleases();
            reclaimInactiveRoutedPointerListeners();
            reclaimInactiveButtonActions();
        });

        const UINodeId targetNode = result.pointQuery.target.node;
        if (!routePathScratch.empty() && !result.targetInvalidated) {
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

            if (!routedEvent.isPropagationStopped()
                && !result.targetInvalidated) {
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

            if (!routedEvent.isPropagationStopped()
                && !result.targetInvalidated) {
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
        }

        if (targetNode.hasValue() && !contains(targetNode)) {
            result.targetInvalidated = true;
        }

        if (primaryButtonDown) {
            clearArmedPrimaryButton();
            if (!routedEvent.isDefaultActionPrevented()
                && nearestButton.hasValue()
                && contains(nearestButton)) {
                const NodeRecord* buttonRecord =
                    nodes.tryGet(nearestButton.storageId());
                if (buttonRecord != nullptr
                    && isDefaultActivatableKind(buttonRecord->kind)) {
                    armedPrimaryButton = nearestButton;
                    armedPrimaryButtonPressed = true;
                    defaultActionFocusButton = nearestButton;
                    routedEvent.consumeInputTransition();
                    static_cast<void>(routedEvent.claimPointerButton(
                        Platform::PointerButton::Primary));
                }
            }
            // Label becomes the IME/text target on primary Down.
            if (result.pointQuery.target.node.hasValue()
                && contains(result.pointQuery.target.node)) {
                const NodeRecord* targetRecord =
                    nodes.tryGet(result.pointQuery.target.node.storageId());
                if (targetRecord != nullptr
                    && targetRecord->kind == UIWidgetKind::Label) {
                    const UINodeId previousFocus = imeFocusLabel;
                    imeFocusLabel = result.pointQuery.target.node;
                    clearImeComposition();
                    if (previousFocus.hasValue()
                        && previousFocus != imeFocusLabel
                        && contains(previousFocus)) {
                        static_cast<void>(markPaintDirty(previousFocus));
                    }
                    static_cast<void>(markPaintDirty(imeFocusLabel));
                }
            }
        } else if (input.kind == UIRoutedPointerEventKind::Move
                   && hadArmedInteraction
                   && armedPrimaryButton == armedButtonAtRouteStart) {
            if (!contains(armedButtonAtRouteStart)) {
                clearArmedPrimaryButton();
            } else {
                armedPrimaryButtonPressed = pointWithinArmedButton;
                static_cast<void>(routedEvent.claimPointerButton(
                    Platform::PointerButton::Primary));
            }
        } else if (primaryButtonUp && hadArmedInteraction) {
            const bool interactionStillArmed =
                armedPrimaryButton == armedButtonAtRouteStart;
            clearArmedPrimaryButton();
            routedEvent.consumeInputTransition();
            if (!routedEvent.isDefaultActionPrevented()
                && interactionStillArmed
                && pointWithinArmedButton
                && contains(armedButtonAtRouteStart)) {
                if (const NodeRecord* armedRecord =
                        nodes.tryGet(armedButtonAtRouteStart.storageId());
                    armedRecord != nullptr
                    && armedRecord->kind == UIWidgetKind::Checkbox
                    && armedButtonAtRouteStart.index()
                        < checkboxCheckedByNodeIndex.size()) {
                    checkboxCheckedByNodeIndex[armedButtonAtRouteStart.index()] =
                        checkboxCheckedByNodeIndex[armedButtonAtRouteStart.index()] == 0
                        ? 1
                        : 0;
                    static_cast<void>(markPaintDirty(armedButtonAtRouteStart));
                }
                invokeButtonAction(
                    actionCandidate,
                    UIButtonActionEvent{
                        .buttonNode = armedButtonAtRouteStart,
                        .source = UIButtonActivationSource::PrimaryPointer,
                        .platformFrame = input.platformFrame,
                        .sourceSequence = input.sourceSequence,
                    },
                    currentButtonRouteSerial);
            }
        }

        result.claimedPointerButtons =
            Detail::UIRoutedPointerEventAccess::claimedPointerButtons(
                routedEvent);
        result.consumed = routedEvent.isInputTransitionConsumed();
        result.stopped = routedEvent.isPropagationStopped();
        return result;
    }

    [[nodiscard]] Core::Status cancelPointerInteraction(
        Platform::WindowId routedWindow)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!routedWindow.hasValue()) {
            return fail(
                UIErrorCode::InvalidPointerInput,
                "UI Pointer interaction cancellation requires a Window");
        }
        if (routedWindow != ownerWindow) {
            return fail(
                UIErrorCode::WrongOwnerWindow,
                "UI Pointer interaction cancellation belongs to another Window");
        }
        clearArmedPrimaryButton();
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIDefaultActionResult> routeDefaultActionActivate(
        Platform::PlatformFrameId platformFrame,
        u64 sourceSequence,
        UIButtonActivationSource source)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!platformFrame.hasValue() || sourceSequence == 0) {
            return fail(
                UIErrorCode::InvalidPointerInput,
                "UI default-action activation requires a platform frame and sequence");
        }
        if (source != UIButtonActivationSource::Keyboard
            && source != UIButtonActivationSource::Gamepad) {
            return fail(
                UIErrorCode::InvalidButtonAction,
                "UI default-action activation source must be Keyboard or Gamepad");
        }
        if (!defaultActionFocusButton.hasValue()
            || !contains(defaultActionFocusButton)) {
            clearDefaultActionFocus();
            return UIDefaultActionResult{};
        }
        const NodeRecord* record =
            nodes.tryGet(defaultActionFocusButton.storageId());
        if (record == nullptr || !isDefaultActivatableKind(record->kind)) {
            clearDefaultActionFocus();
            return UIDefaultActionResult{};
        }

        if (buttonRouteSerial == (std::numeric_limits<u64>::max)()) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI Button route serial is exhausted");
        }
        if (record->kind == UIWidgetKind::Checkbox
            && defaultActionFocusButton.index() < checkboxCheckedByNodeIndex.size()) {
            checkboxCheckedByNodeIndex[defaultActionFocusButton.index()] =
                checkboxCheckedByNodeIndex[defaultActionFocusButton.index()] == 0 ? 1 : 0;
            static_cast<void>(markPaintDirty(defaultActionFocusButton));
        }
        const u64 actionRegistrationSerialBoundary =
            buttonActionRegistrationSerial;
        const ButtonActionInvocationCandidate actionCandidate =
            captureButtonAction(
                defaultActionFocusButton,
                actionRegistrationSerialBoundary);
        if (!actionCandidate.hasValue()) {
            // Focused control without an action still consumes Accept so
            // gameplay does not also fire. Checkbox state already toggled above.
            return UIDefaultActionResult{.consumed = true, .activated = true};
        }
        const u64 currentButtonRouteSerial = ++buttonRouteSerial;
        routeDispatchDepth = 1;
        auto dispatchCleanup = Core::makeScopeExit([this]() noexcept {
            routeDispatchDepth = 0;
            drainDeferredRoutedPointerListenerReleases();
            reclaimInactiveRoutedPointerListeners();
            reclaimInactiveButtonActions();
        });
        invokeButtonAction(
            actionCandidate,
            UIButtonActionEvent{
                .buttonNode = defaultActionFocusButton,
                .source = source,
                .platformFrame = platformFrame,
                .sourceSequence = sourceSequence,
            },
            currentButtonRouteSerial);
        return UIDefaultActionResult{.consumed = true, .activated = true};
    }

    [[nodiscard]] Core::Result<UIDefaultFocusStepResult> routeDefaultActionFocusStep(
        bool reverse)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();

        // Collect visible Targetable Buttons in last committed layout paint order.
        // Fixed stack table keeps this path allocation-free (M7-E3 scope).
        constexpr usize MaxTabCandidates = 256;
        std::array<UINodeId, MaxTabCandidates> candidates{};
        usize candidateCount = 0;
        const std::pmr::vector<UICommittedLayoutEntry>& layout =
            committedLayoutBuffers[publishedLayoutBufferIndex];
        for (const UICommittedLayoutEntry& entry : layout) {
            if (entry.effectiveVisibility != UIVisibility::Visible) {
                continue;
            }
            if (!contains(entry.node)) {
                continue;
            }
            const NodeRecord* record = nodes.tryGet(entry.node.storageId());
            if (record == nullptr || !isDefaultActivatableKind(record->kind)) {
                continue;
            }
            if (entry.node.index() >= pointerHitPoliciesByIndex.size()) {
                continue;
            }
            if (pointerHitPoliciesByIndex[entry.node.index()]
                != UIPointerHitPolicy::Targetable) {
                continue;
            }
            if (candidateCount >= MaxTabCandidates) {
                return fail(
                    UIErrorCode::CapacityExceeded,
                    "UI default-action Tab candidate capacity has been exhausted");
            }
            candidates[candidateCount] = entry.node;
            ++candidateCount;
        }
        if (candidateCount == 0) {
            clearDefaultActionFocus();
            return UIDefaultFocusStepResult{};
        }

        usize currentIndex = candidateCount;
        if (defaultActionFocusButton.hasValue()) {
            for (usize i = 0; i < candidateCount; ++i) {
                if (candidates[i] == defaultActionFocusButton) {
                    currentIndex = i;
                    break;
                }
            }
        }

        usize nextIndex = 0;
        if (currentIndex >= candidateCount) {
            nextIndex = reverse ? candidateCount - 1U : 0U;
        } else if (reverse) {
            nextIndex = currentIndex == 0 ? candidateCount - 1U : currentIndex - 1U;
        } else {
            nextIndex = (currentIndex + 1U) % candidateCount;
        }

        const UINodeId nextFocus = candidates[nextIndex];
        const bool moved =
            !defaultActionFocusButton.hasValue()
            || defaultActionFocusButton != nextFocus;
        defaultActionFocusButton = nextFocus;
        // Tab navigation does not keep a live pointer arm.
        clearArmedPrimaryButton();
        return UIDefaultFocusStepResult{
            .consumed = true,
            .moved = moved,
            .focus = nextFocus,
        };
    }

    [[nodiscard]] UINodeId defaultActionFocus() const noexcept
    {
        if (!defaultActionFocusButton.hasValue()
            || !contains(defaultActionFocusButton)) {
            return {};
        }
        return defaultActionFocusButton;
    }

    [[nodiscard]] UINodeId imeFocus() const noexcept
    {
        if (!isLiveLabel(imeFocusLabel)) {
            return {};
        }
        return imeFocusLabel;
    }

    [[nodiscard]] bool imeCompositionActive() const noexcept
    {
        return imeCompositionActive_ && isLiveLabel(imeFocusLabel);
    }

    [[nodiscard]] std::string_view imePreeditUtf8() const noexcept
    {
        if (!imeCompositionActive_) {
            return {};
        }
        return std::string_view(imePreeditBytes_.data(), imePreeditSize_);
    }

    [[nodiscard]] u32 imePreeditCursorCodepoint() const noexcept
    {
        return imeCompositionActive_ ? imePreeditCursor_ : 0U;
    }

    [[nodiscard]] Core::Result<UITextInputRouteResult> routeTextComposition(
        Platform::WindowId window,
        Platform::PlatformFrameId platformFrame,
        u64 sourceSequence,
        std::string_view preeditUtf8,
        u32 cursorCodepoint,
        Platform::TextCompositionStage stage)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!window.hasValue() || window != ownerWindow) {
            return fail(
                UIErrorCode::WrongOwnerWindow,
                "UI text composition belongs to another owner window");
        }
        if (!platformFrame.hasValue() || sourceSequence == 0) {
            return fail(
                UIErrorCode::InvalidPointerInput,
                "UI text composition requires a platform frame and sequence");
        }
        if (!isLiveLabel(imeFocusLabel)) {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        using Stage = Platform::TextCompositionStage;
        if (stage == Stage::Cancelled || stage == Stage::Ended) {
            // clearImeComposition marks paint dirty when preedit was active.
            clearImeComposition();
            return UITextInputRouteResult{.consumed = true, .applied = true};
        }
        if (!Core::isStrictUtf8WithoutNul(preeditUtf8)) {
            return fail(
                UIErrorCode::InvalidText,
                "UI IME preedit must be strict UTF-8 without embedded NUL");
        }
        if (preeditUtf8.size() > MaxImePreeditBytes) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI IME preedit exceeds the fixed context buffer");
        }
        const auto codepoints =
            Core::countStrictUtf8CodepointsWithoutNul(preeditUtf8);
        if (!codepoints.has_value()) {
            return fail(
                UIErrorCode::InvalidText,
                "UI IME preedit must be strict UTF-8 without embedded NUL");
        }
        if (!preeditUtf8.empty()) {
            std::memcpy(imePreeditBytes_.data(), preeditUtf8.data(), preeditUtf8.size());
        }
        imePreeditSize_ = preeditUtf8.size();
        imePreeditCursor_ = (std::min)(cursorCodepoint, *codepoints);
        imeCompositionActive_ = true;
        if (Core::Status paintStatus = markPaintDirty(imeFocusLabel); !paintStatus) {
            return Core::failure(paintStatus.error());
        }
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    [[nodiscard]] Core::Result<UITextInputRouteResult> routeTextInput(
        Platform::WindowId window,
        Platform::PlatformFrameId platformFrame,
        u64 sourceSequence,
        std::string_view committedUtf8)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!window.hasValue() || window != ownerWindow) {
            return fail(
                UIErrorCode::WrongOwnerWindow,
                "UI text input belongs to another owner window");
        }
        if (!platformFrame.hasValue() || sourceSequence == 0) {
            return fail(
                UIErrorCode::InvalidPointerInput,
                "UI text input requires a platform frame and sequence");
        }
        if (!isLiveLabel(imeFocusLabel)) {
            clearImeFocus();
            return UITextInputRouteResult{};
        }
        if (!Core::isStrictUtf8WithoutNul(committedUtf8)) {
            return fail(
                UIErrorCode::InvalidText,
                "UI text input must be strict UTF-8 without embedded NUL");
        }
        // Commit ends any active preedit.
        clearImeComposition();
        if (committedUtf8.empty()) {
            return UITextInputRouteResult{.consumed = true, .applied = true};
        }

        const NodeRecord* record = nodes.tryGet(imeFocusLabel.storageId());
        if (record == nullptr) {
            clearImeFocus();
            return UITextInputRouteResult{};
        }
        const UINodeId rootNode = idForIndex(record->rootIndex);
        if (!rootNode.hasValue()) {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        const std::string_view current = textViewFor(imeFocusLabel.index());
        if (current.size() > (std::numeric_limits<usize>::max)() - committedUtf8.size()) {
            return fail(
                UIErrorCode::CapacityExceeded,
                "UI text input would overflow the text byte capacity");
        }
        // One-shot commit allocation; not a per-frame hot path.
        std::string combined;
        combined.reserve(current.size() + committedUtf8.size());
        combined.append(current);
        combined.append(committedUtf8);
        if (Core::Status status =
                setTextFromUpdater(rootNode, imeFocusLabel, combined);
            !status) {
            return Core::failure(status.error());
        }
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    [[nodiscard]] UIContextStatistics statistics() const noexcept
    {
        return UIContextStatistics{
            .nodeCapacity = capacityConfig.nodeCapacity,
            .rootCapacity = capacityConfig.rootCapacity,
            .dirtyQueueCapacity = capacityConfig.dirtyQueueCapacity,
            .layoutSnapshotCapacity = capacityConfig.layoutSnapshotCapacity,
            .hitSnapshotCapacity = capacityConfig.hitSnapshotCapacity,
            .paintSnapshotCapacity = capacityConfig.paintSnapshotCapacity,
            .routePathCapacity = capacityConfig.routePathCapacity,
            .routedPointerListenerCapacity =
                capacityConfig.routedPointerListenerCapacity,
            .activeRoutedPointerListenerCount =
                activeRoutedPointerListenerCount,
            .routedPointerListenerHighWater = routedPointerListenerHighWater,
            .buttonActionCapacity = capacityConfig.buttonActionCapacity,
            .activeButtonActionCount = activeButtonActionCount,
            .buttonActionHighWater = buttonActionHighWater,
            .textByteCapacity = capacityConfig.textByteCapacity,
            .textByteUsed = textByteUsed,
            .textByteHighWater = textByteHighWater,
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
            .committedPaintNodeCount =
                committedPaintBuffers[publishedPaintBufferIndex].size(),
            .paintRevision = committedPaintRevision,
            .dirty = structureDirty,
            .layoutDirty = layoutDirty,
            .hitDirty = hitDirty,
            .paintDirty = paintDirty,
            .lastLayoutPassCount = lastLayoutPass.passCount,
            .lastLayoutMeasuredNodeCount = lastLayoutPass.measuredNodeCount,
            .lastLayoutArrangedNodeCount = lastLayoutPass.arrangedNodeCount,
            .lastLayoutPercentMeasureFallbackCount =
                lastLayoutPass.percentMeasureFallbackCount,
            .lastHitRebuildCount = lastHitRebuildCount,
            .lastPaintCacheRebuildCount = lastPaintCacheRebuildCount,
            .lastPaintSnapshotRebuildCount = lastPaintSnapshotRebuildCount,
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

Core::Result<UINodeId> UIRootBuilder::createCheckbox(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Checkbox);
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

Core::Result<UINodeId> UITreeUpdater::createPanel(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Panel);
}

Core::Result<UINodeId> UITreeUpdater::createLabel(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Label);
}

Core::Result<UINodeId> UITreeUpdater::createButton(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Button);
}

Core::Result<UINodeId> UITreeUpdater::createCheckbox(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Checkbox);
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

Core::Status UITreeUpdater::setBoxPaint(UINodeId node, const UIBoxPaint& paint)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setBoxPaintFromUpdater(m_root, node, paint);
}

Core::Status UITreeUpdater::setText(UINodeId node, std::string_view utf8)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextFromUpdater(m_root, node, utf8);
}

Core::Status UITreeUpdater::setTextStyle(UINodeId node, const UITextStyle& style)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextStyleFromUpdater(m_root, node, style);
}

Core::Result<std::string_view> UITreeUpdater::text(UINodeId node)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textFromUpdater(m_root, node);
}

Core::Result<UITextStyle> UITreeUpdater::textStyle(UINodeId node)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textStyleFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setButtonAction(
    UINodeId button,
    UIButtonActionCallback callback)
{
    if (m_context == nullptr) {
        return fail(
            UIErrorCode::WrongContext,
            "UI tree updater is not bound to a context");
    }
    return m_context->setButtonActionFromUpdater(
        m_root,
        button,
        std::move(callback));
}

Core::Status UITreeUpdater::clearButtonAction(UINodeId button)
{
    if (m_context == nullptr) {
        return fail(
            UIErrorCode::WrongContext,
            "UI tree updater is not bound to a context");
    }
    return m_context->clearButtonActionFromUpdater(m_root, button);
}

Core::Result<bool> UITreeUpdater::isButtonPressed(UINodeId button) const
{
    if (m_context == nullptr) {
        return fail(
            UIErrorCode::WrongContext,
            "UI tree updater is not bound to a context");
    }
    return m_context->isButtonPressedFromUpdater(m_root, button);
}

Core::Status UITreeUpdater::setCheckboxAction(
    UINodeId checkbox,
    UIButtonActionCallback callback)
{
    if (m_context == nullptr) {
        return fail(
            UIErrorCode::WrongContext,
            "UI tree updater is not bound to a context");
    }
    return m_context->setCheckboxActionFromUpdater(
        m_root,
        checkbox,
        std::move(callback));
}

Core::Status UITreeUpdater::clearCheckboxAction(UINodeId checkbox)
{
    if (m_context == nullptr) {
        return fail(
            UIErrorCode::WrongContext,
            "UI tree updater is not bound to a context");
    }
    return m_context->clearCheckboxActionFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setChecked(UINodeId checkbox, bool checked)
{
    if (m_context == nullptr) {
        return fail(
            UIErrorCode::WrongContext,
            "UI tree updater is not bound to a context");
    }
    return m_context->setCheckedFromUpdater(m_root, checkbox, checked);
}

Core::Result<bool> UITreeUpdater::isChecked(UINodeId checkbox) const
{
    if (m_context == nullptr) {
        return fail(
            UIErrorCode::WrongContext,
            "UI tree updater is not bound to a context");
    }
    return m_context->isCheckedFromUpdater(m_root, checkbox);
}

Core::Result<bool> UITreeUpdater::isCheckboxPressed(UINodeId checkbox) const
{
    if (m_context == nullptr) {
        return fail(
            UIErrorCode::WrongContext,
            "UI tree updater is not bound to a context");
    }
    return m_context->isCheckboxPressedFromUpdater(m_root, checkbox);
}

Core::Result<UIRoutedPointerListenerToken> UITreeUpdater::addRoutedPointerListener(
    UIRoutedPointerListenerDesc descriptor,
    UIRoutedPointerCallback callback)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->addRoutedPointerListenerFromUpdater(
        m_root,
        descriptor,
        std::move(callback));
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
    // Validate before any allocation against the caller's resource so failed
    // Create remains allocation-free for invalid window/capacity probes.
    if (!ownerWindow.hasValue()) {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }
    if (Core::Status status = validateUIContextCapacityConfig(capacityConfig); !status) {
        return Core::failure(status.error());
    }

    // Placeholder rasterizer construction allocates against the caller's PMR and
    // must surface OOM as Result, not an uncaught bad_alloc (M10-A39 gate).
    try {
        auto rasterizer = createPlaceholderTextRasterizer({}, resource);
        if (!rasterizer) {
            return Core::failure(rasterizer.error());
        }
        return Create(ownerWindow, capacityConfig, std::move(*rasterizer), resource);
    } catch (const std::bad_alloc&) {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI context allocation failed");
    } catch (const std::exception& exception) {
        return fail(Core::CoreErrorCode::Internal, std::string_view(exception.what()));
    } catch (...) {
        return fail(Core::CoreErrorCode::Internal, "UI context allocation failed");
    }
}

Core::Result<std::unique_ptr<UIContext>> UIContext::Create(
    Platform::WindowId ownerWindow,
    UIContextCapacityConfig capacityConfig,
    std::unique_ptr<IUITextRasterizer> textRasterizer,
    std::pmr::memory_resource& resource)
{
    if (!ownerWindow.hasValue()) {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }
    if (!textRasterizer) {
        return fail(UIErrorCode::InvalidFont, "UI context text rasterizer is null");
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

        // Best-effort open of the built-in/empty face. Placeholder accepts {}.
        // FreeType adapters reject empty bytes; they remain without a face until
        // a later font-binding slice opens real bytes.
        auto faceResult = textRasterizer->openFace({});
        if (faceResult) {
            (*implResult)->textFace = *faceResult;
        }
        (*implResult)->textRasterizer = std::move(textRasterizer);

        auto atlasResult = UIGlyphAtlas::Create(
            UIGlyphAtlasCapacity{
                .width = 512,
                .height = 512,
                .maxGlyphs = 1024,
            },
            resource);
        if (atlasResult) {
            (*implResult)->glyphAtlas = std::move(*atlasResult);
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
            || m_impl->listenerCallbackOperationDepth != 0
            || m_impl->buttonActionCallbackOperationDepth != 0) {
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

Core::Status UIContext::openTextFont(
    std::span<const std::byte> fontBytes,
    i32 faceIndex)
{
    return m_impl->openTextFont(fontBytes, faceIndex);
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

UICommittedPaintView UIContext::committedPaint() const noexcept
{
    return m_impl->committedPaint();
}

std::span<const u8> UIContext::glyphAtlasPixels() const noexcept
{
    return m_impl->glyphAtlasPixels();
}

u32 UIContext::glyphAtlasWidth() const noexcept
{
    return m_impl->glyphAtlasWidth();
}

u32 UIContext::glyphAtlasHeight() const noexcept
{
    return m_impl->glyphAtlasHeight();
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
        std::move(callback),
        {});
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

Core::Status UIContext::cancelPointerInteraction(
    Platform::WindowId routedWindow)
{
    return m_impl->cancelPointerInteraction(routedWindow);
}

Core::Result<UIContext::UIDefaultActionResult>
UIContext::routeDefaultActionActivate(
    Platform::PlatformFrameId platformFrame,
    u64 sourceSequence,
    UIButtonActivationSource source)
{
    return m_impl->routeDefaultActionActivate(
        platformFrame, sourceSequence, source);
}

Core::Result<UIContext::UIDefaultFocusStepResult>
UIContext::routeDefaultActionFocusStep(bool reverse)
{
    return m_impl->routeDefaultActionFocusStep(reverse);
}

UINodeId UIContext::defaultActionFocus() const noexcept
{
    if (!m_impl->isOwnerThread()) {
        return {};
    }
    return m_impl->defaultActionFocus();
}

UINodeId UIContext::imeFocus() const noexcept
{
    if (!m_impl->isOwnerThread()) {
        return {};
    }
    return m_impl->imeFocus();
}

bool UIContext::imeCompositionActive() const noexcept
{
    return m_impl->isOwnerThread() && m_impl->imeCompositionActive();
}

std::string_view UIContext::imePreeditUtf8() const noexcept
{
    if (!m_impl->isOwnerThread()) {
        return {};
    }
    return m_impl->imePreeditUtf8();
}

u32 UIContext::imePreeditCursorCodepoint() const noexcept
{
    if (!m_impl->isOwnerThread()) {
        return 0;
    }
    return m_impl->imePreeditCursorCodepoint();
}

Core::Result<UIContext::UITextInputRouteResult> UIContext::routeTextComposition(
    Platform::WindowId window,
    Platform::PlatformFrameId platformFrame,
    u64 sourceSequence,
    std::string_view preeditUtf8,
    u32 cursorCodepoint,
    Platform::TextCompositionStage stage)
{
    return m_impl->routeTextComposition(
        window, platformFrame, sourceSequence, preeditUtf8, cursorCodepoint, stage);
}

Core::Result<UIContext::UITextInputRouteResult> UIContext::routeTextInput(
    Platform::WindowId window,
    Platform::PlatformFrameId platformFrame,
    u64 sourceSequence,
    std::string_view committedUtf8)
{
    return m_impl->routeTextInput(
        window, platformFrame, sourceSequence, committedUtf8);
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

Core::Result<UINodeId> UIContext::createChildFromUpdater(
    UINodeId updaterRoot,
    UINodeId parent,
    UIWidgetKind kind)
{
    return m_impl->createChildFromUpdater(updaterRoot, parent, kind);
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

Core::Status UIContext::setBoxPaintFromUpdater(
    UINodeId updaterRoot,
    UINodeId node,
    const UIBoxPaint& paint)
{
    return m_impl->setBoxPaintFromUpdater(updaterRoot, node, paint);
}

Core::Status UIContext::setTextFromUpdater(
    UINodeId updaterRoot,
    UINodeId node,
    std::string_view utf8)
{
    return m_impl->setTextFromUpdater(updaterRoot, node, utf8);
}

Core::Status UIContext::setTextStyleFromUpdater(
    UINodeId updaterRoot,
    UINodeId node,
    const UITextStyle& style)
{
    return m_impl->setTextStyleFromUpdater(updaterRoot, node, style);
}

Core::Result<std::string_view> UIContext::textFromUpdater(
    UINodeId updaterRoot,
    UINodeId node)
{
    return m_impl->textFromUpdater(updaterRoot, node);
}

Core::Result<UITextStyle> UIContext::textStyleFromUpdater(
    UINodeId updaterRoot,
    UINodeId node)
{
    return m_impl->textStyleFromUpdater(updaterRoot, node);
}

Core::Status UIContext::setButtonActionFromUpdater(
    UINodeId updaterRoot,
    UINodeId button,
    UIButtonActionCallback&& callback)
{
    return m_impl->setButtonActionFromUpdater(
        updaterRoot,
        button,
        std::move(callback));
}

Core::Status UIContext::clearButtonActionFromUpdater(
    UINodeId updaterRoot,
    UINodeId button)
{
    return m_impl->clearButtonActionFromUpdater(updaterRoot, button);
}

Core::Result<bool> UIContext::isButtonPressedFromUpdater(
    UINodeId updaterRoot,
    UINodeId button)
{
    return m_impl->isButtonPressedFromUpdater(updaterRoot, button);
}

Core::Status UIContext::setCheckboxActionFromUpdater(
    UINodeId updaterRoot,
    UINodeId checkbox,
    UIButtonActionCallback&& callback)
{
    return m_impl->setCheckboxActionFromUpdater(
        updaterRoot,
        checkbox,
        std::move(callback));
}

Core::Status UIContext::clearCheckboxActionFromUpdater(
    UINodeId updaterRoot,
    UINodeId checkbox)
{
    return m_impl->clearCheckboxActionFromUpdater(updaterRoot, checkbox);
}

Core::Status UIContext::setCheckedFromUpdater(
    UINodeId updaterRoot,
    UINodeId checkbox,
    bool checked)
{
    return m_impl->setCheckedFromUpdater(updaterRoot, checkbox, checked);
}

Core::Result<bool> UIContext::isCheckedFromUpdater(
    UINodeId updaterRoot,
    UINodeId checkbox) const
{
    return m_impl->isCheckedFromUpdater(updaterRoot, checkbox);
}

Core::Result<bool> UIContext::isCheckboxPressedFromUpdater(
    UINodeId updaterRoot,
    UINodeId checkbox)
{
    return m_impl->isCheckboxPressedFromUpdater(updaterRoot, checkbox);
}

Core::Result<UIRoutedPointerListenerToken> UIContext::addRoutedPointerListenerFromUpdater(
    UINodeId updaterRoot,
    UIRoutedPointerListenerDesc descriptor,
    UIRoutedPointerCallback&& callback)
{
    auto registration = m_impl->addRoutedPointerListenerFromUpdater(
        updaterRoot,
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
