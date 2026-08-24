#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <ranges>
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

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] const UI::UICommittedLayoutEntry* findLayout(
    UI::UICommittedLayoutView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UICommittedLayoutEntry& entry) { return entry.node == node; });
    return found != view.end() ? &*found : nullptr;
}

[[nodiscard]] const UI::UISemanticsEntry* findSemantics(
    UI::UICommittedSemanticsView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UISemanticsEntry& entry) { return entry.node == node; });
    return found != view.end() ? &*found : nullptr;
}

void expectRect(UI::UILogicalRect actual, UI::UILogicalRect expected)
{
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
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

struct TabViewNodes final {
    UI::UINodeId tabView{};
    UI::UINodeId firstTab{};
    UI::UINodeId secondTab{};
    UI::UINodeId firstPanel{};
    UI::UINodeId secondPanel{};
};

[[nodiscard]] TabViewNodes createTabView(
    UI::UITreeUpdater& updater, UI::UINodeId parent,
    UI::UITabViewConfig config = {}, UI::UILayoutStyle layout = {},
    bool publishPanels = false)
{
    auto tabView = updater.createElement(parent, UI::makeTabViewElement(config, layout));
    if (!tabView)
    {
        ADD_FAILURE() << tabView.error().message;
        return {};
    }
    auto firstTab = updater.createElement(
        *tabView, UI::makeTabElement("First", {}, fixedSize(60.0F, 24.0F)));
    auto secondTab = updater.createElement(
        *tabView, UI::makeTabElement("Second", {}, fixedSize(80.0F, 24.0F)));
    UI::UIElementDescriptor firstPanelDescriptor = UI::makePanelElement();
    UI::UIElementDescriptor secondPanelDescriptor = UI::makePanelElement();
    if (publishPanels)
    {
        firstPanelDescriptor.semantics = {
            .mode = UI::UISemanticsMode::Publish,
            .role = UI::UISemanticsRole::Group,
            .name = "First panel",
        };
        secondPanelDescriptor.semantics = {
            .mode = UI::UISemanticsMode::Publish,
            .role = UI::UISemanticsRole::Group,
            .name = "Second panel",
        };
    }
    auto firstPanel = updater.createElement(*tabView, firstPanelDescriptor);
    auto secondPanel = updater.createElement(*tabView, secondPanelDescriptor);
    if (!firstTab || !secondTab || !firstPanel || !secondPanel)
    {
        ADD_FAILURE() << "failed to create TabView children";
        return {};
    }
    const std::array items{
        UI::UITabViewItem{.tab = *firstTab, .panel = *firstPanel},
        UI::UITabViewItem{.tab = *secondTab, .panel = *secondPanel},
    };
    Core::Status linked = updater.setTabViewItems(*tabView, items);
    if (!linked)
    {
        ADD_FAILURE() << linked.error().message;
        return {};
    }
    return {
        .tabView = *tabView,
        .firstTab = *firstTab,
        .secondTab = *secondTab,
        .firstPanel = *firstPanel,
        .secondPanel = *secondPanel,
    };
}

class UITabViewTest : public testing::Test {
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
        if (capacities.nodeCapacity == UI::UIContextCapacityConfig::DefaultNodeCapacity)
        {
            capacities.nodeCapacity = 32;
        }
        if (capacities.rootCapacity == UI::UIContextCapacityConfig::DefaultRootCapacity)
        {
            capacities.rootCapacity = 2;
        }
        capacities.applyDefaultProductChrome = false;
        auto result = UI::UIContext::Create(window, capacities);
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? std::move(*result) : nullptr;
    }

    [[nodiscard]] static UI::UIRootOwner createRoot(UI::UIContext& context)
    {
        auto result = context.authoring().rootBuilder().createRoot();
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? std::move(*result) : UI::UIRootOwner{};
    }

    [[nodiscard]] static UI::UITreeUpdater createUpdater(
        UI::UIContext& context, UI::UIRootOwner& root)
    {
        auto result = context.authoring().treeUpdater(root);
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? std::move(*result) : UI::UITreeUpdater{};
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UITabViewTest, RecipesPublishDedicatedContractsAndRejectMalformedDescriptors)
{
    constexpr UI::UITabViewConfig config{
        .placement = UI::UITabViewPlacement::Left,
        .activationMode = UI::UITabActivationMode::Manual,
        .tabGap = 4.0F,
        .contentGap = 6.0F,
        .wrapKeyboardNavigation = false,
    };
    constexpr UI::UIElementDescriptor viewRecipe = UI::makeTabViewElement(config);
    constexpr UI::UIElementDescriptor tabRecipe = UI::makeTabElement("General");
    static_assert(viewRecipe.tabView == config);
    static_assert(viewRecipe.pointerHitPolicy == UI::UIPointerHitPolicy::Ignore);
    static_assert(viewRecipe.semantics.role == UI::UISemanticsRole::TabList);
    static_assert(tabRecipe.tab.has_value());
    static_assert(tabRecipe.visual.styleRole == UI::UIStyleRoleId::Tab);
    static_assert(tabRecipe.behaviors ==
                  (UI::UIElementBehavior::Focusable | UI::UIElementBehavior::Activate));
    static_assert(tabRecipe.semantics.role == UI::UISemanticsRole::Tab);

    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    auto orphan = updater.createElement(root.rootNodeId(), tabRecipe);
    ASSERT_FALSE(orphan.has_value());
    EXPECT_EQ(orphan.error().code, UI::UIErrorCode::InvalidParent);

    UI::UIElementDescriptor malformed = viewRecipe;
    malformed.tabView->tabGap = -1.0F;
    auto rejected = updater.createElement(root.rootNodeId(), malformed);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);
}

