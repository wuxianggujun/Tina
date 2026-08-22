#include "UIBenchmarkWorkloads.hpp"

#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/integration/UIRenderDisplayList.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/UIDisplayList.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::Bench {
namespace {

inline constexpr int kSchemaVersion = 1;
inline constexpr int kWorkloadVersion = 1;
inline constexpr std::string_view kStaticCommitWorkload = "ui_static_commit_v1";
inline constexpr std::string_view kPaintDirtyWorkload = "ui_paint_dirty_v1";
inline constexpr std::string_view kRouteWorkload = "ui_route_v1";
inline constexpr std::string_view kVirtualCollectionWorkload = "ui_virtual_collection_v1";
inline constexpr std::string_view kImageNineSliceWorkload = "ui_image_nineslice_v1";
inline constexpr std::string_view kComponentBuildWorkload = "ui_component_build_v1";
inline constexpr std::string_view kStyleStateWorkload = "ui_style_state_v1";
inline constexpr std::string_view kMotionWorkload = "ui_motion_v1";
inline constexpr std::string_view kTimelineMotionWorkload = "ui_motion_timeline_v1";
inline constexpr std::string_view kLayoutTimelineMotionWorkload = "ui_motion_layout_v1";
inline constexpr usize kMotionTrackCapacity = 1024;
inline constexpr usize kTimelineCapacity = 256;
inline constexpr usize kTimelineTracksPerDefinition = 4;
inline constexpr usize kTimelineKeyframesPerTrack = 4;
inline constexpr usize kTimelineTrackCapacity =
    kTimelineCapacity * kTimelineTracksPerDefinition;
inline constexpr usize kTimelineKeyframeCapacity =
    kTimelineTrackCapacity * kTimelineKeyframesPerTrack;

inline constexpr usize kLargeNodeCount = 4096;
inline constexpr usize kFlatLeafCount = kLargeNodeCount - 1;
inline constexpr usize kRouteDepth = 64;
inline constexpr u64 kLogicalItemCount = 100'000;
inline constexpr u32 kMaterializedRowCapacity = 64;
inline constexpr usize kImageElementCount = 256;
inline constexpr usize kIconElementCount = 232;
inline constexpr usize kNineSliceElementCount = 512;
inline constexpr usize kUniqueImageResourceCount = 64;
inline constexpr usize kImageBenchmarkElementCount =
    kImageElementCount + kIconElementCount + kNineSliceElementCount;
inline constexpr usize kImageBenchmarkNodeCount = kImageBenchmarkElementCount + 1;
inline constexpr usize kImageBenchmarkQuadCount =
    kImageElementCount + kIconElementCount + (kNineSliceElementCount * 9);
inline constexpr usize kImageBenchmarkBatchCount = kImageBenchmarkElementCount;
inline constexpr float kImageElementExtent = 16.0F;
inline constexpr usize kComponentCount = 256;
inline constexpr usize kComponentNodesPerTransaction = 4;
inline constexpr usize kComponentNodeCount = kComponentCount * kComponentNodesPerTransaction;
inline constexpr usize kComponentContextNodeCount = kComponentNodeCount + 1;
inline constexpr usize kComponentCanvasCommandsPerTransaction = 2;
inline constexpr usize kComponentCanvasCommandCount =
    kComponentCount * kComponentCanvasCommandsPerTransaction;
inline constexpr std::string_view kComponentTextInputText = "Input";
inline constexpr std::string_view kComponentDropdownText = "Select";
inline constexpr usize kComponentTextBytesPerTransaction =
    kComponentTextInputText.size() + kComponentDropdownText.size();
inline constexpr usize kComponentTextByteCount =
    kComponentCount * kComponentTextBytesPerTransaction;
inline constexpr UI::UIComponentBuildBudget kComponentBuildBudget{
    .nodes = kComponentNodesPerTransaction,
    .textBytes = kComponentTextBytesPerTransaction,
    .canvasCommands = kComponentCanvasCommandsPerTransaction,
    .behaviors = {
        .activate = 2,
        .toggle = 1,
        .range = 1,
        .textInput = 1,
        .scroll = 1,
        .selection = 1,
    },
};
inline constexpr usize kStyleClassCount = 64;
inline constexpr usize kStyleRulesPerClass = 4;
inline constexpr usize kStyleRuleCount = kStyleClassCount * kStyleRulesPerClass;
inline constexpr usize kStyleClassesPerNode = 4;
inline constexpr usize kStyledNodeCount = kLargeNodeCount - 1;
inline constexpr usize kStyleNodeClassLinkCount = kStyledNodeCount * kStyleClassesPerNode;

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class CountingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept { return allocationCount_; }
    [[nodiscard]] usize deallocationCount() const noexcept { return deallocationCount_; }
    [[nodiscard]] usize currentBytes() const noexcept { return currentBytes_; }
    [[nodiscard]] usize peakBytes() const noexcept { return peakBytes_; }

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* allocation = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++allocationCount_;
        currentBytes_ += bytes;
        peakBytes_ = (std::max)(peakBytes_, currentBytes_);
        return allocation;
    }

    void do_deallocate(void* allocation, usize bytes, usize alignment) override
    {
        ++deallocationCount_;
        currentBytes_ -= bytes;
        std::pmr::new_delete_resource()->deallocate(allocation, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize allocationCount_ = 0;
    usize deallocationCount_ = 0;
    usize currentBytes_ = 0;
    usize peakBytes_ = 0;
};

class DeterministicHash final {
public:
    void addU8(u8 value) noexcept { addByte(value); }

    void addU32(u32 value) noexcept
    {
        for (u32 shift = 0; shift < 32; shift += 8) {
            addByte(static_cast<u8>((value >> shift) & 0xFFU));
        }
    }

    void addU64(u64 value) noexcept
    {
        for (u32 shift = 0; shift < 64; shift += 8) {
            addByte(static_cast<u8>((value >> shift) & 0xFFULL));
        }
    }

    void addFloat(float value) noexcept { addU32(std::bit_cast<u32>(value)); }

    void addBool(bool value) noexcept { addU8(value ? 1U : 0U); }

    void addString(std::string_view value) noexcept
    {
        addU64(static_cast<u64>(value.size()));
        for (const unsigned char byte : value) {
            addByte(byte);
        }
    }

    [[nodiscard]] u64 value() const noexcept { return value_; }

private:
    void addByte(u8 byte) noexcept
    {
        value_ ^= static_cast<u64>(byte);
        value_ *= 1099511628211ULL;
    }

    u64 value_ = 14695981039346656037ULL;
};

struct TimingSummary final {
    u64 p50 = 0;
    u64 p95 = 0;
    u64 p99 = 0;
    u64 max = 0;
    double mean = 0.0;
    usize count = 0;
};

struct UIBenchmarkReport final {
    std::string_view workload{};
    UIBenchmarkOptions options{};
    std::vector<u64> totalSamples{};
    std::vector<u64> buildSamples{};
    std::vector<u64> commitSamples{};
    std::vector<u64> cleanCommitSamples{};
    std::vector<u64> displayListSamples{};
    std::vector<u64> routeSamples{};
    u64 wallNs = 0;

    UI::UIContextStatistics statistics{};
    usize configuredNodeCount = 0;
    usize configuredRouteDepth = 0;
    u64 configuredLogicalItemCount = 0;
    u32 configuredMaterializedRowCapacity = 0;
    usize configuredImageCount = 0;
    usize configuredIconCount = 0;
    usize configuredNineSliceCount = 0;
    usize configuredUniqueImageResourceCount = 0;
    usize configuredComponentCount = 0;
    usize configuredComponentNodesPerTransaction = 0;
    usize configuredComponentTextBytesPerTransaction = 0;
    usize configuredComponentCanvasCommandsPerTransaction = 0;
    UI::UIBehaviorSlotBudget configuredComponentBehaviorSlotsPerTransaction{};
    usize configuredStyledNodeCount = 0;
    usize configuredStyleClassCount = 0;
    usize configuredStyleRuleCount = 0;
    usize configuredStyleClassesPerNode = 0;
    usize configuredStyleRulesPerBucket = 0;

    u64 workN = 0;
    u64 workP = 0;
    u64 workH = 0;
    u64 workM = 0;
    u64 workQ = 0;
    u64 workU = 0;
    u64 workB = 0;

    u64 layoutPasses = 0;
    u64 measuredNodes = 0;
    u64 arrangedNodes = 0;
    u64 hitRebuilds = 0;
    u64 paintCacheRebuilds = 0;
    u64 paintSnapshotRebuilds = 0;
    u64 paintSnapshotInspectedLayoutNodes = 0;
    u64 paintSnapshotPublishedEntries = 0;

    u64 displayListBuilds = 0;
    u64 displayListSourceEntries = 0;
    u64 displayListSolidQuads = 0;
    u64 displayListGlyphs = 0;
    u64 displayListImageQuads = 0;
    u64 displayListBatches = 0;
    u64 displayListChecksum = 0;

    u64 imageResolverCalls = 0;
    u64 imageResolverHits = 0;
    u64 imageResolverMisses = 0;
    u64 imageResolverNotReady = 0;
    u64 imageExtentMismatches = 0;
    u64 imageResolutionCacheDedupe = 0;
    u64 imagePinAcquisitions = 0;
    u64 imagePinReleases = 0;
    u64 imageResourceInternDedupe = 0;
    u64 imagePinHighWater = 0;
    u64 imageResourceHighWater = 0;
    u64 imageCommandHighWater = 0;
    u64 imageBatchHighWater = 0;

    u64 routeDispatches = 0;
    u64 hitEntriesVisited = 0;
    u64 routePathNodes = 0;
    u64 maxRouteDepth = 0;
    u64 listenerCalls = 0;
    u64 consumedTransitions = 0;
    u64 claimedTransitions = 0;
    u64 pointerCaptureRoutes = 0;

    u64 materializedRowHighWater = 0;
    u64 liveNodeHighWater = 0;
    u64 layoutSnapshotHighWater = 0;
    u64 hitSnapshotHighWater = 0;
    u64 paintSnapshotHighWater = 0;
    u64 selectionKey = 0;
    u64 selectionIndex = 0;
    u64 semanticsEntryCount = 0;
    u64 semanticsChecksum = 0;

    u64 componentTransactionsStarted = 0;
    u64 componentTransactionsCommitted = 0;
    u64 componentNodesRequested = 0;
    u64 componentNodesPublished = 0;
    u64 componentTextBytesRequested = 0;
    u64 componentTextBytesPublished = 0;
    u64 componentCanvasCommandsRequested = 0;
    u64 componentCanvasCommandsPublished = 0;
    u64 componentActivateSlotsRequested = 0;
    u64 componentActivateSlotsPublished = 0;
    u64 componentToggleSlotsRequested = 0;
    u64 componentToggleSlotsPublished = 0;
    u64 componentRangeSlotsRequested = 0;
    u64 componentRangeSlotsPublished = 0;
    u64 componentTextInputSlotsRequested = 0;
    u64 componentTextInputSlotsPublished = 0;
    u64 componentScrollSlotsRequested = 0;
    u64 componentScrollSlotsPublished = 0;
    u64 componentSelectionSlotsRequested = 0;
    u64 componentSelectionSlotsPublished = 0;
    u64 componentCleanCommitCount = 0;
    u64 componentCleanCommitRebuildCount = 0;
    u64 componentTreeChecksum = 0;
    UI::UIComponentBuildStatistics componentReservationStatistics{};

    u64 styleStateChanges = 0;
    u64 styleInspectedNodes = 0;
    u64 styleResolvedNodes = 0;
    u64 styleCandidateRules = 0;
    u64 styleCleanCommitCount = 0;
    u64 styleCleanInspectedNodes = 0;
    u64 styleCleanResolvedNodes = 0;
    u64 styleCleanCandidateRules = 0;
    u64 styleEnabledDisplayListChecksum = 0;
    u64 styleDisabledDisplayListChecksum = 0;
    u64 styleStateChecksum = 0;

    u64 configuredMotionTrackCapacity = 0;
    u64 configuredActiveMotionTracks = 0;
    u64 motionSampledTracks = 0;
    u64 motionActiveTracks = 0;
    u64 motionTrackHighWater = 0;
    u64 motionZeroActiveIterations = 0;
    u64 configuredTimelineCapacity = 0;
    u64 configuredTimelineTrackCapacity = 0;
    u64 configuredTimelineKeyframeCapacity = 0;
    u64 configuredActiveTimelineCapacity = 0;
    u64 configuredActiveTimelineTracks = 0;
    u64 timelineSampledTimelines = 0;
    u64 timelineSampledTracks = 0;
    u64 timelineSampledLayoutTracks = 0;
    u64 timelineSampledSegments = 0;
    u64 timelineActiveCount = 0;
    u64 timelineZeroActiveIterations = 0;
    u64 timelineLayoutCommitFailures = 0;

    usize pmrAllocationsBefore = 0;
    usize pmrAllocationsAfter = 0;
    usize pmrDeallocationsAfter = 0;
    usize pmrBytesBefore = 0;
    usize pmrBytesAfter = 0;
    usize pmrPeakBytes = 0;

    u64 checksum = 0;
};

struct UIFixture final {
    UIFixture() = default;
    UIFixture(const UIFixture&) = delete;
    UIFixture& operator=(const UIFixture&) = delete;
    UIFixture(UIFixture&&) noexcept = default;
    UIFixture& operator=(UIFixture&&) = delete;

    std::unique_ptr<WindowPool> windows{};
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context{};
    UI::UIRootOwner root{};
    UI::UITreeUpdater updater{};
};

struct ListDataSourceState final {
    u64 itemCount = kLogicalItemCount;
};

struct ImageResolverState final {
    UI::UINodeId root{};
    Render::Texture2DFrameResourceResolver resolver{};
    u64 resolverCalls = 0;
    u64 resolverHits = 0;
    u64 pinAcquisitions = 0;
    u64 pinReleases = 0;
    u64 resourceInternDedupe = 0;
    u64 activePins = 0;
    u64 pinHighWater = 0;

    static void releasePin(void* userData) noexcept
    {
        auto& state = *static_cast<ImageResolverState*>(userData);
        ++state.pinReleases;
        if (state.activePins != 0) {
            --state.activePins;
        }
    }

    static Core::Result<std::optional<Render::Texture2DFrameResourceResolution>>
    resolve(void* userData, Core::AssetId asset, Render::FrameResourceSink& sink) noexcept
    {
        auto& state = *static_cast<ImageResolverState*>(userData);
        ++state.resolverCalls;

        const auto& bytes = asset.bytes();
        const u32 resourceOrdinal = std::to_integer<u32>(bytes[1]);
        if (bytes[0] != std::byte{0x49} || resourceOrdinal == 0 ||
            resourceOrdinal > kUniqueImageResourceCount) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "UI image benchmark resolver received an unknown AssetId");
        }

        const u64 bindingKey = 0x55490000ULL + resourceOrdinal;
        ++state.pinAcquisitions;
        ++state.activePins;
        state.pinHighWater = (std::max)(state.pinHighWater, state.activePins);
        const u32 resourcesBefore = sink.resourceCount();
        auto interned = sink.intern(
            {
                .kind = Render::FrameResourceKind::Texture2D,
                .deviceBindingKey = bindingKey,
            },
            Render::FramePin{Render::FramePinKind::Custom, bindingKey, &state, &releasePin});
        if (!interned) {
            return Core::failure(std::move(interned.error()));
        }
        if (sink.resourceCount() == resourcesBefore) {
            ++state.resourceInternDedupe;
        }
        ++state.resolverHits;
        return std::optional<Render::Texture2DFrameResourceResolution>{
            Render::Texture2DFrameResourceResolution{
                .resource = *interned,
                .pixelWidth = 16,
                .pixelHeight = 16,
            }};
    }

    void initialize(UI::UINodeId benchmarkRoot) noexcept
    {
        root = benchmarkRoot;
        resolver = {
            .userData = this,
            .resolve = &resolve,
        };
    }
};

[[nodiscard]] const Render::Texture2DFrameResourceResolver*
findImageBenchmarkResolver(const void* userData, UI::UINodeId root) noexcept
{
    const auto& state = *static_cast<const ImageResolverState*>(userData);
    return root == state.root ? &state.resolver : nullptr;
}

[[nodiscard]] u64 listItemCount(const void* state) noexcept
{
    return static_cast<const ListDataSourceState*>(state)->itemCount;
}

[[nodiscard]] bool resolveListItem(const void* state, u64 logicalIndex,
                                   UI::UIListViewItemDescriptor& output) noexcept
{
    const auto& source = *static_cast<const ListDataSourceState*>(state);
    if (logicalIndex >= source.itemCount) {
        return false;
    }
    output = UI::UIListViewItemDescriptor{
        .key = logicalIndex + 1,
        .label = {},
        .enabled = true,
    };
    return true;
}

[[nodiscard]] std::string hex64(u64 value)
{
    static constexpr char HexDigits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (i32 index = 15; index >= 0; --index) {
        result[static_cast<usize>(index)] = HexDigits[value & 0xFULL];
        value >>= 4;
    }
    return result;
}

void writeJsonString(std::ostream& output, std::string_view value)
{
    static constexpr char HexDigits[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                output << "\\u00" << HexDigits[(byte >> 4U) & 0xFU] << HexDigits[byte & 0xFU];
            } else {
                output.put(static_cast<char>(byte));
            }
            break;
        }
    }
    output.put('"');
}

[[nodiscard]] u64 elapsedNs(Core::MonotonicTimePoint begin,
                            Core::MonotonicTimePoint end) noexcept
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    return static_cast<u64>((std::max)(elapsed, std::int64_t{0}));
}

[[nodiscard]] u64 nearestRank(std::span<const u64> sortedSamples, double quantile) noexcept
{
    if (sortedSamples.empty()) {
        return 0;
    }
    const double rank = quantile * static_cast<double>(sortedSamples.size() - 1);
    const usize index = static_cast<usize>(std::llround(rank));
    return sortedSamples[(std::min)(index, sortedSamples.size() - 1)];
}

[[nodiscard]] TimingSummary summarize(const std::vector<u64>& samples)
{
    TimingSummary summary{.count = samples.size()};
    if (samples.empty()) {
        return summary;
    }

    std::vector<u64> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    summary.p50 = nearestRank(sorted, 0.50);
    summary.p95 = nearestRank(sorted, 0.95);
    summary.p99 = nearestRank(sorted, 0.99);
    long double sum = 0.0L;
    for (const u64 sample : samples) {
        sum += static_cast<long double>(sample);
        summary.max = (std::max)(summary.max, sample);
    }
    summary.mean = static_cast<double>(sum / static_cast<long double>(samples.size()));
    return summary;
}

void writeTimingSummary(std::ostream& output, const TimingSummary& summary,
                        std::optional<u64> wall = std::nullopt)
{
    output << "{\"p50\":" << summary.p50 << ",\"p95\":" << summary.p95
           << ",\"p99\":" << summary.p99 << ",\"max\":" << summary.max
           << ",\"mean\":" << std::setprecision(17) << summary.mean
           << ",\"count\":" << summary.count;
    if (wall.has_value()) {
        output << ",\"wall\":" << *wall;
    }
    output << '}';
}

[[nodiscard]] UI::UILayoutStyle fixedLayout(float width, float height) noexcept
{
    UI::UILayoutStyle layout{};
    layout.size.width = UI::UILayoutLength::Px(width);
    layout.size.height = UI::UILayoutLength::Px(height);
    return layout;
}

[[nodiscard]] UI::UIBoxPaint solidPaint(UI::UIStraightSrgba8Color color) noexcept
{
    UI::UIBoxPaint paint{};
    paint.solidFill = UI::UISolidFill{.color = color};
    return paint;
}

[[nodiscard]] UI::UIElementDescriptor benchmarkPanel(bool targetable, bool painted,
                                                      UI::UIStraightSrgba8Color color = {}) noexcept
{
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedLayout(1.0F, 1.0F));
    descriptor.pointerHitPolicy = targetable ? UI::UIPointerHitPolicy::Targetable
                                             : UI::UIPointerHitPolicy::Ignore;
    if (painted) {
        descriptor.visual.boxPaint = solidPaint(color);
    }
    return descriptor;
}

[[nodiscard]] UI::UIContextCapacityConfig largeContextCapacity() noexcept
{
    UI::UIContextCapacityConfig capacity{};
    capacity.nodeCapacity = kLargeNodeCount;
    capacity.rootCapacity = 1;
    capacity.dirtyQueueCapacity = kLargeNodeCount;
    capacity.layoutSnapshotCapacity = kLargeNodeCount;
    capacity.hitSnapshotCapacity = kLargeNodeCount;
    capacity.paintSnapshotCapacity = kLargeNodeCount;
    capacity.routePathCapacity = 128;
    capacity.routedPointerListenerCapacity = 256;
    capacity.applyDefaultProductChrome = false;
    return capacity;
}

