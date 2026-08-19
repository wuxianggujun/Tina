#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <array>
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

[[nodiscard]] constexpr UI::UILayoutStyle overlay(float x, float y, float width,
                                                   float height) noexcept
{
    UI::UILayoutStyle style = fixedSize(width, height);
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.offset.x = UI::UILayoutLength::Px(x);
    style.overlay.offset.y = UI::UILayoutLength::Px(y);
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

[[nodiscard]] const UI::UICommittedLayoutEntry*
findLayout(UI::UICommittedLayoutView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UICommittedLayoutEntry& entry) {
            return entry.node == node;
        });
    return found != view.end() ? &*found : nullptr;
}

[[nodiscard]] const UI::UISemanticsEntry*
findSemantics(UI::UICommittedSemanticsView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UISemanticsEntry& entry) {
            return entry.node == node;
        });
    return found != view.end() ? &*found : nullptr;
}

[[nodiscard]] UI::UILogicalPoint centerOf(
    const UI::UICommittedLayoutEntry& entry) noexcept
{
    return {
        .x = entry.worldRect.x + entry.worldRect.width * 0.5F,
        .y = entry.worldRect.y + entry.worldRect.height * 0.5F,
    };
}

struct MenuPair final {
    UI::UINodeId anchor{};
    UI::UINodeId menu{};
};

[[nodiscard]] MenuPair createPair(
    UI::UITreeUpdater& updater, UI::UINodeId parent,
    UI::UIMenuConfig config = {},
    UI::UILayoutStyle anchorLayout = overlay(20.0F, 20.0F, 60.0F, 24.0F),
    UI::UILayoutStyle menuLayout = fixedSize(100.0F, 60.0F))
{
    auto anchor = updater.createElement(
        parent, UI::makeButtonElement("Anchor", anchorLayout));
    auto menu = updater.createElement(
        parent, UI::makeMenuElement(config, menuLayout));
    EXPECT_TRUE(anchor.has_value()) << (anchor ? "" : anchor.error().message);
    EXPECT_TRUE(menu.has_value()) << (menu ? "" : menu.error().message);
    if (!anchor || !menu)
    {
        return {};
    }
    EXPECT_TRUE(updater.setMenuAnchor(*menu, *anchor).has_value());
    return {.anchor = *anchor, .menu = *menu};
}

class UIMenuTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(4);
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
                ? 64
                : capacities.nodeCapacity;
        capacities.rootCapacity =
            capacities.rootCapacity == UI::UIContextCapacityConfig::DefaultRootCapacity
                ? 2
                : capacities.rootCapacity;
        if (capacities.routePathCapacity == 0)
        {
            capacities.routePathCapacity = capacities.nodeCapacity;
        }
        if (capacities.paintSnapshotCapacity == 0)
        {
            capacities.paintSnapshotCapacity = capacities.nodeCapacity * 16U;
        }
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