TEST_F(UITabViewTest, RelationshipsRejectDuplicatesNonChildrenAndCrossRootWithoutMutation)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    auto first = createUpdater(*context, firstRoot);
    auto second = createUpdater(*context, secondRoot);
    const TabViewNodes local = createTabView(first, firstRoot.rootNodeId());
    const TabViewNodes foreign = createTabView(second, secondRoot.rootNodeId());
    ASSERT_TRUE(local.tabView.hasValue() && foreign.tabView.hasValue());

    const std::array duplicate{
        UI::UITabViewItem{.tab = local.firstTab, .panel = local.firstPanel},
        UI::UITabViewItem{.tab = local.firstTab, .panel = local.secondPanel},
    };
    Core::Status rejected = first.setTabViewItems(local.tabView, duplicate);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);

    auto descendant = first.createElement(local.firstPanel, UI::makePanelElement());
    ASSERT_TRUE(descendant.has_value());
    const std::array nonChild{
        UI::UITabViewItem{.tab = local.firstTab, .panel = *descendant},
    };
    rejected = first.setTabViewItems(local.tabView, nonChild);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);

    const std::array crossRoot{
        UI::UITabViewItem{.tab = local.firstTab, .panel = foreign.firstPanel},
    };
    rejected = first.setTabViewItems(local.tabView, crossRoot);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidNode);

    const std::array localItems{
        UI::UITabViewItem{.tab = local.firstTab, .panel = local.firstPanel},
        UI::UITabViewItem{.tab = local.secondTab, .panel = local.secondPanel},
    };
    rejected = first.setTabViewItems(local.tabView, localItems, 2);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(first.tabViewItemCount(local.tabView).value(), 2U);
    EXPECT_EQ(first.tabViewActiveTab(local.tabView).value(), local.firstTab);
}