[[nodiscard]] UI::UIContextCapacityConfig styleBenchmarkContextCapacity() noexcept
{
    UI::UIContextCapacityConfig capacity = largeContextCapacity();
    capacity.styleClassCapacity = kStyleClassCount;
    capacity.styleTokenCapacity = kStyleClassCount;
    capacity.styleRuleCapacity = kStyleRuleCount;
    capacity.styleBucketCapacity = kStyleClassCount;
    capacity.styleRulesPerBucketCapacity = kStyleRulesPerClass;
    capacity.nodeStyleClassLinkCapacity = kStyleNodeClassLinkCount;
    return capacity;
}

[[nodiscard]] Core::AssetId imageBenchmarkAsset(usize resourceIndex) noexcept
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x49};
    bytes[1] = static_cast<std::byte>(resourceIndex + 1U);
    const auto asset = Core::AssetId::fromBytes(bytes);
    return asset.value_or(Core::AssetId{});
}

[[nodiscard]] UI::UIImageSource imageBenchmarkSource(usize resourceIndex) noexcept
{
    return UI::UIImageSource{
        .texture = imageBenchmarkAsset(resourceIndex),
        .sourcePixels = {.width = 16, .height = 16},
        .texturePixelExtent = {.width = 16, .height = 16},
        .intrinsicLogicalSize = {.width = kImageElementExtent, .height = kImageElementExtent},
    };
}

[[nodiscard]] UI::UIImageContent imageBenchmarkContent(usize resourceIndex) noexcept
{
    return UI::UIImageContent{
        .source = imageBenchmarkSource(resourceIndex),
        .fit = UI::UIImageFit::Fill,
        .tint = UI::rgba8(255, 255, 255),
        .sampling = UI::UIImageSampling::Nearest,
    };
}

[[nodiscard]] UI::UIIconContent iconBenchmarkContent(usize resourceIndex) noexcept
{
    return UI::UIIconContent{
        .source = imageBenchmarkSource(resourceIndex),
        .tint = UI::rgba8(255, 255, 255),
        .sampling = UI::UIImageSampling::Nearest,
    };
}

[[nodiscard]] UI::UIContextCapacityConfig imageBenchmarkContextCapacity() noexcept
{
    UI::UIContextCapacityConfig capacity{};
    capacity.nodeCapacity = kImageBenchmarkNodeCount;
    capacity.rootCapacity = 1;
    capacity.dirtyQueueCapacity = kImageBenchmarkNodeCount;
    capacity.layoutSnapshotCapacity = kImageBenchmarkNodeCount;
    capacity.hitSnapshotCapacity = kImageBenchmarkNodeCount;
    capacity.paintSnapshotCapacity = kImageBenchmarkQuadCount;
    capacity.canvasCommandCapacity = kNineSliceElementCount;
    capacity.imageContentCapacity = kImageElementCount + kIconElementCount;
    capacity.routePathCapacity = kImageBenchmarkNodeCount;
    capacity.applyDefaultProductChrome = false;
    return capacity;
}

[[nodiscard]] UI::UIContextCapacityConfig componentBenchmarkContextCapacity() noexcept
{
    UI::UIContextCapacityConfig capacity{};
    capacity.nodeCapacity = kComponentContextNodeCount;
    capacity.rootCapacity = 1;
    capacity.dirtyQueueCapacity = kComponentContextNodeCount;
    capacity.layoutSnapshotCapacity = kComponentContextNodeCount;
    capacity.hitSnapshotCapacity = kComponentContextNodeCount;
    capacity.paintSnapshotCapacity = 4'096;
    capacity.canvasCommandCapacity = kComponentCanvasCommandCount;
    capacity.routePathCapacity = kComponentContextNodeCount;
    capacity.textByteCapacity = kComponentTextByteCount;
    capacity.applyDefaultProductChrome = false;
    return capacity;
}

[[nodiscard]] std::array<UI::UICanvasCommand, kComponentCanvasCommandsPerTransaction>
componentCanvasCommands() noexcept
{
    return {
        UI::UICanvasCommand{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 8.0F},
            .color = UI::rgba8(24, 96, 176),
            .cornerRadii = UI::UILogicalCornerRadii::uniform(1.0F),
        },
        UI::UICanvasCommand{
            .bounds = {.x = 2.0F, .y = 2.0F, .width = 4.0F, .height = 4.0F},
            .color = UI::rgba8(232, 200, 72),
        },
    };
}

[[nodiscard]] UI::UIElementDescriptor componentRangeActivateToggleDescriptor() noexcept
{
    UI::UIElementDescriptor descriptor = UI::makeSliderElement(fixedLayout(32.0F, 8.0F));
    descriptor.behaviors |= UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle;
    return descriptor;
}

[[nodiscard]] bool populateImageBenchmarkTree(UIFixture& fixture, std::string& error)
{
    usize resourceCursor = 0;
    const auto nextResource = [&resourceCursor]() noexcept {
        const usize resourceIndex = resourceCursor % kUniqueImageResourceCount;
        ++resourceCursor;
        return resourceIndex;
    };
    const UI::UILayoutStyle layout = fixedLayout(kImageElementExtent, kImageElementExtent);

    for (usize index = 0; index < kImageElementCount; ++index) {
        auto node = fixture.updater.createElement(
            fixture.root.rootNodeId(),
            UI::makeImageElement(imageBenchmarkContent(nextResource()), "Benchmark image", layout));
        if (!node) {
            error = node.error().message;
            return false;
        }
    }
    for (usize index = 0; index < kIconElementCount; ++index) {
        auto node = fixture.updater.createElement(
            fixture.root.rootNodeId(), UI::makeIconElement(iconBenchmarkContent(nextResource()), layout));
        if (!node) {
            error = node.error().message;
            return false;
        }
    }
    for (usize index = 0; index < kNineSliceElementCount; ++index) {
        const UI::UICanvasCommand command{
            .kind = UI::UICanvasCommandKind::NineSlice,
            .bounds = {.width = kImageElementExtent, .height = kImageElementExtent},
            .color = UI::rgba8(255, 255, 255),
            .imageSource = imageBenchmarkSource(nextResource()),
            .imageSourceInsets = {.left = 4, .top = 4, .right = 4, .bottom = 4},
            .imageDestinationInsets = UI::UIEdgeSpacing::All(4.0F),
            .imageSampling = UI::UIImageSampling::Nearest,
        };
        UI::UIElementDescriptor descriptor = UI::makePanelElement(layout);
        descriptor.visual.canvas = std::span(&command, 1);
        auto node = fixture.updater.createElement(fixture.root.rootNodeId(), descriptor);
        if (!node) {
            error = node.error().message;
            return false;
        }
    }
    return true;
}

using FixtureStartup = Core::Status (*)(UI::UIContext&, void* userData);

[[nodiscard]] std::optional<UIFixture> createFixture(
    UI::UIContextCapacityConfig capacity, CountingMemoryResource& memory,
    std::string& error, FixtureStartup startup = nullptr, void* startupUserData = nullptr)
{
    auto windows = WindowPool::Create(1, memory);
    if (!windows) {
        error = windows.error().message;
        return std::nullopt;
    }

    UIFixture fixture{};
    fixture.windows = std::make_unique<WindowPool>(std::move(*windows));
    auto window = fixture.windows->tryEmplace(7);
    if (!window) {
        error = window.error().message;
        return std::nullopt;
    }
    fixture.window = *window;

    auto context = UI::UIContext::Create(fixture.window, capacity, memory);
    if (!context) {
        error = context.error().message;
        return std::nullopt;
    }
    fixture.context = std::move(*context);

    if (startup != nullptr) {
        if (Core::Status status = startup(*fixture.context, startupUserData); !status) {
            error = status.error().message;
            return std::nullopt;
        }
    }

    auto root = fixture.context->rootBuilder().createRoot();
    if (!root) {
        error = root.error().message;
        return std::nullopt;
    }
    fixture.root = std::move(*root);

    auto updater = fixture.context->treeUpdater(fixture.root);
    if (!updater) {
        error = updater.error().message;
        return std::nullopt;
    }
    fixture.updater = std::move(*updater);
    return fixture;
}

struct StyleFixtureStartup final {
    std::array<UI::UIStyleClassId, kStyleClassCount> classes{};
};

[[nodiscard]] Core::Status initializeStyleFixture(UI::UIContext& context,
                                                  void* userData)
{
    auto& startup = *static_cast<StyleFixtureStartup*>(userData);
    for (UI::UIStyleClassId& styleClass : startup.classes) {
        auto registered = context.registerStyleClass();
        if (!registered) {
            return Core::failure(registered.error());
        }
        styleClass = *registered;
    }

    constexpr std::array States{
        UI::UIStyleState::None,
        UI::UIStyleState::Hovered,
        UI::UIStyleState::Disabled,
        UI::UIStyleState::Hovered | UI::UIStyleState::Disabled,
    };
    std::array<UI::UIStyleBoxFillRule, kStyleRuleCount> rules{};
    usize ruleIndex = 0;
    for (usize classIndex = 0; classIndex < startup.classes.size(); ++classIndex) {
        for (usize stateIndex = 0; stateIndex < States.size(); ++stateIndex) {
            const u8 red = static_cast<u8>(32U + ((classIndex * 3U + stateIndex * 17U) % 192U));
            const u8 green = static_cast<u8>(32U + ((classIndex * 5U + stateIndex * 29U) % 192U));
            const u8 blue = static_cast<u8>(32U + ((classIndex * 7U + stateIndex * 43U) % 192U));
            rules[ruleIndex++] = UI::UIStyleBoxFillRule{
                .role = UI::UIStyleRoleId::PanelSurface,
                .styleClass = startup.classes[classIndex],
                .requiredStates = States[stateIndex],
                .color = UI::rgba8(red, green, blue),
            };
        }
    }
    return context.installStyleSheet(rules);
}

[[nodiscard]] bool populateStyleTree(
    UIFixture& fixture,
    const std::array<UI::UIStyleClassId, kStyleClassCount>& registeredClasses,
    std::vector<UI::UINodeId>& leaves, std::string& error)
{
    leaves.reserve(kStyledNodeCount);
    for (usize nodeIndex = 0; nodeIndex < kStyledNodeCount; ++nodeIndex) {
        std::array<UI::UIStyleClassId, kStyleClassesPerNode> classes{};
        for (usize classIndex = 0; classIndex < classes.size(); ++classIndex) {
            classes[classIndex] =
                registeredClasses[(nodeIndex + classIndex * 17U) % registeredClasses.size()];
        }
        UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedLayout(1.0F, 1.0F));
        descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
        descriptor.visual.styleClasses = classes;
        descriptor.semantics = {
            .mode = UI::UISemanticsMode::Publish,
            .role = UI::UISemanticsRole::Group,
        };
        auto node = fixture.updater.createElement(fixture.root.rootNodeId(), descriptor);
        if (!node) {
            error = node.error().message;
            return false;
        }
        leaves.push_back(*node);
    }
    return true;
}

[[nodiscard]] bool populateFlatPaintTree(UIFixture& fixture, std::vector<UI::UINodeId>& leaves,
                                         std::string& error)
{
    leaves.reserve(kFlatLeafCount);
    for (usize index = 0; index < kFlatLeafCount; ++index) {
        const u8 channel = static_cast<u8>(32U + (index % 192U));
        auto node = fixture.updater.createElement(
            fixture.root.rootNodeId(),
            benchmarkPanel(true, true, UI::rgba8(channel, static_cast<u8>(255U - channel), 160U)));
        if (!node) {
            error = node.error().message;
            return false;
        }
        leaves.push_back(*node);
    }
    return true;
}

[[nodiscard]] bool populateLayoutMotionTree(
    UIFixture& fixture, std::vector<UI::UINodeId>& leaves, std::string& error)
{
    leaves.reserve(kFlatLeafCount);
    for (usize index = 0; index < kFlatLeafCount; ++index) {
        const u8 channel = static_cast<u8>(32U + (index % 192U));
        UI::UIElementDescriptor descriptor =
            benchmarkPanel(true, true,
                           UI::rgba8(channel, static_cast<u8>(255U - channel), 160U));
        descriptor.layout.placement = UI::UILayoutPlacement::Overlay;
        descriptor.layout.overlay.offset.x = UI::UILayoutLength::Px(0.0F);
        descriptor.layout.overlay.offset.y =
            UI::UILayoutLength::Px(static_cast<float>(index));
        auto node = fixture.updater.createElement(fixture.root.rootNodeId(), descriptor);
        if (!node) {
            error = node.error().message;
            return false;
        }
        leaves.push_back(*node);
    }
    return true;
}

[[nodiscard]] std::optional<Render::UIDisplayListBuilder>
createDisplayListBuilder(u32 capacity, CountingMemoryResource& memory, std::string& error)
{
    auto builder = Render::UIDisplayListBuilder::Create(
        Render::UIDisplayListCapacity{
            .commandCount = capacity,
            .clipCount = capacity,
            .batchCount = capacity,
        },
        memory);
    if (!builder) {
        error = builder.error().message;
        return std::nullopt;
    }
    return std::move(*builder);
}

void accumulateCommitStatistics(const UI::UIContextStatistics& statistics,
                                UIBenchmarkReport& report) noexcept
{
    report.layoutPasses += statistics.lastLayoutPassCount;
    report.measuredNodes += statistics.lastLayoutMeasuredNodeCount;
    report.arrangedNodes += statistics.lastLayoutArrangedNodeCount;
    report.hitRebuilds += statistics.lastHitRebuildCount;
    report.paintCacheRebuilds += statistics.lastPaintCacheRebuildCount;
    report.paintSnapshotRebuilds += statistics.lastPaintSnapshotRebuildCount;
    if (statistics.lastPaintSnapshotRebuildCount != 0) {
        report.paintSnapshotInspectedLayoutNodes += statistics.committedLayoutNodeCount;
        report.paintSnapshotPublishedEntries += statistics.committedPaintNodeCount;
    }
}

[[nodiscard]] bool buildDisplayList(UIFixture& fixture, Render::UIDisplayListBuilder& builder,
                                    Render::UIPixelRect framebufferViewport, UIBenchmarkReport* report,
                                    std::string& error, u64* checksum = nullptr)
{
    auto build = Integration::buildUIDisplayList(
        builder,
        fixture.context->committedPaint(),
        Integration::UIRenderViewportMapping{.framebufferViewport = framebufferViewport});
    if (!build) {
        error = build.error().message;
        return false;
    }
    const u64 currentChecksum = build->displayList.paintOrderChecksum();
    if (checksum != nullptr) {
        *checksum = currentChecksum;
    }
    if (report != nullptr) {
        ++report->displayListBuilds;
        report->displayListSourceEntries += build->statistics.sourcePaintEntryCount;
        report->displayListSolidQuads += build->statistics.submittedSolidQuadCount;
        report->displayListGlyphs += build->statistics.submittedGlyphCount;
        report->displayListImageQuads += build->statistics.submittedImageQuadCount;
        report->displayListBatches += build->displayList.statistics().batchCount;
        report->displayListChecksum = currentChecksum;
    }
    return true;
}

[[nodiscard]] bool buildImageBenchmarkDisplayList(
    UIFixture& fixture, Render::UIDisplayListBuilder& builder,
    Render::RenderFramePacket& packet, ImageResolverState& resolverState,
    std::span<Integration::UIRenderImageResolutionCacheEntry> cache, u64 frameIndex,
    Render::UIPixelRect framebufferViewport, std::optional<u64>& expectedChecksum,
    UIBenchmarkReport* report, std::string& error)
{
    const u64 resolverCallsBefore = resolverState.resolverCalls;
    const u64 resolverHitsBefore = resolverState.resolverHits;
    const u64 pinAcquisitionsBefore = resolverState.pinAcquisitions;
    const u64 pinReleasesBefore = resolverState.pinReleases;
    const u64 resourceInternDedupeBefore = resolverState.resourceInternDedupe;

    if (Core::Status status = packet.beginFrame(frameIndex); !status) {
        error = status.error().message;
        return false;
    }
    auto build = Integration::buildUIDisplayList(
        builder, fixture.context->committedPaint(),
        Integration::UIRenderViewportMapping{.framebufferViewport = framebufferViewport},
        {
            .resourceSink = &packet.resourceSink(),
            .resolverLookup = {.userData = &resolverState, .find = &findImageBenchmarkResolver},
            .cache = cache,
        });
    if (!build) {
        (void)packet.abandon();
        error = build.error().message;
        return false;
    }

    const u64 displayListChecksum = build->displayList.paintOrderChecksum();
    const u64 frameResourceCount = packet.resourceCount();
    const u64 resolverCalls = resolverState.resolverCalls - resolverCallsBefore;
    const u64 resolverHits = resolverState.resolverHits - resolverHitsBefore;
    const u64 pinAcquisitions = resolverState.pinAcquisitions - pinAcquisitionsBefore;
    const u64 resourceInternDedupe =
        resolverState.resourceInternDedupe - resourceInternDedupeBefore;
    if (Core::Status status = packet.completeSkipped(); !status) {
        error = status.error().message;
        return false;
    }
    const u64 pinReleases = resolverState.pinReleases - pinReleasesBefore;
    if (resolverState.activePins != 0 || packet.resourceCount() != 0) {
        error = "UI image benchmark frame resources were not fully released";
        return false;
    }
    if (expectedChecksum.has_value() && *expectedChecksum != displayListChecksum) {
        error = "UI image benchmark DisplayList checksum changed between clean builds";
        return false;
    }
    expectedChecksum = displayListChecksum;

    if (report != nullptr) {
        ++report->displayListBuilds;
        report->displayListSourceEntries += build->statistics.sourcePaintEntryCount;
        report->displayListSolidQuads += build->statistics.submittedSolidQuadCount;
        report->displayListGlyphs += build->statistics.submittedGlyphCount;
        report->displayListImageQuads += build->statistics.submittedImageQuadCount;
        report->displayListBatches += build->displayList.statistics().batchCount;
        report->displayListChecksum = displayListChecksum;

        report->imageResolverCalls += resolverCalls;
        report->imageResolverHits += resolverHits;
        report->imageResolverMisses += build->statistics.skippedImageMissingResolverCount;
        report->imageResolverNotReady += build->statistics.skippedImageUnavailableCount;
        report->imageExtentMismatches += build->statistics.skippedImageExtentMismatchCount;
        report->imageResolutionCacheDedupe +=
            build->statistics.submittedImageQuadCount - build->statistics.resolvedImageResourceCount;
        report->imagePinAcquisitions += pinAcquisitions;
        report->imagePinReleases += pinReleases;
        report->imageResourceInternDedupe += resourceInternDedupe;
        report->imagePinHighWater = (std::max)(report->imagePinHighWater, resolverState.pinHighWater);
        report->imageResourceHighWater = (std::max)(report->imageResourceHighWater, frameResourceCount);
        report->imageCommandHighWater =
            (std::max)(report->imageCommandHighWater,
                       static_cast<u64>(build->statistics.submittedImageQuadCount));
        report->imageBatchHighWater =
            (std::max)(report->imageBatchHighWater,
                       static_cast<u64>(build->displayList.statistics().batchCount));
        report->workQ = build->statistics.submittedImageQuadCount;
        report->workU = build->statistics.resolvedImageResourceCount;
        report->workB = build->displayList.statistics().batchCount;
    }
    return true;
}

void captureAllocationBaseline(const CountingMemoryResource& memory, UIBenchmarkReport& report) noexcept
{
    report.pmrAllocationsBefore = memory.allocationCount();
    report.pmrBytesBefore = memory.currentBytes();
}

void captureFinalState(const CountingMemoryResource& memory, UIBenchmarkReport& report,
                       const UIFixture& fixture) noexcept
{
    report.pmrAllocationsAfter = memory.allocationCount();
    report.pmrDeallocationsAfter = memory.deallocationCount();
    report.pmrBytesAfter = memory.currentBytes();
    report.pmrPeakBytes = memory.peakBytes();
    report.statistics = fixture.context->statistics();
    report.workN = report.statistics.committedLayoutNodeCount;
    report.workP = report.statistics.committedPaintNodeCount;
    report.workH = report.statistics.committedHitNodeCount;
    report.liveNodeHighWater = (std::max)(report.liveNodeHighWater,
                                          static_cast<u64>(report.statistics.liveNodeCount));
    report.layoutSnapshotHighWater = (std::max)(report.layoutSnapshotHighWater,
                                                static_cast<u64>(report.statistics.committedLayoutNodeCount));
    report.hitSnapshotHighWater = (std::max)(report.hitSnapshotHighWater,
                                             static_cast<u64>(report.statistics.committedHitNodeCount));
    report.paintSnapshotHighWater = (std::max)(report.paintSnapshotHighWater,
                                               static_cast<u64>(report.statistics.committedPaintNodeCount));
}