TEST_F(UIMenuTest, RecipesPublishDedicatedContractsAndRejectMalformedAuthoring)
{
    constexpr UI::UIMenuConfig config{
        .placement = UI::UIMenuPlacement::Left,
        .anchorGap = 7.0F,
        .viewportMargin = 11.0F,
        .matchAnchorWidth = true,
        .wrapKeyboardNavigation = false,
        .closeOnActivate = false,
    };
    constexpr UI::UIElementDescriptor menuRecipe =
        UI::makeMenuElement(config, fixedSize(120.0F, 80.0F));
    constexpr UI::UIElementDescriptor commandRecipe =
        UI::makeMenuItemElement("Command");
    constexpr UI::UIElementDescriptor checkRecipe = UI::makeMenuItemElement(
        "Check", {.kind = UI::UIMenuItemKind::Check, .checked = true});
    constexpr UI::UIElementDescriptor radioRecipe = UI::makeMenuItemElement(
        "Radio", {.kind = UI::UIMenuItemKind::Radio, .radioGroup = 3, .checked = true});
    constexpr UI::UIElementDescriptor separatorRecipe = UI::makeMenuItemElement(
        "ignored", {.kind = UI::UIMenuItemKind::Separator});

    static_assert(menuRecipe.menu == config);
    static_assert(menuRecipe.layout.placement == UI::UILayoutPlacement::Overlay);
    static_assert(menuRecipe.pointerHitPolicy == UI::UIPointerHitPolicy::Ignore);
    static_assert(menuRecipe.focusScopeMode == UI::UIFocusScopeMode::Contain);
    static_assert(menuRecipe.semantics.role == UI::UISemanticsRole::Menu);
    static_assert(menuRecipe.behaviors == UI::UIElementBehavior::None);
    static_assert(commandRecipe.behaviors ==
                  (UI::UIElementBehavior::Focusable | UI::UIElementBehavior::Activate));
    static_assert(commandRecipe.pointerHitPolicy == UI::UIPointerHitPolicy::Targetable);
    static_assert(commandRecipe.semantics.actions ==
                  (UI::UISemanticsAction::Focus | UI::UISemanticsAction::Activate));
    static_assert(UI::hasSemanticsAction(checkRecipe.semantics.actions,
                                        UI::UISemanticsAction::Toggle));
    static_assert(UI::hasSemanticsAction(radioRecipe.semantics.actions,
                                        UI::UISemanticsAction::Toggle));
    static_assert(separatorRecipe.semantics.mode == UI::UISemanticsMode::Exclude);
    static_assert(separatorRecipe.pointerHitPolicy == UI::UIPointerHitPolicy::Ignore);
    static_assert(!separatorRecipe.text.has_value());

    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    auto orphan = updater.createElement(root.rootNodeId(), commandRecipe);
    ASSERT_FALSE(orphan.has_value());
    EXPECT_EQ(orphan.error().code, UI::UIErrorCode::InvalidParent);
    auto menu = updater.createElement(root.rootNodeId(), menuRecipe);
    ASSERT_TRUE(menu.has_value()) << menu.error().message;
    auto command = updater.createElement(*menu, commandRecipe);
    ASSERT_TRUE(command.has_value()) << command.error().message;
    auto invalidMenuChild = updater.createElement(*menu, UI::makePanelElement());
    ASSERT_FALSE(invalidMenuChild.has_value());
    EXPECT_EQ(invalidMenuChild.error().code, UI::UIErrorCode::InvalidParent);
    auto invalidItemChild = updater.createElement(*command, UI::makeLabelElement("child"));
    ASSERT_FALSE(invalidItemChild.has_value());
    EXPECT_EQ(invalidItemChild.error().code, UI::UIErrorCode::InvalidParent);

    Core::Status invalidPolicy = updater.setPointerHitPolicy(
        *menu, UI::UIPointerHitPolicy::Targetable);
    ASSERT_FALSE(invalidPolicy.has_value());
    EXPECT_EQ(invalidPolicy.error().code, UI::UIErrorCode::InvalidPointerPolicy);

    UI::UIElementDescriptor malformed = menuRecipe;
    malformed.menu->anchorGap = -1.0F;
    auto rejected = updater.createElement(root.rootNodeId(), malformed);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    malformed = commandRecipe;
    malformed.menuItem->checked = true;
    rejected = updater.createElement(*menu, malformed);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    malformed = checkRecipe;
    malformed.menuItem->radioGroup = 1;
    rejected = updater.createElement(*menu, malformed);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    auto firstCheckedRadio = updater.createElement(*menu, radioRecipe);
    ASSERT_TRUE(firstCheckedRadio.has_value()) << firstCheckedRadio.error().message;
    rejected = updater.createElement(*menu, radioRecipe);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);
    auto uncheckedPeer = updater.createElement(
        *menu, UI::makeMenuItemElement(
                   "Unchecked", {.kind = UI::UIMenuItemKind::Radio, .radioGroup = 3}));
    ASSERT_TRUE(uncheckedPeer.has_value()) << uncheckedPeer.error().message;
}