TEST_F(UITabViewTest, LayoutPublishesPlacementMetricsAndOnlyActivePanel)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(240.0F, 120.0F)));
    const TabViewNodes nodes = createTabView(
        updater, root.rootNodeId(),
        UI::UITabViewConfig{
            .placement = UI::UITabViewPlacement::Top,
            .tabGap = 4.0F,
            .contentGap = 6.0F,
        },
        fixedSize(240.0F, 120.0F));
    ASSERT_TRUE(nodes.tabView.hasValue());

    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    const UI::UITabViewMetrics metrics = updater.tabViewMetrics(nodes.tabView).value();
    EXPECT_EQ(metrics.placement, UI::UITabViewPlacement::Top);
    EXPECT_EQ(metrics.activeTab, nodes.firstTab);
    EXPECT_EQ(metrics.activePanel, nodes.firstPanel);
    EXPECT_EQ(metrics.activeIndex, 0U);
    EXPECT_EQ(metrics.itemCount, 2U);
    EXPECT_FLOAT_EQ(metrics.tabStripRect.height, 24.0F);
    EXPECT_FLOAT_EQ(metrics.activePanelRect.height, 90.0F);

    const auto layout = context->publication().committedLayout();
    const UI::UICommittedLayoutEntry* firstPanel = findLayout(layout, nodes.firstPanel);
    const UI::UICommittedLayoutEntry* secondPanel = findLayout(layout, nodes.secondPanel);
    ASSERT_NE(firstPanel, nullptr);
    ASSERT_NE(secondPanel, nullptr);
    EXPECT_EQ(firstPanel->effectiveVisibility, UI::UIVisibility::Visible);
    EXPECT_EQ(secondPanel->effectiveVisibility, UI::UIVisibility::Collapsed);

    assertOk(updater.setTabViewActiveTab(nodes.tabView, nodes.secondTab));
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    EXPECT_EQ(updater.tabViewMetrics(nodes.tabView).value().activePanel, nodes.secondPanel);
    firstPanel = findLayout(context->publication().committedLayout(), nodes.firstPanel);
    secondPanel = findLayout(context->publication().committedLayout(), nodes.secondPanel);
    ASSERT_NE(firstPanel, nullptr);
    ASSERT_NE(secondPanel, nullptr);
    EXPECT_EQ(firstPanel->effectiveVisibility, UI::UIVisibility::Collapsed);
    EXPECT_EQ(secondPanel->effectiveVisibility, UI::UIVisibility::Visible);
}

TEST_F(UITabViewTest, BottomLeftAndRightPlacementsPublishExpectedRegions)
{
    struct PlacementCase final {
        UI::UITabViewPlacement placement{};
        UI::UILogicalRect strip{};
        UI::UILogicalRect panel{};
    };
    constexpr std::array cases{
        PlacementCase{
            .placement = UI::UITabViewPlacement::Bottom,
            .strip = {0.0F, 96.0F, 240.0F, 24.0F},
            .panel = {0.0F, 0.0F, 240.0F, 90.0F},
        },
        PlacementCase{
            .placement = UI::UITabViewPlacement::Left,
            .strip = {0.0F, 0.0F, 80.0F, 120.0F},
            .panel = {86.0F, 0.0F, 154.0F, 120.0F},
        },
        PlacementCase{
            .placement = UI::UITabViewPlacement::Right,
            .strip = {160.0F, 0.0F, 80.0F, 120.0F},
            .panel = {0.0F, 0.0F, 154.0F, 120.0F},
        },
    };

    for (const PlacementCase& testCase : cases)
    {
        SCOPED_TRACE(static_cast<int>(testCase.placement));
        auto context = createContext();
        ASSERT_NE(context, nullptr);
        auto root = createRoot(*context);
        auto updater = createUpdater(*context, root);
        assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(240.0F, 120.0F)));
        const TabViewNodes nodes = createTabView(
            updater, root.rootNodeId(),
            UI::UITabViewConfig{
                .placement = testCase.placement,
                .contentGap = 6.0F,
            },
            fixedSize(240.0F, 120.0F));
        ASSERT_TRUE(nodes.tabView.hasValue());
        assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));

        const UI::UITabViewMetrics metrics = updater.tabViewMetrics(nodes.tabView).value();
        EXPECT_EQ(metrics.placement, testCase.placement);
        expectRect(metrics.tabStripRect, testCase.strip);
        expectRect(metrics.activePanelRect, testCase.panel);
    }
}

