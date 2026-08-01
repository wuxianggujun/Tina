#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/integration/UIRenderDisplayList.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class TrackingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return m_allocationCount;
    }

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        ++m_allocationCount;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
};

[[nodiscard]] Render::UIDisplayListBuilder createBuilder(
    Render::UIDisplayListCapacity capacity,
    std::pmr::memory_resource& storage = *std::pmr::get_default_resource())
{
    auto result = Render::UIDisplayListBuilder::Create(capacity, storage);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return std::move(*result);
}

template <usize Count>
[[nodiscard]] UI::UICommittedPaintView paintView(
    const std::array<UI::UICommittedPaintEntry, Count>& entries,
    UI::UILogicalSize viewport)
{
    return UI::UICommittedPaintView{entries, viewport, 1, 1, 1, 1};
}

[[nodiscard]] UI::UICommittedPaintEntry solidEntry(
    u32 ordinal,
    UI::UILogicalRect bounds,
    UI::UILogicalRect clip,
    UI::UIPremultipliedRgba8Color color = {20, 40, 60, 255}) noexcept
{
    return UI::UICommittedPaintEntry{
        .worldRect = bounds,
        .effectiveClip = clip,
        .paintOrdinal = ordinal,
        .solidFill = color,
    };
}

[[nodiscard]] UI::UICommittedPaintEntry glyphEntry(
    u32 ordinal,
    UI::UILogicalRect bounds,
    UI::UILogicalRect clip,
    Render::UIPixelRect atlasUv,
    u32 atlasPage,
    UI::UIPremultipliedRgba8Color color = {80, 60, 40, 128}) noexcept
{
    return UI::UICommittedPaintEntry{
        .worldRect = bounds,
        .effectiveClip = clip,
        .paintOrdinal = ordinal,
        .solidFill = color,
        .kind = UI::UICommittedPaintKind::Glyph,
        .atlasX = static_cast<u32>(atlasUv.x),
        .atlasY = static_cast<u32>(atlasUv.y),
        .atlasWidth = atlasUv.width,
        .atlasHeight = atlasUv.height,
        .atlasPage = atlasPage,
    };
}

[[nodiscard]] Core::AssetId imageAsset()
{
    const auto parsed = Core::AssetId::parseCanonical("00112233445566778899aabbccddeeff");
    EXPECT_TRUE(parsed.has_value());
    return parsed.value_or(Core::AssetId{});
}

[[nodiscard]] UI::UICommittedPaintEntry imageEntry(
    u32 ordinal, UI::UINodeId root, Core::AssetId asset,
    UI::UIImagePixelRect sourcePixels = {.x = 4, .y = 2, .width = 16, .height = 8},
    UI::UIImageSampling sampling = UI::UIImageSampling::Nearest) noexcept
{
    return UI::UICommittedPaintEntry{
        .node = root,
        .root = root,
        .worldRect = {.x = 0.0F, .y = 0.0F, .width = 50.0F, .height = 25.0F},
        .effectiveClip = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F},
        .paintOrdinal = ordinal,
        .solidFill = {.red = 255, .green = 255, .blue = 255, .alpha = 255},
        .kind = UI::UICommittedPaintKind::Image,
        .imageSource = {
            .texture = asset,
            .sourcePixels = sourcePixels,
            .texturePixelExtent = {.width = 32, .height = 16},
            .intrinsicLogicalSize = {.width = 32.0F, .height = 16.0F},
        },
        .imageSampling = sampling,
    };
}

[[nodiscard]] Render::FrameResourceRef internTexture(Render::RenderFramePacket& packet, u64 bindingKey)
{
    auto result = packet.intern(
        {
            .kind = Render::FrameResourceKind::Texture2D,
            .deviceBindingKey = bindingKey,
        },
        Render::FramePin{Render::FramePinKind::Custom, bindingKey, nullptr, nullptr});
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : Render::FrameResourceRef{};
}

struct ImageResolverState final {
    UI::UINodeId root{};
    Render::FrameResourceRef texture{};
    u32 pixelWidth = 32;
    u32 pixelHeight = 16;
    u32 callCount = 0;
    bool available = true;
    bool fail = false;
    bool internThroughSink = false;
    u64 sinkBindingKey = 0;
    Render::Texture2DFrameResourceResolver resolver{};

