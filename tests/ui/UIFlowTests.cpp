#include "UILayoutTestSupport.hpp"

namespace Tina::Tests {
namespace {

using namespace UILayoutTestSupport;

[[nodiscard]] UI::UIVisibility visibilityOf(UI::UIContext& context, UI::UINodeId node)
{
    return requireLayoutEntry(context.committedLayout(), node).effectiveVisibility;
}

struct FlowTree final {
    UI::UINodeId layer{};
    UI::UINodeId first{};
    UI::UINodeId second{};
    UI::UINodeId third{};
};

[[nodiscard]] FlowTree createFlowTree(UI::UITreeUpdater& updater, UI::UINodeId root)
{
    FlowTree tree{};
    auto layer = updater.createElement(root, UI::makePanelElement());
    EXPECT_TRUE(layer.has_value()) << (layer ? "" : layer.error().message);
    if (!layer)
    {
        return tree;
    }
    tree.layer = *layer;

    const auto createScreen = [&]() {
        auto screen = updater.createElement(tree.layer, UI::makePanelElement());
        EXPECT_TRUE(screen.has_value()) << (screen ? "" : screen.error().message);
        return screen ? *screen : UI::UINodeId{};
    };
    tree.first = createScreen();
    tree.second = createScreen();
    tree.third = createScreen();

    assertOk(updater.setLayoutStyle(root, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(tree.layer, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(tree.first, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(tree.second, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(tree.third, percentSize(100.0F, 100.0F)));
    return tree;
}

TEST_F(UILayoutTest, FlowStackPublishesOnlyTheTopScreenAndPreservesStackOrder)
{
    auto context = makeContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .flowLayerCapacity = 2,
        .flowScreenCapacity = 4,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    const FlowTree tree = createFlowTree(updater, root.rootNodeId());
    ASSERT_TRUE(tree.third.hasValue());

    auto layer = updater.registerFlowLayer(tree.layer);
    ASSERT_TRUE(layer.has_value()) << (layer ? "" : layer.error().message);
    auto first = updater.registerFlowScreen(*layer, tree.first);
    auto second = updater.registerFlowScreen(*layer, tree.second);
    auto third = updater.registerFlowScreen(*layer, tree.third);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(first->nodeId(), tree.first);
    EXPECT_EQ(layer->nodeId(), tree.layer);

    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Collapsed);
    EXPECT_EQ(visibilityOf(*context, tree.second), UI::UIVisibility::Collapsed);
    EXPECT_FALSE(updater.activeFlowScreen(*layer)->hasValue());

    assertOk(updater.pushFlowScreen(*first));
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Visible);
    EXPECT_EQ(visibilityOf(*context, tree.second), UI::UIVisibility::Collapsed);
    EXPECT_EQ(*updater.activeFlowScreen(*layer), *first);
    EXPECT_TRUE(*updater.isFlowScreenActive(*first));

    assertOk(updater.pushFlowScreen(*second));
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Collapsed);
    EXPECT_EQ(visibilityOf(*context, tree.second), UI::UIVisibility::Visible);

    auto popped = updater.popFlowScreen(*layer);
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(*popped, *second);
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Visible);
    EXPECT_EQ(visibilityOf(*context, tree.second), UI::UIVisibility::Collapsed);

    auto replaced = updater.replaceFlowScreen(*third);
    ASSERT_TRUE(replaced.has_value());
    EXPECT_EQ(*replaced, *first);
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Collapsed);
    EXPECT_EQ(visibilityOf(*context, tree.third), UI::UIVisibility::Visible);
    EXPECT_EQ(*updater.activeFlowScreen(*layer), *third);

