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
    auto rootResult = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    auto imageResult = updater.createElement(
        root.rootNodeId(), UI::makeImageElement(imageContent(), "Inventory icon", fixedSize(100.0F, 100.0F)));
    ASSERT_TRUE(imageResult.has_value()) << imageResult.error().message;
    const UI::UINodeId imageNode = *imageResult;

    const Core::Status committed = context->publication().commitLayout({.width = 320.0F, .height = 200.0F});
    ASSERT_TRUE(committed.has_value()) << committed.error().message;

    const UI::UICommittedPaintView paint = context->publication().committedPaint();
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

    const UI::UICommittedSemanticsView semantics = context->publication().committedSemantics();
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
    constexpr UI::UIElementDescriptor icon = UI::makeIconElement(UI::UIIconContent{});
    EXPECT_EQ(icon.semantics.mode, UI::UISemanticsMode::Exclude);
    ASSERT_TRUE(icon.pointerHitPolicy.has_value());
    EXPECT_EQ(*icon.pointerHitPolicy, UI::UIPointerHitPolicy::Ignore);
    ASSERT_TRUE(icon.image.has_value());
    EXPECT_EQ(icon.image->fit, UI::UIImageFit::Contain);
    EXPECT_EQ(icon.contentAlignment.horizontal, UI::UIAxisAlignment::Center);
    EXPECT_EQ(icon.contentAlignment.vertical, UI::UIAxisAlignment::Center);
}

TEST_F(UIImageTest, ImageButtonsOwnControlBehaviorPaintAndSemanticsOnOneNode)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor buttonDescriptor =
        UI::makeButtonElement({}, fixedSize(40.0F, 40.0F));
    buttonDescriptor.text.reset();
    buttonDescriptor.image = imageContent();
    const usize baselineNodes = context->liveNodeCount();
    const auto unnamedButton =
        updater.createElement(root.rootNodeId(), buttonDescriptor);
    ASSERT_FALSE(unnamedButton.has_value());
    EXPECT_EQ(unnamedButton.error().code,
              UI::UIErrorCode::InvalidElementDescriptor);
    EXPECT_EQ(context->liveNodeCount(), baselineNodes);

    buttonDescriptor.semantics.name = "Refresh";
    buttonDescriptor.semantics.useContentAsName = false;
    const auto button = updater.createElement(root.rootNodeId(), buttonDescriptor);
    ASSERT_TRUE(button.has_value()) << button.error().message;

    UI::UIElementDescriptor radioDescriptor =
        UI::makeRadioButtonElement({}, fixedSize(40.0F, 40.0F));
    radioDescriptor.text.reset();
    radioDescriptor.image = imageContent();
    radioDescriptor.visual.styleRole = UI::UIStyleRoleId::SegmentedButton;
    radioDescriptor.semantics.name = "Move";
    radioDescriptor.semantics.useContentAsName = false;
    const auto radio = updater.createElement(root.rootNodeId(), radioDescriptor);
    ASSERT_TRUE(radio.has_value()) << radio.error().message;

    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    const UI::UICommittedPaintView paint = context->publication().committedPaint();
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();
    for (const UI::UINodeId node : {*button, *radio})
    {
        const auto image = std::ranges::find_if(
            paint, [node](const UI::UICommittedPaintEntry& entry) {
                return entry.node == node &&
                       entry.kind == UI::UICommittedPaintKind::Image;
            });
        ASSERT_NE(image, paint.end());
        const auto placement = std::ranges::find_if(
            layout, [node](const UI::UICommittedLayoutEntry& entry) {
                return entry.node == node;
            });
        ASSERT_NE(placement, layout.end());
        const UI::UILogicalRect contentBox = placement->contentPlacement.contentBox;
        EXPECT_GT(image->worldRect.width, 0.0F);
        EXPECT_GT(image->worldRect.height, 0.0F);
        EXPECT_GE(image->worldRect.x, contentBox.x);
        EXPECT_GE(image->worldRect.y, contentBox.y);
        EXPECT_LE(image->worldRect.right(), contentBox.right());
        EXPECT_LE(image->worldRect.bottom(), contentBox.bottom());
        EXPECT_FLOAT_EQ(image->worldRect.x + image->worldRect.width * 0.5F,
                        contentBox.x + contentBox.width * 0.5F);
        EXPECT_FLOAT_EQ(image->worldRect.y + image->worldRect.height * 0.5F,
                        contentBox.y + contentBox.height * 0.5F);
    }

    const UI::UICommittedSemanticsView semantics = context->publication().committedSemantics();
    const auto buttonSemantics = std::ranges::find_if(
        semantics, [button](const UI::UISemanticsEntry& entry) {
            return entry.node == *button;
        });
    ASSERT_NE(buttonSemantics, semantics.end());
    EXPECT_EQ(buttonSemantics->role, UI::UISemanticsRole::Button);
    EXPECT_EQ(buttonSemantics->name, "Refresh");

    const auto radioSemantics = std::ranges::find_if(
        semantics, [radio](const UI::UISemanticsEntry& entry) {
            return entry.node == *radio;
        });
    ASSERT_NE(radioSemantics, semantics.end());
    EXPECT_EQ(radioSemantics->role, UI::UISemanticsRole::RadioButton);
    EXPECT_EQ(radioSemantics->name, "Move");
}