    static Core::Result<std::optional<Render::Texture2DFrameResourceResolution>>
    resolve(void* userData, Core::AssetId, Render::FrameResourceSink& sink) noexcept
    {
        auto& state = *static_cast<ImageResolverState*>(userData);
        ++state.callCount;
        if (state.fail)
        {
            return Core::failure(Core::CoreErrorCode::Internal, "Test image resolver failure");
        }
        if (!state.available)
        {
            return std::optional<Render::Texture2DFrameResourceResolution>{};
        }

        Render::FrameResourceRef texture = state.texture;
        if (state.internThroughSink)
        {
            auto interned = sink.intern(
                {
                    .kind = Render::FrameResourceKind::Texture2D,
                    .deviceBindingKey = state.sinkBindingKey,
                },
                Render::FramePin{Render::FramePinKind::Custom, state.sinkBindingKey, nullptr, nullptr});
            if (!interned)
            {
                return Core::failure(std::move(interned.error()));
            }
            texture = *interned;
        }
        return std::optional<Render::Texture2DFrameResourceResolution>{
            Render::Texture2DFrameResourceResolution{
                .resource = texture,
                .pixelWidth = state.pixelWidth,
                .pixelHeight = state.pixelHeight,
            }};
    }

    void initializeResolver() noexcept
    {
        resolver = {
            .userData = this,
            .resolve = &resolve,
        };
    }
};

[[nodiscard]] const Render::Texture2DFrameResourceResolver*
findImageResolver(const void* userData, UI::UINodeId root) noexcept
{
    const auto& state = *static_cast<const ImageResolverState*>(userData);
    return state.root == root ? &state.resolver : nullptr;
}

class RejectingFrameResourceSink final : public Render::FrameResourceSink {
  public:
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    intern(Render::FrameResourceDescriptor, Render::FramePin&&) noexcept override
    {
        ++internCount;
        return Core::failure(Render::RenderErrorCode::InvalidFrameResource, "Test frame resource sink failure");
    }

    [[nodiscard]] u32 resourceCount() const noexcept override
    {
        return 0;
    }

    u32 internCount = 0;
};

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window)
{
    auto result = UI::UIContext::Create(
        window,
        {.nodeCapacity = 3, .rootCapacity = 1});
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

class UIRenderDisplayListTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto poolResult = WindowPool::Create(1);
        ASSERT_TRUE(poolResult.has_value()) << poolResult.error().message;
        windows.emplace(std::move(*poolResult));
        auto windowResult = windows->tryEmplace(0);
        ASSERT_TRUE(windowResult.has_value()) << windowResult.error().message;
        window = *windowResult;
    }

    std::optional<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UIRenderDisplayListTest, ConvertsCommittedUISolidFillAndIntegerPremultiplicationEndToEnd)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto panelResult = context->rootBuilder().createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(panelResult.has_value()) << panelResult.error().message;
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << updaterResult.error().message;

    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(10.0F);
    style.size.height = UI::UILayoutLength::Px(20.0F);
    ASSERT_TRUE(updaterResult->setLayoutStyle(*panelResult, style).has_value());
    UI::UIBoxPaint paint;
    paint.solidFill = UI::UISolidFill{
        .color = {.red = 200, .green = 100, .blue = 50, .alpha = 128},
    };
    ASSERT_TRUE(updaterResult->setBoxPaint(*panelResult, paint).has_value());
    ASSERT_TRUE(context->commitLayout({100.0F, 50.0F}).has_value());

    auto builder = createBuilder({.commandCount = 4, .clipCount = 4, .batchCount = 4});
    auto result = Integration::buildUIDisplayList(
        builder,
        context->committedPaint(),
        {.framebufferViewport = {0, 0, 200, 100}});
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    ASSERT_EQ(result->displayList.commands().size(), 1U);
    const Render::UIDrawCommand& command = result->displayList.commands().front();
    EXPECT_EQ(command.bounds, (Render::UIPixelRect{0, 0, 20, 40}));
    EXPECT_EQ(
        command.color,
        (Render::UIPremultipliedRgba8{100, 50, 25, 128}));
    EXPECT_FALSE(command.clip.hasClip());
    EXPECT_EQ(result->statistics.sourcePaintEntryCount, 1U);
    EXPECT_EQ(result->statistics.submittedSolidQuadCount, 1U);
}