void hashComponentBuildPool(DeterministicHash& hash,
                            const UI::UIComponentBuildPoolStatistics& statistics) noexcept
{
    hash.addU64(statistics.requested);
    hash.addU64(statistics.reserved);
    hash.addU64(statistics.published);
    hash.addU64(statistics.capacityFailures);
    hash.addU64(statistics.outstandingReservations);
}

void hashComponentBuildStatistics(DeterministicHash& hash,
                                  const UI::UIComponentBuildStatistics& statistics) noexcept
{
    hashComponentBuildPool(hash, statistics.nodes);
    hashComponentBuildPool(hash, statistics.textBytes);
    hashComponentBuildPool(hash, statistics.canvasCommands);
    hashComponentBuildPool(hash, statistics.behaviors.activate);
    hashComponentBuildPool(hash, statistics.behaviors.toggle);
    hashComponentBuildPool(hash, statistics.behaviors.range);
    hashComponentBuildPool(hash, statistics.behaviors.textInput);
    hashComponentBuildPool(hash, statistics.behaviors.scroll);
    hashComponentBuildPool(hash, statistics.behaviors.selection);
    hash.addU64(statistics.activeTransactionCount);
    hash.addU64(statistics.transactionFailureCount);
}

void hashStyleStatistics(DeterministicHash& hash,
                         const UI::UIStyleStatistics& statistics) noexcept
{
    hash.addU64(statistics.classCapacity);
    hash.addU64(statistics.registeredClassCount);
    hash.addU64(statistics.classHighWater);
    hash.addU64(statistics.tokenCapacity);
    hash.addU64(statistics.registeredTokenCount);
    hash.addU64(statistics.tokenHighWater);
    hash.addU64(statistics.ruleCapacity);
    hash.addU64(statistics.activeRuleCount);
    hash.addU64(statistics.ruleHighWater);
    hash.addU64(statistics.bucketCapacity);
    hash.addU64(statistics.activeBucketCount);
    hash.addU64(statistics.bucketHighWater);
    hash.addU64(statistics.rulesPerBucketCapacity);
    hash.addU64(statistics.bucketCandidateHighWater);
    hash.addU64(statistics.nodeClassLinkCapacity);
    hash.addU64(statistics.activeNodeClassLinkCount);
    hash.addU64(statistics.nodeClassLinkHighWater);
    hash.addU64(statistics.compileFailureCount);
    hash.addU64(statistics.capacityFailureCount);
    hash.addU64(statistics.revision);
}

void hashStatistics(DeterministicHash& hash, const UIBenchmarkReport& report) noexcept
{
    hash.addString(report.workload);
    hash.addU64(kWorkloadVersion);
    hash.addU64(report.options.seed);
    hash.addU64(report.options.warmUpIterations);
    hash.addU64(report.options.measureIterations);
    hash.addU64(report.configuredNodeCount);
    hash.addU64(report.configuredRouteDepth);
    hash.addU64(report.configuredLogicalItemCount);
    hash.addU32(report.configuredMaterializedRowCapacity);
    hash.addU64(report.configuredComponentCount);
    hash.addU64(report.configuredComponentNodesPerTransaction);
    hash.addU64(report.configuredComponentTextBytesPerTransaction);
    hash.addU64(report.configuredComponentCanvasCommandsPerTransaction);
    hash.addU64(report.configuredComponentBehaviorSlotsPerTransaction.activate);
    hash.addU64(report.configuredComponentBehaviorSlotsPerTransaction.toggle);
    hash.addU64(report.configuredComponentBehaviorSlotsPerTransaction.range);
    hash.addU64(report.configuredComponentBehaviorSlotsPerTransaction.textInput);
    hash.addU64(report.configuredComponentBehaviorSlotsPerTransaction.scroll);
    hash.addU64(report.configuredComponentBehaviorSlotsPerTransaction.selection);
    hash.addU64(report.workN);
    hash.addU64(report.workP);
    hash.addU64(report.workH);
    hash.addU64(report.workM);
    hash.addU64(report.workQ);
    hash.addU64(report.workU);
    hash.addU64(report.workB);
    hash.addU64(report.layoutPasses);
    hash.addU64(report.measuredNodes);
    hash.addU64(report.arrangedNodes);
    hash.addU64(report.hitRebuilds);
    hash.addU64(report.paintCacheRebuilds);
    hash.addU64(report.paintSnapshotRebuilds);
    hash.addU64(report.paintSnapshotInspectedLayoutNodes);
    hash.addU64(report.paintSnapshotPublishedEntries);
    hash.addU64(report.displayListBuilds);
    hash.addU64(report.displayListSourceEntries);
    hash.addU64(report.displayListSolidQuads);
    hash.addU64(report.displayListGlyphs);
    hash.addU64(report.displayListBatches);
    hash.addU64(report.displayListChecksum);
    hash.addU64(report.routeDispatches);
    hash.addU64(report.hitEntriesVisited);
    hash.addU64(report.routePathNodes);
    hash.addU64(report.maxRouteDepth);
    hash.addU64(report.listenerCalls);
    hash.addU64(report.consumedTransitions);
    hash.addU64(report.claimedTransitions);
    hash.addU64(report.pointerCaptureRoutes);
    hash.addU64(report.materializedRowHighWater);
    hash.addU64(report.liveNodeHighWater);
    hash.addU64(report.layoutSnapshotHighWater);
    hash.addU64(report.hitSnapshotHighWater);
    hash.addU64(report.paintSnapshotHighWater);
    hash.addU64(report.selectionKey);
    hash.addU64(report.selectionIndex);
    hash.addU64(report.semanticsEntryCount);
    hash.addU64(report.semanticsChecksum);
    hash.addU64(report.componentTransactionsStarted);
    hash.addU64(report.componentTransactionsCommitted);
    hash.addU64(report.componentNodesRequested);
    hash.addU64(report.componentNodesPublished);
    hash.addU64(report.componentTextBytesRequested);
    hash.addU64(report.componentTextBytesPublished);
    hash.addU64(report.componentCanvasCommandsRequested);
    hash.addU64(report.componentCanvasCommandsPublished);
    hash.addU64(report.componentActivateSlotsRequested);
    hash.addU64(report.componentActivateSlotsPublished);
    hash.addU64(report.componentToggleSlotsRequested);
    hash.addU64(report.componentToggleSlotsPublished);
    hash.addU64(report.componentRangeSlotsRequested);
    hash.addU64(report.componentRangeSlotsPublished);
    hash.addU64(report.componentTextInputSlotsRequested);
    hash.addU64(report.componentTextInputSlotsPublished);
    hash.addU64(report.componentScrollSlotsRequested);
    hash.addU64(report.componentScrollSlotsPublished);
    hash.addU64(report.componentSelectionSlotsRequested);
    hash.addU64(report.componentSelectionSlotsPublished);
    hash.addU64(report.componentCleanCommitCount);
    hash.addU64(report.componentCleanCommitRebuildCount);
    hash.addU64(report.componentTreeChecksum);
    hashComponentBuildStatistics(hash, report.componentReservationStatistics);
    if (report.workload == kStyleStateWorkload) {
        hash.addU64(report.configuredStyledNodeCount);
        hash.addU64(report.configuredStyleClassCount);
        hash.addU64(report.configuredStyleRuleCount);
        hash.addU64(report.configuredStyleClassesPerNode);
        hash.addU64(report.configuredStyleRulesPerBucket);
        hash.addU64(report.styleStateChanges);
        hash.addU64(report.styleInspectedNodes);
        hash.addU64(report.styleResolvedNodes);
        hash.addU64(report.styleCandidateRules);
        hash.addU64(report.styleCleanCommitCount);
        hash.addU64(report.styleCleanInspectedNodes);
        hash.addU64(report.styleCleanResolvedNodes);
        hash.addU64(report.styleCleanCandidateRules);
        hash.addU64(report.styleEnabledDisplayListChecksum);
        hash.addU64(report.styleDisabledDisplayListChecksum);
        hash.addU64(report.styleStateChecksum);
        hashStyleStatistics(hash, report.statistics.style);
    }
    if (report.workload == kMotionWorkload) {
        hash.addU64(report.configuredMotionTrackCapacity);
        hash.addU64(report.configuredActiveMotionTracks);
        hash.addU64(report.motionSampledTracks);
        hash.addU64(report.motionActiveTracks);
        hash.addU64(report.motionTrackHighWater);
        hash.addU64(report.motionZeroActiveIterations);
    }
    if (report.workload == kTimelineMotionWorkload ||
        report.workload == kLayoutTimelineMotionWorkload) {
        hash.addU64(report.configuredTimelineCapacity);
        hash.addU64(report.configuredTimelineTrackCapacity);
        hash.addU64(report.configuredTimelineKeyframeCapacity);
        hash.addU64(report.configuredActiveTimelineCapacity);
        hash.addU64(report.configuredActiveTimelineTracks);
        hash.addU64(report.timelineSampledTimelines);
        hash.addU64(report.timelineSampledTracks);
        hash.addU64(report.timelineSampledSegments);
        hash.addU64(report.timelineActiveCount);
        hash.addU64(report.timelineZeroActiveIterations);
        hash.addU64(report.statistics.motion.timelineHighWater);
        hash.addU64(report.statistics.motion.timelineTrackHighWater);
        hash.addU64(report.statistics.motion.keyframeHighWater);
        hash.addU64(report.statistics.motion.activeTimelineHighWater);
        if (report.workload == kLayoutTimelineMotionWorkload) {
            hash.addU64(report.timelineSampledLayoutTracks);
            hash.addU64(report.timelineLayoutCommitFailures);
        }
    }
}

class BenchMotionClock final : public Core::IMonotonicClock {
  public:
    [[nodiscard]] Core::MonotonicTimePoint now() const noexcept override
    {
        return now_;
    }

    void advance(Core::Duration delta) noexcept
    {
        now_ += std::chrono::duration_cast<Core::MonotonicDuration>(delta);
    }

  private:
    Core::MonotonicTimePoint now_{};
};

[[nodiscard]] u64 hashSemantics(UI::UICommittedSemanticsView semantics) noexcept
{
    DeterministicHash hash{};
    hash.addU64(semantics.size());
    for (const UI::UISemanticsEntry& entry : semantics) {
        hash.addU32(entry.node.index());
        hash.addU32(entry.node.generation());
        hash.addU32(entry.parent.index());
        hash.addU8(static_cast<u8>(entry.role));
        hash.addU8(static_cast<u8>(entry.actions));
        hash.addFloat(entry.worldRect.x);
        hash.addFloat(entry.worldRect.y);
        hash.addFloat(entry.worldRect.width);
        hash.addFloat(entry.worldRect.height);
        hash.addString(entry.name);
        hash.addString(entry.description);
        hash.addString(entry.valueText);
        hash.addFloat(entry.value);
        hash.addFloat(entry.minValue);
        hash.addFloat(entry.maxValue);
        hash.addBool(entry.hasRange);
        hash.addBool(entry.checked);
        hash.addBool(entry.selected);
        hash.addBool(entry.enabled);
        hash.addBool(entry.focused);
        hash.addBool(entry.readOnly);
        hash.addU64(entry.virtualItemKey);
        hash.addU64(entry.virtualItemIndex);
        hash.addU32(entry.level);
        hash.addBool(entry.expandable);
        hash.addBool(entry.expanded);
    }
    return hash.value();
}

void hashLogicalRect(DeterministicHash& hash, const UI::UILogicalRect& rect) noexcept
{
    hash.addFloat(rect.x);
    hash.addFloat(rect.y);
    hash.addFloat(rect.width);
    hash.addFloat(rect.height);
}

[[nodiscard]] u64 hashStableSemantics(UI::UICommittedSemanticsView semantics) noexcept
{
    DeterministicHash hash{};
    hash.addU64(semantics.size());
    for (const UI::UISemanticsEntry& entry : semantics) {
        hash.addU8(static_cast<u8>(entry.role));
        hash.addU8(static_cast<u8>(entry.actions));
        hashLogicalRect(hash, entry.worldRect);
        hash.addString(entry.name);
        hash.addString(entry.description);
        hash.addString(entry.valueText);
        hash.addFloat(entry.value);
        hash.addFloat(entry.minValue);
        hash.addFloat(entry.maxValue);
        hash.addBool(entry.hasRange);
        hash.addBool(entry.checked);
        hash.addBool(entry.selected);
        hash.addBool(entry.enabled);
        hash.addBool(entry.focused);
        hash.addBool(entry.readOnly);
    }
    return hash.value();
}

[[nodiscard]] u64 hashComponentTree(const UIFixture& fixture) noexcept
{
    DeterministicHash hash{};
    const UI::UICommittedStructureView structure = fixture.context->committedStructure();
    hash.addU64(structure.size());
    for (const UI::UICommittedNodeEntry& entry : structure) {
        hash.addU32(entry.depth);
        hash.addU32(entry.preorder);
        hash.addU32(entry.paintOrdinal);
        hash.addBool(entry.parent.hasValue());
    }

    const UI::UICommittedLayoutView layout = fixture.context->committedLayout();
    hash.addU64(layout.size());
    for (const UI::UICommittedLayoutEntry& entry : layout) {
        hashLogicalRect(hash, entry.localRect);
        hashLogicalRect(hash, entry.worldRect);
        hashLogicalRect(hash, entry.effectiveClip);
        hash.addU8(static_cast<u8>(entry.effectiveVisibility));
        hash.addU32(entry.layoutOrdinal);
        hash.addU32(entry.paintOrdinal);
    }

    const UI::UICommittedPaintView paint = fixture.context->committedPaint();
    hash.addU64(paint.size());
    for (const UI::UICommittedPaintEntry& entry : paint) {
        hashLogicalRect(hash, entry.worldRect);
        hashLogicalRect(hash, entry.effectiveClip);
        hash.addU32(entry.paintOrdinal);
        hash.addU8(entry.solidFill.red);
        hash.addU8(entry.solidFill.green);
        hash.addU8(entry.solidFill.blue);
        hash.addU8(entry.solidFill.alpha);
        hash.addFloat(entry.cornerRadii.topLeft);
        hash.addFloat(entry.cornerRadii.topRight);
        hash.addFloat(entry.cornerRadii.bottomRight);
        hash.addFloat(entry.cornerRadii.bottomLeft);
        hash.addU8(static_cast<u8>(entry.kind));
        hash.addU32(entry.atlasX);
        hash.addU32(entry.atlasY);
        hash.addU32(entry.atlasWidth);
        hash.addU32(entry.atlasHeight);
        hash.addU32(entry.atlasPage);
        hash.addFloat(entry.lineStart.x);
        hash.addFloat(entry.lineStart.y);
        hash.addFloat(entry.lineEnd.x);
        hash.addFloat(entry.lineEnd.y);
        hash.addFloat(entry.lineThickness);
        hash.addFloat(entry.ellipseStrokeWidth);
    }

    hash.addU64(hashStableSemantics(fixture.context->committedSemantics()));

    const UI::UIContextStatistics statistics = fixture.context->statistics();
    hash.addU64(statistics.liveNodeCount);
    hash.addU64(statistics.textByteUsed);
    hash.addU64(statistics.activeCanvasCommandCount);
    hash.addU64(statistics.activeActivateBehaviorCount);
    hash.addU64(statistics.activeToggleBehaviorCount);
    return hash.value();
}

[[nodiscard]] bool runStaticCommit(const UIBenchmarkOptions& options, UIBenchmarkReport& report,
                                   std::string& error)
{
    CountingMemoryResource memory{};
    auto fixtureResult = createFixture(largeContextCapacity(), memory, error);
    if (!fixtureResult) {
        return false;
    }
    UIFixture fixture = std::move(*fixtureResult);
    std::vector<UI::UINodeId> leaves;
    if (!populateFlatPaintTree(fixture, leaves, error)) {
        return false;
    }
    auto builderResult = createDisplayListBuilder(static_cast<u32>(kLargeNodeCount), memory, error);
    if (!builderResult) {
        return false;
    }
    Render::UIDisplayListBuilder builder = std::move(*builderResult);
    constexpr UI::UILogicalSize Viewport{.width = 1.0F, .height = static_cast<float>(kLargeNodeCount)};
    constexpr Render::UIPixelRect Framebuffer{.x = 0, .y = 0, .width = 1, .height = kLargeNodeCount};

    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }
    if (!buildDisplayList(fixture, builder, Framebuffer, nullptr, error)) {
        return false;
    }

    Core::SteadyMonotonicClock clock{};
    const auto wallBegin = clock.now();
    const u64 totalIterations = options.warmUpIterations + options.measureIterations;
    if (options.warmUpIterations == 0) {
        captureAllocationBaseline(memory, report);
    }
    for (u64 iteration = 0; iteration < totalIterations; ++iteration) {
        const bool measured = iteration >= options.warmUpIterations;
        const auto totalBegin = clock.now();
        const auto commitBegin = clock.now();
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitEnd = clock.now();
        const auto displayBegin = clock.now();
        if (!buildDisplayList(fixture, builder, Framebuffer, measured ? &report : nullptr, error)) {
            return false;
        }
        const auto displayEnd = clock.now();
        if (measured) {
            report.commitSamples.push_back(elapsedNs(commitBegin, commitEnd));
            report.displayListSamples.push_back(elapsedNs(displayBegin, displayEnd));
            report.totalSamples.push_back(elapsedNs(totalBegin, displayEnd));
            accumulateCommitStatistics(fixture.context->statistics(), report);
        }
        if (iteration + 1 == options.warmUpIterations) {
            captureAllocationBaseline(memory, report);
        }
    }
    report.wallNs = elapsedNs(wallBegin, clock.now());
    captureFinalState(memory, report, fixture);

    report.configuredNodeCount = kLargeNodeCount;
    report.layoutSnapshotHighWater = report.workN;
    report.hitSnapshotHighWater = report.workH;
    report.paintSnapshotHighWater = report.workP;
    if (report.pmrAllocationsAfter != report.pmrAllocationsBefore || report.layoutPasses != 0 ||
        report.measuredNodes != 0 || report.arrangedNodes != 0 || report.hitRebuilds != 0 ||
        report.paintCacheRebuilds != 0 || report.paintSnapshotRebuilds != 0 ||
        report.workN != kLargeNodeCount || report.workH != kLargeNodeCount || report.workP != kFlatLeafCount) {
        error = "ui_static_commit_v1 invariant failed";
        return false;
    }

    DeterministicHash hash{};
    hashStatistics(hash, report);
    report.checksum = hash.value();
    return true;
}

[[nodiscard]] bool runPaintDirty(const UIBenchmarkOptions& options, UIBenchmarkReport& report,
                                 std::string& error)
{
    CountingMemoryResource memory{};
    auto fixtureResult = createFixture(largeContextCapacity(), memory, error);
    if (!fixtureResult) {
        return false;
    }
    UIFixture fixture = std::move(*fixtureResult);
    std::vector<UI::UINodeId> leaves;
    if (!populateFlatPaintTree(fixture, leaves, error)) {
        return false;
    }
    const UI::UINodeId dirtyLeaf = leaves[static_cast<usize>(options.seed % leaves.size())];
    auto builderResult = createDisplayListBuilder(static_cast<u32>(kLargeNodeCount), memory, error);
    if (!builderResult) {
        return false;
    }
    Render::UIDisplayListBuilder builder = std::move(*builderResult);
    constexpr UI::UILogicalSize Viewport{.width = 1.0F, .height = static_cast<float>(kLargeNodeCount)};
    constexpr Render::UIPixelRect Framebuffer{.x = 0, .y = 0, .width = 1, .height = kLargeNodeCount};

    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }
    if (!buildDisplayList(fixture, builder, Framebuffer, nullptr, error)) {
        return false;
    }

    Core::SteadyMonotonicClock clock{};
    const auto wallBegin = clock.now();
    const u64 totalIterations = options.warmUpIterations + options.measureIterations;
    if (options.warmUpIterations == 0) {
        captureAllocationBaseline(memory, report);
    }
    for (u64 iteration = 0; iteration < totalIterations; ++iteration) {
        const bool measured = iteration >= options.warmUpIterations;
        const UI::UIStraightSrgba8Color color = (iteration & 1U) == 0U
                                                   ? UI::rgba8(37, 149, 211)
                                                   : UI::rgba8(229, 91, 53);
        const auto totalBegin = clock.now();
        if (Core::Status status = fixture.updater.setBoxPaint(dirtyLeaf, solidPaint(color)); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitBegin = clock.now();
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitEnd = clock.now();
        const auto displayBegin = clock.now();
        if (!buildDisplayList(fixture, builder, Framebuffer, measured ? &report : nullptr, error)) {
            return false;
        }
        const auto displayEnd = clock.now();
        if (measured) {
            report.commitSamples.push_back(elapsedNs(commitBegin, commitEnd));
            report.displayListSamples.push_back(elapsedNs(displayBegin, displayEnd));
            report.totalSamples.push_back(elapsedNs(totalBegin, displayEnd));
            accumulateCommitStatistics(fixture.context->statistics(), report);
        }
        if (iteration + 1 == options.warmUpIterations) {
            captureAllocationBaseline(memory, report);
        }
    }
    report.wallNs = elapsedNs(wallBegin, clock.now());
    captureFinalState(memory, report, fixture);

    report.configuredNodeCount = kLargeNodeCount;
    report.layoutSnapshotHighWater = report.workN;
    report.hitSnapshotHighWater = report.workH;
    report.paintSnapshotHighWater = report.workP;
    if (report.pmrAllocationsAfter != report.pmrAllocationsBefore || report.layoutPasses != 0 ||
        report.measuredNodes != 0 || report.arrangedNodes != 0 || report.hitRebuilds != 0 ||
        report.paintCacheRebuilds != options.measureIterations ||
        report.paintSnapshotRebuilds != options.measureIterations || report.workN != kLargeNodeCount ||
        report.workH != kLargeNodeCount || report.workP != kFlatLeafCount) {
        error = "ui_paint_dirty_v1 invariant failed";
        return false;
    }

    DeterministicHash hash{};
    hashStatistics(hash, report);
    hash.addU32(dirtyLeaf.index());
    report.checksum = hash.value();
    return true;
}