TEST_F(UITabViewTest, AutomaticAndManualCommandsShareFocusButDifferInSelection)
{
    auto automaticContext = createContext();
    ASSERT_NE(automaticContext, nullptr);
    auto automaticRoot = createRoot(*automaticContext);
    auto automaticUpdater = createUpdater(*automaticContext, automaticRoot);
    const TabViewNodes automatic = createTabView(
        automaticUpdater, automaticRoot.rootNodeId(), {}, fixedSize(240.0F, 120.0F));
    assertOk(automaticContext->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    assertOk(automaticContext->input().requestFocus(automatic.firstTab));
    auto automaticResult = automaticUpdater.routeTabViewCommand(
        automatic.tabView, UI::UITabViewCommand::Next);
    ASSERT_TRUE(automaticResult.has_value()) << automaticResult.error().message;
    EXPECT_TRUE(automaticResult->focusChanged);
    EXPECT_TRUE(automaticResult->selectionChanged);
    EXPECT_EQ(automaticContext->input().defaultActionFocus(), automatic.secondTab);
    EXPECT_EQ(automaticUpdater.tabViewActiveTab(automatic.tabView).value(), automatic.secondTab);

    auto manualContext = createContext();
    ASSERT_NE(manualContext, nullptr);
    auto manualRoot = createRoot(*manualContext);
    auto manualUpdater = createUpdater(*manualContext, manualRoot);
    const TabViewNodes manual = createTabView(
        manualUpdater, manualRoot.rootNodeId(),
        UI::UITabViewConfig{.activationMode = UI::UITabActivationMode::Manual},
        fixedSize(240.0F, 120.0F));
    assertOk(manualContext->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    assertOk(manualContext->input().requestFocus(manual.firstTab));
    auto manualResult = manualUpdater.routeTabViewCommand(
        manual.tabView, UI::UITabViewCommand::Next);
    ASSERT_TRUE(manualResult.has_value()) << manualResult.error().message;
    EXPECT_TRUE(manualResult->focusChanged);
    EXPECT_FALSE(manualResult->selectionChanged);
    EXPECT_EQ(manualContext->input().defaultActionFocus(), manual.secondTab);
    EXPECT_EQ(manualUpdater.tabViewActiveTab(manual.tabView).value(), manual.firstTab);

    manualResult = manualUpdater.routeTabViewCommand(
        manual.tabView, UI::UITabViewCommand::Next);
    ASSERT_TRUE(manualResult.has_value());
    EXPECT_EQ(manualContext->input().defaultActionFocus(), manual.firstTab);
}

TEST_F(UITabViewTest, CommandsHonorNonWrappingEdgesAndSkipDisabledTabs)
{
    auto boundedContext = createContext();
    ASSERT_NE(boundedContext, nullptr);
    auto boundedRoot = createRoot(*boundedContext);
    auto boundedUpdater = createUpdater(*boundedContext, boundedRoot);
    const TabViewNodes bounded = createTabView(
        boundedUpdater, boundedRoot.rootNodeId(),
        UI::UITabViewConfig{.wrapKeyboardNavigation = false},
        fixedSize(240.0F, 120.0F));
    assertOk(boundedContext->publication().commitLayout({.width = 240.0F, .height = 120.0F}));

    assertOk(boundedContext->input().requestFocus(bounded.secondTab));
    auto atEnd = boundedUpdater.routeTabViewCommand(
        bounded.tabView, UI::UITabViewCommand::Next);
    ASSERT_TRUE(atEnd.has_value()) << atEnd.error().message;
    EXPECT_FALSE(atEnd->focusChanged);
    EXPECT_FALSE(atEnd->selectionChanged);
    EXPECT_EQ(atEnd->tab, bounded.secondTab);
    EXPECT_EQ(boundedContext->input().defaultActionFocus(), bounded.secondTab);

    assertOk(boundedContext->input().requestFocus(bounded.firstTab));
    auto atStart = boundedUpdater.routeTabViewCommand(
        bounded.tabView, UI::UITabViewCommand::Previous);
    ASSERT_TRUE(atStart.has_value()) << atStart.error().message;
    EXPECT_FALSE(atStart->focusChanged);
    EXPECT_FALSE(atStart->selectionChanged);
    EXPECT_EQ(atStart->tab, bounded.firstTab);
    EXPECT_EQ(boundedContext->input().defaultActionFocus(), bounded.firstTab);

    auto skippingContext = createContext();
    ASSERT_NE(skippingContext, nullptr);
    auto skippingRoot = createRoot(*skippingContext);
    auto skippingUpdater = createUpdater(*skippingContext, skippingRoot);
    const TabViewNodes skipping = createTabView(
        skippingUpdater, skippingRoot.rootNodeId(), {}, fixedSize(240.0F, 120.0F));
    auto thirdTab = skippingUpdater.createElement(
        skipping.tabView, UI::makeTabElement("Third", {}, fixedSize(70.0F, 24.0F)));
    auto thirdPanel = skippingUpdater.createElement(
        skipping.tabView, UI::makePanelElement());
    ASSERT_TRUE(thirdTab && thirdPanel);
    const std::array items{
        UI::UITabViewItem{.tab = skipping.firstTab, .panel = skipping.firstPanel},
        UI::UITabViewItem{.tab = skipping.secondTab, .panel = skipping.secondPanel},
        UI::UITabViewItem{.tab = *thirdTab, .panel = *thirdPanel},
    };
    assertOk(skippingUpdater.setTabViewItems(skipping.tabView, items));
    assertOk(skippingContext->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    assertOk(skippingContext->input().requestFocus(skipping.firstTab));
    assertOk(skippingUpdater.setEnabled(skipping.secondTab, false));
    assertOk(skippingContext->publication().commitLayout({.width = 240.0F, .height = 120.0F}));

    auto skipped = skippingUpdater.routeTabViewCommand(
        skipping.tabView, UI::UITabViewCommand::Next);
    ASSERT_TRUE(skipped.has_value()) << skipped.error().message;
    EXPECT_EQ(skipped->tab, *thirdTab);
    EXPECT_TRUE(skipped->focusChanged);
    EXPECT_TRUE(skipped->selectionChanged);
    EXPECT_EQ(skippingContext->input().defaultActionFocus(), *thirdTab);
    EXPECT_EQ(skippingUpdater.tabViewActiveTab(skipping.tabView).value(), *thirdTab);
}

TEST_F(UITabViewTest, PointerAndAccessibilityActivationUseTheSharedSelectionPath)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(240.0F, 120.0F)));
    const TabViewNodes nodes = createTabView(
        updater, root.rootNodeId(), {}, fixedSize(240.0F, 120.0F));
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));

    const UI::UICommittedLayoutEntry* second = findLayout(context->publication().committedLayout(), nodes.secondTab);
    ASSERT_NE(second, nullptr);
    const UI::UILogicalPoint point{
        .x = second->worldRect.x + second->worldRect.width * 0.5F,
        .y = second->worldRect.y + second->worldRect.height * 0.5F,
    };
    auto down = context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 1, point));
    auto up = context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 2, point));
    ASSERT_TRUE(down.has_value()) << down.error().message;
    ASSERT_TRUE(up.has_value()) << up.error().message;
    EXPECT_EQ(updater.tabViewActiveTab(nodes.tabView).value(), nodes.secondTab);
    EXPECT_EQ(context->input().defaultActionFocus(), nodes.secondTab);

    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    assertOk(context->input().performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::Invoke,
        .node = nodes.firstTab,
    }));
    EXPECT_EQ(updater.tabViewActiveTab(nodes.tabView).value(), nodes.firstTab);
}