TEST_F(UIRenderDisplayListTest, PreservesMixedSolidAndGlyphPaintOrderClipAndBatchBoundaries)
{
    constexpr UI::UILogicalRect sharedClip{
        .x = 10.0F,
        .y = 10.0F,
        .width = 40.0F,
        .height = 40.0F,
    };
    const std::array entries{
        solidEntry(3, {0.0F, 15.0F, 20.0F, 10.0F}, sharedClip),
        glyphEntry(
            7,
            {5.0F, 20.0F, 15.0F, 12.0F},
            sharedClip,
            {11, 13, 7, 9},
            2),
        glyphEntry(
            11,
            {45.0F, 22.0F, 10.0F, 14.0F},
            sharedClip,
            {23, 29, 8, 10},
            2),
        solidEntry(20, {40.0F, 30.0F, 20.0F, 10.0F}, sharedClip),
    };
    auto builder = createBuilder({.commandCount = 4, .clipCount = 1, .batchCount = 3});

    auto result = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {100.0F, 100.0F}),
        {.framebufferViewport = {0, 0, 200, 100}});

    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    const auto commands = result->displayList.commands();
    ASSERT_EQ(commands.size(), 4U);
    EXPECT_EQ(commands[0].paintOrdinal, 3U);
    EXPECT_EQ(commands[1].paintOrdinal, 7U);
    EXPECT_EQ(commands[2].paintOrdinal, 11U);
    EXPECT_EQ(commands[3].paintOrdinal, 20U);
    EXPECT_EQ(commands[0].kind, Render::UIDrawCommandKind::SolidQuad);
    EXPECT_EQ(commands[1].kind, Render::UIDrawCommandKind::Glyph);
    EXPECT_EQ(commands[2].kind, Render::UIDrawCommandKind::Glyph);
    EXPECT_EQ(commands[3].kind, Render::UIDrawCommandKind::SolidQuad);

    EXPECT_EQ(commands[1].atlasPage, 2U);
    EXPECT_EQ(commands[1].atlasUv, (Render::UIPixelRect{11, 13, 7, 9}));
    EXPECT_EQ(commands[2].atlasPage, 2U);
    EXPECT_EQ(commands[2].atlasUv, (Render::UIPixelRect{23, 29, 8, 10}));

    ASSERT_EQ(result->displayList.clips().size(), 1U);
    ASSERT_TRUE(commands[0].clip.hasClip());
    EXPECT_EQ(commands[1].clip, commands[0].clip);
    EXPECT_EQ(commands[2].clip, commands[0].clip);
    EXPECT_EQ(commands[3].clip, commands[0].clip);
    const Render::UIPixelRect* resolvedClip =
        result->displayList.resolveClip(commands[0].clip);
    ASSERT_NE(resolvedClip, nullptr);
    EXPECT_EQ(*resolvedClip, (Render::UIPixelRect{20, 10, 80, 40}));

    const auto batches = result->displayList.batches();
    ASSERT_EQ(batches.size(), 3U);
    EXPECT_EQ(batches[0].kind, Render::UIDrawCommandKind::SolidQuad);
    EXPECT_EQ(batches[0].clip, commands[0].clip);
    EXPECT_EQ(batches[0].firstCommand, 0U);
    EXPECT_EQ(batches[0].commandCount, 1U);
    EXPECT_EQ(batches[1].kind, Render::UIDrawCommandKind::Glyph);
    EXPECT_EQ(batches[1].clip, commands[0].clip);
    EXPECT_EQ(batches[1].atlasPage, 2U);
    EXPECT_EQ(batches[1].firstCommand, 1U);
    EXPECT_EQ(batches[1].commandCount, 2U);
    EXPECT_EQ(batches[2].kind, Render::UIDrawCommandKind::SolidQuad);
    EXPECT_EQ(batches[2].clip, commands[0].clip);
    EXPECT_EQ(batches[2].firstCommand, 3U);
    EXPECT_EQ(batches[2].commandCount, 1U);

    EXPECT_EQ(result->statistics.sourcePaintEntryCount, 4U);
    EXPECT_EQ(result->statistics.submittedSolidQuadCount, 2U);
    EXPECT_EQ(result->statistics.submittedGlyphCount, 2U);
    EXPECT_EQ(result->displayList.statistics().solidQuadCommandCount, 2U);
    EXPECT_EQ(result->displayList.statistics().glyphCommandCount, 2U);
}

TEST_F(UIRenderDisplayListTest, UsesOutwardFractionalRoundingAndClampsToFramebufferViewport)
{
    const std::array entries{
        solidEntry(
            4,
            {.x = -0.25F, .y = 0.2F, .width = 1.5F, .height = 0.8F},
            {.x = -10.0F, .y = -10.0F, .width = 30.0F, .height = 30.0F}),
    };
    auto builder = createBuilder({.commandCount = 2, .clipCount = 2, .batchCount = 2});
    auto result = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {3.0F, 2.0F}),
        {.framebufferViewport = {10, 20, 7, 5}});
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    ASSERT_EQ(result->displayList.commands().size(), 1U);
    EXPECT_EQ(
        result->displayList.commands().front().bounds,
        (Render::UIPixelRect{10, 20, 3, 3}));
}

TEST_F(UIRenderDisplayListTest, ProjectsAndClampsLogicalCornerRadiusWithAnisotropicScale)
{
    auto rounded = solidEntry(
        1,
        {.x = 10.0F, .y = 10.0F, .width = 10.0F, .height = 8.0F},
        {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F});
    rounded.cornerRadius = 20.0F;
    const std::array entries{rounded};
    auto builder = createBuilder({.commandCount = 1, .clipCount = 0, .batchCount = 1});

    auto result = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {100.0F, 100.0F}),
        {.framebufferViewport = {0, 0, 200, 100}});

    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    ASSERT_EQ(result->displayList.commands().size(), 1U);
    const Render::UIDrawCommand& command = result->displayList.commands().front();
    EXPECT_EQ(command.bounds, (Render::UIPixelRect{20, 10, 20, 8}));
    EXPECT_FLOAT_EQ(command.cornerRadius, 4.0F);
}

