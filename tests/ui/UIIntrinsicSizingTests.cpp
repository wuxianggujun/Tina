#include "UILayoutTestSupport.hpp"

#include <gtest/gtest.h>

namespace Tina::Tests {
namespace {

using namespace UILayoutTestSupport;

constexpr float TextAdvance = 16.0F * 0.6F;
constexpr float TextLineHeight = 16.0F * 1.2F;

class UIIntrinsicSizingTests : public UILayoutTest {};

TEST_F(UIIntrinsicSizingTests, WordsTextResolvesMinAndMaxContentWidths)
{
    auto context = makeContext(
        {.nodeCapacity = 4,
         .rootCapacity = 1,
         .paintSnapshotCapacity = 16,
         .textByteCapacity = 64});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    UI::UILayoutStyle rootStyle{};
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    UI::UILayoutStyle minStyle{};
    minStyle.size.width = UI::UILayoutLength::MinContent();
    auto minLabel = updater.createElement(
        root.rootNodeId(), UI::makeLabelElement("AA AAAA", minStyle));
    ASSERT_TRUE(minLabel.has_value()) << minLabel.error().message;

    UI::UILayoutStyle maxStyle{};
    maxStyle.size.width = UI::UILayoutLength::MaxContent();
    auto maxLabel = updater.createElement(
        root.rootNodeId(), UI::makeLabelElement("AA AAAA", maxStyle));
    ASSERT_TRUE(maxLabel.has_value()) << maxLabel.error().message;

    assertOk(context->publication().commitLayout(
        {.width = 300.0F, .height = 160.0F}));
    const auto layout = context->publication().committedLayout();
    const auto& minEntry = requireLayoutEntry(layout, *minLabel);
    const auto& maxEntry = requireLayoutEntry(layout, *maxLabel);

    EXPECT_NEAR(minEntry.worldRect.width, TextAdvance * 4.0F, 0.001F);
    EXPECT_NEAR(minEntry.worldRect.height, TextLineHeight * 2.0F, 0.001F);
    EXPECT_NEAR(maxEntry.worldRect.width, TextAdvance * 7.0F, 0.001F);
    EXPECT_NEAR(maxEntry.worldRect.height, TextLineHeight, 0.001F);
}

TEST_F(UIIntrinsicSizingTests, FlexBasisAcceptsMinAndMaxContent)
{
    auto context = makeContext(
        {.nodeCapacity = 4,
         .rootCapacity = 1,
         .paintSnapshotCapacity = 16,
         .textByteCapacity = 64});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    UI::UILayoutStyle rootStyle = fixedSize(200.0F, 100.0F);
    rootStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    UI::UILayoutStyle minBasisStyle{};
    minBasisStyle.size.width = UI::UILayoutLength::Px(1.0F);
    minBasisStyle.flexItem.basis = UI::UILayoutLength::MinContent();
    auto minBasisLabel = updater.createElement(
        root.rootNodeId(),
        UI::makeLabelElement("AA AAAA", minBasisStyle));
    ASSERT_TRUE(minBasisLabel.has_value()) << minBasisLabel.error().message;

    UI::UILayoutStyle maxBasisStyle{};
    maxBasisStyle.size.width = UI::UILayoutLength::Px(1.0F);
    maxBasisStyle.flexItem.basis = UI::UILayoutLength::MaxContent();
    auto maxBasisLabel = updater.createElement(
        root.rootNodeId(),
        UI::makeLabelElement("AA AAAA", maxBasisStyle));
    ASSERT_TRUE(maxBasisLabel.has_value()) << maxBasisLabel.error().message;

    assertOk(context->publication().commitLayout(
        {.width = 200.0F, .height = 100.0F}));
    const auto layout = context->publication().committedLayout();

    EXPECT_NEAR(
        requireLayoutEntry(layout, *minBasisLabel).worldRect.width,
        TextAdvance * 4.0F, 0.001F);
    EXPECT_NEAR(
        requireLayoutEntry(layout, *maxBasisLabel).worldRect.width,
        TextAdvance * 7.0F, 0.001F);
}

TEST_F(UIIntrinsicSizingTests, AspectRatioDerivesOnlyTheAutoAxis)
{
    auto context = makeContext({.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    UI::UILayoutStyle rootStyle{};
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    UI::UILayoutStyle fixedWidth{};
    fixedWidth.size.width = UI::UILayoutLength::Px(120.0F);
    fixedWidth.aspectRatio = 2.0F;
    auto fixedWidthPanel = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedWidth));
    ASSERT_TRUE(fixedWidthPanel.has_value()) << fixedWidthPanel.error().message;

    UI::UILayoutStyle fixedHeight{};
    fixedHeight.size.height = UI::UILayoutLength::Px(40.0F);
    fixedHeight.aspectRatio = 2.0F;
    auto fixedHeightPanel = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedHeight));
    ASSERT_TRUE(fixedHeightPanel.has_value()) << fixedHeightPanel.error().message;

    UI::UILayoutStyle fixedBoth = fixedSize(90.0F, 30.0F);
    fixedBoth.aspectRatio = 2.0F;
    auto fixedBothPanel = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedBoth));
    ASSERT_TRUE(fixedBothPanel.has_value()) << fixedBothPanel.error().message;

    assertOk(context->publication().commitLayout(
        {.width = 300.0F, .height = 200.0F}));
    const auto layout = context->publication().committedLayout();

    EXPECT_EQ(requireLayoutEntry(layout, *fixedWidthPanel).worldRect.size(),
              (UI::UILogicalSize{.width = 120.0F, .height = 60.0F}));
    EXPECT_EQ(requireLayoutEntry(layout, *fixedHeightPanel).worldRect.size(),
              (UI::UILogicalSize{.width = 80.0F, .height = 40.0F}));
    EXPECT_EQ(requireLayoutEntry(layout, *fixedBothPanel).worldRect.size(),
              (UI::UILogicalSize{.width = 90.0F, .height = 30.0F}));
}