TEST_F(UIMenuTest, AnchorRelationsRejectCyclesCrossRootStaleAndIncompatibleNodes)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    auto first = createUpdater(*context, firstRoot);
    auto second = createUpdater(*context, secondRoot);

    auto container = first.createElement(firstRoot.rootNodeId(), UI::makePanelElement());
    auto anchor = first.createElement(firstRoot.rootNodeId(), UI::makeButtonElement("Anchor"));
    auto menu = first.createElement(*container, UI::makeMenuElement());
    auto item = first.createElement(*menu, UI::makeMenuItemElement("Item"));
    auto secondMenu = first.createElement(firstRoot.rootNodeId(), UI::makeMenuElement());
    auto foreignAnchor = second.createElement(
        secondRoot.rootNodeId(), UI::makeButtonElement("Foreign"));
    ASSERT_TRUE(container && anchor && menu && item && secondMenu && foreignAnchor);

    assertOk(first.setMenuAnchor(*menu, *anchor));
    EXPECT_EQ(first.menuAnchor(*menu).value(), *anchor);
    assertOk(first.setMenuAnchor(*menu, *anchor));

    Core::Status rejected = context->setMenuAnchor(*menu, *menu);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);
    rejected = context->setMenuAnchor(*menu, *container);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);
    rejected = context->setMenuAnchor(*menu, *item);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);
    rejected = context->setMenuAnchor(*menu, *foreignAnchor);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);
    rejected = first.setMenuAnchor(*secondMenu, *anchor);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);

    const UI::UINodeId staleAnchor = *anchor;
    assertOk(first.destroy(*anchor));
    EXPECT_FALSE(first.menuAnchor(*menu).value().hasValue());
    rejected = context->setMenuAnchor(*menu, staleAnchor);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidNode);
    rejected = first.setMenuOpen(*menu, true);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);
}

TEST_F(UIMenuTest, PlacementSupportsFourDirectionsAutoFlipClampAndAnchorWidth)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const auto configFor = [](UI::UIMenuPlacement placement, float margin = 8.0F,
                              bool matchWidth = false) {
        return UI::UIMenuConfig{
            .placement = placement,
            .anchorGap = 5.0F,
            .viewportMargin = margin,
            .matchAnchorWidth = matchWidth,
        };
    };

    const MenuPair above = createPair(
        updater, root.rootNodeId(), configFor(UI::UIMenuPlacement::Above),
        overlay(80, 60, 20, 20), fixedSize(40, 20));
    const MenuPair below = createPair(
        updater, root.rootNodeId(), configFor(UI::UIMenuPlacement::Below),
        overlay(80, 60, 20, 20), fixedSize(40, 20));
    const MenuPair left = createPair(
        updater, root.rootNodeId(), configFor(UI::UIMenuPlacement::Left),
        overlay(80, 60, 20, 20), fixedSize(40, 20));
    const MenuPair right = createPair(
        updater, root.rootNodeId(), configFor(UI::UIMenuPlacement::Right),
        overlay(80, 60, 20, 20), fixedSize(40, 20));
    const MenuPair automatic = createPair(
        updater, root.rootNodeId(), configFor(UI::UIMenuPlacement::Auto),
        overlay(80, 130, 20, 20), fixedSize(40, 20));
    const MenuPair explicitFlip = createPair(
        updater, root.rootNodeId(), configFor(UI::UIMenuPlacement::Below),
        overlay(130, 130, 20, 20), fixedSize(40, 20));
    const MenuPair clamped = createPair(
        updater, root.rootNodeId(), configFor(UI::UIMenuPlacement::Below, 10.0F),
        overlay(0, 50, 20, 20), fixedSize(80, 20));
    const MenuPair matched = createPair(
        updater, root.rootNodeId(), configFor(UI::UIMenuPlacement::Below, 8.0F, true),
        overlay(120, 30, 30, 20), fixedSize(80, 20));
    ASSERT_TRUE(above.menu.hasValue() && below.menu.hasValue() && left.menu.hasValue() &&
                right.menu.hasValue() && automatic.menu.hasValue() &&
                explicitFlip.menu.hasValue() && clamped.menu.hasValue() &&
                matched.menu.hasValue());
    assertOk(context->commitLayout({.width = 200.0F, .height = 160.0F}));

    const auto expectPlacement = [&](MenuPair pair, UI::UIMenuPlacement placement,
                                     float x, float y, float width) {
        assertOk(updater.setMenuOpen(pair.menu, true));
        assertOk(context->commitLayout({.width = 200.0F, .height = 160.0F}));
        const UI::UIMenuMetrics metrics = updater.menuMetrics(pair.menu).value();
        EXPECT_TRUE(metrics.open);
        EXPECT_EQ(metrics.resolvedPlacement, placement);
        EXPECT_FLOAT_EQ(metrics.menuRect.x, x);
        EXPECT_FLOAT_EQ(metrics.menuRect.y, y);
        EXPECT_FLOAT_EQ(metrics.menuRect.width, width);
    };
    expectPlacement(above, UI::UIMenuPlacement::Above, 80.0F, 35.0F, 40.0F);
    expectPlacement(below, UI::UIMenuPlacement::Below, 80.0F, 85.0F, 40.0F);
    expectPlacement(left, UI::UIMenuPlacement::Left, 35.0F, 60.0F, 40.0F);
    expectPlacement(right, UI::UIMenuPlacement::Right, 105.0F, 60.0F, 40.0F);
    expectPlacement(automatic, UI::UIMenuPlacement::Above, 80.0F, 105.0F, 40.0F);
    expectPlacement(explicitFlip, UI::UIMenuPlacement::Above, 130.0F, 105.0F, 40.0F);
    expectPlacement(clamped, UI::UIMenuPlacement::Below, 10.0F, 75.0F, 80.0F);
    expectPlacement(matched, UI::UIMenuPlacement::Below, 120.0F, 55.0F, 30.0F);

    assertOk(updater.setMenuOpen(matched.menu, false));
    assertOk(context->commitLayout({.width = 200.0F, .height = 160.0F}));
    EXPECT_FALSE(updater.menuMetrics(matched.menu).value().open);
    const auto* collapsed = findLayout(context->committedLayout(), matched.menu);
    ASSERT_NE(collapsed, nullptr);
    EXPECT_EQ(collapsed->effectiveVisibility, UI::UIVisibility::Collapsed);
}