TEST_F(UIRenderDisplayListTest, ElidesAClipThatCoversTheProjectedCommand)
{
    const std::array entries{
        solidEntry(
            1,
            {.x = 10.0F, .y = 10.0F, .width = 20.0F, .height = 20.0F},
            {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F}),
    };
    auto builder = createBuilder({.commandCount = 1, .clipCount = 0, .batchCount = 1});
    auto result = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {100.0F, 100.0F}),
        {.framebufferViewport = {0, 0, 100, 100}});
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    ASSERT_EQ(result->displayList.commands().size(), 1U);
    EXPECT_FALSE(result->displayList.commands().front().clip.hasClip());
    EXPECT_TRUE(result->displayList.clips().empty());
    EXPECT_EQ(result->statistics.redundantClipElisionCount, 1U);
}

TEST_F(UIRenderDisplayListTest, InternsAndResolvesAPartialProjectedClip)
{
    const std::array entries{
        solidEntry(
            7,
            {.x = 10.0F, .y = 10.0F, .width = 50.0F, .height = 50.0F},
            {.x = 20.0F, .y = 20.0F, .width = 10.0F, .height = 10.0F}),
    };
    auto builder = createBuilder({.commandCount = 1, .clipCount = 1, .batchCount = 1});
    auto result = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {100.0F, 100.0F}),
        {.framebufferViewport = {0, 0, 200, 100}});
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    ASSERT_EQ(result->displayList.commands().size(), 1U);
    const Render::UIDrawCommand& command = result->displayList.commands().front();
    EXPECT_EQ(command.bounds, (Render::UIPixelRect{20, 10, 100, 50}));
    ASSERT_TRUE(command.clip.hasClip());
    const Render::UIPixelRect* clip = result->displayList.resolveClip(command.clip);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(*clip, (Render::UIPixelRect{40, 20, 20, 10}));
}

TEST_F(UIRenderDisplayListTest, RejectsDuplicateOrDescendingSourcePaintOrdinals)
{
    auto builder = createBuilder({.commandCount = 4, .clipCount = 4, .batchCount = 4});
    const auto verifyRejected = [&builder](u32 secondOrdinal) {
        const std::array entries{
            solidEntry(5, {0.0F, 0.0F, 10.0F, 10.0F}, {0.0F, 0.0F, 10.0F, 10.0F}),
            solidEntry(secondOrdinal, {10.0F, 0.0F, 10.0F, 10.0F}, {10.0F, 0.0F, 10.0F, 10.0F}),
        };
        auto result = Integration::buildUIDisplayList(
            builder,
            paintView(entries, {20.0F, 10.0F}),
            {.framebufferViewport = {0, 0, 20, 10}});
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Core::CoreErrorCode::InvalidArgument);
        EXPECT_TRUE(builder.publishedView().empty());
    };

    verifyRejected(5);
    verifyRejected(4);
    EXPECT_EQ(builder.statistics().rolledBackBuildCount, 2U);
}

TEST_F(UIRenderDisplayListTest, EmptyLogicalOrFramebufferViewportCommitsAnEmptyList)
{
    const std::array entries{
        solidEntry(1, {0.0F, 0.0F, 10.0F, 10.0F}, {0.0F, 0.0F, 10.0F, 10.0F}),
    };
    auto builder = createBuilder({.commandCount = 1, .clipCount = 1, .batchCount = 1});

    auto noLogicalWidth = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {0.0F, 10.0F}),
        {.framebufferViewport = {0, 0, 100, 100}});
    ASSERT_TRUE(noLogicalWidth.has_value()) << noLogicalWidth.error().message;
    EXPECT_TRUE(noLogicalWidth->displayList.empty());
    EXPECT_EQ(noLogicalWidth->statistics.sourcePaintEntryCount, 1U);
    EXPECT_EQ(noLogicalWidth->statistics.submittedSolidQuadCount, 0U);

    auto noFramebufferWidth = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {10.0F, 10.0F}),
        {.framebufferViewport = {0, 0, 0, 100}});
    ASSERT_TRUE(noFramebufferWidth.has_value()) << noFramebufferWidth.error().message;
    EXPECT_TRUE(noFramebufferWidth->displayList.empty());
}

