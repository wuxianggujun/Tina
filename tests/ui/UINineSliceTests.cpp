#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <vector>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] Core::AssetId nineSliceAsset()
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x73};
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] UI::UIImageSource imageSource(u32 width = 30, u32 height = 30)
{
    return UI::UIImageSource{
        .texture = nineSliceAsset(),
        .sourcePixels = {.x = 4, .y = 6, .width = width, .height = height},
        .texturePixelExtent = {.width = 64, .height = 64},
        .intrinsicLogicalSize = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
    };
}

[[nodiscard]] UI::UICanvasCommand nineSliceCommand(
    UI::UILogicalRect bounds = {.x = 10.0F, .y = 20.0F, .width = 60.0F, .height = 40.0F},
    UI::UIImagePixelInsets sourceInsets = {.left = 5, .top = 6, .right = 7, .bottom = 8},
    UI::UIEdgeSpacing destinationInsets = {.left = 10.0F, .top = 12.0F, .right = 14.0F, .bottom = 16.0F})
{
    return UI::UICanvasCommand{
        .kind = UI::UICanvasCommandKind::NineSlice,
        .bounds = bounds,
        .color = UI::rgba8(240, 160, 80, 192),
        .imageSource = imageSource(),
        .imageSourceInsets = sourceInsets,
        .imageDestinationInsets = destinationInsets,
        .imageSampling = UI::UIImageSampling::Nearest,
    };
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height)
{
    UI::UILayoutStyle layout{};
    layout.size = {
        .width = UI::UILayoutLength::Px(width),
        .height = UI::UILayoutLength::Px(height),
    };
    return layout;
}

[[nodiscard]] std::vector<const UI::UICommittedPaintEntry*> paintsFor(
    UI::UICommittedPaintView paint, UI::UINodeId node)
{
    std::vector<const UI::UICommittedPaintEntry*> result;
    for (const UI::UICommittedPaintEntry& entry : paint)
    {
        if (entry.node == node)
        {
            result.push_back(&entry);
        }
    }
    return result;
}

class UINineSliceTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    [[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
        UI::UIContextCapacityConfig capacities = {
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 16,
            .canvasCommandCapacity = 4,
        })
    {
        capacities.applyDefaultProductChrome = false;
        auto context = UI::UIContext::Create(window, capacities);
        EXPECT_TRUE(context.has_value()) << (context ? "" : context.error().message);
        return context ? std::move(*context) : nullptr;
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UINineSliceTest, CanvasImagePublishesThroughTheCommittedImagePath)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const UI::UICanvasCommand image{
        .kind = UI::UICanvasCommandKind::Image,
        .bounds = {.x = 3.0F, .y = 4.0F, .width = 20.0F, .height = 10.0F},
        .color = UI::rgba8(128, 64, 32, 200),
        .imageSource = imageSource(),
        .imageSampling = UI::UIImageSampling::Nearest,
    };
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(40.0F, 30.0F));
    descriptor.visual.canvas = std::span(&image, 1);
    auto element = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(element.has_value()) << element.error().message;
    ASSERT_TRUE(context->commitLayout({100.0F, 100.0F}).has_value());

    const auto entries = paintsFor(context->committedPaint(), *element);
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0]->kind, UI::UICommittedPaintKind::Image);
    EXPECT_EQ(entries[0]->root, root.rootNodeId());
    EXPECT_EQ(entries[0]->worldRect, (UI::UILogicalRect{.x = 3.0F, .y = 4.0F, .width = 20.0F, .height = 10.0F}));
    EXPECT_EQ(entries[0]->solidFill, UI::premultiply(image.color));
    EXPECT_EQ(entries[0]->imageSource, image.imageSource);
    EXPECT_EQ(entries[0]->imageSampling, UI::UIImageSampling::Nearest);
    EXPECT_EQ(entries[0]->imageBoundsProjection, UI::UICommittedImageBoundsProjection::Cover);
    EXPECT_EQ(entries[0]->imageProjectionEnd, UI::UILogicalPoint{});
}