TEST_F(UITabViewTest, SemanticsPublishTabListTabsSelectedStateAndActiveTabPanel)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const TabViewNodes nodes = createTabView(
        updater, root.rootNodeId(), {}, fixedSize(240.0F, 120.0F), true);
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));

    const UI::UISemanticsEntry* view = findSemantics(context->publication().committedSemantics(), nodes.tabView);
    const UI::UISemanticsEntry* firstTab = findSemantics(context->publication().committedSemantics(), nodes.firstTab);
    const UI::UISemanticsEntry* secondTab = findSemantics(context->publication().committedSemantics(), nodes.secondTab);
    const UI::UISemanticsEntry* firstPanel = findSemantics(context->publication().committedSemantics(), nodes.firstPanel);
    ASSERT_NE(view, nullptr);
    ASSERT_NE(firstTab, nullptr);
    ASSERT_NE(secondTab, nullptr);
    ASSERT_NE(firstPanel, nullptr);
    EXPECT_EQ(view->role, UI::UISemanticsRole::TabList);
    EXPECT_EQ(firstTab->role, UI::UISemanticsRole::Tab);
    EXPECT_TRUE(firstTab->selected);
    EXPECT_FALSE(secondTab->selected);
    EXPECT_EQ(firstPanel->role, UI::UISemanticsRole::TabPanel);
    EXPECT_EQ(findSemantics(context->publication().committedSemantics(), nodes.secondPanel), nullptr);
}