[[nodiscard]] u64 motionActiveTrackTarget(u64 seed) noexcept
{
    // Docs: exercise 0 / 64 / 1024 active tracks under fixed capacity.
    switch (seed % 3U) {
    case 0:
        return 0;
    case 1:
        return 64;
    default:
        return kMotionTrackCapacity;
    }
}

[[nodiscard]] bool runMotion(const UIBenchmarkOptions& options, UIBenchmarkReport& report,
                             std::string& error)
{
    const u64 activeTarget = motionActiveTrackTarget(options.seed);
    CountingMemoryResource memory{};
    UI::UIContextCapacityConfig capacity = largeContextCapacity();
    capacity.motionTrackCapacity = kMotionTrackCapacity;
    auto fixtureResult = createFixture(capacity, memory, error);
    if (!fixtureResult) {
        return false;
    }
    UIFixture fixture = std::move(*fixtureResult);
    std::vector<UI::UINodeId> leaves;
    if (!populateFlatPaintTree(fixture, leaves, error)) {
        return false;
    }
    if (activeTarget > leaves.size()) {
        error = "ui_motion_v1 leaf count is below active track target";
        return false;
    }

    BenchMotionClock motionClock{};
    if (Core::Status status = fixture.context->setMotionClock(&motionClock); !status) {
        error = status.error().message;
        return false;
    }

    auto builderResult = createDisplayListBuilder(static_cast<u32>(kLargeNodeCount), memory, error);
    if (!builderResult) {
        return false;
    }
    Render::UIDisplayListBuilder builder = std::move(*builderResult);
    constexpr UI::UILogicalSize Viewport{.width = 1.0F, .height = static_cast<float>(kLargeNodeCount)};
    constexpr Render::UIPixelRect Framebuffer{.x = 0, .y = 0, .width = 1, .height = kLargeNodeCount};

    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }
    if (!buildDisplayList(fixture, builder, Framebuffer, nullptr, error)) {
        return false;
    }

    report.configuredNodeCount = kLargeNodeCount;
    report.configuredMotionTrackCapacity = kMotionTrackCapacity;
    report.configuredActiveMotionTracks = activeTarget;

    Core::SteadyMonotonicClock wallClock{};
    const auto wallBegin = wallClock.now();
    const u64 totalIterations = options.warmUpIterations + options.measureIterations;
    if (options.warmUpIterations == 0) {
        captureAllocationBaseline(memory, report);
    }

    UI::UITransitionSpec spec{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{0.100},
        .delay = Core::Duration{0.0},
        .easing = UI::UIEasing::Linear,
    };

    for (u64 iteration = 0; iteration < totalIterations; ++iteration) {
        const bool measured = iteration >= options.warmUpIterations;
        const auto totalBegin = wallClock.now();

        // Retarget or start M tracks each iteration (M==0 skips begin).
        for (u64 index = 0; index < activeTarget; ++index) {
            const UI::UINodeId leaf = leaves[static_cast<usize>(index)];
            const UI::UIStraightSrgba8Color target =
                ((iteration + index) & 1U) == 0U ? UI::rgba8(40, 120, 200)
                                                 : UI::rgba8(200, 80, 40);
            if (Core::Status status =
                    fixture.context->beginBackgroundColorTransition(leaf, target, spec);
                !status) {
                error = status.error().message;
                return false;
            }
        }

        motionClock.advance(Core::Duration{0.050});
        const auto commitBegin = wallClock.now();
        // commitLayout samples motion via context clock.
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitEnd = wallClock.now();
        const UI::UIContextStatistics statistics = fixture.context->statistics();
        const auto displayBegin = wallClock.now();
        if (!buildDisplayList(fixture, builder, Framebuffer, measured ? &report : nullptr, error)) {
            return false;
        }
        const auto displayEnd = wallClock.now();

        if (measured) {
            report.commitSamples.push_back(elapsedNs(commitBegin, commitEnd));
            report.displayListSamples.push_back(elapsedNs(displayBegin, displayEnd));
            report.totalSamples.push_back(elapsedNs(totalBegin, displayEnd));
            accumulateCommitStatistics(statistics, report);
            report.motionSampledTracks += statistics.motion.lastSampledTrackCount;
            report.motionActiveTracks += statistics.motion.activeTrackCount;
            report.motionTrackHighWater =
                (std::max)(report.motionTrackHighWater, statistics.motion.trackHighWater);
            if (activeTarget == 0) {
                ++report.motionZeroActiveIterations;
                if (statistics.motion.lastSampledTrackCount != 0 ||
                    statistics.motion.activeTrackCount != 0 || statistics.layoutDirty ||
                    statistics.hitDirty) {
                    error = "ui_motion_v1 M==0 produced motion/layout/hit work";
                    return false;
                }
            } else if (statistics.layoutDirty || statistics.hitDirty ||
                       statistics.lastLayoutPassCount != 0 || statistics.lastHitRebuildCount != 0) {
                error = "ui_motion_v1 paint-only sample dirtied layout/hit";
                return false;
            }
        }
        if (iteration + 1 == options.warmUpIterations) {
            captureAllocationBaseline(memory, report);
        }
    }

    report.wallNs = elapsedNs(wallBegin, wallClock.now());
    captureFinalState(memory, report, fixture);
    report.layoutSnapshotHighWater = report.workN;
    report.hitSnapshotHighWater = report.workH;
    report.paintSnapshotHighWater = report.workP;

    if (report.pmrAllocationsAfter != report.pmrAllocationsBefore || report.layoutPasses != 0 ||
        report.measuredNodes != 0 || report.arrangedNodes != 0 || report.hitRebuilds != 0) {
        error = "ui_motion_v1 layout/hit/allocation invariant failed";
        return false;
    }
    if (activeTarget == 0) {
        if (report.motionZeroActiveIterations != options.measureIterations ||
            report.motionSampledTracks != 0) {
            error = "ui_motion_v1 zero-active counter invariant failed";
            return false;
        }
    } else if (report.motionSampledTracks == 0 ||
               report.motionTrackHighWater < activeTarget) {
        error = "ui_motion_v1 active-track counter invariant failed";
        return false;
    }

    DeterministicHash hash{};
    hashStatistics(hash, report);
    report.checksum = hash.value();
    return true;
}

[[nodiscard]] bool runTimelineMotion(
    const UIBenchmarkOptions& options, UIBenchmarkReport& report, std::string& error)
{
    const bool layoutTimeline = report.workload == kLayoutTimelineMotionWorkload;
    const u64 activeTrackTarget = motionActiveTrackTarget(options.seed);
    const usize activeTimelineTarget =
        static_cast<usize>(activeTrackTarget / kTimelineTracksPerDefinition);
    CountingMemoryResource memory{};
    UI::UIContextCapacityConfig capacity = largeContextCapacity();
    capacity.timelineCapacity = kTimelineCapacity;
    capacity.timelineTrackCapacity = kTimelineTrackCapacity;
    capacity.timelineKeyframeCapacity = kTimelineKeyframeCapacity;
    capacity.activeTimelineCapacity = kTimelineCapacity;
    auto fixtureResult = createFixture(capacity, memory, error);
    if (!fixtureResult) {
        return false;
    }
    UIFixture fixture = std::move(*fixtureResult);
    std::vector<UI::UINodeId> leaves;
    const bool populated = layoutTimeline
                               ? populateLayoutMotionTree(fixture, leaves, error)
                               : populateFlatPaintTree(fixture, leaves, error);
    if (!populated) {
        return false;
    }
    const usize requiredLeafCount = kTimelineCapacity * (layoutTimeline ? 2U : 1U);
    if (requiredLeafCount > leaves.size()) {
        error = std::string(report.workload) +
                " leaf count is below active timeline target";
        return false;
    }

    BenchMotionClock motionClock{};
    if (Core::Status status = fixture.context->setMotionClock(&motionClock); !status) {
        error = status.error().message;
        return false;
    }

    std::vector<UI::UITimelineId> timelines;
    timelines.reserve(kTimelineCapacity);
    for (usize timelineIndex = 0; timelineIndex < kTimelineCapacity; ++timelineIndex) {
        const auto retainTimeline = [&](const auto& tracks) {
            auto timeline = fixture.context->createTimeline(UI::UITimelineDesc{
                .duration = Core::Duration{0.100},
                .delay = Core::Duration{0.0},
                .tracks = tracks,
            });
            if (!timeline) {
                error = timeline.error().message;
                return false;
            }
            timelines.push_back(*timeline);
            return true;
        };
        if (layoutTimeline) {
            const usize primaryLeafIndex = timelineIndex * 2U;
            const UI::UINodeId primaryNode = leaves[primaryLeafIndex];
            const UI::UINodeId secondaryNode = leaves[primaryLeafIndex + 1U];
            const float primaryY = static_cast<float>(primaryLeafIndex);
            const float secondaryY = primaryY + 1.0F;
            const std::array widthFrames{
                UI::UIKeyframe{.normalizedTime = 0.0F,
                               .value = UI::UIKeyframeValue::Scalar(1.0F)},
                UI::UIKeyframe{.normalizedTime = 0.33F,
                               .value = UI::UIKeyframeValue::Scalar(1.25F)},
                UI::UIKeyframe{.normalizedTime = 0.66F,
                               .value = UI::UIKeyframeValue::Scalar(1.5F)},
                UI::UIKeyframe{.normalizedTime = 1.0F,
                               .value = UI::UIKeyframeValue::Scalar(1.75F)},
            };
            const std::array heightFrames{
                UI::UIKeyframe{.normalizedTime = 0.0F,
                               .value = UI::UIKeyframeValue::Scalar(1.0F)},
                UI::UIKeyframe{.normalizedTime = 0.33F,
                               .value = UI::UIKeyframeValue::Scalar(0.9F)},
                UI::UIKeyframe{.normalizedTime = 0.66F,
                               .value = UI::UIKeyframeValue::Scalar(1.1F)},
                UI::UIKeyframe{.normalizedTime = 1.0F,
                               .value = UI::UIKeyframeValue::Scalar(1.25F)},
            };
            const std::array primaryOffsetFrames{
                UI::UIKeyframe{.normalizedTime = 0.0F,
                               .value = UI::UIKeyframeValue::Offset(0.0F, primaryY)},
                UI::UIKeyframe{.normalizedTime = 0.33F,
                               .value = UI::UIKeyframeValue::Offset(0.1F, primaryY + 0.1F)},
                UI::UIKeyframe{.normalizedTime = 0.66F,
                               .value = UI::UIKeyframeValue::Offset(0.2F, primaryY + 0.2F)},
                UI::UIKeyframe{.normalizedTime = 1.0F,
                               .value = UI::UIKeyframeValue::Offset(0.0F, primaryY + 0.3F)},
            };
            const std::array secondaryOffsetFrames{
                UI::UIKeyframe{.normalizedTime = 0.0F,
                               .value = UI::UIKeyframeValue::Offset(0.0F, secondaryY)},
                UI::UIKeyframe{.normalizedTime = 0.33F,
                               .value = UI::UIKeyframeValue::Offset(0.2F, secondaryY + 0.1F)},
                UI::UIKeyframe{.normalizedTime = 0.66F,
                               .value = UI::UIKeyframeValue::Offset(0.1F, secondaryY + 0.2F)},
                UI::UIKeyframe{.normalizedTime = 1.0F,
                               .value = UI::UIKeyframeValue::Offset(0.0F, secondaryY + 0.3F)},
            };
            const std::array tracks{
                UI::UITimelineTrackDesc{.node = primaryNode,
                                        .property = UI::UIAnimatableProperty::LayoutWidth,
                                        .keyframes = widthFrames},
                UI::UITimelineTrackDesc{.node = primaryNode,
                                        .property = UI::UIAnimatableProperty::LayoutHeight,
                                        .keyframes = heightFrames},
                UI::UITimelineTrackDesc{.node = primaryNode,
                                        .property = UI::UIAnimatableProperty::LayoutOffset,
                                        .keyframes = primaryOffsetFrames},
                UI::UITimelineTrackDesc{.node = secondaryNode,
                                        .property = UI::UIAnimatableProperty::LayoutOffset,
                                        .keyframes = secondaryOffsetFrames},
            };
            if (!retainTimeline(tracks)) {
                return false;
            }
        } else {
            const UI::UINodeId node = leaves[timelineIndex];
            const std::array colorFrames{
                UI::UIKeyframe{.normalizedTime = 0.0F,
                               .value = UI::UIKeyframeValue::Color(UI::rgba8(20, 40, 80))},
                UI::UIKeyframe{.normalizedTime = 0.33F,
                               .value = UI::UIKeyframeValue::Color(UI::rgba8(60, 100, 160)),
                               .easingToNext = UI::UIEasing::EaseOut},
                UI::UIKeyframe{.normalizedTime = 0.66F,
                               .value = UI::UIKeyframeValue::Color(UI::rgba8(160, 90, 50)),
                               .easingToNext = UI::UIEasing::EaseInOut},
                UI::UIKeyframe{.normalizedTime = 1.0F,
                               .value = UI::UIKeyframeValue::Color(UI::rgba8(220, 130, 70))},
            };
            const std::array opacityFrames{
                UI::UIKeyframe{.normalizedTime = 0.0F,
                               .value = UI::UIKeyframeValue::Scalar(1.0F)},
                UI::UIKeyframe{.normalizedTime = 0.33F,
                               .value = UI::UIKeyframeValue::Scalar(0.85F)},
                UI::UIKeyframe{.normalizedTime = 0.66F,
                               .value = UI::UIKeyframeValue::Scalar(0.7F)},
                UI::UIKeyframe{.normalizedTime = 1.0F,
                               .value = UI::UIKeyframeValue::Scalar(0.55F)},
            };
            const std::array radiusFrames{
                UI::UIKeyframe{.normalizedTime = 0.0F,
                               .value = UI::UIKeyframeValue::Scalar(0.0F)},
                UI::UIKeyframe{.normalizedTime = 0.33F,
                               .value = UI::UIKeyframeValue::Scalar(2.0F)},
                UI::UIKeyframe{.normalizedTime = 0.66F,
                               .value = UI::UIKeyframeValue::Scalar(4.0F)},
                UI::UIKeyframe{.normalizedTime = 1.0F,
                               .value = UI::UIKeyframeValue::Scalar(8.0F)},
            };
            const std::array offsetFrames{
                UI::UIKeyframe{.normalizedTime = 0.0F,
                               .value = UI::UIKeyframeValue::Offset(0.0F, 0.0F)},
                UI::UIKeyframe{.normalizedTime = 0.33F,
                               .value = UI::UIKeyframeValue::Offset(1.0F, 0.0F)},
                UI::UIKeyframe{.normalizedTime = 0.66F,
                               .value = UI::UIKeyframeValue::Offset(1.0F, 1.0F)},
                UI::UIKeyframe{.normalizedTime = 1.0F,
                               .value = UI::UIKeyframeValue::Offset(2.0F, 1.0F)},
            };
            const std::array tracks{
                UI::UITimelineTrackDesc{.node = node,
                                        .property = UI::UIAnimatableProperty::BackgroundColor,
                                        .keyframes = colorFrames},
                UI::UITimelineTrackDesc{.node = node,
                                        .property = UI::UIAnimatableProperty::Opacity,
                                        .keyframes = opacityFrames},
                UI::UITimelineTrackDesc{.node = node,
                                        .property = UI::UIAnimatableProperty::CornerRadius,
                                        .keyframes = radiusFrames},
                UI::UITimelineTrackDesc{.node = node,
                                        .property = UI::UIAnimatableProperty::VisualOffset,
                                        .keyframes = offsetFrames},
            };
            if (!retainTimeline(tracks)) {
                return false;
            }
        }
    }

    auto builderResult = createDisplayListBuilder(static_cast<u32>(kLargeNodeCount), memory, error);
    if (!builderResult) {
        return false;
    }
    Render::UIDisplayListBuilder builder = std::move(*builderResult);
    constexpr UI::UILogicalSize Viewport{.width = 1.0F, .height = static_cast<float>(kLargeNodeCount)};
    constexpr Render::UIPixelRect Framebuffer{.x = 0, .y = 0, .width = 1, .height = kLargeNodeCount};
    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }
    if (!buildDisplayList(fixture, builder, Framebuffer, nullptr, error)) {
        return false;
    }

    report.configuredNodeCount = kLargeNodeCount;
    report.configuredTimelineCapacity = kTimelineCapacity;
    report.configuredTimelineTrackCapacity = kTimelineTrackCapacity;
    report.configuredTimelineKeyframeCapacity = kTimelineKeyframeCapacity;
    report.configuredActiveTimelineCapacity = kTimelineCapacity;
    report.configuredActiveTimelineTracks = activeTrackTarget;

    Core::SteadyMonotonicClock wallClock{};
    const auto wallBegin = wallClock.now();
    const u64 totalIterations = options.warmUpIterations + options.measureIterations;
    if (options.warmUpIterations == 0) {
        captureAllocationBaseline(memory, report);
    }
    for (u64 iteration = 0; iteration < totalIterations; ++iteration) {
        const bool measured = iteration >= options.warmUpIterations;
        const auto totalBegin = wallClock.now();
        for (usize timelineIndex = 0; timelineIndex < activeTimelineTarget;
             ++timelineIndex) {
            if (Core::Status status =
                    fixture.context->playTimeline(timelines[timelineIndex]);
                !status) {
                error = status.error().message;
                return false;
            }
        }
        motionClock.advance(Core::Duration{0.025});
        const auto commitBegin = wallClock.now();
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitEnd = wallClock.now();
        const UI::UIContextStatistics statistics = fixture.context->statistics();
        const auto displayBegin = wallClock.now();
        if (!buildDisplayList(fixture, builder, Framebuffer, measured ? &report : nullptr, error)) {
            return false;
        }
        const auto displayEnd = wallClock.now();

        if (measured) {
            report.commitSamples.push_back(elapsedNs(commitBegin, commitEnd));
            report.displayListSamples.push_back(elapsedNs(displayBegin, displayEnd));
            report.totalSamples.push_back(elapsedNs(totalBegin, displayEnd));
            accumulateCommitStatistics(statistics, report);
            report.timelineSampledTimelines += statistics.motion.lastSampledTimelineCount;
            report.timelineSampledTracks += statistics.motion.lastSampledTimelineTrackCount;
            if (layoutTimeline) {
                report.timelineSampledLayoutTracks +=
                    statistics.motion.lastSampledTimelineLayoutTrackCount;
            }
            report.timelineSampledSegments += statistics.motion.lastSampledKeyframeSegmentCount;
            report.timelineActiveCount += statistics.motion.activeTimelineCount;
            if (activeTrackTarget == 0) {
                ++report.timelineZeroActiveIterations;
                if (statistics.motion.lastSampledTimelineCount != 0 ||
                    statistics.motion.lastSampledTimelineTrackCount != 0 ||
                    statistics.motion.lastSampledTimelineLayoutTrackCount != 0 ||
                    statistics.motion.activeTimelineCount != 0 ||
                    statistics.lastLayoutPassCount != 0 ||
                    statistics.lastHitRebuildCount != 0 ||
                    statistics.lastPaintCacheRebuildCount != 0 ||
                    statistics.lastPaintSnapshotRebuildCount != 0) {
                    error = std::string(report.workload) +
                            " active=0 rebuilt a clean UI snapshot";
                    return false;
                }
            } else if (layoutTimeline) {
                if (statistics.motion.lastSampledTimelineTrackCount != activeTrackTarget ||
                    statistics.motion.lastSampledTimelineLayoutTrackCount != activeTrackTarget ||
                    statistics.lastLayoutPassCount != 1 ||
                    statistics.lastHitRebuildCount != 1 ||
                    statistics.lastPaintSnapshotRebuildCount != 1 ||
                    statistics.motion.layoutTimelineCommitFailureCount != 0 ||
                    statistics.layoutDirty || statistics.hitDirty || statistics.paintDirty) {
                    error = "ui_motion_layout_v1 did not publish one atomic Layout/Hit/Paint rebuild";
                    return false;
                }
            } else if (statistics.lastLayoutPassCount != 0 ||
                       statistics.lastHitRebuildCount != 0 || statistics.layoutDirty ||
                       statistics.hitDirty) {
                error = "ui_motion_timeline_v1 paint-only sample dirtied layout/hit";
                return false;
            }
        }
        if (iteration + 1 == options.warmUpIterations) {
            captureAllocationBaseline(memory, report);
        }
    }

    report.wallNs = elapsedNs(wallBegin, wallClock.now());
    captureFinalState(memory, report, fixture);
    report.layoutSnapshotHighWater = report.workN;
    report.hitSnapshotHighWater = report.workH;
    report.paintSnapshotHighWater = report.workP;
    report.workM = report.timelineSampledTracks;
    report.timelineLayoutCommitFailures =
        report.statistics.motion.layoutTimelineCommitFailureCount;
    if (!layoutTimeline &&
        (report.pmrAllocationsAfter != report.pmrAllocationsBefore ||
         report.layoutPasses != 0 || report.measuredNodes != 0 ||
         report.arrangedNodes != 0 || report.hitRebuilds != 0)) {
        error = "ui_motion_timeline_v1 layout/hit/allocation invariant failed";
        return false;
    }
    if (layoutTimeline &&
        (report.pmrAllocationsAfter != report.pmrAllocationsBefore ||
         report.timelineLayoutCommitFailures != 0)) {
        error = "ui_motion_layout_v1 allocation/failure invariant failed";
        return false;
    }
    if (report.statistics.motion.timelineHighWater != kTimelineCapacity ||
        report.statistics.motion.timelineTrackHighWater != kTimelineTrackCapacity ||
        report.statistics.motion.keyframeHighWater != kTimelineKeyframeCapacity ||
        report.statistics.motion.activeTimelineHighWater != activeTimelineTarget) {
        error = std::string(report.workload) +
                " definition/active high-water invariant failed";
        return false;
    }
    if (activeTrackTarget == 0) {
        if (report.timelineZeroActiveIterations != options.measureIterations ||
            report.timelineSampledTimelines != 0 || report.timelineSampledTracks != 0 ||
            report.timelineSampledLayoutTracks != 0 || report.timelineSampledSegments != 0 ||
            report.timelineActiveCount != 0 || report.paintCacheRebuilds != 0 ||
            report.paintSnapshotRebuilds != 0 || report.layoutPasses != 0 ||
            report.hitRebuilds != 0) {
            error = std::string(report.workload) +
                    " zero-active counter invariant failed";
            return false;
        }
    } else {
        const u64 expectedSampledTimelines =
            static_cast<u64>(activeTimelineTarget) * options.measureIterations;
        const u64 expectedSampledTracks = activeTrackTarget * options.measureIterations;
        if (report.timelineSampledTimelines != expectedSampledTimelines ||
            report.timelineSampledTracks != expectedSampledTracks ||
            report.timelineSampledSegments != expectedSampledTracks ||
            report.timelineActiveCount != expectedSampledTimelines) {
            error = std::string(report.workload) +
                    " active timeline counter invariant failed";
            return false;
        }
        if (layoutTimeline &&
            (report.timelineSampledLayoutTracks != expectedSampledTracks ||
             report.layoutPasses != options.measureIterations ||
             report.hitRebuilds != options.measureIterations ||
             report.paintSnapshotRebuilds != options.measureIterations)) {
            error = "ui_motion_layout_v1 atomic rebuild counter invariant failed";
            return false;
        }
        if (!layoutTimeline &&
            (report.timelineSampledLayoutTracks != 0 ||
             report.paintCacheRebuilds != expectedSampledTimelines ||
             report.paintSnapshotRebuilds != options.measureIterations)) {
            error = "ui_motion_timeline_v1 paint publication counter invariant failed";
            return false;
        }
    }

    DeterministicHash hash{};
    hashStatistics(hash, report);
    report.checksum = hash.value();
    return true;
}