TEST_F(UIRenderDisplayListTest, RejectsNonFiniteNegativeAndUnrepresentableGeometry)
{
    auto builder = createBuilder({.commandCount = 2, .clipCount = 2, .batchCount = 2});
    const std::array validEntries{
        solidEntry(1, {0.0F, 0.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 1.0F, 1.0F}),
    };

    auto nonFiniteViewport = Integration::buildUIDisplayList(
        builder,
        paintView(
            validEntries,
            {(std::numeric_limits<float>::quiet_NaN)(), 1.0F}),
        {.framebufferViewport = {0, 0, 1, 1}});
    ASSERT_FALSE(nonFiniteViewport.has_value());
    EXPECT_EQ(nonFiniteViewport.error().code, Core::CoreErrorCode::InvalidArgument);

    const std::array negativeExtent{
        solidEntry(1, {0.0F, 0.0F, -1.0F, 1.0F}, {0.0F, 0.0F, 1.0F, 1.0F}),
    };
    auto negative = Integration::buildUIDisplayList(
        builder,
        paintView(negativeExtent, {1.0F, 1.0F}),
        {.framebufferViewport = {0, 0, 1, 1}});
    ASSERT_FALSE(negative.has_value());
    EXPECT_EQ(negative.error().code, Core::CoreErrorCode::InvalidArgument);

    auto overflow = Integration::buildUIDisplayList(
        builder,
        paintView(validEntries, {1.0F, 1.0F}),
        {.framebufferViewport = {
             (std::numeric_limits<i32>::max)(), 0, 2, 1}});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_EQ(builder.statistics().rolledBackBuildCount, 3U);
}

TEST_F(UIRenderDisplayListTest, RejectsNonPremultipliedSourceColorBeforeEmission)
{
    const std::array entries{
        solidEntry(
            1,
            {0.0F, 0.0F, 1.0F, 1.0F},
            {0.0F, 0.0F, 1.0F, 1.0F},
            {.red = 200, .green = 10, .blue = 10, .alpha = 128}),
    };
    auto builder = createBuilder({.commandCount = 1, .clipCount = 1, .batchCount = 1});
    auto result = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {1.0F, 1.0F}),
        {.framebufferViewport = {0, 0, 1, 1}});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_TRUE(builder.publishedView().empty());
    EXPECT_EQ(builder.statistics().rolledBackBuildCount, 1U);
    EXPECT_EQ(builder.statistics().invalidInputFailureCount, 0U);
}

TEST_F(UIRenderDisplayListTest, PrunesTransparentEmptyAndOutsideEntriesWithoutBreakingPaintOrder)
{
    const std::array entries{
        solidEntry(
            2,
            {0.0F, 0.0F, 10.0F, 10.0F},
            {0.0F, 0.0F, 100.0F, 100.0F},
            {}),
        solidEntry(
            4,
            {20.0F, 20.0F, 0.0F, 10.0F},
            {0.0F, 0.0F, 100.0F, 100.0F}),
        solidEntry(
            8,
            {200.0F, 20.0F, 10.0F, 10.0F},
            {0.0F, 0.0F, 100.0F, 100.0F}),
        solidEntry(
            9,
            {30.0F, 30.0F, 10.0F, 10.0F},
            {0.0F, 0.0F, 100.0F, 100.0F}),
    };
    auto builder = createBuilder({.commandCount = 4, .clipCount = 1, .batchCount = 4});
    auto result = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {100.0F, 100.0F}),
        {.framebufferViewport = {0, 0, 100, 100}});

    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    ASSERT_EQ(result->displayList.commands().size(), 1U);
    EXPECT_EQ(result->displayList.commands().front().paintOrdinal, 9U);
    EXPECT_EQ(result->statistics.sourcePaintEntryCount, 4U);
    EXPECT_EQ(result->statistics.submittedSolidQuadCount, 4U);
    EXPECT_EQ(result->statistics.redundantClipElisionCount, 2U);
    EXPECT_EQ(result->displayList.statistics().prunedTransparentCount, 1U);
    EXPECT_EQ(result->displayList.statistics().prunedEmptyBoundsCount, 2U);
}

TEST_F(UIRenderDisplayListTest, CapacityFailureAfterSuccessLeavesNoOldOrTruncatedPublishedView)
{
    auto builder = createBuilder({.commandCount = 1, .clipCount = 0, .batchCount = 1});
    const std::array firstEntries{
        solidEntry(1, {0.0F, 0.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 1.0F, 1.0F}),
    };
    auto first = Integration::buildUIDisplayList(
        builder,
        paintView(firstEntries, {2.0F, 1.0F}),
        {.framebufferViewport = {0, 0, 2, 1}});
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_EQ(first->displayList.commands().size(), 1U);

    const std::array replacementEntries{
        solidEntry(2, {0.0F, 0.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 1.0F, 1.0F}),
        solidEntry(3, {1.0F, 0.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 1.0F, 1.0F}),
    };
    auto replacement = Integration::buildUIDisplayList(
        builder,
        paintView(replacementEntries, {2.0F, 1.0F}),
        {.framebufferViewport = {0, 0, 2, 1}});
    ASSERT_FALSE(replacement.has_value());
    EXPECT_EQ(
        replacement.error().code,
        Render::RenderErrorCode::DisplayListCapacityExceeded);
    EXPECT_TRUE(builder.publishedView().empty());
    EXPECT_EQ(builder.statistics().committedBuildCount, 1U);
    EXPECT_EQ(builder.statistics().rolledBackBuildCount, 1U);
}