TEST_F(UITabViewTest, DedicatedTabPaintPublishesSelectedChromeAndLinkedViewRejectsExtraChildren)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const TabViewNodes nodes = createTabView(
        updater, root.rootNodeId(), {}, fixedSize(240.0F, 120.0F));
    ASSERT_TRUE(nodes.tabView.hasValue());

    const UI::UITabPaint tabPaint{
        .selectedBackgroundColor = UI::rgb(0x123456),
        .hoveredBackgroundColor = UI::rgb(0x234567),
        .focusedBackgroundColor = UI::rgb(0x345678),
        .pressedBackgroundColor = UI::rgb(0x456789),
        .disabledBackgroundColor = UI::rgb(0x56789A),
        .focusedBorderColor = UI::rgb(0xFFFFFF),
    };
    assertOk(updater.setBoxPaint(nodes.firstTab, UI::makeSolidBox(UI::rgb(0x010203))));
    assertOk(updater.setBoxPaint(nodes.secondTab, UI::makeSolidBox(UI::rgb(0x010203))));
    assertOk(updater.setTabPaint(nodes.firstTab, tabPaint));
    assertOk(updater.setTabPaint(nodes.secondTab, tabPaint));
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));

    bool selectedFillFound = false;
    for (const UI::UICommittedPaintEntry& entry : context->publication().committedPaint().entries())
    {
        if (entry.node == nodes.firstTab && entry.kind != UI::UICommittedPaintKind::Glyph &&
            entry.solidFill == UI::premultiply(tabPaint.selectedBackgroundColor))
        {
            selectedFillFound = true;
        }
    }
    EXPECT_TRUE(selectedFillFound);

    auto extra = updater.createElement(nodes.tabView, UI::makePanelElement());
    ASSERT_TRUE(extra.has_value());
    EXPECT_EQ(updater.tabViewItemCount(nodes.tabView).value(), 0U);
}

TEST_F(UITabViewTest, DestroyGenerationReuseAndFailedCommitDoNotLeakRelationshipsOrMetrics)
{
    auto context = createContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .layoutSnapshotCapacity = 6,
        .paintSnapshotCapacity = 64,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(240.0F, 120.0F)));
    const TabViewNodes nodes = createTabView(
        updater, root.rootNodeId(), {}, fixedSize(240.0F, 120.0F));
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    const UI::UITabViewMetrics published = updater.tabViewMetrics(nodes.tabView).value();

    assertOk(updater.setTabViewActiveTab(nodes.tabView, nodes.secondTab));
    auto overflow = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedSize(10.0F, 10.0F)));
    ASSERT_TRUE(overflow.has_value());
    Core::Status failed = context->publication().commitLayout({.width = 240.0F, .height = 120.0F});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(updater.tabViewMetrics(nodes.tabView).value(), published);
    EXPECT_EQ(updater.tabViewActiveTab(nodes.tabView).value(), nodes.secondTab);

    assertOk(updater.destroy(*overflow));
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    EXPECT_EQ(updater.tabViewMetrics(nodes.tabView).value().activeTab, nodes.secondTab);

    const UI::UINodeId destroyedPanel = nodes.secondPanel;
    assertOk(updater.destroy(destroyedPanel));
    EXPECT_EQ(updater.tabViewItemCount(nodes.tabView).value(), 0U);
    auto replacement = updater.createElement(nodes.tabView, UI::makePanelElement());
    ASSERT_TRUE(replacement.has_value());
    EXPECT_EQ(replacement->index(), destroyedPanel.index());
    EXPECT_NE(*replacement, destroyedPanel);
    EXPECT_EQ(updater.tabViewItemCount(nodes.tabView).value(), 0U);
}

