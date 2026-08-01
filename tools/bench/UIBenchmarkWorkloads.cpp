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
    std::vector<u64> commitSamples{};
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
            fixture.root.rootNodeId(), UI::makeIconElement(imageBenchmarkContent(nextResource()), layout));
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

[[nodiscard]] std::optional<UIFixture> createFixture(UI::UIContextCapacityConfig capacity,
                                                     CountingMemoryResource& memory,
                                                     std::string& error)
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
                                    std::string& error)
{
    auto build = Integration::buildUIDisplayList(
        builder,
        fixture.context->committedPaint(),
        Integration::UIRenderViewportMapping{.framebufferViewport = framebufferViewport});
    if (!build) {
        error = build.error().message;
        return false;
    }
    if (report != nullptr) {
        ++report->displayListBuilds;
        report->displayListSourceEntries += build->statistics.sourcePaintEntryCount;
        report->displayListSolidQuads += build->statistics.submittedSolidQuadCount;
        report->displayListGlyphs += build->statistics.submittedGlyphCount;
        report->displayListImageQuads += build->statistics.submittedImageQuadCount;
        report->displayListBatches += build->displayList.statistics().batchCount;
        report->displayListChecksum = build->displayList.paintOrderChecksum();
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
}

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
    report.commitSamples.reserve(count);
    report.displayListSamples.reserve(count);
    report.routeSamples.reserve(count);
    return true;
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
    const TimingSummary commit = summarize(report.commitSamples);
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
           << "}}";
    output << ",\"fingerprint\":{\"buildType\":";
    writeJsonString(output, BuildType);
    output << ",\"hostOs\":";
    writeJsonString(output, HostOs);
    output << ",\"taskSystem\":\"None\",\"platform\":\"SyntheticWindowId\","
              "\"render\":\"BackendNeutralDisplayList\",\"ui\":\"Tina::UI\"}";
    output << ",\"timing_ns\":";
    writeTimingSummary(output, total, report.wallNs);
    output << ",\"stage_timing_ns\":{\"commit\":";
    writeTimingSummary(output, commit);
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
    output << ",\"capacity\":{\"nodes\":" << report.statistics.nodeCapacity
           << ",\"dirty_queue\":" << report.statistics.dirtyQueueCapacity
           << ",\"layout_snapshot\":" << report.statistics.layoutSnapshotCapacity
           << ",\"hit_snapshot\":" << report.statistics.hitSnapshotCapacity
           << ",\"paint_snapshot\":" << report.statistics.paintSnapshotCapacity
           << ",\"canvas_commands\":" << report.statistics.canvasCommandCapacity
           << ",\"image_content\":" << report.statistics.imageContentCapacity
           << ",\"route_path\":" << report.statistics.routePathCapacity
           << ",\"listeners\":" << report.statistics.routedPointerListenerCapacity
           << ",\"text_bytes\":" << report.statistics.textByteCapacity << '}';
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
           << ",\"text_bytes\":" << report.statistics.textByteHighWater << '}';
    output << ",\"allocation\":{\"domain\":\"ui_pmr\",\"before\":"
           << report.pmrAllocationsBefore << ",\"after\":" << report.pmrAllocationsAfter
           << ",\"delta\":" << allocationDelta
           << ",\"deallocations\":" << report.pmrDeallocationsAfter
           << ",\"bytes_before\":" << report.pmrBytesBefore
           << ",\"bytes_after\":" << report.pmrBytesAfter
           << ",\"peak_bytes\":" << report.pmrPeakBytes << '}';
    output << ",\"checksums\":{\"display_list\":\"" << hex64(report.displayListChecksum)
           << "\",\"semantics\":\"" << hex64(report.semanticsChecksum)
           << "\"},\"checksum\":\"" << hex64(report.checksum)
           << "\",\"exit\":\"Completed\",\"notes\":["
              "\"shared_dev_or_ci_is_provisional_not_hard_gate\","
              "\"tracy_disabled\",\"single_process_run\","
              "\"allocation_delta_counts_tina_routed_ui_pmr_only\"]}\n";
}

} // namespace

bool isUIBenchmarkWorkload(std::string_view workload) noexcept
{
    return workload == kStaticCommitWorkload || workload == kPaintDirtyWorkload ||
           workload == kRouteWorkload || workload == kVirtualCollectionWorkload ||
           workload == kImageNineSliceWorkload;
}

void printUIBenchmarkHelp(std::ostream& output)
{
    output << "  --workload=ui_static_commit_v1       4096-node clean commit + DisplayList\n"
           << "  --workload=ui_paint_dirty_v1         one paint-only leaf mutation per iteration\n"
           << "  --workload=ui_route_v1               4096 hit entries, depth-64 route sequence\n"
           << "  --workload=ui_virtual_collection_v1  100k items through a fixed 64-row pool\n"
           << "  --workload=ui_image_nineslice_v1     5096 image quads, 64 unique resources\n";
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