TEST_F(UIImageTest, IconRecipeReusesImageStorageAndPaintResourceChain)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const UI::UIIconContent content{
        .source = imageContent().source,
        .tint = UI::rgba8(24, 48, 96, 192),
        .sampling = UI::UIImageSampling::Nearest,
        .alignment = {
            .horizontal = UI::UIAxisAlignment::End,
            .vertical = UI::UIAxisAlignment::Start,
        },
    };
    const auto icon = updater.createElement(
        root.rootNodeId(), UI::makeIconElement(content, fixedSize(100.0F, 100.0F)));
    ASSERT_TRUE(icon.has_value()) << icon.error().message;
    ASSERT_TRUE(context->publication().commitLayout({.width = 320.0F, .height = 200.0F}).has_value());

    const UI::UICommittedPaintView paint = context->publication().committedPaint();
    const auto entry = std::ranges::find_if(
        paint, [icon](const UI::UICommittedPaintEntry& candidate) {
            return candidate.node == *icon;
        });
    ASSERT_NE(entry, paint.end());
    EXPECT_EQ(entry->kind, UI::UICommittedPaintKind::Image);
    EXPECT_EQ(entry->imageSource, content.source);
    EXPECT_EQ(entry->imageSampling, content.sampling);
    EXPECT_EQ(entry->solidFill, UI::premultiply(content.tint));
    EXPECT_FLOAT_EQ(entry->worldRect.x, 0.0F);
    EXPECT_FLOAT_EQ(entry->worldRect.y, 0.0F);
    EXPECT_FLOAT_EQ(entry->worldRect.width, 100.0F);
    EXPECT_FLOAT_EQ(entry->worldRect.height, 50.0F);

    const UI::UICommittedLayoutView layout = context->publication().committedLayout();
    const auto layoutEntry = std::ranges::find_if(
        layout, [icon](const UI::UICommittedLayoutEntry& candidate) {
            return candidate.node == *icon;
        });
    ASSERT_NE(layoutEntry, layout.end());
    EXPECT_TRUE(layoutEntry->contentPlacement.hasIntrinsicContent);
    EXPECT_FLOAT_EQ(layoutEntry->contentPlacement.origin.x, 60.0F);
    EXPECT_FLOAT_EQ(layoutEntry->contentPlacement.origin.y, 0.0F);
    EXPECT_FLOAT_EQ(layoutEntry->contentPlacement.intrinsicSize.width, 40.0F);
    EXPECT_FLOAT_EQ(layoutEntry->contentPlacement.intrinsicSize.height, 20.0F);

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.activeImageContentCount, 1U);
    EXPECT_EQ(statistics.imageContentHighWater, 1U);

    const UI::UICommittedSemanticsView semantics = context->publication().committedSemantics();
    EXPECT_EQ(std::ranges::find_if(
                  semantics, [icon](const UI::UISemanticsEntry& candidate) {
                      return candidate.node == *icon;
                  }),
              semantics.end());
}

TEST_F(UIImageTest, InvalidImageAndMixedTextFailWithoutPublishingNodes)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->authoring().treeUpdater(root);
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

TEST_F(UIImageTest, RuntimeImageTintIsPaintOnlyAndNoOpPreservesDirty)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const auto image = updater.createElement(
        root.rootNodeId(), UI::makeImageElement(imageContent(), "Tinted", fixedSize(80.0F, 40.0F)));
    ASSERT_TRUE(image.has_value()) << image.error().message;
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    const UI::UIContextStatistics afterCommit = context->statistics();
    EXPECT_FALSE(afterCommit.layoutDirty);
    EXPECT_FALSE(afterCommit.hitDirty);
    EXPECT_FALSE(afterCommit.paintDirty);

    const auto initialTint = updater.imageTint(*image);
    ASSERT_TRUE(initialTint.has_value()) << initialTint.error().message;
    EXPECT_EQ(*initialTint, UI::rgba8(255, 128, 64, 200));

    ASSERT_TRUE(updater.setImageTint(*image, UI::rgba8(255, 128, 64, 200)).has_value());
    const UI::UIContextStatistics afterNoOp = context->statistics();
    EXPECT_FALSE(afterNoOp.layoutDirty);
    EXPECT_FALSE(afterNoOp.hitDirty);
    EXPECT_FALSE(afterNoOp.paintDirty);
    EXPECT_EQ(afterNoOp.paintRevision, afterCommit.paintRevision);

    ASSERT_TRUE(updater.setImageTint(*image, UI::rgba8(16, 32, 48, 255)).has_value());
    const UI::UIContextStatistics afterTint = context->statistics();
    EXPECT_FALSE(afterTint.layoutDirty);
    EXPECT_FALSE(afterTint.hitDirty);
    EXPECT_TRUE(afterTint.paintDirty);
    // markPaintDirty also dirties Semantics phase (shared paint invalidation path).

    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    const UI::UICommittedPaintView paint = context->publication().committedPaint();
    const auto paintEntry = std::ranges::find_if(
        paint, [image](const UI::UICommittedPaintEntry& entry) { return entry.node == *image; });
    ASSERT_NE(paintEntry, paint.end());
    EXPECT_EQ(paintEntry->kind, UI::UICommittedPaintKind::Image);
    EXPECT_EQ(paintEntry->solidFill, UI::premultiply(UI::rgba8(16, 32, 48, 255)));
    EXPECT_EQ(*updater.imageTint(*image), UI::rgba8(16, 32, 48, 255));

    const auto missing = updater.imageTint(root.rootNodeId());
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, UI::UIErrorCode::InvalidElementDescriptor);
    const Core::Status rejected = updater.setImageTint(root.rootNodeId(), UI::rgba8(1, 2, 3, 4));
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);
}

TEST_F(UIImageTest, CapacityFailureRollsBackNodeAndStorage)
{
    auto context = createContext(1);
    ASSERT_NE(context, nullptr);
    auto rootResult = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->authoring().treeUpdater(root);
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
