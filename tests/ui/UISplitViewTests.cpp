#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] constexpr UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UIPointerInputEvent pointerInput(
    Platform::WindowId window, UI::UIRoutedPointerEventKind kind, u64 sequence,
    UI::UILogicalPoint position) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = static_cast<usize>(sequence - 1U),
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = position,
        .button = Platform::PointerButton::Primary,
    };
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] const UI::UISemanticsEntry*
findSemantics(UI::UICommittedSemanticsView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UISemanticsEntry& entry) { return entry.node == node; });
    return found != view.end() ? &*found : nullptr;
}

struct SplitViewNodes final {
    UI::UINodeId splitView{};
    UI::UINodeId primaryPane{};
    UI::UINodeId splitter{};
    UI::UINodeId secondaryPane{};
};

[[nodiscard]] SplitViewNodes createSplitView(
    UI::UITreeUpdater& updater, UI::UINodeId parent,
    UI::UISplitViewConfig config = {}, UI::UISplitterConfig splitterConfig = {},
    UI::UILayoutStyle layout = {})
{
    auto splitView = updater.createElement(parent, UI::makeSplitViewElement(config, layout));
    if (!splitView)
    {
        ADD_FAILURE() << splitView.error().message;
        return {};
    }
    auto primary = updater.createElement(*splitView, UI::makePanelElement());
    auto splitter = updater.createElement(*splitView, UI::makeSplitterElement(splitterConfig));
    auto secondary = updater.createElement(*splitView, UI::makePanelElement());
    if (!primary || !splitter || !secondary)
    {
        ADD_FAILURE() << "failed to create SplitView parts";
        return {};
    }
    Core::Status linked = updater.setSplitViewParts(*splitView, *primary, *splitter, *secondary);
    if (!linked)
    {
        ADD_FAILURE() << linked.error().message;
        return {};
    }
    return {
        .splitView = *splitView,
        .primaryPane = *primary,
        .splitter = *splitter,
        .secondaryPane = *secondary,
    };
}

class UISplitViewTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(2);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    [[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
        UI::UIContextCapacityConfig capacities = {})
    {
        capacities.nodeCapacity =
            capacities.nodeCapacity == UI::UIContextCapacityConfig::DefaultNodeCapacity
                ? 24
                : capacities.nodeCapacity;
        capacities.rootCapacity =
            capacities.rootCapacity == UI::UIContextCapacityConfig::DefaultRootCapacity
                ? 2
                : capacities.rootCapacity;
        capacities.applyDefaultProductChrome = false;
        auto result = UI::UIContext::Create(window, capacities);
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? std::move(*result) : nullptr;
    }

    [[nodiscard]] static UI::UIRootOwner createRoot(UI::UIContext& context)
    {
        auto result = context.rootBuilder().createRoot();
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? std::move(*result) : UI::UIRootOwner{};
    }

    [[nodiscard]] static UI::UITreeUpdater createUpdater(
        UI::UIContext& context, UI::UIRootOwner& root)
    {
        auto result = context.treeUpdater(root);
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? std::move(*result) : UI::UITreeUpdater{};
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UISplitViewTest, RecipesPublishDedicatedContractsAndRejectMalformedDescriptors)
{
    constexpr UI::UISplitViewConfig splitConfig{
        .orientation = UI::UISplitViewOrientation::Vertical,
        .initialFraction = 0.35F,
        .minPrimarySize = 20.0F,
        .minSecondarySize = 30.0F,
        .splitterExtent = 8.0F,
    };
    constexpr UI::UISplitterConfig splitterConfig{.keyboardStep = 0.1F};
    constexpr UI::UIElementDescriptor splitRecipe =
        UI::makeSplitViewElement(splitConfig, fixedSize(200.0F, 120.0F));
    constexpr UI::UIElementDescriptor splitterRecipe =
        UI::makeSplitterElement(splitterConfig);
    static_assert(splitRecipe.splitView == splitConfig);
    static_assert(splitRecipe.layout.flexContainer.direction == UI::UIFlexDirection::Column);
    static_assert(splitRecipe.pointerHitPolicy == UI::UIPointerHitPolicy::Ignore);
    static_assert(splitRecipe.behaviors == UI::UIElementBehavior::None);
    static_assert(splitterRecipe.splitter == splitterConfig);
    static_assert(splitterRecipe.pointerHitPolicy == UI::UIPointerHitPolicy::Targetable);
    static_assert(splitterRecipe.behaviors ==
                  (UI::UIElementBehavior::Focusable | UI::UIElementBehavior::RangeInput));
    static_assert(splitterRecipe.semantics.role == UI::UISemanticsRole::Slider);

    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    auto orphanSplitter = updater.createElement(root.rootNodeId(), splitterRecipe);
    ASSERT_FALSE(orphanSplitter.has_value());
    EXPECT_EQ(orphanSplitter.error().code, UI::UIErrorCode::InvalidParent);

    UI::UIElementDescriptor malformedSplit = splitRecipe;
    malformedSplit.splitView->initialFraction = 1.5F;
    auto rejected = updater.createElement(root.rootNodeId(), malformedSplit);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    UI::UIElementDescriptor malformedSplitter = splitterRecipe;
    malformedSplitter.splitter->keyboardStep = 0.0F;
    auto splitView = updater.createElement(root.rootNodeId(), splitRecipe);
    ASSERT_TRUE(splitView.has_value());
    rejected = updater.createElement(*splitView, malformedSplitter);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    auto splitter = updater.createElement(*splitView, splitterRecipe);
    ASSERT_TRUE(splitter.has_value());
    EXPECT_FALSE(context->isSplitterDragging(*splitter).value());
}

TEST_F(UISplitViewTest, SplitterPaintRoundTripsAndRejectsInvalidOrForeignNodesAtomically)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    auto first = createUpdater(*context, firstRoot);
    auto second = createUpdater(*context, secondRoot);
    const SplitViewNodes nodes = createSplitView(first, firstRoot.rootNodeId());
    ASSERT_TRUE(nodes.splitter.hasValue());

    const UI::UISplitterPaint expected{
        .lineColor = UI::rgb(0x203040),
        .hoveredLineColor = UI::rgb(0x405060),
        .draggingLineColor = UI::rgb(0x607080),
        .focusRingColor = UI::rgb(0x8090A0),
        .lineThickness = 2.0F,
        .focusRingThickness = 4.0F,
    };
    assertOk(first.setSplitterPaint(nodes.splitter, expected));
    EXPECT_EQ(first.splitterPaint(nodes.splitter).value(), expected);
    EXPECT_EQ(context->splitterPaint(nodes.splitter).value(), expected);

    UI::UISplitterPaint invalid = expected;
    invalid.lineThickness = 0.0F;
    auto rejected = first.setSplitterPaint(nodes.splitter, invalid);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);

    invalid = expected;
    invalid.focusRingThickness = 1.0F;
    rejected = first.setSplitterPaint(nodes.splitter, invalid);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);

    invalid = expected;
    invalid.lineThickness = (std::numeric_limits<float>::quiet_NaN)();
    rejected = first.setSplitterPaint(nodes.splitter, invalid);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(first.splitterPaint(nodes.splitter).value(), expected);

    rejected = first.setSplitterPaint(nodes.splitView, expected);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    rejected = second.setSplitterPaint(nodes.splitter, expected);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(UISplitViewTest, PartsRequireDistinctDirectChildrenAndOneSameRootSplitter)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    auto first = createUpdater(*context, firstRoot);
    auto second = createUpdater(*context, secondRoot);
    const SplitViewNodes local = createSplitView(first, firstRoot.rootNodeId());
    const SplitViewNodes foreign = createSplitView(second, secondRoot.rootNodeId());
    ASSERT_TRUE(local.splitter.hasValue() && foreign.splitter.hasValue());
    const UI::UISplitViewParts published = first.splitViewParts(local.splitView).value();

    Core::Status rejected = first.setSplitViewParts(
        local.splitView, local.splitView, local.splitter, local.secondaryPane);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);

    rejected = first.setSplitViewParts(
        local.splitView, local.primaryPane, local.primaryPane, local.secondaryPane);
    ASSERT_FALSE(rejected.has_value());

    auto descendant = first.createElement(local.primaryPane, UI::makePanelElement());
    ASSERT_TRUE(descendant.has_value());
    rejected = first.setSplitViewParts(
        local.splitView, *descendant, local.splitter, local.secondaryPane);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);

    rejected = context->setSplitViewParts(
        local.splitView, foreign.primaryPane, local.splitter, local.secondaryPane);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::WrongContext);
    EXPECT_EQ(first.splitViewParts(local.splitView).value(), published);

    assertOk(first.clearSplitViewParts(local.splitView));
    EXPECT_FALSE(first.splitViewParts(local.splitView).value().hasValue());
    EXPECT_FALSE(first.isSplitterDragging(local.splitter).value());
    assertOk(first.setSplitViewParts(
        local.splitView, local.primaryPane, local.splitter, local.secondaryPane));
}