TEST_F(UIIntrinsicSizingTests, FlexArrangePreservesAspectRatio)
{
    auto context = makeContext(
        {.nodeCapacity = 6, .rootCapacity = 1, .textByteCapacity = 64});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    UI::UILayoutStyle rootStyle = fixedSize(150.0F, 100.0F);
    rootStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    UI::UILayoutStyle ratioStyle{};
    ratioStyle.size.width = UI::UILayoutLength::Px(100.0F);
    ratioStyle.aspectRatio = 2.0F;
    auto ratioPanel = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(ratioStyle));
    ASSERT_TRUE(ratioPanel.has_value()) << ratioPanel.error().message;
    auto peerPanel = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedSize(100.0F, 20.0F)));
    ASSERT_TRUE(peerPanel.has_value()) << peerPanel.error().message;

    assertOk(context->publication().commitLayout(
        {.width = 150.0F, .height = 100.0F}));
    const auto firstLayout = context->publication().committedLayout();
    const auto& ratioEntry = requireLayoutEntry(firstLayout, *ratioPanel);
    EXPECT_NEAR(ratioEntry.worldRect.width, 75.0F, 0.001F);
    EXPECT_NEAR(ratioEntry.worldRect.height, 37.5F, 0.001F);
}

TEST_F(UIIntrinsicSizingTests, FlexShrinkUsesMinContentFloor)
{
    auto context = makeContext(
        {.nodeCapacity = 4,
         .rootCapacity = 1,
         .paintSnapshotCapacity = 8,
         .textByteCapacity = 64});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    UI::UILayoutStyle narrowRootStyle = fixedSize(40.0F, 100.0F);
    narrowRootStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    narrowRootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), narrowRootStyle));

    UI::UILayoutStyle labelStyle{};
    labelStyle.size.width = UI::UILayoutLength::MaxContent();
    auto label = updater.createElement(
        root.rootNodeId(), UI::makeLabelElement("AA AAAA", labelStyle));
    ASSERT_TRUE(label.has_value()) << label.error().message;
    auto fixedPeer = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedSize(20.0F, 20.0F)));
    ASSERT_TRUE(fixedPeer.has_value()) << fixedPeer.error().message;

    assertOk(context->publication().commitLayout(
        {.width = 40.0F, .height = 100.0F}));
    const auto secondLayout = context->publication().committedLayout();
    EXPECT_NEAR(requireLayoutEntry(secondLayout, *label).worldRect.width,
                TextAdvance * 4.0F, 0.001F);
}

TEST_F(UIIntrinsicSizingTests, NestedContainerIncludesSpecifiedChildConstraints)
{
    auto context = makeContext({.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    UI::UILayoutStyle rootStyle{};
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    UI::UILayoutStyle containerStyle{};
    containerStyle.size.width = UI::UILayoutLength::MinContent();
    containerStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    auto container = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(containerStyle));
    ASSERT_TRUE(container.has_value()) << container.error().message;

    UI::UILayoutStyle childStyle{};
    childStyle.size.height = UI::UILayoutLength::Px(30.0F);
    childStyle.aspectRatio = 2.0F;
    childStyle.minMax.minWidth = UI::UILayoutLength::Px(80.0F);
    auto child = updater.createElement(
        *container, UI::makePanelElement(childStyle));
    ASSERT_TRUE(child.has_value()) << child.error().message;

    assertOk(context->publication().commitLayout(
        {.width = 300.0F, .height = 160.0F}));
    const auto layout = context->publication().committedLayout();
    EXPECT_FLOAT_EQ(
        requireLayoutEntry(layout, *container).worldRect.width, 80.0F);
    EXPECT_EQ(requireLayoutEntry(layout, *child).worldRect.size(),
              (UI::UILogicalSize{.width = 80.0F, .height = 30.0F}));
}

TEST_F(UIIntrinsicSizingTests, WrappedFlexMinContentUsesOneItemPerLine)
{
    auto context = makeContext({.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    UI::UILayoutStyle rootStyle{};
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    UI::UILayoutStyle containerStyle{};
    containerStyle.size.width = UI::UILayoutLength::MinContent();
    containerStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    containerStyle.flexContainer.wrap = UI::UIFlexWrap::Wrap;
    containerStyle.flexContainer.gap = {.row = 5.0F, .column = 7.0F};
    auto container = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(containerStyle));
    ASSERT_TRUE(container.has_value()) << container.error().message;

    auto first = updater.createElement(
        *container, UI::makePanelElement(fixedSize(40.0F, 10.0F)));
    auto second = updater.createElement(
        *container, UI::makePanelElement(fixedSize(60.0F, 20.0F)));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    assertOk(context->publication().commitLayout(
        {.width = 300.0F, .height = 160.0F}));
    const auto layout = context->publication().committedLayout();
    const auto& containerEntry = requireLayoutEntry(layout, *container);
    EXPECT_FLOAT_EQ(containerEntry.worldRect.width, 60.0F);
    EXPECT_FLOAT_EQ(containerEntry.worldRect.height, 35.0F);
}

} // namespace
} // namespace Tina::Tests