[[nodiscard]] bool runStyleState(const UIBenchmarkOptions& options,
                                 UIBenchmarkReport& report, std::string& error)
{
    constexpr u64 kCandidateRulesPerIteration =
        static_cast<u64>(kStyleClassesPerNode) * kStyleRulesPerClass;
    if (options.measureIterations >
        (std::numeric_limits<u64>::max)() / kCandidateRulesPerIteration) {
        error = "UI style benchmark measured counter range is invalid";
        return false;
    }

    CountingMemoryResource memory{};
    StyleFixtureStartup startup{};
    auto fixtureResult = createFixture(styleBenchmarkContextCapacity(), memory, error,
                                       &initializeStyleFixture, &startup);
    if (!fixtureResult) {
        return false;
    }
    UIFixture fixture = std::move(*fixtureResult);
    std::vector<UI::UINodeId> leaves;
    if (!populateStyleTree(fixture, startup.classes, leaves, error)) {
        return false;
    }
    const UI::UINodeId stateLeaf = leaves[static_cast<usize>(options.seed % leaves.size())];
    auto builderResult = createDisplayListBuilder(static_cast<u32>(kLargeNodeCount), memory, error);
    if (!builderResult) {
        return false;
    }
    Render::UIDisplayListBuilder builder = std::move(*builderResult);
    constexpr UI::UILogicalSize Viewport{
        .width = 1.0F,
        .height = static_cast<float>(kLargeNodeCount),
    };
    constexpr Render::UIPixelRect Framebuffer{
        .x = 0,
        .y = 0,
        .width = 1,
        .height = kLargeNodeCount,
    };

    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }
    u64 enabledDisplayListChecksum = 0;
    if (!buildDisplayList(fixture, builder, Framebuffer, nullptr, error,
                          &enabledDisplayListChecksum)) {
        return false;
    }
    std::optional<u64> disabledDisplayListChecksum;

    Core::SteadyMonotonicClock clock{};
    DeterministicHash styleStateHash{};
    const auto wallBegin = clock.now();
    const u64 totalIterations = options.warmUpIterations + options.measureIterations;
    if (options.warmUpIterations == 0) {
        captureAllocationBaseline(memory, report);
    }
    for (u64 iteration = 0; iteration < totalIterations; ++iteration) {
        const bool measured = iteration >= options.warmUpIterations;
        const bool enabled = (iteration & 1U) != 0U;
        const auto totalBegin = clock.now();
        if (Core::Status status = fixture.updater.setEnabled(stateLeaf, enabled); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitBegin = clock.now();
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitEnd = clock.now();
        const UI::UIContextStatistics mutationStatistics = fixture.context->statistics();
        const auto displayBegin = clock.now();
        u64 currentDisplayListChecksum = 0;
        if (!buildDisplayList(fixture, builder, Framebuffer, measured ? &report : nullptr,
                              error, &currentDisplayListChecksum)) {
            return false;
        }
        const auto displayEnd = clock.now();
        if (enabled) {
            if (currentDisplayListChecksum != enabledDisplayListChecksum) {
                error = "ui_style_state_v1 enabled DisplayList checksum changed";
                return false;
            }
        } else if (!disabledDisplayListChecksum.has_value()) {
            disabledDisplayListChecksum = currentDisplayListChecksum;
        } else if (currentDisplayListChecksum != *disabledDisplayListChecksum) {
            error = "ui_style_state_v1 disabled DisplayList checksum changed";
            return false;
        }
        if (!enabled && currentDisplayListChecksum == enabledDisplayListChecksum) {
            error = "ui_style_state_v1 state change did not change the DisplayList";
            return false;
        }
        const auto cleanCommitBegin = clock.now();
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto cleanCommitEnd = clock.now();
        const UI::UIContextStatistics cleanStatistics = fixture.context->statistics();

        if (mutationStatistics.lastStyleInspectedNodeCount != 1U ||
            mutationStatistics.lastStyleResolvedNodeCount != 1U ||
            mutationStatistics.lastStyleCandidateRuleCount !=
                kStyleClassesPerNode * kStyleRulesPerClass ||
            mutationStatistics.lastLayoutPassCount != 0U ||
            mutationStatistics.lastHitRebuildCount != 0U ||
            mutationStatistics.lastPaintCacheRebuildCount != 1U ||
            mutationStatistics.lastPaintSnapshotRebuildCount != 1U) {
            error = "ui_style_state_v1 single-node state invariant failed";
            return false;
        }
        if (cleanStatistics.lastStyleInspectedNodeCount != 0U ||
            cleanStatistics.lastStyleResolvedNodeCount != 0U ||
            cleanStatistics.lastStyleCandidateRuleCount != 0U ||
            cleanStatistics.lastLayoutPassCount != 0U ||
            cleanStatistics.lastHitRebuildCount != 0U ||
            cleanStatistics.lastPaintCacheRebuildCount != 0U ||
            cleanStatistics.lastPaintSnapshotRebuildCount != 0U) {
            error = "ui_style_state_v1 clean commit invariant failed";
            return false;
        }

        if (measured) {
            report.commitSamples.push_back(elapsedNs(commitBegin, commitEnd));
            report.displayListSamples.push_back(elapsedNs(displayBegin, displayEnd));
            report.cleanCommitSamples.push_back(elapsedNs(cleanCommitBegin, cleanCommitEnd));
            report.totalSamples.push_back(elapsedNs(totalBegin, cleanCommitEnd));
            accumulateCommitStatistics(mutationStatistics, report);
            ++report.styleStateChanges;
            report.styleInspectedNodes += mutationStatistics.lastStyleInspectedNodeCount;
            report.styleResolvedNodes += mutationStatistics.lastStyleResolvedNodeCount;
            report.styleCandidateRules += mutationStatistics.lastStyleCandidateRuleCount;
            ++report.styleCleanCommitCount;
            report.styleCleanInspectedNodes += cleanStatistics.lastStyleInspectedNodeCount;
            report.styleCleanResolvedNodes += cleanStatistics.lastStyleResolvedNodeCount;
            report.styleCleanCandidateRules += cleanStatistics.lastStyleCandidateRuleCount;
            styleStateHash.addBool(enabled);
            styleStateHash.addU64(currentDisplayListChecksum);
            styleStateHash.addU64(mutationStatistics.lastStyleCandidateRuleCount);
        }
        if (iteration + 1U == options.warmUpIterations) {
            captureAllocationBaseline(memory, report);
        }
    }
    report.wallNs = elapsedNs(wallBegin, clock.now());
    if (!disabledDisplayListChecksum.has_value()) {
        error = "ui_style_state_v1 did not observe the disabled state";
        return false;
    }
    report.styleEnabledDisplayListChecksum = enabledDisplayListChecksum;
    report.styleDisabledDisplayListChecksum = *disabledDisplayListChecksum;
    report.styleStateChecksum = styleStateHash.value();
    captureFinalState(memory, report, fixture);

    report.configuredNodeCount = kLargeNodeCount;
    report.configuredStyledNodeCount = kStyledNodeCount;
    report.configuredStyleClassCount = kStyleClassCount;
    report.configuredStyleRuleCount = kStyleRuleCount;
    report.configuredStyleClassesPerNode = kStyleClassesPerNode;
    report.configuredStyleRulesPerBucket = kStyleRulesPerClass;

    const u64 measuredCandidates = options.measureIterations * kCandidateRulesPerIteration;
    const UI::UIStyleStatistics& style = report.statistics.style;
    if (report.pmrAllocationsAfter != report.pmrAllocationsBefore ||
        report.workN != kLargeNodeCount || report.workH != kLargeNodeCount ||
        report.workP != kStyledNodeCount || report.layoutPasses != 0U ||
        report.measuredNodes != 0U || report.arrangedNodes != 0U ||
        report.hitRebuilds != 0U ||
        report.paintCacheRebuilds != options.measureIterations ||
        report.paintSnapshotRebuilds != options.measureIterations ||
        report.styleStateChanges != options.measureIterations ||
        report.styleInspectedNodes != options.measureIterations ||
        report.styleResolvedNodes != options.measureIterations ||
        report.styleCandidateRules != measuredCandidates ||
        report.styleCleanCommitCount != options.measureIterations ||
        report.styleCleanInspectedNodes != 0U || report.styleCleanResolvedNodes != 0U ||
        report.styleCleanCandidateRules != 0U ||
        style.registeredClassCount != kStyleClassCount ||
        style.registeredTokenCount != 0U ||
        style.activeRuleCount != kStyleRuleCount || style.activeBucketCount != kStyleClassCount ||
        style.bucketCandidateHighWater != kStyleRulesPerClass ||
        style.activeNodeClassLinkCount != kStyleNodeClassLinkCount ||
        style.nodeClassLinkHighWater != kStyleNodeClassLinkCount ||
        style.compileFailureCount != 0U || style.capacityFailureCount != 0U ||
        style.revision != 1U || report.displayListChecksum == 0U ||
        report.styleEnabledDisplayListChecksum == 0U ||
        report.styleDisabledDisplayListChecksum == 0U ||
        report.styleEnabledDisplayListChecksum == report.styleDisabledDisplayListChecksum ||
        report.styleStateChecksum == 0U) {
        error = "ui_style_state_v1 invariant failed";
        return false;
    }

    DeterministicHash hash{};
    hashStatistics(hash, report);
    hash.addU32(stateLeaf.index());
    report.checksum = hash.value();
    return true;
}

[[nodiscard]] bool populateRouteTree(UIFixture& fixture, std::vector<UI::UINodeId>& routeNodes,
                                     UI::UINodeId& target, std::string& error)
{
    routeNodes.reserve(kRouteDepth);
    routeNodes.push_back(fixture.root.rootNodeId());
    UI::UINodeId parent = fixture.root.rootNodeId();
    for (usize depth = 1; depth + 1 < kRouteDepth; ++depth) {
        auto node = fixture.updater.createElement(parent, benchmarkPanel(false, false));
        if (!node) {
            error = node.error().message;
            return false;
        }
        parent = *node;
        routeNodes.push_back(parent);
    }
    auto targetResult = fixture.updater.createElement(parent, benchmarkPanel(true, false));
    if (!targetResult) {
        error = targetResult.error().message;
        return false;
    }
    target = *targetResult;
    routeNodes.push_back(target);

    const usize remainingNodes = kLargeNodeCount - routeNodes.size();
    for (usize nodeIndex = 0; nodeIndex < remainingNodes; ++nodeIndex) {
        auto node = fixture.updater.createElement(fixture.root.rootNodeId(), benchmarkPanel(true, false));
        if (!node) {
            error = node.error().message;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool registerRouteListeners(UIFixture& fixture, std::span<const UI::UINodeId> routeNodes,
                                          UI::UINodeId target,
                                          std::vector<UI::UIRoutedPointerListenerToken>& tokens,
                                          std::string& error)
{
    tokens.reserve(routeNodes.size() + 2U);
    for (const UI::UINodeId node : routeNodes) {
        auto token = fixture.context->addRoutedPointerListener(
            UI::UIRoutedPointerListenerDesc{
                .node = node,
                .kind = UI::UIRoutedPointerEventKind::Move,
                .phases = UI::UIEventPhaseMask::All,
            },
            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}});
        if (!token) {
            error = token.error().message;
            return false;
        }
        tokens.push_back(std::move(*token));
    }

    auto down = fixture.context->addRoutedPointerListener(
        UI::UIRoutedPointerListenerDesc{
            .node = target,
            .kind = UI::UIRoutedPointerEventKind::ButtonDown,
            .phases = UI::UIEventPhaseMask::Target,
        },
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            event.consumeInputTransition();
            (void)event.claimPointerButton(Platform::PointerButton::Primary);
            event.capturePointer();
        }});
    if (!down) {
        error = down.error().message;
        return false;
    }
    tokens.push_back(std::move(*down));

    auto up = fixture.context->addRoutedPointerListener(
        UI::UIRoutedPointerListenerDesc{
            .node = target,
            .kind = UI::UIRoutedPointerEventKind::ButtonUp,
            .phases = UI::UIEventPhaseMask::Target,
        },
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            event.consumeInputTransition();
            event.releasePointerCapture();
        }});
    if (!up) {
        error = up.error().message;
        return false;
    }
    tokens.push_back(std::move(*up));
    return true;
}

void accumulateRouteResult(const UI::UIPointerRouteResult& route, UIBenchmarkReport& report,
                           DeterministicHash& hash) noexcept
{
    ++report.routeDispatches;
    report.hitEntriesVisited += route.pointQuery.visitedEntryCount;
    report.routePathNodes += route.routeDepth;
    report.maxRouteDepth = (std::max)(report.maxRouteDepth, static_cast<u64>(route.routeDepth));
    report.listenerCalls += route.listenerInvocationCount;
    report.consumedTransitions += route.consumed ? 1U : 0U;
    const usize primaryIndex = static_cast<usize>(Platform::PointerButton::Primary);
    report.claimedTransitions += route.claimedPointerButtons.test(primaryIndex) ? 1U : 0U;
    report.pointerCaptureRoutes += route.routedThroughPointerCapture ? 1U : 0U;

    hash.addU64(route.pointQuery.visitedEntryCount);
    hash.addU64(route.routeDepth);
    hash.addU64(route.listenerInvocationCount);
    hash.addBool(route.consumed);
    hash.addBool(route.claimedPointerButtons.test(primaryIndex));
    hash.addBool(route.routedThroughPointerCapture);
    hash.addU32(route.pointQuery.hasTarget() ? route.pointQuery.target.node.index()
                                            : UI::UINodeId{}.index());
    hash.addU32(route.hasRoutedTarget() ? route.routedTarget.node.index()
                                       : UI::UINodeId{}.index());
}

[[nodiscard]] bool routeOne(UIFixture& fixture, Platform::PlatformFrameId frame, u64 sequence,
                            usize ordinal, UI::UIRoutedPointerEventKind kind, UI::UILogicalPoint position,
                            UIBenchmarkReport* report, DeterministicHash* hash, std::string& error)
{
    auto route = fixture.context->routePointerInput(UI::UIPointerInputEvent{
        .platformFrame = frame,
        .transitionOrdinal = ordinal,
        .sourceSequence = sequence,
        .window = fixture.window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = position,
        .delta = {},
        .button = Platform::PointerButton::Primary,
    });
    if (!route) {
        error = route.error().message;
        return false;
    }
    if (report != nullptr && hash != nullptr) {
        accumulateRouteResult(*route, *report, *hash);
    }
    return true;
}

[[nodiscard]] bool runRouteSequence(UIFixture& fixture, u64 iteration, UIBenchmarkReport* report,
                                    DeterministicHash* hash, std::string& error)
{
    const Platform::PlatformFrameId frame{.value = iteration + 1U};
    const u64 sequenceBase = iteration * 5U + 1U;
    constexpr UI::UILogicalPoint TargetPoint{.x = 0.5F, .y = 0.5F};
    constexpr UI::UILogicalPoint MissPoint{.x = 2.0F, .y = 0.5F};
    return routeOne(fixture, frame, sequenceBase, 0, UI::UIRoutedPointerEventKind::Move,
                    TargetPoint, report, hash, error) &&
           routeOne(fixture, frame, sequenceBase + 1U, 1, UI::UIRoutedPointerEventKind::ButtonDown,
                    TargetPoint, report, hash, error) &&
           routeOne(fixture, frame, sequenceBase + 2U, 2, UI::UIRoutedPointerEventKind::Move,
                    MissPoint, report, hash, error) &&
           routeOne(fixture, frame, sequenceBase + 3U, 3, UI::UIRoutedPointerEventKind::ButtonUp,
                    MissPoint, report, hash, error) &&
           routeOne(fixture, frame, sequenceBase + 4U, 4, UI::UIRoutedPointerEventKind::Move,
                    MissPoint, report, hash, error);
}