TEST_F(UIMenuTest, CommandsSkipUnavailableItemsWrapAndShareActivationState)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const MenuPair pair = createPair(
        updater, root.rootNodeId(),
        UI::UIMenuConfig{.closeOnActivate = false},
        overlay(20, 10, 80, 24), fixedSize(140, 128));
    ASSERT_TRUE(pair.menu.hasValue());

    auto command = updater.createElement(
        pair.menu, UI::makeMenuItemElement("Command", {}, fixedSize(132, 20)));
    auto separator = updater.createElement(
        pair.menu, UI::makeMenuItemElement(
                       {}, {.kind = UI::UIMenuItemKind::Separator}, fixedSize(132, 8)));
    auto disabled = updater.createElement(
        pair.menu, UI::makeMenuItemElement("Disabled", {}, fixedSize(132, 20)));
    auto check = updater.createElement(
        pair.menu, UI::makeMenuItemElement(
                       "Check", {.kind = UI::UIMenuItemKind::Check}, fixedSize(132, 20)));
    auto firstRadio = updater.createElement(
        pair.menu, UI::makeMenuItemElement(
                       "Radio A",
                       {.kind = UI::UIMenuItemKind::Radio, .radioGroup = 7, .checked = true},
                       fixedSize(132, 20)));
    auto secondRadio = updater.createElement(
        pair.menu, UI::makeMenuItemElement(
                       "Radio B",
                       {.kind = UI::UIMenuItemKind::Radio, .radioGroup = 7},
                       fixedSize(132, 20)));
    ASSERT_TRUE(command.has_value()) << command.error().message;
    ASSERT_TRUE(separator.has_value()) << separator.error().message;
    ASSERT_TRUE(disabled.has_value()) << disabled.error().message;
    ASSERT_TRUE(check.has_value()) << check.error().message;
    ASSERT_TRUE(firstRadio.has_value()) << firstRadio.error().message;
    ASSERT_TRUE(secondRadio.has_value()) << secondRadio.error().message;
    assertOk(updater.setEnabled(*disabled, false));
    usize commandActivations = 0;
    assertOk(updater.setButtonAction(
        *command, UI::UIButtonActionCallback(
                      [&](const UI::UIButtonActionEvent&) noexcept { ++commandActivations; })));

    assertOk(context->commitLayout({.width = 220.0F, .height = 180.0F}));
    assertOk(updater.setMenuOpen(pair.menu, true));
    assertOk(context->commitLayout({.width = 220.0F, .height = 180.0F}));

    auto first = updater.routeMenuCommand(pair.menu, UI::UIMenuCommand::First);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_TRUE(first->consumed);
    EXPECT_EQ(first->focus, *command);
    auto activated = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1}, 1, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(activated.has_value()) << activated.error().message;
    EXPECT_TRUE(activated->activated);
    EXPECT_EQ(commandActivations, 1U);
    EXPECT_TRUE(updater.isMenuOpen(pair.menu).value());

    auto next = updater.routeMenuCommand(pair.menu, UI::UIMenuCommand::Next);
    ASSERT_TRUE(next.has_value()) << next.error().message;
    EXPECT_EQ(next->focus, *check);
    activated = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{2}, 2, UI::UIButtonActivationSource::Gamepad);
    ASSERT_TRUE(activated.has_value()) << activated.error().message;
    EXPECT_TRUE(updater.isMenuItemChecked(*check).value());

    assertOk(context->commitLayout({.width = 220.0F, .height = 180.0F}));
    const UI::UISemanticsEntry* commandSemantics =
        findSemantics(context->committedSemantics(), *command);
    const UI::UISemanticsEntry* checkSemantics =
        findSemantics(context->committedSemantics(), *check);
    ASSERT_NE(commandSemantics, nullptr);
    ASSERT_NE(checkSemantics, nullptr);
    EXPECT_EQ(commandSemantics->role, UI::UISemanticsRole::MenuItem);
    EXPECT_FALSE(UI::hasSemanticsAction(commandSemantics->actions,
                                        UI::UISemanticsAction::Toggle));
    EXPECT_TRUE(UI::hasSemanticsAction(checkSemantics->actions,
                                       UI::UISemanticsAction::Toggle));
    EXPECT_TRUE(checkSemantics->checked);
    EXPECT_EQ(findSemantics(context->committedSemantics(), *separator), nullptr);

    assertOk(context->performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::Toggle,
        .node = *check,
    }));
    EXPECT_FALSE(updater.isMenuItemChecked(*check).value());

    assertOk(context->requestFocus(*secondRadio));
    activated = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{3}, 3, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(activated.has_value()) << activated.error().message;
    EXPECT_FALSE(updater.isMenuItemChecked(*firstRadio).value());
    EXPECT_TRUE(updater.isMenuItemChecked(*secondRadio).value());
    Core::Status rejected = updater.setMenuItemChecked(*secondRadio, false);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);

    auto last = updater.routeMenuCommand(pair.menu, UI::UIMenuCommand::Last);
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->focus, *secondRadio);
    next = updater.routeMenuCommand(pair.menu, UI::UIMenuCommand::Next);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->focus, *command);
    auto dismissed = updater.routeMenuCommand(pair.menu, UI::UIMenuCommand::Dismiss);
    ASSERT_TRUE(dismissed.has_value());
    EXPECT_TRUE(dismissed->dismissed);
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());
    EXPECT_EQ(context->defaultActionFocus(), pair.anchor);

    const MenuPair closingPair = createPair(
        updater, root.rootNodeId(), {}, overlay(140, 10, 60, 24), fixedSize(80, 32));
    auto closingCommand = updater.createElement(
        closingPair.menu, UI::makeMenuItemElement("Close", {}, fixedSize(72, 24)));
    ASSERT_TRUE(closingCommand.has_value());
    assertOk(context->commitLayout({.width = 240.0F, .height = 180.0F}));
    assertOk(context->requestFocus(closingPair.anchor));
    assertOk(updater.setMenuOpen(closingPair.menu, true));
    assertOk(context->commitLayout({.width = 240.0F, .height = 180.0F}));
    assertOk(context->requestFocus(*closingCommand));
    activated = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{4}, 4, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(activated.has_value()) << activated.error().message;
    EXPECT_TRUE(activated->activated);
    EXPECT_FALSE(updater.isMenuOpen(closingPair.menu).value());
    EXPECT_EQ(context->defaultActionFocus(), closingPair.anchor);
}