TEST_F(UIRenderDisplayListTest, AlreadyOpenBuilderTransactionRemainsOwnedByTheCaller)
{
    auto builder = createBuilder({.commandCount = 2, .clipCount = 1, .batchCount = 2});
    ASSERT_TRUE(builder.beginFrame().has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 9,
                        .bounds = {1, 2, 3, 4},
                        .color = {1, 2, 3, 255},
                    })
                    .has_value());
    const std::array entries{
        solidEntry(1, {0.0F, 0.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 1.0F, 1.0F}),
    };
    auto rejected = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {1.0F, 1.0F}),
        {.framebufferViewport = {0, 0, 1, 1}});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(
        rejected.error().code,
        Render::RenderErrorCode::DisplayListBuildAlreadyOpen);
    EXPECT_EQ(builder.statistics().rolledBackBuildCount, 0U);

    auto callerCommit = builder.commit();
    ASSERT_TRUE(callerCommit.has_value()) << callerCommit.error().message;
    ASSERT_EQ(callerCommit->commands().size(), 1U);
    EXPECT_EQ(callerCommit->commands().front().paintOrdinal, 9U);
}

TEST_F(UIRenderDisplayListTest, ReusesFixedBuilderStorageForThreeHundredBuilds)
{
    TrackingMemoryResource storage;
    auto builder = createBuilder(
        {.commandCount = 2, .clipCount = 2, .batchCount = 2}, storage);
    const usize allocationCount = storage.allocationCount();
    const std::array entries{
        solidEntry(
            1,
            {.x = 0.25F, .y = 0.25F, .width = 1.0F, .height = 1.0F},
            {.x = 0.0F, .y = 0.0F, .width = 2.0F, .height = 2.0F}),
    };

    for (usize frame = 0; frame < 300; ++frame)
    {
        auto result = Integration::buildUIDisplayList(
            builder,
            paintView(entries, {2.0F, 2.0F}),
            {.framebufferViewport = {0, 0, 200, 100}});
        ASSERT_TRUE(result.has_value())
            << "frame=" << frame << " " << result.error().message;
        ASSERT_EQ(result->displayList.commands().size(), 1U);
    }
    EXPECT_EQ(storage.allocationCount(), allocationCount);
    EXPECT_EQ(builder.statistics().committedBuildCount, 300U);
    EXPECT_EQ(builder.statistics().rolledBackBuildCount, 0U);
}

// UI-003 first slice: content scale is carried only by the logical/framebuffer extent pair
// (see UIRenderViewportMapping). 100%/150%/200% DPI-like mappings must be deterministic.
TEST_F(UIRenderDisplayListTest, ContentScale100_150_200PercentMapsLogicalRectDeterministically)
{
    // Logical design rect: x=10,y=20,w=40,h=30 in a 100x100 logical viewport.
    const std::array entries{
        solidEntry(
            1,
            {.x = 10.0F, .y = 20.0F, .width = 40.0F, .height = 30.0F},
            {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F}),
    };
    auto builder = createBuilder({.commandCount = 4, .clipCount = 4, .batchCount = 4});

    struct Case final {
        const char* label = nullptr;
        u32 framebufferW = 0;
        u32 framebufferH = 0;
        Render::UIPixelRect expected{};
    };
    // scale = framebuffer / logical. Clip covers full viewport so elides.
    const std::array cases{
        Case{"100%", 100, 100, {10, 20, 40, 30}},
        Case{"150%", 150, 150, {15, 30, 60, 45}},
        Case{"200%", 200, 200, {20, 40, 80, 60}},
    };

    for (const Case& c : cases)
    {
        auto result = Integration::buildUIDisplayList(
            builder,
            paintView(entries, {100.0F, 100.0F}),
            {.framebufferViewport = {0, 0, c.framebufferW, c.framebufferH}});
        ASSERT_TRUE(result.has_value()) << c.label << " " << (result ? "" : result.error().message);
        ASSERT_EQ(result->displayList.commands().size(), 1U) << c.label;
        EXPECT_EQ(result->displayList.commands().front().bounds, c.expected) << c.label;
        EXPECT_EQ(result->statistics.submittedSolidQuadCount, 1U) << c.label;
    }
}

TEST_F(UIRenderDisplayListTest, ContentScale150PercentProjectsPartialClip)
{
    // Logical: command 0..100, clip 20..40 (10x10). At 150%: command 0..150, clip 30..60 (15x15).
    const std::array entries{
        solidEntry(
            1,
            {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F},
            {.x = 20.0F, .y = 20.0F, .width = 10.0F, .height = 10.0F}),
    };
    auto builder = createBuilder({.commandCount = 1, .clipCount = 1, .batchCount = 1});
    auto result = Integration::buildUIDisplayList(
        builder,
        paintView(entries, {100.0F, 100.0F}),
        {.framebufferViewport = {0, 0, 150, 150}});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->displayList.commands().size(), 1U);
    const Render::UIDrawCommand& command = result->displayList.commands().front();
    EXPECT_EQ(command.bounds, (Render::UIPixelRect{0, 0, 150, 150}));
    ASSERT_TRUE(command.clip.hasClip());
    const Render::UIPixelRect* clip = result->displayList.resolveClip(command.clip);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(*clip, (Render::UIPixelRect{30, 30, 15, 15}));
}