TEST_F(UISplitViewTest, LayoutResolvesHorizontalVerticalMinimumsAndCommittedMetrics)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto horizontalRoot = createRoot(*context);
    auto verticalRoot = createRoot(*context);
    auto horizontalUpdater = createUpdater(*context, horizontalRoot);
    auto verticalUpdater = createUpdater(*context, verticalRoot);

    assertOk(horizontalUpdater.setLayoutStyle(
        horizontalRoot.rootNodeId(), fixedSize(300.0F, 100.0F)));
    const SplitViewNodes horizontal = createSplitView(
        horizontalUpdater, horizontalRoot.rootNodeId(),
        UI::UISplitViewConfig{
            .orientation = UI::UISplitViewOrientation::Horizontal,
            .initialFraction = 0.2F,
            .minPrimarySize = 100.0F,
            .minSecondarySize = 80.0F,
            .splitterExtent = 10.0F,
        }, {}, fixedSize(300.0F, 100.0F));

    assertOk(verticalUpdater.setLayoutStyle(
        verticalRoot.rootNodeId(), fixedSize(160.0F, 200.0F)));
    const SplitViewNodes vertical = createSplitView(
        verticalUpdater, verticalRoot.rootNodeId(),
        UI::UISplitViewConfig{
            .orientation = UI::UISplitViewOrientation::Vertical,
            .initialFraction = 0.8F,
            .minPrimarySize = 40.0F,
            .minSecondarySize = 80.0F,
            .splitterExtent = 8.0F,
        }, {}, fixedSize(160.0F, 200.0F));
    ASSERT_TRUE(horizontal.splitView.hasValue() && vertical.splitView.hasValue());

    assertOk(context->commitLayout({.width = 400.0F, .height = 300.0F}));
    const UI::UISplitViewMetrics horizontalMetrics =
        horizontalUpdater.splitViewMetrics(horizontal.splitView).value();
    EXPECT_EQ(horizontalMetrics.orientation, UI::UISplitViewOrientation::Horizontal);
    EXPECT_FLOAT_EQ(horizontalMetrics.primaryRect.width, 100.0F);
    EXPECT_FLOAT_EQ(horizontalMetrics.splitterRect.width, 10.0F);
    EXPECT_FLOAT_EQ(horizontalMetrics.secondaryRect.width, 190.0F);
    EXPECT_NEAR(horizontalMetrics.fraction, 100.0F / 290.0F, 0.0001F);

    const UI::UISplitViewMetrics verticalMetrics =
        verticalUpdater.splitViewMetrics(vertical.splitView).value();
    EXPECT_EQ(verticalMetrics.orientation, UI::UISplitViewOrientation::Vertical);
    EXPECT_FLOAT_EQ(verticalMetrics.primaryRect.height, 112.0F);
    EXPECT_FLOAT_EQ(verticalMetrics.splitterRect.height, 8.0F);
    EXPECT_FLOAT_EQ(verticalMetrics.secondaryRect.height, 80.0F);
    EXPECT_NEAR(verticalMetrics.fraction, 112.0F / 192.0F, 0.0001F);

    const auto layout = context->committedLayout();
    const auto splitterEntry = std::ranges::find_if(
        layout, [horizontal](const UI::UICommittedLayoutEntry& entry) {
            return entry.node == horizontal.splitter;
        });
    ASSERT_NE(splitterEntry, layout.end());
    EXPECT_EQ(splitterEntry->worldRect, horizontalMetrics.splitterRect);
}