    const UI::UIFlowStatistics statistics = context->statistics().flow;
    EXPECT_EQ(statistics.registeredLayerCount, 1U);
    EXPECT_EQ(statistics.registeredScreenCount, 3U);
    EXPECT_EQ(statistics.stackedScreenCount, 1U);
    EXPECT_EQ(statistics.stackHighWater, 2U);
}

TEST_F(UILayoutTest, FlowStackDirtyCapacityFailureLeavesTheActiveScreenUnchanged)
{
    auto context = makeContext({
        .nodeCapacity = 6,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 3,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);

    auto layerNode = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto firstNode = updater.createElement(*layerNode, UI::makePanelElement());
    auto secondNode = updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(firstNode.has_value());
    ASSERT_TRUE(secondNode.has_value());
    assertOk(updater.setLayoutStyle(root.rootNodeId(), percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*layerNode, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*firstNode, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*secondNode, percentSize(100.0F, 100.0F)));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    auto layer = updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto first = updater.registerFlowScreen(*layer, *firstNode);
    ASSERT_TRUE(first.has_value());
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    auto second = updater.registerFlowScreen(*layer, *secondNode);
    ASSERT_TRUE(second.has_value());
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    assertOk(updater.pushFlowScreen(*first));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    const UI::UIContextStatistics before = context->statistics();
    const Core::Status rejected = updater.pushFlowScreen(*second);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(*updater.activeFlowScreen(*layer), *first);
    EXPECT_TRUE(*updater.isFlowScreenActive(*first));
    EXPECT_FALSE(*updater.isFlowScreenActive(*second));
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, before.dirtyQueuePendingCount);
    EXPECT_EQ(context->statistics().flow.stackedScreenCount, 1U);
    EXPECT_EQ(context->statistics().flow.capacityFailureCount,
              before.flow.capacityFailureCount + 1U);

    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_EQ(visibilityOf(*context, *firstNode), UI::UIVisibility::Visible);
    EXPECT_EQ(visibilityOf(*context, *secondNode), UI::UIVisibility::Collapsed);
}

TEST_F(UILayoutTest, FlowRegistrationCapacityIsReclaimedByNodeLifetime)
{
    auto context = makeContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    const FlowTree tree = createFlowTree(updater, root.rootNodeId());
    auto spareLayerNode = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(spareLayerNode.has_value());

    auto layer = updater.registerFlowLayer(tree.layer);
    ASSERT_TRUE(layer.has_value());
    const auto layerOverflow = updater.registerFlowLayer(*spareLayerNode);
    ASSERT_FALSE(layerOverflow.has_value());
    EXPECT_EQ(layerOverflow.error().code, UI::UIErrorCode::CapacityExceeded);

    auto first = updater.registerFlowScreen(*layer, tree.first);
    auto second = updater.registerFlowScreen(*layer, tree.second);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    const auto screenOverflow = updater.registerFlowScreen(*layer, tree.third);
    ASSERT_FALSE(screenOverflow.has_value());
    EXPECT_EQ(screenOverflow.error().code, UI::UIErrorCode::CapacityExceeded);
    assertOk(updater.pushFlowScreen(*first));
    assertOk(updater.pushFlowScreen(*second));

    assertOk(updater.destroy(tree.second));
    EXPECT_EQ(*updater.activeFlowScreen(*layer), *first);
    EXPECT_EQ(context->statistics().flow.registeredScreenCount, 1U);
    auto third = updater.registerFlowScreen(*layer, tree.third);
    ASSERT_TRUE(third.has_value());
    assertOk(updater.pushFlowScreen(*third));

    assertOk(updater.destroy(tree.layer));
    const UI::UIFlowStatistics afterLayerDestroy = context->statistics().flow;
    EXPECT_EQ(afterLayerDestroy.registeredLayerCount, 0U);
    EXPECT_EQ(afterLayerDestroy.registeredScreenCount, 0U);
    EXPECT_EQ(afterLayerDestroy.stackedScreenCount, 0U);
    auto replacementLayer = updater.registerFlowLayer(*spareLayerNode);
    ASSERT_TRUE(replacementLayer.has_value());
    EXPECT_EQ(context->statistics().flow.capacityFailureCount, 2U);

    const auto staleScreen = updater.isFlowScreenActive(*third);
    ASSERT_FALSE(staleScreen.has_value());
    EXPECT_EQ(staleScreen.error().code, UI::UIErrorCode::InvalidFlowScreen);
}

TEST_F(UILayoutTest, FlowScreenDeactivationClearsCommittedFocus)
{
    auto context = makeContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    auto layerNode = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto firstNode = updater.createElement(*layerNode, UI::makePanelElement());
    auto secondNode = updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(firstNode.has_value());
    ASSERT_TRUE(secondNode.has_value());
    auto firstButton = updater.createElement(*firstNode, UI::makeButtonElement());
    auto secondButton = updater.createElement(*secondNode, UI::makeButtonElement());
    ASSERT_TRUE(firstButton.has_value());
    ASSERT_TRUE(secondButton.has_value());
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*layerNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*firstNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*secondNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*firstButton, fixedSize(80.0F, 30.0F)));
    assertOk(updater.setLayoutStyle(*secondButton, fixedSize(80.0F, 30.0F)));

    auto layer = updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto first = updater.registerFlowScreen(*layer, *firstNode);
    auto second = updater.registerFlowScreen(*layer, *secondNode);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    assertOk(updater.pushFlowScreen(*first));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    assertOk(updater.requestFocus(*firstButton));
    EXPECT_EQ(context->defaultActionFocus(), *firstButton);

    assertOk(updater.pushFlowScreen(*second));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    assertOk(updater.requestFocus(*secondButton));
    EXPECT_EQ(context->defaultActionFocus(), *secondButton);
}