TEST_F(UINineSliceTest, FullNineSliceExpandsToNineCommittedImagesInRowMajorOrder)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const UI::UICanvasCommand command = nineSliceCommand();
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(80.0F, 70.0F));
    descriptor.visual.canvas = std::span(&command, 1);
    auto element = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(element.has_value()) << element.error().message;
    ASSERT_TRUE(context->commitLayout({100.0F, 100.0F}).has_value());

    const auto entries = paintsFor(context->committedPaint(), *element);
    ASSERT_EQ(entries.size(), 9U);
    constexpr std::array<float, 4> DestinationX{10.0F, 20.0F, 56.0F, 70.0F};
    constexpr std::array<float, 4> DestinationY{20.0F, 32.0F, 44.0F, 60.0F};
    constexpr std::array<u32, 4> SourceX{4, 9, 27, 34};
    constexpr std::array<u32, 4> SourceY{6, 12, 28, 36};
    for (usize row = 0; row < 3; ++row)
    {
        for (usize column = 0; column < 3; ++column)
        {
            const usize index = row * 3 + column;
            const UI::UICommittedPaintEntry& entry = *entries[index];
            EXPECT_EQ(entry.kind, UI::UICommittedPaintKind::Image);
            EXPECT_EQ(entry.root, root.rootNodeId());
            EXPECT_EQ(entry.solidFill, UI::premultiply(command.color));
            EXPECT_EQ(entry.imageSampling, UI::UIImageSampling::Nearest);
            EXPECT_EQ(entry.imageBoundsProjection, UI::UICommittedImageBoundsProjection::SharedBoundary);
            EXPECT_EQ(entry.imageProjectionEnd,
                      (UI::UILogicalPoint{.x = DestinationX[column + 1], .y = DestinationY[row + 1]}));
            EXPECT_EQ(
                entry.worldRect,
                (UI::UILogicalRect{
                    .x = DestinationX[column],
                    .y = DestinationY[row],
                    .width = DestinationX[column + 1] - DestinationX[column],
                    .height = DestinationY[row + 1] - DestinationY[row],
                }));
            EXPECT_EQ(
                entry.imageSource.sourcePixels,
                (UI::UIImagePixelRect{
                    .x = SourceX[column],
                    .y = SourceY[row],
                    .width = SourceX[column + 1] - SourceX[column],
                    .height = SourceY[row + 1] - SourceY[row],
                }));
            if (column > 0)
            {
                EXPECT_FLOAT_EQ(entries[index - 1]->imageProjectionEnd.x, entry.worldRect.x);
            }
            if (row > 0)
            {
                EXPECT_FLOAT_EQ(entries[index - 3]->imageProjectionEnd.y, entry.worldRect.y);
            }
            if (index > 0)
            {
                EXPECT_EQ(entry.paintOrdinal, entries[index - 1]->paintOrdinal + 1U);
            }
        }
    }
}

TEST_F(UINineSliceTest, ZeroAreaSourceRowsAndColumnsAreEliminated)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UICanvasCommand command = nineSliceCommand(
        {.width = 30.0F, .height = 30.0F},
        {.left = 0, .top = 5, .right = 5, .bottom = 5},
        UI::UIEdgeSpacing::All(5.0F));
    command.imageSource = imageSource(10, 10);
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(30.0F, 30.0F));
    descriptor.visual.canvas = std::span(&command, 1);
    auto element = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(element.has_value()) << element.error().message;
    ASSERT_TRUE(context->commitLayout({40.0F, 40.0F}).has_value());

    const auto entries = paintsFor(context->committedPaint(), *element);
    ASSERT_EQ(entries.size(), 4U);
    for (const UI::UICommittedPaintEntry* entry : entries)
    {
        EXPECT_GT(entry->worldRect.width, 0.0F);
        EXPECT_GT(entry->worldRect.height, 0.0F);
        EXPECT_GT(entry->imageSource.sourcePixels.width, 0U);
        EXPECT_GT(entry->imageSource.sourcePixels.height, 0U);
    }
}