TEST_F(UITabViewTest, ClearPublishesEmptyMetricsWithoutReusingOldGeometry)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(240.0F, 120.0F)));
    const TabViewNodes nodes = createTabView(
        updater, root.rootNodeId(),
        UI::UITabViewConfig{
            .placement = UI::UITabViewPlacement::Right,
            .contentGap = 6.0F,
        },
        fixedSize(240.0F, 120.0F));
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    ASSERT_EQ(updater.tabViewMetrics(nodes.tabView).value().itemCount, 2U);

    assertOk(updater.clearTabViewItems(nodes.tabView));
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    const UI::UITabViewMetrics cleared = updater.tabViewMetrics(nodes.tabView).value();
    EXPECT_EQ(cleared.placement, UI::UITabViewPlacement::Right);
    EXPECT_EQ(cleared.itemCount, 0U);
    EXPECT_FALSE(cleared.activeTab.hasValue());
    EXPECT_FALSE(cleared.activePanel.hasValue());
    expectRect(cleared.tabStripRect, {});
    expectRect(cleared.activePanelRect, {});
}

TEST_F(UITabViewTest, RelationshipAndCommandCapacityFailuresPreserveStateAtomically)
{
    auto relationshipContext = createContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 2,
        .paintSnapshotCapacity = 64,
    });
    ASSERT_NE(relationshipContext, nullptr);
    auto relationshipRoot = createRoot(*relationshipContext);
    auto relationshipUpdater = createUpdater(*relationshipContext, relationshipRoot);
    const TabViewNodes relationship = createTabView(
        relationshipUpdater, relationshipRoot.rootNodeId(), {}, fixedSize(240.0F, 120.0F));
    ASSERT_TRUE(relationship.tabView.hasValue());
    assertOk(relationshipContext->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    assertOk(relationshipUpdater.setBoxPaint(
        relationship.firstPanel, UI::makeSolidBox(UI::rgb(0x123456))));
    const std::array relationshipItems{
        UI::UITabViewItem{.tab = relationship.firstTab, .panel = relationship.firstPanel},
        UI::UITabViewItem{.tab = relationship.secondTab, .panel = relationship.secondPanel},
    };

    Core::Status rejected = relationshipUpdater.setTabViewItems(
        relationship.tabView, relationshipItems, 1);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(relationshipUpdater.tabViewItemCount(relationship.tabView).value(), 2U);
    EXPECT_EQ(relationshipUpdater.tabViewActiveTab(relationship.tabView).value(), relationship.firstTab);
    rejected = relationshipUpdater.clearTabViewItems(relationship.tabView);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(relationshipUpdater.tabViewItemCount(relationship.tabView).value(), 2U);

    auto commandContext = createContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 5,
        .paintSnapshotCapacity = 64,
    });
    ASSERT_NE(commandContext, nullptr);
    auto commandRoot = createRoot(*commandContext);
    auto commandUpdater = createUpdater(*commandContext, commandRoot);
    const TabViewNodes command = createTabView(
        commandUpdater, commandRoot.rootNodeId(), {}, fixedSize(240.0F, 120.0F));
    ASSERT_TRUE(command.tabView.hasValue());
    assertOk(commandContext->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    assertOk(commandContext->input().requestFocus(command.firstTab));
    assertOk(commandContext->publication().commitLayout({.width = 240.0F, .height = 120.0F}));

    auto commandRejected = commandUpdater.routeTabViewCommand(
        command.tabView, UI::UITabViewCommand::Next);
    ASSERT_FALSE(commandRejected.has_value());
    EXPECT_EQ(commandRejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(commandContext->input().defaultActionFocus(), command.firstTab);
    EXPECT_EQ(commandUpdater.tabViewActiveTab(command.tabView).value(), command.firstTab);
}

} // namespace
} // namespace Tina::Tests