[[nodiscard]] bool runRoute(const UIBenchmarkOptions& options, UIBenchmarkReport& report,
                            std::string& error)
{
    CountingMemoryResource memory{};
    auto fixtureResult = createFixture(largeContextCapacity(), memory, error);
    if (!fixtureResult) {
        return false;
    }
    UIFixture fixture = std::move(*fixtureResult);
    std::vector<UI::UINodeId> routeNodes;
    UI::UINodeId target{};
    if (!populateRouteTree(fixture, routeNodes, target, error)) {
        return false;
    }
    constexpr UI::UILogicalSize Viewport{.width = 1.0F, .height = static_cast<float>(kLargeNodeCount)};
    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }
    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }

    std::vector<UI::UIRoutedPointerListenerToken> tokens;
    if (!registerRouteListeners(fixture, routeNodes, target, tokens, error)) {
        return false;
    }

    DeterministicHash routeHash{};
    Core::SteadyMonotonicClock clock{};
    const auto wallBegin = clock.now();
    const u64 totalIterations = options.warmUpIterations + options.measureIterations;
    if (options.warmUpIterations == 0) {
        captureAllocationBaseline(memory, report);
    }
    for (u64 iteration = 0; iteration < totalIterations; ++iteration) {
        const bool measured = iteration >= options.warmUpIterations;
        const auto routeBegin = clock.now();
        if (!runRouteSequence(fixture, iteration, measured ? &report : nullptr,
                              measured ? &routeHash : nullptr, error)) {
            return false;
        }
        const auto routeEnd = clock.now();
        if (measured) {
            const u64 elapsed = elapsedNs(routeBegin, routeEnd);
            report.routeSamples.push_back(elapsed);
            report.totalSamples.push_back(elapsed);
        }
        if (iteration + 1 == options.warmUpIterations) {
            captureAllocationBaseline(memory, report);
        }
    }
    report.wallNs = elapsedNs(wallBegin, clock.now());
    captureFinalState(memory, report, fixture);

    report.configuredNodeCount = kLargeNodeCount;
    report.configuredRouteDepth = kRouteDepth;
    report.layoutSnapshotHighWater = report.workN;
    report.hitSnapshotHighWater = report.workH;
    report.paintSnapshotHighWater = report.workP;
    if (report.pmrAllocationsAfter != report.pmrAllocationsBefore || report.workN != kLargeNodeCount ||
        report.workH != kLargeNodeCount || report.maxRouteDepth != kRouteDepth ||
        report.routeDispatches != options.measureIterations * 5U ||
        report.pointerCaptureRoutes < options.measureIterations || report.listenerCalls == 0) {
        error = "ui_route_v1 invariant failed";
        return false;
    }

    DeterministicHash hash{};
    hashStatistics(hash, report);
    hash.addU64(routeHash.value());
    hash.addU32(target.index());
    report.checksum = hash.value();
    return true;
}

[[nodiscard]] UI::UIContextCapacityConfig collectionContextCapacity() noexcept
{
    UI::UIContextCapacityConfig capacity{};
    capacity.nodeCapacity = 128;
    capacity.rootCapacity = 1;
    capacity.dirtyQueueCapacity = 128;
    capacity.layoutSnapshotCapacity = 128;
    capacity.hitSnapshotCapacity = 128;
    capacity.paintSnapshotCapacity = 128;
    capacity.routePathCapacity = 128;
    capacity.routedPointerListenerCapacity = 128;
    capacity.applyDefaultProductChrome = false;
    return capacity;
}

[[nodiscard]] bool runVirtualCollection(const UIBenchmarkOptions& options, UIBenchmarkReport& report,
                                        std::string& error)
{
    CountingMemoryResource memory{};
    auto fixtureResult = createFixture(collectionContextCapacity(), memory, error);
    if (!fixtureResult) {
        return false;
    }
    UIFixture fixture = std::move(*fixtureResult);
    ListDataSourceState dataSourceState{};

    UI::UILayoutStyle listLayout = fixedLayout(64.0F, 64.0F);
    auto listResult = fixture.updater.createElement(
        fixture.root.rootNodeId(),
        UI::makeListViewElement(
            UI::UIListViewCreateConfig{.materializedItemCapacity = kMaterializedRowCapacity},
            listLayout));
    if (!listResult) {
        error = listResult.error().message;
        return false;
    }
    const UI::UINodeId listView = *listResult;
    if (Core::Status status = fixture.updater.setListViewDataSource(
            listView,
            UI::UIListViewDataSource{
                .state = &dataSourceState,
                .itemCount = &listItemCount,
                .resolveItem = &resolveListItem,
            });
        !status) {
        error = status.error().message;
        return false;
    }
    if (Core::Status status = fixture.updater.setListViewStyle(
            listView,
            UI::UIListViewStyle{
                .rowHeight = 1.0F,
                .overscanRows = 0,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
                .wheelStep = 1.0F,
            });
        !status) {
        error = status.error().message;
        return false;
    }

    auto builderResult = createDisplayListBuilder(128, memory, error);
    if (!builderResult) {
        return false;
    }
    Render::UIDisplayListBuilder builder = std::move(*builderResult);
    constexpr UI::UILogicalSize Viewport{.width = 64.0F, .height = 64.0F};
    constexpr Render::UIPixelRect Framebuffer{.x = 0, .y = 0, .width = 64, .height = 64};
    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }
    if (!buildDisplayList(fixture, builder, Framebuffer, nullptr, error)) {
        return false;
    }

    constexpr std::array<u64, 4> ScrollSequence{0, 16'384, 65'536, 99'936};
    DeterministicHash collectionHash{};
    Core::SteadyMonotonicClock clock{};
    const auto wallBegin = clock.now();
    const u64 totalIterations = options.warmUpIterations + options.measureIterations;
    if (options.warmUpIterations == 0) {
        captureAllocationBaseline(memory, report);
    }
    for (u64 iteration = 0; iteration < totalIterations; ++iteration) {
        const bool measured = iteration >= options.warmUpIterations;
        const usize sequenceIndex = static_cast<usize>((iteration + options.seed) % ScrollSequence.size());
        const u64 logicalIndex = ScrollSequence[sequenceIndex];
        const auto totalBegin = clock.now();
        if (Core::Status status = fixture.updater.setListViewSelectedIndex(listView, logicalIndex); !status) {
            error = status.error().message;
            return false;
        }
        if (Core::Status status = fixture.updater.scrollListViewToIndex(
                listView, logicalIndex, UI::UIListViewScrollAlignment::Start);
            !status) {
            error = status.error().message;
            return false;
        }
        const auto commitBegin = clock.now();
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitEnd = clock.now();
        const auto displayBegin = clock.now();
        if (!buildDisplayList(fixture, builder, Framebuffer, measured ? &report : nullptr, error)) {
            return false;
        }
        const auto displayEnd = clock.now();

        auto metrics = fixture.updater.listViewMetrics(listView);
        auto selection = fixture.updater.listViewSelection(listView);
        if (!metrics || !selection) {
            error = metrics ? selection.error().message : metrics.error().message;
            return false;
        }
        report.materializedRowHighWater = (std::max)(
            report.materializedRowHighWater, static_cast<u64>(metrics->materializedItemCount));
        report.liveNodeHighWater = (std::max)(
            report.liveNodeHighWater, static_cast<u64>(fixture.context->liveNodeCount()));

        if (measured) {
            report.commitSamples.push_back(elapsedNs(commitBegin, commitEnd));
            report.displayListSamples.push_back(elapsedNs(displayBegin, displayEnd));
            report.totalSamples.push_back(elapsedNs(totalBegin, displayEnd));
            accumulateCommitStatistics(fixture.context->statistics(), report);
            collectionHash.addU64(metrics->logicalItemCount);
            collectionHash.addU64(metrics->firstVisibleIndex);
            collectionHash.addU64(metrics->firstMaterializedIndex);
            collectionHash.addU32(metrics->materializedItemCount);
            collectionHash.addU64(selection->key);
            collectionHash.addU64(selection->logicalIndex);
        }
        if (iteration + 1 == options.warmUpIterations) {
            captureAllocationBaseline(memory, report);
        }
    }
    report.wallNs = elapsedNs(wallBegin, clock.now());

    auto finalMetrics = fixture.updater.listViewMetrics(listView);
    auto finalSelection = fixture.updater.listViewSelection(listView);
    if (!finalMetrics || !finalSelection) {
        error = finalMetrics ? finalSelection.error().message : finalMetrics.error().message;
        return false;
    }
    report.selectionKey = finalSelection->key;
    report.selectionIndex = finalSelection->logicalIndex;
    const UI::UICommittedSemanticsView semantics = fixture.context->committedSemantics();
    report.semanticsEntryCount = semantics.size();
    report.semanticsChecksum = hashSemantics(semantics);
    captureFinalState(memory, report, fixture);

    report.configuredNodeCount = 128;
    report.configuredLogicalItemCount = kLogicalItemCount;
    report.configuredMaterializedRowCapacity = kMaterializedRowCapacity;
    if (report.pmrAllocationsAfter != report.pmrAllocationsBefore ||
        finalMetrics->logicalItemCount != kLogicalItemCount ||
        finalMetrics->materializedItemCapacity != kMaterializedRowCapacity ||
        report.materializedRowHighWater > kMaterializedRowCapacity || !finalSelection->hasValue() ||
        report.semanticsEntryCount == 0) {
        error = "ui_virtual_collection_v1 invariant failed";
        return false;
    }

    DeterministicHash hash{};
    hashStatistics(hash, report);
    hash.addU64(collectionHash.value());
    report.checksum = hash.value();
    return true;
}

[[nodiscard]] bool runImageNineSlice(const UIBenchmarkOptions& options,
                                     UIBenchmarkReport& report, std::string& error)
{
    CountingMemoryResource memory{};
    auto fixtureResult = createFixture(imageBenchmarkContextCapacity(), memory, error);
    if (!fixtureResult) {
        return false;
    }
    UIFixture fixture = std::move(*fixtureResult);
    if (!populateImageBenchmarkTree(fixture, error)) {
        return false;
    }
    auto builderResult =
        createDisplayListBuilder(static_cast<u32>(kImageBenchmarkQuadCount), memory, error);
    if (!builderResult) {
        return false;
    }
    Render::UIDisplayListBuilder builder = std::move(*builderResult);
    Render::RenderFramePacket packet{};
    ImageResolverState resolverState{};
    resolverState.initialize(fixture.root.rootNodeId());
    std::array<Integration::UIRenderImageResolutionCacheEntry, kUniqueImageResourceCount> cache{};
    constexpr UI::UILogicalSize Viewport{
        .width = kImageElementExtent,
        .height = kImageElementExtent * static_cast<float>(kImageBenchmarkElementCount),
    };
    constexpr Render::UIPixelRect Framebuffer{
        .x = 0,
        .y = 0,
        .width = static_cast<u32>(kImageElementExtent),
        .height = static_cast<u32>(kImageElementExtent *
                                   static_cast<float>(kImageBenchmarkElementCount)),
    };

    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }
    std::optional<u64> expectedDisplayListChecksum{};
    if (!buildImageBenchmarkDisplayList(
            fixture, builder, packet, resolverState, cache, 1, Framebuffer,
            expectedDisplayListChecksum, nullptr, error)) {
        return false;
    }

    Core::SteadyMonotonicClock clock{};
    const auto wallBegin = clock.now();
    const u64 totalIterations = options.warmUpIterations + options.measureIterations;
    if (options.warmUpIterations == 0) {
        captureAllocationBaseline(memory, report);
    }
    for (u64 iteration = 0; iteration < totalIterations; ++iteration) {
        const bool measured = iteration >= options.warmUpIterations;
        const auto totalBegin = clock.now();
        const auto commitBegin = clock.now();
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitEnd = clock.now();
        const auto displayBegin = clock.now();
        if (!buildImageBenchmarkDisplayList(
                fixture, builder, packet, resolverState, cache, iteration + 2U, Framebuffer,
                expectedDisplayListChecksum, measured ? &report : nullptr, error)) {
            return false;
        }
        const auto displayEnd = clock.now();
        if (measured) {
            report.commitSamples.push_back(elapsedNs(commitBegin, commitEnd));
            report.displayListSamples.push_back(elapsedNs(displayBegin, displayEnd));
            report.totalSamples.push_back(elapsedNs(totalBegin, displayEnd));
            accumulateCommitStatistics(fixture.context->statistics(), report);
        }
        if (iteration + 1 == options.warmUpIterations) {
            captureAllocationBaseline(memory, report);
        }
    }
    report.wallNs = elapsedNs(wallBegin, clock.now());
    captureFinalState(memory, report, fixture);

    report.configuredNodeCount = kImageBenchmarkNodeCount;
    report.configuredImageCount = kImageElementCount;
    report.configuredIconCount = kIconElementCount;
    report.configuredNineSliceCount = kNineSliceElementCount;
    report.configuredUniqueImageResourceCount = kUniqueImageResourceCount;
    report.layoutSnapshotHighWater = report.workN;
    report.hitSnapshotHighWater = report.workH;
    report.paintSnapshotHighWater = report.workP;
    const u64 expectedResolveCount = kUniqueImageResourceCount * options.measureIterations;
    const u64 expectedImageQuadCount = kImageBenchmarkQuadCount * options.measureIterations;
    const u64 expectedBatchCount = kImageBenchmarkBatchCount * options.measureIterations;
    const u64 expectedCacheDedupe =
        (kImageBenchmarkQuadCount - kUniqueImageResourceCount) * options.measureIterations;
    if (report.pmrAllocationsAfter != report.pmrAllocationsBefore || report.layoutPasses != 0 ||
        report.measuredNodes != 0 || report.arrangedNodes != 0 || report.hitRebuilds != 0 ||
        report.paintCacheRebuilds != 0 || report.paintSnapshotRebuilds != 0 ||
        report.workN != kImageBenchmarkNodeCount || report.workH != kImageBenchmarkNodeCount ||
        report.workP != kImageBenchmarkQuadCount || report.workQ != kImageBenchmarkQuadCount ||
        report.workU != kUniqueImageResourceCount || report.workB != kImageBenchmarkBatchCount ||
        report.displayListBuilds != options.measureIterations ||
        report.displayListSourceEntries != expectedImageQuadCount ||
        report.displayListImageQuads != expectedImageQuadCount || report.displayListSolidQuads != 0 ||
        report.displayListGlyphs != 0 || report.displayListBatches != expectedBatchCount ||
        report.imageResolverCalls != expectedResolveCount ||
        report.imageResolverHits != expectedResolveCount || report.imageResolverMisses != 0 ||
        report.imageResolverNotReady != 0 || report.imageExtentMismatches != 0 ||
        report.imageResolutionCacheDedupe != expectedCacheDedupe ||
        report.imagePinAcquisitions != expectedResolveCount ||
        report.imagePinReleases != expectedResolveCount || report.imageResourceInternDedupe != 0 ||
        report.imagePinHighWater != kUniqueImageResourceCount ||
        report.imageResourceHighWater != kUniqueImageResourceCount ||
        report.imageCommandHighWater != kImageBenchmarkQuadCount ||
        report.imageBatchHighWater != kImageBenchmarkBatchCount ||
        report.statistics.activeCanvasCommandCount != kNineSliceElementCount ||
        report.statistics.canvasCommandHighWater != kNineSliceElementCount ||
        report.statistics.activeImageContentCount != kImageElementCount + kIconElementCount ||
        report.statistics.imageContentHighWater != kImageElementCount + kIconElementCount ||
        resolverState.activePins != 0 || packet.resourceCount() != 0 ||
        !expectedDisplayListChecksum.has_value() || report.displayListChecksum == 0) {
        error = "ui_image_nineslice_v1 invariant failed";
        return false;
    }

    DeterministicHash hash{};
    hashStatistics(hash, report);
    hash.addU64(report.configuredImageCount);
    hash.addU64(report.configuredIconCount);
    hash.addU64(report.configuredNineSliceCount);
    hash.addU64(report.configuredUniqueImageResourceCount);
    hash.addU64(report.displayListImageQuads);
    hash.addU64(report.imageResolverCalls);
    hash.addU64(report.imageResolverHits);
    hash.addU64(report.imageResolverMisses);
    hash.addU64(report.imageResolverNotReady);
    hash.addU64(report.imageExtentMismatches);
    hash.addU64(report.imageResolutionCacheDedupe);
    hash.addU64(report.imagePinAcquisitions);
    hash.addU64(report.imagePinReleases);
    hash.addU64(report.imageResourceInternDedupe);
    hash.addU64(report.imagePinHighWater);
    hash.addU64(report.imageResourceHighWater);
    hash.addU64(report.imageCommandHighWater);
    hash.addU64(report.imageBatchHighWater);
    report.checksum = hash.value();
    return true;
}

using ComponentRootArray = std::array<UI::UINodeId, kComponentCount>;

void recordComponentBudgetRequested(UIBenchmarkReport& report,
                                    const UI::UIComponentBuildBudget& budget) noexcept
{
    report.componentNodesRequested += budget.nodes;
    report.componentTextBytesRequested += budget.textBytes;
    report.componentCanvasCommandsRequested += budget.canvasCommands;
    report.componentActivateSlotsRequested += budget.behaviors.activate;
    report.componentToggleSlotsRequested += budget.behaviors.toggle;
    report.componentRangeSlotsRequested += budget.behaviors.range;
    report.componentTextInputSlotsRequested += budget.behaviors.textInput;
    report.componentScrollSlotsRequested += budget.behaviors.scroll;
    report.componentSelectionSlotsRequested += budget.behaviors.selection;
}

void recordComponentBudgetPublished(UIBenchmarkReport& report,
                                    const UI::UIComponentBuildBudget& budget) noexcept
{
    report.componentNodesPublished += budget.nodes;
    report.componentTextBytesPublished += budget.textBytes;
    report.componentCanvasCommandsPublished += budget.canvasCommands;
    report.componentActivateSlotsPublished += budget.behaviors.activate;
    report.componentToggleSlotsPublished += budget.behaviors.toggle;
    report.componentRangeSlotsPublished += budget.behaviors.range;
    report.componentTextInputSlotsPublished += budget.behaviors.textInput;
    report.componentScrollSlotsPublished += budget.behaviors.scroll;
    report.componentSelectionSlotsPublished += budget.behaviors.selection;
}

[[nodiscard]] bool buildReservedComponents(UIFixture& fixture,
                                           ComponentRootArray& componentRoots,
                                           UIBenchmarkReport* report,
                                           std::string& error)
{
    const auto canvasCommands = componentCanvasCommands();
    UI::UIElementDescriptor rootDescriptor =
        UI::makeScrollViewElement(fixedLayout(64.0F, 32.0F));
    rootDescriptor.visual.canvas = canvasCommands;
    const UI::UIElementDescriptor rangeDescriptor =
        componentRangeActivateToggleDescriptor();
    const UI::UIElementDescriptor textInputDescriptor =
        UI::makeTextEditElement(kComponentTextInputText, fixedLayout(32.0F, 8.0F));
    const UI::UIElementDescriptor dropdownDescriptor =
        UI::makeDropdownElement(kComponentDropdownText, fixedLayout(32.0F, 8.0F));

    for (usize index = 0; index < componentRoots.size(); ++index) {
        auto transactionResult = fixture.updater.beginBuildTransaction(
            fixture.root.rootNodeId(), rootDescriptor, kComponentBuildBudget);
        if (!transactionResult) {
            error = transactionResult.error().message;
            return false;
        }
        UI::UIElementBuildTransaction transaction = std::move(*transactionResult);
        if (report != nullptr) {
            ++report->componentTransactionsStarted;
            recordComponentBudgetRequested(*report, kComponentBuildBudget);
        }

        const UI::UINodeId componentRoot = transaction.rootNodeId();
        auto range = transaction.createElement(componentRoot, rangeDescriptor);
        if (!range) {
            error = range.error().message;
            return false;
        }
        auto textInput = transaction.createElement(componentRoot, textInputDescriptor);
        if (!textInput) {
            error = textInput.error().message;
            return false;
        }
        auto dropdown = transaction.createElement(componentRoot, dropdownDescriptor);
        if (!dropdown) {
            error = dropdown.error().message;
            return false;
        }
        auto committedRoot = transaction.commit();
        if (!committedRoot) {
            error = committedRoot.error().message;
            return false;
        }
        componentRoots[index] = *committedRoot;
        if (report != nullptr) {
            ++report->componentTransactionsCommitted;
            recordComponentBudgetPublished(*report, kComponentBuildBudget);
        }
    }
    return true;
}