TEST_F(UIRenderDisplayListTest, ResolvesAndDeduplicatesImageResourcesWithExactUvSamplingAndBatch)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    const Render::FrameResourceRef texture = internTexture(packet, 81);
    ASSERT_TRUE(texture.hasValue());

    ImageResolverState resolverState{
        .root = root.rootNodeId(),
        .texture = texture,
    };
    resolverState.initializeResolver();
    const Core::AssetId asset = imageAsset();
    const std::array entries{
        imageEntry(1, root.rootNodeId(), asset),
        imageEntry(2, root.rootNodeId(), asset),
    };
    std::array<Integration::UIRenderImageResolutionCacheEntry, 2> cache{};
    auto builder = createBuilder({.commandCount = 2, .clipCount = 0, .batchCount = 2});

    auto result = Integration::buildUIDisplayList(
        builder, paintView(entries, {100.0F, 100.0F}), {.framebufferViewport = {0, 0, 200, 100}},
        {
            .resourceSink = &packet.resourceSink(),
            .resolverLookup = {.userData = &resolverState, .find = &findImageResolver},
            .cache = cache,
        });
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    EXPECT_EQ(resolverState.callCount, 1U);
    EXPECT_EQ(packet.resourceCount(), 1U);
    EXPECT_EQ(result->statistics.resolvedImageResourceCount, 1U);
    EXPECT_EQ(result->statistics.submittedImageQuadCount, 2U);
    ASSERT_EQ(result->displayList.commands().size(), 2U);
    const Render::UIDrawCommand& first = result->displayList.commands().front();
    EXPECT_EQ(first.kind, Render::UIDrawCommandKind::ImageQuad);
    EXPECT_EQ(first.texture, texture);
    EXPECT_EQ(first.resourceOrdinal, 0U);
    EXPECT_EQ(first.uv, (Render::UINormalizedUvRect{.u0 = 0.125F, .v0 = 0.125F, .u1 = 0.625F, .v1 = 0.625F}));
    EXPECT_EQ(first.sampling, Render::UITextureSampling::Nearest);
    ASSERT_EQ(result->displayList.batches().size(), 1U);
    EXPECT_EQ(result->displayList.batches().front().kind, Render::UIDrawCommandKind::ImageQuad);
    EXPECT_EQ(result->displayList.batches().front().texture, texture);
    EXPECT_EQ(result->displayList.batches().front().sampling, Render::UITextureSampling::Nearest);
    EXPECT_EQ(result->displayList.batches().front().commandCount, 2U);
}

TEST_F(UIRenderDisplayListTest, SkipsImagesWithoutAResolverWhenUnavailableOrWithMismatchedExtent)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const Core::AssetId asset = imageAsset();
    const std::array entries{imageEntry(1, root.rootNodeId(), asset)};
    std::array<Integration::UIRenderImageResolutionCacheEntry, 1> cache{};
    auto builder = createBuilder({.commandCount = 1, .clipCount = 0, .batchCount = 1});

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    auto missingResolver = Integration::buildUIDisplayList(
        builder, paintView(entries, {100.0F, 100.0F}), {.framebufferViewport = {0, 0, 100, 100}},
        {.resourceSink = &packet.resourceSink(), .cache = cache});
    ASSERT_TRUE(missingResolver.has_value()) << (missingResolver ? "" : missingResolver.error().message);
    EXPECT_TRUE(missingResolver->displayList.empty());
    EXPECT_EQ(missingResolver->statistics.skippedImageMissingResolverCount, 1U);

    ImageResolverState unavailable{
        .root = root.rootNodeId(),
        .available = false,
    };
    unavailable.initializeResolver();
    auto unavailableBuild = Integration::buildUIDisplayList(
        builder, paintView(entries, {100.0F, 100.0F}), {.framebufferViewport = {0, 0, 100, 100}},
        {
            .resourceSink = &packet.resourceSink(),
            .resolverLookup = {.userData = &unavailable, .find = &findImageResolver},
            .cache = cache,
        });
    ASSERT_TRUE(unavailableBuild.has_value()) << (unavailableBuild ? "" : unavailableBuild.error().message);
    EXPECT_TRUE(unavailableBuild->displayList.empty());
    EXPECT_EQ(unavailableBuild->statistics.skippedImageUnavailableCount, 1U);

    const Render::FrameResourceRef texture = internTexture(packet, 82);
    ASSERT_TRUE(texture.hasValue());
    ImageResolverState mismatch{
        .root = root.rootNodeId(),
        .texture = texture,
        .pixelWidth = 64,
        .pixelHeight = 16,
    };
    mismatch.initializeResolver();
    auto mismatchBuild = Integration::buildUIDisplayList(
        builder, paintView(entries, {100.0F, 100.0F}), {.framebufferViewport = {0, 0, 100, 100}},
        {
            .resourceSink = &packet.resourceSink(),
            .resolverLookup = {.userData = &mismatch, .find = &findImageResolver},
            .cache = cache,
        });
    ASSERT_TRUE(mismatchBuild.has_value()) << (mismatchBuild ? "" : mismatchBuild.error().message);
    EXPECT_TRUE(mismatchBuild->displayList.empty());
    EXPECT_EQ(mismatchBuild->statistics.resolvedImageResourceCount, 1U);
    EXPECT_EQ(mismatchBuild->statistics.skippedImageExtentMismatchCount, 1U);
}