TEST_F(UISplitViewTest, PointerDragCapturesSplitterAndKeyboardRangeInputUpdatesFraction)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(300.0F, 100.0F)));
    const SplitViewNodes nodes = createSplitView(
        updater, root.rootNodeId(),
        UI::UISplitViewConfig{.initialFraction = 0.5F, .splitterExtent = 10.0F},
        UI::UISplitterConfig{.keyboardStep = 0.1F}, fixedSize(300.0F, 100.0F));
    ASSERT_TRUE(nodes.splitter.hasValue());
    assertOk(context->commitLayout({.width = 300.0F, .height = 100.0F}));

    const UI::UISplitViewMetrics before = updater.splitViewMetrics(nodes.splitView).value();
    const float grabX = before.splitterRect.x + 3.0F;
    auto down = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 1,
        {.x = grabX, .y = 50.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->pointerCapture(), nodes.splitter);
    EXPECT_TRUE(updater.isSplitterDragging(nodes.splitter).value());

    auto move = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Move, 2,
        {.x = 220.0F, .y = 50.0F}));
    ASSERT_TRUE(move.has_value()) << (move ? "" : move.error().message);
    // Pointer drag remains continuous; keyboardStep only controls keyboard and
    // accessibility RangeInput commands.
    EXPECT_NEAR(updater.splitViewFraction(nodes.splitView).value(), 217.0F / 290.0F,
                0.0001F);

    auto up = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 3,
        {.x = 220.0F, .y = 50.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_FALSE(updater.isSplitterDragging(nodes.splitter).value());
    EXPECT_FALSE(context->pointerCapture().hasValue());

    assertOk(updater.setSplitViewFraction(nodes.splitView, 0.5F));
    assertOk(context->commitLayout({.width = 300.0F, .height = 100.0F}));
    assertOk(context->requestFocus(nodes.splitter));
    auto increased = context->routeRangeInputCommand(
        Platform::PlatformFrameId{4}, 4, UI::UIRangeInputCommand::Increase, true,
        Platform::KeyControlIdentity{.window = window, .key = Platform::Key::Right});
    ASSERT_TRUE(increased.has_value()) << (increased ? "" : increased.error().message);
    EXPECT_TRUE(increased->targeted);
    EXPECT_TRUE(increased->consumed);
    EXPECT_TRUE(increased->changed);
    EXPECT_NEAR(updater.splitViewFraction(nodes.splitView).value(), 0.6F, 0.0001F);
}

TEST_F(UISplitViewTest, FailedCommitKeepsPublishedMetricsUntilACommitSucceeds)
{
    auto context = createContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .layoutSnapshotCapacity = 5,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 100.0F)));
    const SplitViewNodes nodes = createSplitView(
        updater, root.rootNodeId(), {}, {}, fixedSize(200.0F, 100.0F));
    ASSERT_TRUE(nodes.splitView.hasValue());
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));
    const UI::UISplitViewMetrics published = updater.splitViewMetrics(nodes.splitView).value();
    const u64 layoutRevision = context->committedLayout().layoutRevision();

    assertOk(updater.setSplitViewFraction(nodes.splitView, 0.75F));
    auto overflow = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedSize(10.0F, 10.0F)));
    ASSERT_TRUE(overflow.has_value());
    Core::Status failed = context->commitLayout({.width = 200.0F, .height = 100.0F});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(updater.splitViewMetrics(nodes.splitView).value(), published);
    EXPECT_FLOAT_EQ(updater.splitViewFraction(nodes.splitView).value(), 0.75F);
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision);

    assertOk(updater.destroy(*overflow));
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));
    EXPECT_NEAR(updater.splitViewMetrics(nodes.splitView).value().fraction, 0.75F,
                0.0001F);
}