TEST_F(UIMenuTest, PointerBarrierBlocksChromeAndOutsideClickThroughButItemsActivate)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const MenuPair pair = createPair(
        updater, root.rootNodeId(),
        UI::UIMenuConfig{.placement = UI::UIMenuPlacement::Below},
        overlay(10, 10, 40, 20), fixedSize(100, 60));
    auto item = updater.createElement(
        pair.menu, UI::makeMenuItemElement(
                       "Check", {.kind = UI::UIMenuItemKind::Check}, fixedSize(92, 20)));
    auto underlay = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Underlay", overlay(10, 34, 100, 60)));
    auto outside = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Outside", overlay(140, 10, 60, 24)));
    ASSERT_TRUE(item.has_value()) << item.error().message;
    ASSERT_TRUE(underlay.has_value()) << underlay.error().message;
    ASSERT_TRUE(outside.has_value()) << outside.error().message;
    usize underlayActivations = 0;
    usize outsideActivations = 0;
    assertOk(updater.setButtonAction(
        *underlay, UI::UIButtonActionCallback(
                       [&](const UI::UIButtonActionEvent&) noexcept { ++underlayActivations; })));
    assertOk(updater.setButtonAction(
        *outside, UI::UIButtonActionCallback(
                      [&](const UI::UIButtonActionEvent&) noexcept { ++outsideActivations; })));

    assertOk(context->commitLayout({.width = 220.0F, .height = 140.0F}));
    assertOk(updater.setMenuOpen(pair.menu, true));
    assertOk(context->commitLayout({.width = 220.0F, .height = 140.0F}));
    EXPECT_EQ(context->activeMenu(), pair.menu);
    const UI::UIPointerHitQueryResult blankHit =
        context->queryPointerHit({.x = 11.0F, .y = 35.0F});
    ASSERT_TRUE(blankHit.hasTarget());
    EXPECT_EQ(blankHit.target.node, *underlay);
    EXPECT_FALSE(blankHit.modalBarrierActive);

    auto down = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 11, .y = 35}));
    ASSERT_TRUE(down.has_value()) << down.error().message;
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(updater.isMenuOpen(pair.menu).value());
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_FALSE(updater.isButtonPressed(*underlay).value());
    auto up = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 2, {.x = 11, .y = 35}));
    ASSERT_TRUE(up.has_value()) << up.error().message;
    EXPECT_TRUE(up->consumed);
    EXPECT_EQ(underlayActivations, 0U);

    down = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 3, {.x = 30, .y = 20}));
    ASSERT_TRUE(down.has_value()) << down.error().message;
    EXPECT_TRUE(updater.isMenuOpen(pair.menu).value());
    up = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 4, {.x = 30, .y = 20}));
    ASSERT_TRUE(up.has_value()) << up.error().message;
    EXPECT_TRUE(updater.isMenuOpen(pair.menu).value());

    down = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 5, {.x = 170, .y = 20}));
    ASSERT_TRUE(down.has_value()) << down.error().message;
    EXPECT_TRUE(down->consumed);
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());
    up = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 6, {.x = 170, .y = 20}));
    ASSERT_TRUE(up.has_value()) << up.error().message;
    EXPECT_TRUE(up->consumed);
    EXPECT_EQ(outsideActivations, 0U);

    assertOk(context->commitLayout({.width = 220.0F, .height = 140.0F}));
    assertOk(updater.setMenuOpen(pair.menu, true));
    assertOk(context->commitLayout({.width = 220.0F, .height = 140.0F}));
    const auto* itemLayout = findLayout(context->committedLayout(), *item);
    ASSERT_NE(itemLayout, nullptr);
    const UI::UILogicalPoint itemCenter = centerOf(*itemLayout);
    down = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 7, itemCenter));
    ASSERT_TRUE(down.has_value()) << down.error().message;
    up = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 8, itemCenter));
    ASSERT_TRUE(up.has_value()) << up.error().message;
    EXPECT_TRUE(up->consumed);
    EXPECT_TRUE(updater.isMenuItemChecked(*item).value());
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());
}