TEST_F(UILayoutTest, FlowBackActionRoutesOnceAndClaimsItsReleaseAcrossScreenPop)
{
    auto context = makeContext({
        .nodeCapacity = 6,
        .rootCapacity = 1,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    auto layerNode = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto screenNode = updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(screenNode.has_value());
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*layerNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*screenNode, fixedSize(200.0F, 120.0F)));

    auto layer = updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto screen = updater.registerFlowScreen(*layer, *screenNode);
    ASSERT_TRUE(screen.has_value());
    assertOk(updater.pushFlowScreen(*screen));

    usize invocationCount = 0;
    UI::UIFlowActionEvent lastEvent{};
    assertOk(updater.setFlowScreenAction(
        *screen, UI::UIFlowAction::Back,
        UI::UIFlowActionCallback{
            [&invocationCount, &lastEvent](const UI::UIFlowActionEvent& event) noexcept {
                ++invocationCount;
                lastEvent = event;
            }}));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    const Platform::DigitalControlIdentity escape = Platform::KeyControlIdentity{
        .window = context->ownerWindow(),
        .key = Platform::Key::Escape,
    };
    auto down = context->routeFlowAction({1}, 1, UI::UIFlowAction::Back,
                                         UI::UIFlowActionSource::Keyboard, true, escape);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(down->invoked);
    EXPECT_EQ(down->screen, *screen);
    EXPECT_EQ(invocationCount, 1U);
    EXPECT_EQ(lastEvent.screen, *screen);
    EXPECT_EQ(lastEvent.source, UI::UIFlowActionSource::Keyboard);

    auto repeatedDown = context->routeFlowAction({2}, 2, UI::UIFlowAction::Back,
                                                 UI::UIFlowActionSource::Keyboard, true, escape);
    ASSERT_TRUE(repeatedDown.has_value());
    EXPECT_TRUE(repeatedDown->consumed);
    EXPECT_FALSE(repeatedDown->invoked);
    EXPECT_EQ(invocationCount, 1U);

    auto popped = updater.popFlowScreen(*layer);
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(*popped, *screen);
    auto release = context->routeFlowAction({3}, 3, UI::UIFlowAction::Back,
                                            UI::UIFlowActionSource::Keyboard, false, escape);
    ASSERT_TRUE(release.has_value());
    EXPECT_TRUE(release->consumed);
    EXPECT_FALSE(release->invoked);

    assertOk(updater.clearFlowScreenAction(*screen, UI::UIFlowAction::Back));
    const UI::UIFlowStatistics statistics = context->statistics().flow;
    EXPECT_EQ(statistics.registeredActionCount, 0U);
    EXPECT_EQ(statistics.actionHighWater, 1U);
    EXPECT_EQ(statistics.actionInvocationCount, 1U);
}

} // namespace
} // namespace Tina::Tests