TEST_F(UISplitViewTest, SplitterPublishesFocusedRangeSemanticsAndAccessibilityUsesSharedMutation)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 100.0F)));

    auto splitView = updater.createElement(
        root.rootNodeId(), UI::makeSplitViewElement({}, fixedSize(200.0F, 100.0F)));
    ASSERT_TRUE(splitView.has_value());
    auto primary = updater.createElement(*splitView, UI::makePanelElement());
    UI::UIElementDescriptor splitterDescriptor = UI::makeSplitterElement();
    splitterDescriptor.semantics.name = "Resize panels";
    auto splitter = updater.createElement(*splitView, splitterDescriptor);
    auto secondary = updater.createElement(*splitView, UI::makePanelElement());
    ASSERT_TRUE(primary && splitter && secondary);
    assertOk(updater.setSplitViewParts(*splitView, *primary, *splitter, *secondary));
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));
    assertOk(context->requestFocus(*splitter));
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UISemanticsEntry* entry =
        findSemantics(context->committedSemantics(), *splitter);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->role, UI::UISemanticsRole::Slider);
    EXPECT_EQ(entry->name, "Resize panels");
    EXPECT_TRUE(entry->focused);
    EXPECT_TRUE(entry->hasRange);
    EXPECT_FLOAT_EQ(entry->minValue, 0.0F);
    EXPECT_FLOAT_EQ(entry->maxValue, 1.0F);
    EXPECT_FLOAT_EQ(entry->value, 0.5F);
    EXPECT_EQ(entry->actions, UI::UISemanticsAction::Focus |
                                  UI::UISemanticsAction::SetRangeValue);
    EXPECT_EQ(findSemantics(context->committedSemantics(), *splitView), nullptr);

    assertOk(context->performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::SetRangeValue,
        .node = *splitter,
        .rangeValue = 0.8,
    }));
    EXPECT_NEAR(updater.splitViewFraction(*splitView).value(), 0.8F, 0.0001F);
}

TEST_F(UISplitViewTest, DestroyGenerationReuseAndRootReleaseClearEveryPartsRelation)
{
    auto context = createContext({.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    SplitViewNodes nodes = createSplitView(updater, root.rootNodeId());
    ASSERT_TRUE(nodes.splitView.hasValue());

    const UI::UINodeId destroyedPrimary = nodes.primaryPane;
    assertOk(updater.destroy(nodes.primaryPane));
    EXPECT_FALSE(updater.splitViewParts(nodes.splitView).value().hasValue());
    auto replacement = updater.createElement(nodes.splitView, UI::makePanelElement());
    ASSERT_TRUE(replacement.has_value());
    EXPECT_NE(*replacement, destroyedPrimary);
    assertOk(updater.setSplitViewParts(
        nodes.splitView, *replacement, nodes.splitter, nodes.secondaryPane));

    const UI::UINodeId destroyedSplitter = nodes.splitter;
    assertOk(updater.destroy(nodes.splitter));
    EXPECT_FALSE(updater.splitViewParts(nodes.splitView).value().hasValue());
    auto replacementSplitter = updater.createElement(
        nodes.splitView, UI::makeSplitterElement());
    ASSERT_TRUE(replacementSplitter.has_value());
    EXPECT_NE(*replacementSplitter, destroyedSplitter);
    assertOk(updater.setSplitViewParts(
        nodes.splitView, *replacement, *replacementSplitter, nodes.secondaryPane));

    const UI::UINodeId staleSplitView = nodes.splitView;
    const UI::UINodeId staleSplitter = *replacementSplitter;
    root.reset();
    EXPECT_EQ(context->liveNodeCount(), 0U);
    auto staleParts = context->splitViewParts(staleSplitView);
    ASSERT_FALSE(staleParts.has_value());
    EXPECT_TRUE(staleParts.error().code == UI::UIErrorCode::InvalidNode ||
                staleParts.error().code == UI::UIErrorCode::RootRequired);
    auto staleDragging = context->isSplitterDragging(staleSplitter);
    ASSERT_FALSE(staleDragging.has_value());
    EXPECT_TRUE(staleDragging.error().code == UI::UIErrorCode::InvalidNode ||
                staleDragging.error().code == UI::UIErrorCode::RootRequired);

    auto newRoot = createRoot(*context);
    auto newUpdater = createUpdater(*context, newRoot);
    const SplitViewNodes reused = createSplitView(newUpdater, newRoot.rootNodeId());
    ASSERT_TRUE(reused.splitView.hasValue());
    EXPECT_NE(reused.splitView, staleSplitView);
    EXPECT_TRUE(newUpdater.splitViewParts(reused.splitView).value().hasValue());
}

} // namespace
} // namespace Tina::Tests