TEST_F(UIMenuTest, MenuAndPopupShareOneTransientOverlaySlot)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const MenuPair first = createPair(
        updater, root.rootNodeId(), {}, overlay(10, 10, 40, 20), fixedSize(80, 40));
    const MenuPair second = createPair(
        updater, root.rootNodeId(), {}, overlay(70, 10, 40, 20), fixedSize(80, 40));
    auto dropdown = updater.createElement(
        root.rootNodeId(), UI::makeDropdownElement("Dropdown", overlay(130, 10, 70, 24)));
    ASSERT_TRUE(first.menu.hasValue() && second.menu.hasValue() && dropdown);
    auto popup = updater.createElement(*dropdown, UI::makePopupElement(fixedSize(70, 40)));
    ASSERT_TRUE(popup.has_value()) << popup.error().message;
    auto popupItem = updater.createElement(*popup, UI::makeDropdownItemElement("Popup item"));
    ASSERT_TRUE(popupItem.has_value()) << popupItem.error().message;
    Core::Status rejected = updater.setMenuAnchor(first.menu, *popupItem);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);
    assertOk(context->commitLayout({.width = 240.0F, .height = 140.0F}));

    assertOk(updater.setMenuOpen(first.menu, true));
    EXPECT_EQ(context->activeMenu(), first.menu);
    assertOk(updater.setMenuOpen(second.menu, true));
    EXPECT_FALSE(updater.isMenuOpen(first.menu).value());
    EXPECT_TRUE(updater.isMenuOpen(second.menu).value());
    EXPECT_EQ(context->activeMenu(), second.menu);

    assertOk(updater.setPopupOpen(*popup, true));
    EXPECT_FALSE(context->activeMenu().hasValue());
    EXPECT_EQ(context->activePopup(), *popup);
    assertOk(updater.setMenuOpen(first.menu, true));
    EXPECT_FALSE(context->activePopup().hasValue());
    EXPECT_EQ(context->activeMenu(), first.menu);
}