TEST_F(UIRenderDisplayListTest, ResolverAndSinkFailuresRollBackTheDisplayList)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const Core::AssetId asset = imageAsset();
    const std::array entries{imageEntry(1, root.rootNodeId(), asset)};
    std::array<Integration::UIRenderImageResolutionCacheEntry, 1> cache{};
    auto builder = createBuilder({.commandCount = 1, .clipCount = 0, .batchCount = 1});

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    ImageResolverState resolverFailure{
        .root = root.rootNodeId(),
        .fail = true,
    };
    resolverFailure.initializeResolver();
    auto failedResolverBuild = Integration::buildUIDisplayList(
        builder, paintView(entries, {100.0F, 100.0F}), {.framebufferViewport = {0, 0, 100, 100}},
        {
            .resourceSink = &packet.resourceSink(),
            .resolverLookup = {.userData = &resolverFailure, .find = &findImageResolver},
            .cache = cache,
        });
    ASSERT_FALSE(failedResolverBuild.has_value());
    EXPECT_EQ(failedResolverBuild.error().code, Core::CoreErrorCode::Internal);
    EXPECT_TRUE(builder.publishedView().empty());

    RejectingFrameResourceSink rejectingSink;
    ImageResolverState sinkFailure{
        .root = root.rootNodeId(),
        .internThroughSink = true,
        .sinkBindingKey = 83,
    };
    sinkFailure.initializeResolver();
    auto failedSinkBuild = Integration::buildUIDisplayList(
        builder, paintView(entries, {100.0F, 100.0F}), {.framebufferViewport = {0, 0, 100, 100}},
        {
            .resourceSink = &rejectingSink,
            .resolverLookup = {.userData = &sinkFailure, .find = &findImageResolver},
            .cache = cache,
        });
    ASSERT_FALSE(failedSinkBuild.has_value());
    EXPECT_EQ(failedSinkBuild.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(rejectingSink.internCount, 1U);
    EXPECT_TRUE(builder.publishedView().empty());
}

TEST_F(UIRenderDisplayListTest, RejectsMalformedImageMetadataBeforeResolverInvocation)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const Core::AssetId asset = imageAsset();

    ImageResolverState resolverState{
        .root = root.rootNodeId(),
    };
    resolverState.initializeResolver();
    std::array<Integration::UIRenderImageResolutionCacheEntry, 1> cache{};
    auto builder = createBuilder({.commandCount = 1, .clipCount = 0, .batchCount = 1});
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());

    std::array malformedEntries{
        imageEntry(1, root.rootNodeId(), asset),
        imageEntry(1, root.rootNodeId(), asset),
        imageEntry(1, root.rootNodeId(), asset),
        imageEntry(1, root.rootNodeId(), asset),
    };
    malformedEntries[0].imageSource.sourcePixels.width = 64;
    malformedEntries[1].imageSource.texturePixelExtent.width = 0;
    malformedEntries[2].imageSource.intrinsicLogicalSize.width =
        (std::numeric_limits<float>::quiet_NaN)();
    malformedEntries[3].imageSampling = static_cast<UI::UIImageSampling>(255);

    for (const UI::UICommittedPaintEntry& malformed : malformedEntries)
    {
        const std::array entries{malformed};
        auto result = Integration::buildUIDisplayList(
            builder, paintView(entries, {100.0F, 100.0F}),
            {.framebufferViewport = {0, 0, 100, 100}},
            {
                .resourceSink = &packet.resourceSink(),
                .resolverLookup = {.userData = &resolverState, .find = &findImageResolver},
                .cache = cache,
            });
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Core::CoreErrorCode::InvalidArgument);
        EXPECT_TRUE(builder.publishedView().empty());
    }
    EXPECT_EQ(resolverState.callCount, 0U);
    EXPECT_EQ(packet.resourceCount(), 0U);
    EXPECT_EQ(builder.statistics().rolledBackBuildCount, malformedEntries.size());
}

} // namespace
} // namespace Tina::Tests