[[nodiscard]] bool destroyComponents(UIFixture& fixture,
                                     const ComponentRootArray& componentRoots,
                                     std::string& error)
{
    for (const UI::UINodeId componentRoot : componentRoots) {
        if (Core::Status status = fixture.updater.destroy(componentRoot); !status) {
            error = status.error().message;
            return false;
        }
    }
    return true;
}

[[nodiscard]] UI::UIComponentBuildPoolStatistics
componentBuildPoolDelta(const UI::UIComponentBuildPoolStatistics& before,
                        const UI::UIComponentBuildPoolStatistics& after) noexcept
{
    return {
        .requested = after.requested - before.requested,
        .reserved = after.reserved - before.reserved,
        .published = after.published - before.published,
        .capacityFailures = after.capacityFailures - before.capacityFailures,
        .outstandingReservations = after.outstandingReservations,
    };
}

[[nodiscard]] UI::UIComponentBuildStatistics
componentBuildStatisticsDelta(const UI::UIComponentBuildStatistics& before,
                              const UI::UIComponentBuildStatistics& after) noexcept
{
    return {
        .nodes = componentBuildPoolDelta(before.nodes, after.nodes),
        .textBytes = componentBuildPoolDelta(before.textBytes, after.textBytes),
        .canvasCommands = componentBuildPoolDelta(before.canvasCommands, after.canvasCommands),
        .behaviors = {
            .activate = componentBuildPoolDelta(before.behaviors.activate,
                                                after.behaviors.activate),
            .toggle = componentBuildPoolDelta(before.behaviors.toggle,
                                              after.behaviors.toggle),
            .range = componentBuildPoolDelta(before.behaviors.range,
                                             after.behaviors.range),
            .textInput = componentBuildPoolDelta(before.behaviors.textInput,
                                                 after.behaviors.textInput),
            .scroll = componentBuildPoolDelta(before.behaviors.scroll,
                                              after.behaviors.scroll),
            .selection = componentBuildPoolDelta(before.behaviors.selection,
                                                 after.behaviors.selection),
        },
        .activeTransactionCount = after.activeTransactionCount,
        .transactionFailureCount =
            after.transactionFailureCount - before.transactionFailureCount,
    };
}

[[nodiscard]] bool matchesComponentBuildPool(
    const UI::UIComponentBuildPoolStatistics& statistics, u64 expected) noexcept
{
    return statistics.requested == expected && statistics.reserved == expected &&
           statistics.published == expected && statistics.capacityFailures == 0 &&
           statistics.outstandingReservations == 0;
}

using ComponentBuilder = bool (*)(UIFixture&, ComponentRootArray&, UIBenchmarkReport*,
                                  std::string&);

[[nodiscard]] bool runComponentBuild(const UIBenchmarkOptions& options,
                                     UIBenchmarkReport& report,
                                     const UI::UIComponentBuildBudget& budget,
                                     ComponentBuilder buildComponents,
                                     std::string& error)
{
    if (options.measureIterations >
        (std::numeric_limits<u64>::max)() / kComponentTextByteCount) {
        error = "UI component benchmark measured counter range is invalid";
        return false;
    }

    CountingMemoryResource memory{};
    auto fixtureResult = createFixture(componentBenchmarkContextCapacity(), memory, error);
    if (!fixtureResult) {
        return false;
    }
    UIFixture fixture = std::move(*fixtureResult);
    ComponentRootArray componentRoots{};
    constexpr UI::UILogicalSize Viewport{.width = 64.0F, .height = 8'192.0F};

    if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
        error = status.error().message;
        return false;
    }
    UI::UIComponentBuildStatistics componentStatisticsBaseline =
        fixture.context->statistics().componentBuild;

    Core::SteadyMonotonicClock clock{};
    const auto wallBegin = clock.now();
    const u64 totalIterations = options.warmUpIterations + options.measureIterations;
    std::optional<u64> expectedTreeChecksum{};
    if (options.warmUpIterations == 0) {
        captureAllocationBaseline(memory, report);
    }

    for (u64 iteration = 0; iteration < totalIterations; ++iteration) {
        const bool measured = iteration >= options.warmUpIterations;
        const auto totalBegin = clock.now();
        const auto buildBegin = clock.now();
        if (!buildComponents(fixture, componentRoots, measured ? &report : nullptr, error)) {
            return false;
        }
        const auto buildEnd = clock.now();

        const UI::UIContextStatistics authoredStatistics = fixture.context->statistics();
        if (authoredStatistics.liveNodeCount != kComponentContextNodeCount ||
            authoredStatistics.textByteUsed != kComponentTextByteCount ||
            authoredStatistics.activeCanvasCommandCount != kComponentCanvasCommandCount ||
            authoredStatistics.activeActivateBehaviorCount !=
                kComponentCount * budget.behaviors.activate ||
            authoredStatistics.activeToggleBehaviorCount !=
                kComponentCount * budget.behaviors.toggle ||
            authoredStatistics.activeRangeInputBehaviorCount !=
                kComponentCount * budget.behaviors.range ||
            authoredStatistics.activeTextInputBehaviorCount !=
                kComponentCount * budget.behaviors.textInput ||
            authoredStatistics.activeScrollBehaviorCount !=
                kComponentCount * budget.behaviors.scroll ||
            authoredStatistics.activeSelectBehaviorCount !=
                kComponentCount * budget.behaviors.selection) {
            error = "UI component benchmark authored-state invariant failed";
            return false;
        }

        const auto commitBegin = clock.now();
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto commitEnd = clock.now();
        const UI::UIContextStatistics committedStatistics = fixture.context->statistics();
        const u64 treeChecksum = hashComponentTree(fixture);
        if (expectedTreeChecksum.has_value() && *expectedTreeChecksum != treeChecksum) {
            error = "UI component benchmark tree checksum changed between rebuilds";
            return false;
        }
        expectedTreeChecksum = treeChecksum;

        const auto cleanCommitBegin = clock.now();
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const auto cleanCommitEnd = clock.now();
        const UI::UIContextStatistics cleanStatistics = fixture.context->statistics();
        const u64 cleanRebuildCount = cleanStatistics.lastLayoutPassCount +
                                      cleanStatistics.lastHitRebuildCount +
                                      cleanStatistics.lastPaintCacheRebuildCount +
                                      cleanStatistics.lastPaintSnapshotRebuildCount;
        if (cleanRebuildCount != 0 || cleanStatistics.structureDirty ||
            cleanStatistics.layoutDirty || cleanStatistics.hitDirty ||
            cleanStatistics.paintDirty || cleanStatistics.semanticsDirty) {
            error = "UI component benchmark clean commit rebuilt a snapshot";
            return false;
        }

        if (measured) {
            report.buildSamples.push_back(elapsedNs(buildBegin, buildEnd));
            report.commitSamples.push_back(elapsedNs(commitBegin, commitEnd));
            report.cleanCommitSamples.push_back(elapsedNs(cleanCommitBegin, cleanCommitEnd));
            report.totalSamples.push_back(elapsedNs(totalBegin, cleanCommitEnd));
            accumulateCommitStatistics(committedStatistics, report);
            ++report.componentCleanCommitCount;
            report.componentCleanCommitRebuildCount += cleanRebuildCount;
            report.componentTreeChecksum = treeChecksum;
            report.semanticsEntryCount = committedStatistics.committedSemanticsNodeCount;
            report.semanticsChecksum =
                hashStableSemantics(fixture.context->committedSemantics());
            report.liveNodeHighWater =
                (std::max)(report.liveNodeHighWater,
                           static_cast<u64>(committedStatistics.liveNodeCount));
            report.layoutSnapshotHighWater =
                (std::max)(report.layoutSnapshotHighWater,
                           static_cast<u64>(committedStatistics.committedLayoutNodeCount));
            report.hitSnapshotHighWater =
                (std::max)(report.hitSnapshotHighWater,
                           static_cast<u64>(committedStatistics.committedHitNodeCount));
            report.paintSnapshotHighWater =
                (std::max)(report.paintSnapshotHighWater,
                           static_cast<u64>(committedStatistics.committedPaintNodeCount));
            report.workN = committedStatistics.committedLayoutNodeCount;
            report.workP = committedStatistics.committedPaintNodeCount;
            report.workH = committedStatistics.committedHitNodeCount;
        }

        if (!destroyComponents(fixture, componentRoots, error)) {
            return false;
        }
        if (Core::Status status = fixture.context->commitLayout(Viewport); !status) {
            error = status.error().message;
            return false;
        }
        const UI::UIContextStatistics releasedStatistics = fixture.context->statistics();
        if (releasedStatistics.liveNodeCount != 1 || releasedStatistics.textByteUsed != 0 ||
            releasedStatistics.activeCanvasCommandCount != 0 ||
            releasedStatistics.activeActivateBehaviorCount != 0 ||
            releasedStatistics.activeToggleBehaviorCount != 0 ||
            releasedStatistics.activeRangeInputBehaviorCount != 0 ||
            releasedStatistics.activeTextInputBehaviorCount != 0 ||
            releasedStatistics.activeScrollBehaviorCount != 0 ||
            releasedStatistics.activeSelectBehaviorCount != 0) {
            error = "UI component benchmark retained state was not fully released";
            return false;
        }
        if (iteration + 1 == options.warmUpIterations) {
            componentStatisticsBaseline = releasedStatistics.componentBuild;
            captureAllocationBaseline(memory, report);
        }
    }

    report.wallNs = elapsedNs(wallBegin, clock.now());
    const u64 builtWorkN = report.workN;
    const u64 builtWorkP = report.workP;
    const u64 builtWorkH = report.workH;
    captureFinalState(memory, report, fixture);
    report.workN = builtWorkN;
    report.workP = builtWorkP;
    report.workH = builtWorkH;
    report.configuredNodeCount = kComponentContextNodeCount;
    report.configuredComponentCount = kComponentCount;
    report.configuredComponentNodesPerTransaction = kComponentNodesPerTransaction;
    report.configuredComponentTextBytesPerTransaction = kComponentTextBytesPerTransaction;
    report.configuredComponentCanvasCommandsPerTransaction =
        kComponentCanvasCommandsPerTransaction;
    report.configuredComponentBehaviorSlotsPerTransaction = budget.behaviors;
    report.componentReservationStatistics = componentBuildStatisticsDelta(
        componentStatisticsBaseline, report.statistics.componentBuild);

    const u64 measuredTransactions = options.measureIterations * kComponentCount;
    const auto expectedPoolCount = [measuredTransactions](usize perTransaction) noexcept {
        return measuredTransactions * perTransaction;
    };
    if (report.pmrAllocationsAfter != report.pmrAllocationsBefore ||
        report.componentTransactionsStarted != measuredTransactions ||
        report.componentTransactionsCommitted != measuredTransactions ||
        report.componentNodesRequested != options.measureIterations * kComponentNodeCount ||
        report.componentNodesPublished != report.componentNodesRequested ||
        report.componentTextBytesRequested != options.measureIterations * kComponentTextByteCount ||
        report.componentTextBytesPublished != report.componentTextBytesRequested ||
        report.componentCanvasCommandsRequested !=
            options.measureIterations * kComponentCanvasCommandCount ||
        report.componentCanvasCommandsPublished != report.componentCanvasCommandsRequested ||
        report.componentActivateSlotsRequested !=
            expectedPoolCount(budget.behaviors.activate) ||
        report.componentActivateSlotsPublished != report.componentActivateSlotsRequested ||
        report.componentToggleSlotsRequested !=
            expectedPoolCount(budget.behaviors.toggle) ||
        report.componentToggleSlotsPublished != report.componentToggleSlotsRequested ||
        report.componentRangeSlotsRequested != expectedPoolCount(budget.behaviors.range) ||
        report.componentRangeSlotsPublished != report.componentRangeSlotsRequested ||
        report.componentTextInputSlotsRequested !=
            expectedPoolCount(budget.behaviors.textInput) ||
        report.componentTextInputSlotsPublished != report.componentTextInputSlotsRequested ||
        report.componentScrollSlotsRequested != expectedPoolCount(budget.behaviors.scroll) ||
        report.componentScrollSlotsPublished != report.componentScrollSlotsRequested ||
        report.componentSelectionSlotsRequested !=
            expectedPoolCount(budget.behaviors.selection) ||
        report.componentSelectionSlotsPublished != report.componentSelectionSlotsRequested ||
        report.componentCleanCommitCount != options.measureIterations ||
        report.componentCleanCommitRebuildCount != 0 || report.componentTreeChecksum == 0 ||
        report.statistics.liveNodeCount != 1 || report.statistics.textByteUsed != 0 ||
        report.statistics.activeCanvasCommandCount != 0 ||
        report.statistics.activeActivateBehaviorCount != 0 ||
        report.statistics.activeToggleBehaviorCount != 0 ||
        report.statistics.activeRangeInputBehaviorCount != 0 ||
        report.statistics.activeTextInputBehaviorCount != 0 ||
        report.statistics.activeScrollBehaviorCount != 0 ||
        report.statistics.activeSelectBehaviorCount != 0 ||
        report.statistics.canvasCommandHighWater != kComponentCanvasCommandCount ||
        report.statistics.textByteHighWater != kComponentTextByteCount ||
        report.statistics.activateBehaviorHighWater !=
            kComponentCount * budget.behaviors.activate ||
        report.statistics.toggleBehaviorHighWater !=
            kComponentCount * budget.behaviors.toggle ||
        report.statistics.rangeInputBehaviorHighWater !=
            kComponentCount * budget.behaviors.range ||
        report.statistics.textInputBehaviorHighWater !=
            kComponentCount * budget.behaviors.textInput ||
        report.statistics.scrollBehaviorHighWater !=
            kComponentCount * budget.behaviors.scroll ||
        report.statistics.selectBehaviorHighWater !=
            kComponentCount * budget.behaviors.selection ||
        !matchesComponentBuildPool(report.componentReservationStatistics.nodes,
                                   expectedPoolCount(budget.nodes)) ||
        !matchesComponentBuildPool(report.componentReservationStatistics.textBytes,
                                   expectedPoolCount(budget.textBytes)) ||
        !matchesComponentBuildPool(report.componentReservationStatistics.canvasCommands,
                                   expectedPoolCount(budget.canvasCommands)) ||
        !matchesComponentBuildPool(report.componentReservationStatistics.behaviors.activate,
                                   expectedPoolCount(budget.behaviors.activate)) ||
        !matchesComponentBuildPool(report.componentReservationStatistics.behaviors.toggle,
                                   expectedPoolCount(budget.behaviors.toggle)) ||
        !matchesComponentBuildPool(report.componentReservationStatistics.behaviors.range,
                                   expectedPoolCount(budget.behaviors.range)) ||
        !matchesComponentBuildPool(report.componentReservationStatistics.behaviors.textInput,
                                   expectedPoolCount(budget.behaviors.textInput)) ||
        !matchesComponentBuildPool(report.componentReservationStatistics.behaviors.scroll,
                                   expectedPoolCount(budget.behaviors.scroll)) ||
        !matchesComponentBuildPool(report.componentReservationStatistics.behaviors.selection,
                                   expectedPoolCount(budget.behaviors.selection)) ||
        report.componentReservationStatistics.activeTransactionCount != 0 ||
        report.componentReservationStatistics.transactionFailureCount != 0) {
        error = std::string(report.workload) + " invariant failed";
        return false;
    }

    DeterministicHash hash{};
    hashStatistics(hash, report);
    report.checksum = hash.value();
    return true;
}

[[nodiscard]] bool reserveReportSamples(const UIBenchmarkOptions& options,
                                        UIBenchmarkReport& report, std::string& error)
{
    if (options.measureIterations == 0 ||
        options.measureIterations > static_cast<u64>((std::numeric_limits<usize>::max)()) ||
        options.warmUpIterations > (std::numeric_limits<u64>::max)() - options.measureIterations) {
        error = "UI benchmark measure iteration count is invalid";
        return false;
    }
    const usize count = static_cast<usize>(options.measureIterations);
    report.totalSamples.reserve(count);
    report.buildSamples.reserve(count);
    report.commitSamples.reserve(count);
    report.cleanCommitSamples.reserve(count);
    report.displayListSamples.reserve(count);
    report.routeSamples.reserve(count);
    return true;
}

void writeComponentBuildPool(std::ostream& output,
                             const UI::UIComponentBuildPoolStatistics& statistics)
{
    output << "{\"requested\":" << statistics.requested
           << ",\"reserved\":" << statistics.reserved
           << ",\"published\":" << statistics.published
           << ",\"capacity_failures\":" << statistics.capacityFailures
           << ",\"outstanding\":" << statistics.outstandingReservations << '}';
}