TEST_F(UIMenuTest, InputVisibilityDisableDestroyAndModalChangesDismiss)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UILayoutStyle anchorLayout = overlay(20, 20, 60, 24);
    UI::UILayoutStyle menuLayout = fixedSize(90, 48);
    menuLayout.placement = UI::UILayoutPlacement::Overlay;
    const MenuPair pair = createPair(
        updater, root.rootNodeId(), {}, anchorLayout, menuLayout);
    ASSERT_TRUE(pair.menu.hasValue());
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    const auto open = [&] {
        assertOk(updater.setMenuOpen(pair.menu, true));
        assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
        EXPECT_TRUE(updater.isMenuOpen(pair.menu).value());
    };

    open();
    auto wheel = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Wheel, 1, {.x = 180, .y = 100}));
    ASSERT_TRUE(wheel.has_value()) << wheel.error().message;
    EXPECT_TRUE(wheel->consumed);
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());

    open();
    auto text = context->routeTextInput(
        window, Platform::PlatformFrameId{2}, 2, "x");
    ASSERT_TRUE(text.has_value()) << text.error().message;
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());

    open();
    assertOk(updater.setEnabled(pair.anchor, false));
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());
    assertOk(updater.setEnabled(pair.anchor, true));

    open();
    UI::UILayoutStyle hiddenMenu = menuLayout;
    hiddenMenu.placement = UI::UILayoutPlacement::Overlay;
    hiddenMenu.visibility = UI::UIVisibility::Hidden;
    assertOk(updater.setLayoutStyle(pair.menu, hiddenMenu));
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());
    assertOk(updater.setLayoutStyle(pair.menu, menuLayout));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    open();
    UI::UILayoutStyle collapsedAnchor = anchorLayout;
    collapsedAnchor.visibility = UI::UIVisibility::Collapsed;
    assertOk(updater.setLayoutStyle(pair.anchor, collapsedAnchor));
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());
    assertOk(updater.setLayoutStyle(pair.anchor, anchorLayout));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    open();
    auto modal = updater.createElement(
        root.rootNodeId(), UI::makeModalElement(overlay(100, 40, 80, 60)));
    ASSERT_TRUE(modal.has_value());
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_EQ(context->activeModal(), *modal);
    assertOk(updater.destroy(*modal));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    open();
    assertOk(updater.destroy(pair.anchor));
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());
    EXPECT_FALSE(updater.menuAnchor(pair.menu).value().hasValue());
}

TEST_F(UIMenuTest, FailedCommitPreservesCommittedMetricsAndSnapshots)
{
    auto context = createContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .layoutSnapshotCapacity = 3,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const MenuPair pair = createPair(updater, root.rootNodeId());
    ASSERT_TRUE(pair.menu.hasValue());
    assertOk(context->commitLayout({.width = 180.0F, .height = 100.0F}));
    assertOk(updater.setMenuOpen(pair.menu, true));
    assertOk(context->commitLayout({.width = 180.0F, .height = 100.0F}));
    const UI::UIMenuMetrics published = updater.menuMetrics(pair.menu).value();
    ASSERT_TRUE(published.open);
    const u64 layoutRevision = context->committedLayout().layoutRevision();
    const u64 paintRevision = context->committedPaint().paintRevision();
    const u64 semanticsRevision = context->committedSemantics().semanticsRevision();

    assertOk(updater.setMenuOpen(pair.menu, false));
    auto overflow = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedSize(10, 10)));
    ASSERT_TRUE(overflow.has_value());
    Core::Status failed =
        context->commitLayout({.width = 180.0F, .height = 100.0F});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(updater.menuMetrics(pair.menu).value(), published);
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision);
    EXPECT_EQ(context->committedSemantics().semanticsRevision(), semanticsRevision);

    assertOk(updater.destroy(*overflow));
    assertOk(context->commitLayout({.width = 180.0F, .height = 100.0F}));
    EXPECT_FALSE(updater.menuMetrics(pair.menu).value().open);
}