TEST_F(UINineSliceTest, SmallDestinationCompressesFixedEdgesProportionallyAndDropsTheCenter)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const UI::UICanvasCommand command = nineSliceCommand(
        {.x = 2.0F, .y = 3.0F, .width = 10.0F, .height = 30.0F},
        {.left = 10, .top = 10, .right = 10, .bottom = 10},
        {.left = 8.0F, .top = 5.0F, .right = 12.0F, .bottom = 5.0F});
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(20.0F, 40.0F));
    descriptor.visual.canvas = std::span(&command, 1);
    auto element = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(element.has_value()) << element.error().message;
    ASSERT_TRUE(context->commitLayout({40.0F, 50.0F}).has_value());

    const auto entries = paintsFor(context->committedPaint(), *element);
    ASSERT_EQ(entries.size(), 6U);
    for (usize row = 0; row < 3; ++row)
    {
        const UI::UICommittedPaintEntry& left = *entries[row * 2];
        const UI::UICommittedPaintEntry& right = *entries[row * 2 + 1];
        EXPECT_FLOAT_EQ(left.worldRect.x, 2.0F);
        EXPECT_FLOAT_EQ(left.worldRect.width, 4.0F);
        EXPECT_FLOAT_EQ(right.worldRect.x, 6.0F);
        EXPECT_FLOAT_EQ(right.worldRect.width, 6.0F);
        EXPECT_FLOAT_EQ(left.imageProjectionEnd.x, right.worldRect.x);
        EXPECT_EQ(left.imageSource.sourcePixels.x, 4U);
        EXPECT_EQ(right.imageSource.sourcePixels.x, 24U);
    }
}

TEST_F(UINineSliceTest, TransparentTintAndZeroDestinationPublishNoPatches)
{
    auto context = createContext({
        .nodeCapacity = 3,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 1,
        .canvasCommandCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    std::array commands{
        nineSliceCommand(),
        nineSliceCommand({.width = 0.0F, .height = 20.0F}),
    };
    commands[0].color.alpha = 0;
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(80.0F, 70.0F));
    descriptor.visual.canvas = commands;
    ASSERT_TRUE(updater.createElement(root.rootNodeId(), descriptor).has_value());
    ASSERT_TRUE(context->commitLayout({100.0F, 100.0F}).has_value());
    EXPECT_TRUE(context->committedPaint().empty());
}

TEST_F(UINineSliceTest, PaintCapacityFailurePreservesTheOldCommittedSnapshot)
{
    auto context = createContext({
        .nodeCapacity = 3,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 1,
        .canvasCommandCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const UI::UICanvasCommand solid{
        .bounds = {.width = 10.0F, .height = 10.0F},
        .color = UI::rgb(0x336699),
    };
    UI::UIElementDescriptor firstDescriptor = UI::makePanelElement(fixedSize(10.0F, 10.0F));
    firstDescriptor.visual.canvas = std::span(&solid, 1);
    auto first = updater.createElement(root.rootNodeId(), firstDescriptor);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(context->commitLayout({100.0F, 100.0F}).has_value());
    const UI::UICommittedPaintView oldPaint = context->committedPaint();
    ASSERT_EQ(oldPaint.size(), 1U);
    const UI::UICommittedPaintEntry* oldData = oldPaint.entries().data();

    const UI::UICanvasCommand nineSlice = nineSliceCommand();
    UI::UIElementDescriptor secondDescriptor = UI::makePanelElement(fixedSize(80.0F, 70.0F));
    secondDescriptor.visual.canvas = std::span(&nineSlice, 1);
    ASSERT_TRUE(updater.createElement(root.rootNodeId(), secondDescriptor).has_value());
    const Core::Status rejected = context->commitLayout({100.0F, 100.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedPaint().entries().data(), oldData);
    EXPECT_EQ(context->committedPaint().paintRevision(), oldPaint.paintRevision());
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(context->committedPaint().entries().front().node, *first);
}

TEST_F(UINineSliceTest, DestroyAndBuildRollbackRecycleTheRetainedCommandSlot)
{
    auto context = createContext({
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 9,
        .canvasCommandCapacity = 1,
    });
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const UI::UICanvasCommand command = nineSliceCommand();
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(80.0F, 70.0F));
    descriptor.visual.canvas = std::span(&command, 1);
    auto transactionResult = updater.beginBuildTransaction(
        root.rootNodeId(), descriptor,
        {.nodes = 1, .canvasCommands = 1});
    ASSERT_TRUE(transactionResult.has_value()) << transactionResult.error().message;
    UI::UIElementBuildTransaction transaction = std::move(*transactionResult);
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 1U);
    transaction.reset();
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 0U);

    auto replacement = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 1U);
    ASSERT_TRUE(updater.destroy(*replacement).has_value());
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 0U);
    EXPECT_EQ(context->statistics().canvasCommandHighWater, 1U);
}

} // namespace
} // namespace Tina::Tests