void writeReport(std::ostream& output, const UIBenchmarkReport& report)
{
#if defined(NDEBUG)
    constexpr std::string_view BuildType = "Release";
#else
    constexpr std::string_view BuildType = "Debug";
#endif

#if defined(_WIN32)
    constexpr std::string_view HostOs = "windows";
#elif defined(__linux__)
    constexpr std::string_view HostOs = "linux";
#else
    constexpr std::string_view HostOs = "unknown";
#endif

    const TimingSummary total = summarize(report.totalSamples);
    const TimingSummary build = summarize(report.buildSamples);
    const TimingSummary commit = summarize(report.commitSamples);
    const TimingSummary cleanCommit = summarize(report.cleanCommitSamples);
    const TimingSummary displayList = summarize(report.displayListSamples);
    const TimingSummary route = summarize(report.routeSamples);
    const usize allocationDelta = report.pmrAllocationsAfter - report.pmrAllocationsBefore;

    output << "{\"status\":\"ok\",\"schema\":" << kSchemaVersion
           << ",\"schemaName\":\"tina_bench\",\"conclusion\":\"provisional\",\"workload\":{";
    output << "\"id\":";
    writeJsonString(output, report.workload);
    output << ",\"version\":" << kWorkloadVersion << ",\"seed\":" << report.options.seed
           << ",\"parameters\":{\"warmup_iterations\":" << report.options.warmUpIterations
           << ",\"measure_iterations\":" << report.options.measureIterations
           << ",\"node_count\":" << report.configuredNodeCount
           << ",\"route_depth\":" << report.configuredRouteDepth
           << ",\"logical_item_count\":" << report.configuredLogicalItemCount
           << ",\"materialized_row_capacity\":" << report.configuredMaterializedRowCapacity
           << ",\"image_count\":" << report.configuredImageCount
           << ",\"icon_count\":" << report.configuredIconCount
           << ",\"nine_slice_count\":" << report.configuredNineSliceCount
           << ",\"unique_image_resources\":" << report.configuredUniqueImageResourceCount
           << ",\"component_count\":" << report.configuredComponentCount
           << ",\"component_nodes_per_transaction\":"
           << report.configuredComponentNodesPerTransaction
           << ",\"component_text_bytes_per_transaction\":"
           << report.configuredComponentTextBytesPerTransaction
           << ",\"component_canvas_commands_per_transaction\":"
           << report.configuredComponentCanvasCommandsPerTransaction
           << ",\"component_activate_slots_per_transaction\":"
           << report.configuredComponentBehaviorSlotsPerTransaction.activate
           << ",\"component_toggle_slots_per_transaction\":"
           << report.configuredComponentBehaviorSlotsPerTransaction.toggle
           << ",\"component_range_slots_per_transaction\":"
           << report.configuredComponentBehaviorSlotsPerTransaction.range
           << ",\"component_text_input_slots_per_transaction\":"
           << report.configuredComponentBehaviorSlotsPerTransaction.textInput
           << ",\"component_scroll_slots_per_transaction\":"
           << report.configuredComponentBehaviorSlotsPerTransaction.scroll
           << ",\"component_selection_slots_per_transaction\":"
           << report.configuredComponentBehaviorSlotsPerTransaction.selection;
    if (report.workload == kStyleStateWorkload) {
        output << ",\"styled_node_count\":" << report.configuredStyledNodeCount
               << ",\"style_class_count\":" << report.configuredStyleClassCount
               << ",\"style_rule_count\":" << report.configuredStyleRuleCount
               << ",\"style_classes_per_node\":" << report.configuredStyleClassesPerNode
               << ",\"style_rules_per_bucket\":" << report.configuredStyleRulesPerBucket;
    }
    if (report.workload == kMotionWorkload) {
        output << ",\"motion_track_capacity\":" << report.configuredMotionTrackCapacity
               << ",\"active_motion_tracks\":" << report.configuredActiveMotionTracks;
    }
    if (report.workload == kTimelineMotionWorkload ||
        report.workload == kLayoutTimelineMotionWorkload) {
        output << ",\"timeline_capacity\":" << report.configuredTimelineCapacity
               << ",\"timeline_track_capacity\":"
               << report.configuredTimelineTrackCapacity
               << ",\"timeline_keyframe_capacity\":"
               << report.configuredTimelineKeyframeCapacity
               << ",\"active_timeline_capacity\":"
               << report.configuredActiveTimelineCapacity
               << ",\"active_timeline_tracks\":"
               << report.configuredActiveTimelineTracks;
    }
    output << "}}";
    output << ",\"fingerprint\":{\"buildType\":";
    writeJsonString(output, BuildType);
    output << ",\"hostOs\":";
    writeJsonString(output, HostOs);
    output << ",\"taskSystem\":\"None\",\"platform\":\"SyntheticWindowId\","
              "\"render\":\"BackendNeutralDisplayList\",\"ui\":\"Tina::UI\"}";
    output << ",\"timing_ns\":";
    writeTimingSummary(output, total, report.wallNs);
    output << ",\"stage_timing_ns\":{\"build\":";
    writeTimingSummary(output, build);
    output << ",\"commit\":";
    writeTimingSummary(output, commit);
    output << ",\"clean_commit\":";
    writeTimingSummary(output, cleanCommit);
    output << ",\"display_list\":";
    writeTimingSummary(output, displayList);
    output << ",\"route\":";
    writeTimingSummary(output, route);
    output << '}';

    output << ",\"work_units\":{\"N\":" << report.workN << ",\"P\":" << report.workP
           << ",\"H\":" << report.workH << ",\"M\":" << report.workM
           << ",\"Q\":" << report.workQ << ",\"U\":" << report.workU
           << ",\"B\":" << report.workB << '}';
    output << ",\"dirty_rebuild\":{\"layout_passes\":" << report.layoutPasses
           << ",\"measured_nodes\":" << report.measuredNodes
           << ",\"arranged_nodes\":" << report.arrangedNodes
           << ",\"hit_rebuilds\":" << report.hitRebuilds
           << ",\"paint_cache_rebuilds\":" << report.paintCacheRebuilds
           << ",\"paint_snapshot_rebuilds\":" << report.paintSnapshotRebuilds
           << ",\"paint_snapshot_inspected_layout_nodes\":" << report.paintSnapshotInspectedLayoutNodes
           << ",\"paint_snapshot_published_entries\":" << report.paintSnapshotPublishedEntries << '}';
    output << ",\"display_list\":{\"builds\":" << report.displayListBuilds
           << ",\"source_entries\":" << report.displayListSourceEntries
           << ",\"solid_quads\":" << report.displayListSolidQuads
           << ",\"glyphs\":" << report.displayListGlyphs
           << ",\"image_quads\":" << report.displayListImageQuads
           << ",\"batches\":" << report.displayListBatches << '}';
    output << ",\"image_resources\":{\"resolver_calls\":" << report.imageResolverCalls
           << ",\"resolver_hits\":" << report.imageResolverHits
           << ",\"resolver_misses\":" << report.imageResolverMisses
           << ",\"resolver_not_ready\":" << report.imageResolverNotReady
           << ",\"extent_mismatches\":" << report.imageExtentMismatches
           << ",\"cache_deduplicated_entries\":" << report.imageResolutionCacheDedupe
           << ",\"pin_acquisitions\":" << report.imagePinAcquisitions
           << ",\"pin_releases\":" << report.imagePinReleases
           << ",\"resource_intern_deduplications\":" << report.imageResourceInternDedupe
           << '}';
    output << ",\"route\":{\"dispatches\":" << report.routeDispatches
           << ",\"visited_hit_entries\":" << report.hitEntriesVisited
           << ",\"path_nodes\":" << report.routePathNodes
           << ",\"max_depth\":" << report.maxRouteDepth
           << ",\"listener_calls\":" << report.listenerCalls
           << ",\"consumed_transitions\":" << report.consumedTransitions
           << ",\"claimed_transitions\":" << report.claimedTransitions
           << ",\"pointer_capture_routes\":" << report.pointerCaptureRoutes << '}';
    output << ",\"collection\":{\"materialized_row_high_water\":" << report.materializedRowHighWater
           << ",\"selection_key\":" << report.selectionKey
           << ",\"selection_index\":" << report.selectionIndex
           << ",\"semantics_entries\":" << report.semanticsEntryCount << '}';
    if (report.workload == kComponentBuildWorkload) {
        const UI::UIComponentBuildStatistics& statistics =
            report.componentReservationStatistics;
        output << ",\"component_build\":{\"coverage\":\"all_reserved_pools\""
                  ",\"transaction_scope\":\"UIElementBuildTransaction\""
                  ",\"frozen_workload_complete\":true"
                  ",\"reservation_counters_available\":true"
                  ",\"transactions_started\":"
               << report.componentTransactionsStarted
               << ",\"transactions_committed\":" << report.componentTransactionsCommitted
               << ",\"clean_commits\":" << report.componentCleanCommitCount
               << ",\"clean_commit_rebuilds\":"
               << report.componentCleanCommitRebuildCount
               << ",\"active_transactions\":" << statistics.activeTransactionCount
               << ",\"transaction_failures\":" << statistics.transactionFailureCount
               << ",\"reservations\":{\"nodes\":";
        writeComponentBuildPool(output, statistics.nodes);
        output << ",\"text_bytes\":";
        writeComponentBuildPool(output, statistics.textBytes);
        output << ",\"canvas_commands\":";
        writeComponentBuildPool(output, statistics.canvasCommands);
        output << ",\"behaviors\":{\"activate\":";
        writeComponentBuildPool(output, statistics.behaviors.activate);
        output << ",\"toggle\":";
        writeComponentBuildPool(output, statistics.behaviors.toggle);
        output << ",\"range\":";
        writeComponentBuildPool(output, statistics.behaviors.range);
        output << ",\"text_input\":";
        writeComponentBuildPool(output, statistics.behaviors.textInput);
        output << ",\"scroll\":";
        writeComponentBuildPool(output, statistics.behaviors.scroll);
        output << ",\"selection\":";
        writeComponentBuildPool(output, statistics.behaviors.selection);
        output << "}}}";
    }
    if (report.workload == kStyleStateWorkload) {
        output << ",\"style_state\":{\"state_changes\":" << report.styleStateChanges
               << ",\"inspected_nodes\":" << report.styleInspectedNodes
               << ",\"resolved_nodes\":" << report.styleResolvedNodes
               << ",\"candidate_rules\":" << report.styleCandidateRules
               << ",\"clean_commits\":" << report.styleCleanCommitCount
               << ",\"clean_inspected_nodes\":" << report.styleCleanInspectedNodes
               << ",\"clean_resolved_nodes\":" << report.styleCleanResolvedNodes
               << ",\"clean_candidate_rules\":" << report.styleCleanCandidateRules
               << ",\"registered_classes\":"
               << report.statistics.style.registeredClassCount
               << ",\"registered_tokens\":"
               << report.statistics.style.registeredTokenCount
               << ",\"active_rules\":" << report.statistics.style.activeRuleCount
               << ",\"active_buckets\":" << report.statistics.style.activeBucketCount
               << ",\"active_node_class_links\":"
               << report.statistics.style.activeNodeClassLinkCount
               << ",\"compile_failures\":"
               << report.statistics.style.compileFailureCount
               << ",\"capacity_failures\":"
               << report.statistics.style.capacityFailureCount
                << ",\"revision\":" << report.statistics.style.revision << '}';
    }
    if (report.workload == kMotionWorkload) {
        output << ",\"motion\":{\"sampled_tracks\":" << report.motionSampledTracks
               << ",\"active_tracks_sum\":" << report.motionActiveTracks
               << ",\"track_high_water\":" << report.motionTrackHighWater
               << ",\"zero_active_iterations\":" << report.motionZeroActiveIterations
               << ",\"track_capacity\":" << report.statistics.motion.trackCapacity << '}';
    } else if (report.workload == kTimelineMotionWorkload ||
               report.workload == kLayoutTimelineMotionWorkload) {
        output << ",\"timeline_motion\":{\"sampled_timelines\":"
               << report.timelineSampledTimelines
               << ",\"sampled_tracks\":" << report.timelineSampledTracks;
        if (report.workload == kLayoutTimelineMotionWorkload) {
            output << ",\"sampled_layout_tracks\":"
                   << report.timelineSampledLayoutTracks
                   << ",\"layout_commit_failures\":"
                   << report.timelineLayoutCommitFailures;
        }
        output
               << ",\"sampled_segments\":" << report.timelineSampledSegments
               << ",\"active_timelines_sum\":" << report.timelineActiveCount
               << ",\"zero_active_iterations\":" << report.timelineZeroActiveIterations
               << ",\"timeline_capacity\":"
               << report.statistics.motion.timelineCapacity
               << ",\"timeline_count\":" << report.statistics.motion.timelineCount
               << ",\"timeline_track_capacity\":"
               << report.statistics.motion.timelineTrackCapacity
               << ",\"timeline_track_count\":"
               << report.statistics.motion.timelineTrackCount
               << ",\"keyframe_capacity\":"
               << report.statistics.motion.keyframeCapacity
               << ",\"keyframe_count\":" << report.statistics.motion.keyframeCount
               << ",\"active_timeline_capacity\":"
               << report.statistics.motion.activeTimelineCapacity
               << ",\"active_timeline_count\":"
               << report.statistics.motion.activeTimelineCount
               << ",\"timeline_high_water\":"
               << report.statistics.motion.timelineHighWater
               << ",\"timeline_track_high_water\":"
               << report.statistics.motion.timelineTrackHighWater
               << ",\"keyframe_high_water\":"
               << report.statistics.motion.keyframeHighWater
               << ",\"active_timeline_high_water\":"
               << report.statistics.motion.activeTimelineHighWater
               << ",\"last_sampled_timeline_count\":"
               << report.statistics.motion.lastSampledTimelineCount
               << ",\"last_sampled_track_count\":"
               << report.statistics.motion.lastSampledTimelineTrackCount
               << ",\"last_sampled_segment_count\":"
               << report.statistics.motion.lastSampledKeyframeSegmentCount << '}';
    }
    output << ",\"capacity\":{\"nodes\":" << report.statistics.nodeCapacity
           << ",\"dirty_queue\":" << report.statistics.dirtyQueueCapacity
           << ",\"layout_snapshot\":" << report.statistics.layoutSnapshotCapacity
           << ",\"hit_snapshot\":" << report.statistics.hitSnapshotCapacity
           << ",\"paint_snapshot\":" << report.statistics.paintSnapshotCapacity
           << ",\"canvas_commands\":" << report.statistics.canvasCommandCapacity
           << ",\"image_content\":" << report.statistics.imageContentCapacity
           << ",\"route_path\":" << report.statistics.routePathCapacity
           << ",\"listeners\":" << report.statistics.routedPointerListenerCapacity
           << ",\"activate_behavior\":" << report.statistics.activateBehaviorCapacity
           << ",\"toggle_behavior\":" << report.statistics.toggleBehaviorCapacity
           << ",\"range_behavior\":" << report.statistics.rangeInputBehaviorCapacity
           << ",\"text_input_behavior\":" << report.statistics.textInputBehaviorCapacity
           << ",\"scroll_behavior\":" << report.statistics.scrollBehaviorCapacity
           << ",\"selection_behavior\":" << report.statistics.selectBehaviorCapacity
           << ",\"text_bytes\":" << report.statistics.textByteCapacity;
    if (report.workload == kStyleStateWorkload) {
        output << ",\"style_classes\":" << report.statistics.style.classCapacity
               << ",\"style_tokens\":" << report.statistics.style.tokenCapacity
               << ",\"style_rules\":" << report.statistics.style.ruleCapacity
               << ",\"style_buckets\":" << report.statistics.style.bucketCapacity
               << ",\"style_rules_per_bucket\":"
               << report.statistics.style.rulesPerBucketCapacity
               << ",\"node_style_class_links\":"
               << report.statistics.style.nodeClassLinkCapacity;
    }
    output << '}';
    output << ",\"high_water\":{\"live_nodes\":" << report.liveNodeHighWater
           << ",\"dirty_queue\":" << report.statistics.dirtyQueueHighWater
           << ",\"layout_snapshot\":" << report.layoutSnapshotHighWater
           << ",\"hit_snapshot\":" << report.hitSnapshotHighWater
           << ",\"paint_snapshot\":" << report.paintSnapshotHighWater
           << ",\"listeners\":" << report.statistics.routedPointerListenerHighWater
           << ",\"canvas_commands\":" << report.statistics.canvasCommandHighWater
           << ",\"image_content\":" << report.statistics.imageContentHighWater
           << ",\"image_commands\":" << report.imageCommandHighWater
           << ",\"image_batches\":" << report.imageBatchHighWater
           << ",\"image_resources\":" << report.imageResourceHighWater
           << ",\"image_pins\":" << report.imagePinHighWater
           << ",\"activate_behavior\":" << report.statistics.activateBehaviorHighWater
           << ",\"toggle_behavior\":" << report.statistics.toggleBehaviorHighWater
           << ",\"range_behavior\":" << report.statistics.rangeInputBehaviorHighWater
           << ",\"text_input_behavior\":" << report.statistics.textInputBehaviorHighWater
           << ",\"scroll_behavior\":" << report.statistics.scrollBehaviorHighWater
           << ",\"selection_behavior\":" << report.statistics.selectBehaviorHighWater
           << ",\"text_bytes\":" << report.statistics.textByteHighWater;
    if (report.workload == kStyleStateWorkload) {
        output << ",\"style_classes\":" << report.statistics.style.classHighWater
               << ",\"style_tokens\":" << report.statistics.style.tokenHighWater
               << ",\"style_rules\":" << report.statistics.style.ruleHighWater
               << ",\"style_buckets\":" << report.statistics.style.bucketHighWater
               << ",\"style_bucket_candidates\":"
               << report.statistics.style.bucketCandidateHighWater
               << ",\"node_style_class_links\":"
               << report.statistics.style.nodeClassLinkHighWater;
    }
    if (report.workload == kTimelineMotionWorkload ||
        report.workload == kLayoutTimelineMotionWorkload) {
        output << ",\"timelines\":" << report.statistics.motion.timelineHighWater
               << ",\"timeline_tracks\":"
               << report.statistics.motion.timelineTrackHighWater
               << ",\"keyframes\":" << report.statistics.motion.keyframeHighWater
               << ",\"active_timelines\":"
               << report.statistics.motion.activeTimelineHighWater;
    }
    output << '}';
    output << ",\"allocation\":{\"domain\":\"ui_pmr\",\"before\":"
           << report.pmrAllocationsBefore << ",\"after\":" << report.pmrAllocationsAfter
           << ",\"delta\":" << allocationDelta
           << ",\"deallocations\":" << report.pmrDeallocationsAfter
           << ",\"bytes_before\":" << report.pmrBytesBefore
           << ",\"bytes_after\":" << report.pmrBytesAfter
           << ",\"peak_bytes\":" << report.pmrPeakBytes << '}';
    output << ",\"checksums\":{\"display_list\":\"" << hex64(report.displayListChecksum)
           << "\",\"semantics\":\"" << hex64(report.semanticsChecksum)
           << "\",\"component_tree\":\"" << hex64(report.componentTreeChecksum)
           << '"';
    if (report.workload == kStyleStateWorkload) {
        output << ",\"style_state\":\"" << hex64(report.styleStateChecksum)
               << "\",\"style_enabled_display_list\":\""
               << hex64(report.styleEnabledDisplayListChecksum)
               << "\",\"style_disabled_display_list\":\""
               << hex64(report.styleDisabledDisplayListChecksum) << '"';
    }
    output << "},\"checksum\":\"" << hex64(report.checksum)
           << "\",\"exit\":\"Completed\",\"notes\":["
              "\"shared_dev_or_ci_is_provisional_not_hard_gate\","
              "\"tracy_disabled\",\"single_process_run\","
              "\"allocation_delta_counts_tina_routed_ui_pmr_only\"";
    if (report.workload == kComponentBuildWorkload) {
        output << ",\"component_cleanup_excluded_from_stage_timing\""
                  ",\"component_reservation_counters_are_measurement_window_deltas\"";
    } else if (report.workload == kStyleStateWorkload) {
        output << ",\"style_rules_compiled_before_first_retained_node\""
                  ",\"single_node_state_change_resolves_only_the_dirty_node\"";
    } else if (report.workload == kMotionWorkload) {
        output << ",\"motion_samples_only_active_tracks\""
                  ",\"m_equals_zero_adds_no_extra_dirty_or_rebuild\""
                  ",\"seed_selects_active_track_count_0_64_or_1024\"";
    } else if (report.workload == kTimelineMotionWorkload) {
        output << ",\"timeline_samples_only_compact_active_index\""
                  ",\"paint_only_timeline_does_not_rebuild_layout_or_hit\""
                  ",\"seed_selects_active_track_count_0_64_or_1024\""
                  ",\"paint_only_workload_excludes_layout_tracks\"";
    } else if (report.workload == kLayoutTimelineMotionWorkload) {
        output << ",\"timeline_samples_only_compact_active_index\""
                  ",\"layout_timeline_rebuilds_layout_hit_and_paint_atomically\""
                  ",\"seed_selects_active_track_count_0_64_or_1024\"";
    }
    output << "]}\n";
}

} // namespace

bool isUIBenchmarkWorkload(std::string_view workload) noexcept
{
    return workload == kStaticCommitWorkload || workload == kPaintDirtyWorkload ||
           workload == kRouteWorkload || workload == kVirtualCollectionWorkload ||
           workload == kImageNineSliceWorkload || workload == kComponentBuildWorkload ||
           workload == kStyleStateWorkload ||
           workload == kMotionWorkload || workload == kTimelineMotionWorkload ||
           workload == kLayoutTimelineMotionWorkload;
}

void printUIBenchmarkHelp(std::ostream& output)
{
    output << "  --workload=ui_static_commit_v1       4096-node clean commit + DisplayList\n"
           << "  --workload=ui_paint_dirty_v1         one paint-only leaf mutation per iteration\n"
           << "  --workload=ui_route_v1               4096 hit entries, depth-64 route sequence\n"
           << "  --workload=ui_virtual_collection_v1  100k items through a fixed 64-row pool\n"
           << "  --workload=ui_image_nineslice_v1     5096 image quads, 64 unique resources\n"
           << "  --workload=ui_component_build_v1     256 reserved four-node full-pool components\n"
           << "  --workload=ui_style_state_v1         4096 nodes, 256 rules, one state change\n"
           << "  --workload=ui_motion_v1              4096 nodes, active tracks 0/64/1024 by seed\n"
           << "  --workload=ui_motion_timeline_v1    4096 nodes, 4-track/4-keyframe timelines, active tracks 0/64/1024\n"
           << "  --workload=ui_motion_layout_v1      4096 nodes, 4095 overlay leaves, atomic Layout/Hit/Paint timeline rebuilds\n";
}

int runUIBenchmark(std::string_view workload, const UIBenchmarkOptions& options,
                   std::ostream& output, std::ostream& errors)
{
    UIBenchmarkReport report{
        .workload = workload,
        .options = options,
    };
    std::string error;
    try {
        if (!reserveReportSamples(options, report, error)) {
            // Error is emitted below through the same schema path.
        } else if (workload == kStaticCommitWorkload) {
            (void)runStaticCommit(options, report, error);
        } else if (workload == kPaintDirtyWorkload) {
            (void)runPaintDirty(options, report, error);
        } else if (workload == kRouteWorkload) {
            (void)runRoute(options, report, error);
        } else if (workload == kVirtualCollectionWorkload) {
            (void)runVirtualCollection(options, report, error);
        } else if (workload == kImageNineSliceWorkload) {
            (void)runImageNineSlice(options, report, error);
        } else if (workload == kComponentBuildWorkload) {
            (void)runComponentBuild(options, report, kComponentBuildBudget,
                                    &buildReservedComponents, error);
        } else if (workload == kStyleStateWorkload) {
            (void)runStyleState(options, report, error);
        } else if (workload == kMotionWorkload) {
            (void)runMotion(options, report, error);
        } else if (workload == kTimelineMotionWorkload) {
            (void)runTimelineMotion(options, report, error);
        } else if (workload == kLayoutTimelineMotionWorkload) {
            (void)runTimelineMotion(options, report, error);
        } else {
            error = "unknown UI benchmark workload";
        }
    } catch (const std::bad_alloc&) {
        error = "UI benchmark process allocation failed";
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "UI benchmark failed with an unknown exception";
    }

    if (!error.empty()) {
        errors << "{\"status\":\"error\",\"schema\":" << kSchemaVersion << ",\"workload\":";
        writeJsonString(errors, workload);
        errors << ",\"message\":";
        writeJsonString(errors, error);
        errors << "}\n";
        return 1;
    }

    writeReport(output, report);
    return 0;
}

} // namespace Tina::Bench