TEST_F(UIMenuTest, ActivationCapacityFailureLeavesCheckedOpenAndFocusStateAtomic)
{
    auto context = createContext({
        .nodeCapacity = 64,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 8,
        .routePathCapacity = 16,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const MenuPair pair = createPair(
        updater, root.rootNodeId(), {}, overlay(20, 10, 80, 24), fixedSize(100, 40));
    auto check = updater.createElement(
        pair.menu, UI::makeMenuItemElement(
                       "Check", {.kind = UI::UIMenuItemKind::Check}, fixedSize(92, 24)));
    ASSERT_TRUE(check.has_value()) << check.error().message;
    std::array<UI::UINodeId, 8> blockers{};
    for (UI::UINodeId& blocker : blockers)
    {
        auto created = updater.createElement(root.rootNodeId(), UI::makePanelElement());
        ASSERT_TRUE(created.has_value()) << created.error().message;
        blocker = *created;
    }
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    assertOk(updater.setMenuOpen(pair.menu, true));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    assertOk(context->requestFocus(*check));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    const UI::UIBoxPaint blockerPaint = UI::makeSolidBox(UI::rgb(0x123456));
    for (UI::UINodeId blocker : blockers)
    {
        assertOk(updater.setBoxPaint(blocker, blockerPaint));
    }
    auto rejected = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1}, 1, UI::UIButtonActivationSource::Keyboard);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(updater.isMenuItemChecked(*check).value());
    EXPECT_TRUE(updater.isMenuOpen(pair.menu).value());
    EXPECT_EQ(context->defaultActionFocus(), *check);

    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    auto activated = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{2}, 2, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(activated.has_value()) << activated.error().message;
    EXPECT_TRUE(activated->activated);
    EXPECT_TRUE(updater.isMenuItemChecked(*check).value());
    EXPECT_FALSE(updater.isMenuOpen(pair.menu).value());
}

TEST_F(UIMenuTest, DestroyGenerationReuseAndRootReleaseLeaveNoRelationships)
{
    auto context = createContext({.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    MenuPair pair = createPair(updater, root.rootNodeId());
    ASSERT_TRUE(pair.menu.hasValue());
    assertOk(context->commitLayout({.width = 160.0F, .height = 90.0F}));
    assertOk(updater.setMenuOpen(pair.menu, true));
    assertOk(context->commitLayout({.width = 160.0F, .height = 90.0F}));

    const UI::UINodeId destroyedAnchor = pair.anchor;
    assertOk(updater.destroy(pair.anchor));
    auto replacement = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Replacement"));
    ASSERT_TRUE(replacement.has_value());
    EXPECT_NE(*replacement, destroyedAnchor);
    EXPECT_FALSE(updater.menuAnchor(pair.menu).value().hasValue());
    assertOk(updater.setMenuAnchor(pair.menu, *replacement));

    const UI::UINodeId destroyedMenu = pair.menu;
    assertOk(updater.destroy(pair.menu));
    auto replacementMenu = updater.createElement(
        root.rootNodeId(), UI::makeMenuElement());
    ASSERT_TRUE(replacementMenu.has_value());
    EXPECT_NE(*replacementMenu, destroyedMenu);
    assertOk(updater.setMenuAnchor(*replacementMenu, *replacement));

    root.reset();
    EXPECT_EQ(context->liveNodeCount(), 0U);
    auto stale = context->menuAnchor(*replacementMenu);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, UI::UIErrorCode::InvalidNode);

    auto newRoot = createRoot(*context);
    auto newUpdater = createUpdater(*context, newRoot);
    const MenuPair newPair = createPair(newUpdater, newRoot.rootNodeId());
    ASSERT_TRUE(newPair.menu.hasValue());
    assertOk(context->commitLayout({.width = 160.0F, .height = 90.0F}));
    assertOk(newUpdater.setMenuOpen(newPair.menu, true));
    assertOk(context->commitLayout({.width = 160.0F, .height = 90.0F}));
    EXPECT_TRUE(newUpdater.isMenuOpen(newPair.menu).value());
}

} // namespace
} // namespace Tina::Tests
