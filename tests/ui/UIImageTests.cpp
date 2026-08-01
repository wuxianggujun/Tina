#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <memory>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] Core::AssetId imageAssetId()
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x42};
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] UI::UIImageContent imageContent(UI::UIImageFit fit = UI::UIImageFit::Contain)
{
    return UI::UIImageContent{
        .source = {
            .texture = imageAssetId(),
            .sourcePixels = {.x = 4, .y = 8, .width = 40, .height = 20},
            .texturePixelExtent = {.width = 64, .height = 64},
            .intrinsicLogicalSize = {.width = 40.0F, .height = 20.0F},
        },
        .fit = fit,
        .alignment = {
            .horizontal = UI::UIAxisAlignment::Center,
            .vertical = UI::UIAxisAlignment::Center,
        },
        .tint = UI::rgba8(255, 128, 64, 200),
        .sampling = UI::UIImageSampling::Nearest,
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

class UIImageTest : public testing::Test {
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

    [[nodiscard]] std::unique_ptr<UI::UIContext> createContext(usize imageCapacity = 2)
    {
        UI::UIContextCapacityConfig capacities{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .layoutSnapshotCapacity = 8,
            .paintSnapshotCapacity = 8,
            .imageContentCapacity = imageCapacity,
            .textByteCapacity = 128,
            .applyDefaultProductChrome = false,
        };
        auto context = UI::UIContext::Create(window, capacities);
        EXPECT_TRUE(context.has_value()) << (context ? "" : context.error().message);
        return context ? std::move(*context) : nullptr;
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UIImageTest, ImageRecipePublishesIntrinsicLayoutPaintAndSemantics)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    auto imageResult = updater.createElement(
        root.rootNodeId(), UI::makeImageElement(imageContent(), "Inventory icon", fixedSize(100.0F, 100.0F)));
    ASSERT_TRUE(imageResult.has_value()) << imageResult.error().message;
    const UI::UINodeId imageNode = *imageResult;

    const Core::Status committed = context->commitLayout({.width = 320.0F, .height = 200.0F});
    ASSERT_TRUE(committed.has_value()) << committed.error().message;

    const UI::UICommittedPaintView paint = context->committedPaint();
    const auto paintEntry = std::ranges::find_if(
        paint, [imageNode](const UI::UICommittedPaintEntry& entry) {
            return entry.node == imageNode;
        });
    ASSERT_NE(paintEntry, paint.end());
    EXPECT_EQ(paintEntry->kind, UI::UICommittedPaintKind::Image);
    EXPECT_EQ(paintEntry->root, root.rootNodeId());
    EXPECT_EQ(paintEntry->imageSource.texture, imageAssetId());
    EXPECT_EQ(paintEntry->imageSampling, UI::UIImageSampling::Nearest);
    EXPECT_FLOAT_EQ(paintEntry->worldRect.width, 100.0F);
    EXPECT_FLOAT_EQ(paintEntry->worldRect.height, 50.0F);
    EXPECT_FLOAT_EQ(paintEntry->worldRect.y, 25.0F);

    const UI::UICommittedSemanticsView semantics = context->committedSemantics();
    const auto semanticsEntry = std::ranges::find_if(
        semantics, [imageNode](const UI::UISemanticsEntry& entry) {
            return entry.node == imageNode;
        });
    ASSERT_NE(semanticsEntry, semantics.end());
    EXPECT_EQ(semanticsEntry->role, UI::UISemanticsRole::Image);
    EXPECT_EQ(semanticsEntry->name, "Inventory icon");

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.imageContentCapacity, 2U);
    EXPECT_EQ(statistics.activeImageContentCount, 1U);
    EXPECT_EQ(statistics.imageContentHighWater, 1U);
}

TEST_F(UIImageTest, IconRecipeIsDecorativeAndIgnoresPointerHits)
{
    constexpr UI::UIElementDescriptor icon = UI::makeIconElement(UI::UIImageContent{});
    EXPECT_EQ(icon.semantics.mode, UI::UISemanticsMode::Exclude);
    ASSERT_TRUE(icon.pointerHitPolicy.has_value());
    EXPECT_EQ(*icon.pointerHitPolicy, UI::UIPointerHitPolicy::Ignore);
    ASSERT_TRUE(icon.image.has_value());
    EXPECT_EQ(icon.image->fit, UI::UIImageFit::Contain);
}

TEST_F(UIImageTest, InvalidImageAndMixedTextFailWithoutPublishingNodes)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    const usize baseline = context->liveNodeCount();

    UI::UIImageContent invalid = imageContent();
    invalid.source.sourcePixels.width = 128;
    const auto invalidResult = updater.createElement(
        root.rootNodeId(), UI::makeImageElement(invalid, "Invalid"));
    ASSERT_FALSE(invalidResult.has_value());
    EXPECT_EQ(invalidResult.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    UI::UIElementDescriptor mixed = UI::makeImageElement(imageContent(), "Mixed");
    mixed.text = "not allowed";
    const auto mixedResult = updater.createElement(root.rootNodeId(), mixed);
    ASSERT_FALSE(mixedResult.has_value());
    EXPECT_EQ(mixedResult.error().code, UI::UIErrorCode::InvalidElementDescriptor);
    EXPECT_EQ(context->liveNodeCount(), baseline);
    EXPECT_EQ(context->statistics().activeImageContentCount, 0U);
}

TEST_F(UIImageTest, CapacityFailureRollsBackNodeAndStorage)
{
    auto context = createContext(1);
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const auto first = updater.createElement(
        root.rootNodeId(), UI::makeImageElement(imageContent(), "First"));
    ASSERT_TRUE(first.has_value());
    const usize liveWithFirst = context->liveNodeCount();
    const auto second = updater.createElement(
        root.rootNodeId(), UI::makeImageElement(imageContent(), "Second"));
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->liveNodeCount(), liveWithFirst);
    EXPECT_EQ(context->statistics().activeImageContentCount, 1U);

    ASSERT_TRUE(updater.destroy(*first).has_value());
    const auto replacement = updater.createElement(
        root.rootNodeId(), UI::makeImageElement(imageContent(), "Replacement"));
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    EXPECT_EQ(context->statistics().activeImageContentCount, 1U);
}

} // namespace
} // namespace Tina::Tests
