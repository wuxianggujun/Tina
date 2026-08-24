#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class ObservingMemoryResource final : public std::pmr::memory_resource {
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

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
};

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] std::unique_ptr<UI::UIContext>
createContext(Platform::WindowId window, UI::UIContextCapacityConfig capacities = {},
              std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    capacities.nodeCapacity = capacities.nodeCapacity == UI::UIContextCapacityConfig::DefaultNodeCapacity
                                  ? 128
                                  : capacities.nodeCapacity;
    capacities.rootCapacity = capacities.rootCapacity == UI::UIContextCapacityConfig::DefaultRootCapacity
                                  ? 1
                                  : capacities.rootCapacity;
    capacities.routePathCapacity = capacities.routePathCapacity == 0 ? capacities.nodeCapacity
                                                                      : capacities.routePathCapacity;
    capacities.paintSnapshotCapacity = capacities.paintSnapshotCapacity == 0 ? capacities.nodeCapacity
                                                                               : capacities.paintSnapshotCapacity;
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.authoring().rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
{
    auto result = context.authoring().treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UILayoutStyle popupSize(float width, float height) noexcept
{
    UI::UILayoutStyle style = fixedSize(width, height);
    style.placement = UI::UILayoutPlacement::Overlay;
    return style;
}

[[nodiscard]] UI::UIPointerInputEvent pointerInput(Platform::WindowId window,
                                                   UI::UIRoutedPointerEventKind kind, u64 sequence,
                                                   UI::UILogicalPoint position) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = position,
        .button = Platform::PointerButton::Primary,
    };
}

[[nodiscard]] const UI::UICommittedLayoutEntry* findLayoutEntry(UI::UICommittedLayoutView view,
                                                                UI::UINodeId node) noexcept
{
    for (const UI::UICommittedLayoutEntry& entry : view.entries())
    {
        if (entry.node == node)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const UI::UISemanticsEntry* findSemanticsEntry(UI::UICommittedSemanticsView view,
                                                             UI::UINodeId node) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries())
    {
        if (entry.node == node)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] UI::UILogicalPoint centerOf(const UI::UICommittedLayoutEntry& entry) noexcept
{
    return {
        .x = entry.worldRect.x + entry.worldRect.width * 0.5F,
        .y = entry.worldRect.y + entry.worldRect.height * 0.5F,
    };
}

struct DropdownTree final {
    UI::UINodeId before{};
    UI::UINodeId dropdown{};
    UI::UINodeId popup{};
    UI::UINodeId firstItem{};
    UI::UINodeId secondItem{};
    UI::UINodeId after{};
};

[[nodiscard]] DropdownTree createDropdownTree(UI::UITreeUpdater& updater, UI::UINodeId root)
{
    auto before = updater.createElement(root, UI::makeButtonElement());
    auto dropdown = updater.createElement(root, UI::makeDropdownElement());
    if (!before || !dropdown)
    {
        EXPECT_TRUE(before.has_value()) << (before ? "" : before.error().message);
        EXPECT_TRUE(dropdown.has_value()) << (dropdown ? "" : dropdown.error().message);
        return {};
    }
    auto popup = updater.createElement(*dropdown, UI::makePopupElement());
    if (!popup)
    {
        EXPECT_TRUE(popup.has_value()) << popup.error().message;
        return {};
    }
    auto firstItem = updater.createElement(*popup, UI::makeDropdownItemElement());
    auto secondItem = updater.createElement(*popup, UI::makeDropdownItemElement());
    auto after = updater.createElement(root, UI::makeButtonElement());
    if (!firstItem || !secondItem || !after)
    {
        EXPECT_TRUE(firstItem.has_value()) << (firstItem ? "" : firstItem.error().message);
        EXPECT_TRUE(secondItem.has_value()) << (secondItem ? "" : secondItem.error().message);
        EXPECT_TRUE(after.has_value()) << (after ? "" : after.error().message);
        return {};
    }

    assertOk(updater.setLayoutStyle(*before, fixedSize(120.0F, 28.0F)));
    assertOk(updater.setLayoutStyle(*dropdown, fixedSize(120.0F, 32.0F)));
    assertOk(updater.setLayoutStyle(*popup, popupSize(120.0F, 52.0F)));
    assertOk(updater.setLayoutStyle(*firstItem, fixedSize(120.0F, 26.0F)));
    assertOk(updater.setLayoutStyle(*secondItem, fixedSize(120.0F, 26.0F)));
    assertOk(updater.setLayoutStyle(*after, fixedSize(120.0F, 28.0F)));
    assertOk(updater.setText(*dropdown, "Choose"));
    assertOk(updater.setText(*firstItem, "First"));
    assertOk(updater.setText(*secondItem, "Second"));
    return {
        .before = *before,
        .dropdown = *dropdown,
        .popup = *popup,
        .firstItem = *firstItem,
        .secondItem = *secondItem,
        .after = *after,
    };
}

class UIPopupDropdownTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
        context = createContext(window);
        ASSERT_NE(context, nullptr);
        root = createRoot(*context);
        ASSERT_TRUE(root.hasValue());
        updater = createUpdater(*context, root);
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
};

TEST_F(UIPopupDropdownTest, EnforcesCompositeOwnershipAndRoundTripsState)
{
    auto invalidPopup = updater.createElement(root.rootNodeId(), UI::makePopupElement());
    ASSERT_FALSE(invalidPopup.has_value());
    EXPECT_EQ(invalidPopup.error().code, UI::UIErrorCode::InvalidParent);

    auto dropdown = updater.createElement(root.rootNodeId(), UI::makeDropdownElement());
    ASSERT_TRUE(dropdown.has_value()) << dropdown.error().message;
    auto invalidItem = updater.createElement(*dropdown, UI::makeDropdownItemElement());
    ASSERT_FALSE(invalidItem.has_value());
    EXPECT_EQ(invalidItem.error().code, UI::UIErrorCode::InvalidParent);

    auto popup = updater.createElement(*dropdown, UI::makePopupElement());
    ASSERT_TRUE(popup.has_value()) << popup.error().message;
    auto duplicatePopup = updater.createElement(*dropdown, UI::makePopupElement());
    ASSERT_FALSE(duplicatePopup.has_value());
    EXPECT_EQ(duplicatePopup.error().code, UI::UIErrorCode::InvalidParent);
    auto item = updater.createElement(*popup, UI::makeDropdownItemElement());
    ASSERT_TRUE(item.has_value()) << item.error().message;

    EXPECT_EQ(updater.focusScopeMode(*popup).value(), UI::UIFocusScopeMode::Contain);
    EXPECT_FALSE(updater.isPopupOpen(*popup).value());
    EXPECT_FALSE(updater.isDropdownOpen(*dropdown).value());
    EXPECT_FALSE(context->input().activePopup().hasValue());
    EXPECT_FALSE(updater.dropdownSelectedItem(*dropdown).value().hasValue());

    const UI::UIPopupStyle expected{
        .placement = UI::UIPopupPlacement::Above,
        .anchorGap = 7.0F,
        .matchAnchorWidth = false,
    };
    assertOk(updater.setPopupStyle(*popup, expected));
    EXPECT_EQ(updater.popupStyle(*popup).value(), expected);
    const Core::Status invalidStyle = updater.setPopupStyle(
        *popup, {.placement = UI::UIPopupPlacement::Auto, .anchorGap = -1.0F});
    ASSERT_FALSE(invalidStyle.has_value());
    EXPECT_EQ(updater.popupStyle(*popup).value(), expected);

    assertOk(updater.setDropdownSelectedItem(*dropdown, *item));
    EXPECT_EQ(updater.dropdownSelectedItem(*dropdown).value(), *item);
    EXPECT_TRUE(updater.isDropdownItemSelected(*item).value());
    assertOk(updater.setDropdownSelectedItem(*dropdown, {}));
    EXPECT_FALSE(updater.dropdownSelectedItem(*dropdown).value().hasValue());
}

TEST_F(UIPopupDropdownTest, LayoutPublishesPopupSubtreesAfterNormalContentInStablePreorder)
{
    auto localContext = createContext(window, {
        .nodeCapacity = 128,
        .rootCapacity = 2,
        .paintSnapshotCapacity = 128,
    });
    ASSERT_NE(localContext, nullptr);
    auto firstRoot = createRoot(*localContext);
    auto secondRoot = createRoot(*localContext);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);
    auto firstUpdater = createUpdater(*localContext, firstRoot);
    auto secondUpdater = createUpdater(*localContext, secondRoot);
    const DropdownTree first = createDropdownTree(firstUpdater, firstRoot.rootNodeId());
    const DropdownTree second = createDropdownTree(secondUpdater, secondRoot.rootNodeId());
    ASSERT_TRUE(first.after.hasValue());
    ASSERT_TRUE(second.after.hasValue());

    assertOk(localContext->publication().commitLayout({.width = 320.0F, .height = 240.0F}));
    const std::array expectedOrder{
        firstRoot.rootNodeId(),
        first.before,
        first.dropdown,
        first.after,
        secondRoot.rootNodeId(),
        second.before,
        second.dropdown,
        second.after,
        first.popup,
        first.firstItem,
        first.secondItem,
        second.popup,
        second.firstItem,
        second.secondItem,
    };
    const std::array expectedLayoutOrdinals{
        0U, 1U, 2U, 6U, 7U, 8U, 9U, 13U, 3U, 4U, 5U, 10U, 11U, 12U,
    };
    const UI::UICommittedLayoutView layout = localContext->publication().committedLayout();
    ASSERT_EQ(layout.size(), expectedOrder.size());
    for (usize index = 0; index < expectedOrder.size(); ++index)
    {
        EXPECT_EQ(layout.entries()[index].node, expectedOrder[index]) << "index=" << index;
        EXPECT_EQ(layout.entries()[index].layoutOrdinal, expectedLayoutOrdinals[index]) << "index=" << index;
        EXPECT_EQ(layout.entries()[index].paintOrdinal, index) << "index=" << index;
    }
}

TEST_F(UIPopupDropdownTest, AutoPlacementMatchesAnchorAndClampsToViewport)
{
    auto dropdown = updater.createElement(root.rootNodeId(), UI::makeDropdownElement());
    ASSERT_TRUE(dropdown.has_value()) << dropdown.error().message;
    auto popup = updater.createElement(*dropdown, UI::makePopupElement());
    ASSERT_TRUE(popup.has_value()) << popup.error().message;
    auto item = updater.createElement(*popup, UI::makeDropdownItemElement());
    ASSERT_TRUE(item.has_value()) << item.error().message;

    UI::UILayoutStyle dropdownLayout = fixedSize(80.0F, 20.0F);
    dropdownLayout.placement = UI::UILayoutPlacement::Overlay;
    dropdownLayout.overlay.offset.x = UI::UILayoutLength::Px(170.0F);
    dropdownLayout.overlay.offset.y = UI::UILayoutLength::Px(70.0F);
    assertOk(updater.setLayoutStyle(*dropdown, dropdownLayout));
    assertOk(updater.setLayoutStyle(*popup, popupSize(120.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(*item, fixedSize(120.0F, 40.0F)));
    assertOk(updater.setPopupOpen(*popup, true));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UIPopupMetrics metrics = updater.popupMetrics(*popup).value();
    EXPECT_TRUE(metrics.open);
    EXPECT_EQ(metrics.resolvedPlacement, UI::UIPopupPlacement::Above);
    EXPECT_FLOAT_EQ(metrics.anchorRect.x, 170.0F);
    EXPECT_FLOAT_EQ(metrics.anchorRect.y, 70.0F);
    EXPECT_FLOAT_EQ(metrics.popupRect.width, 80.0F);
    EXPECT_FLOAT_EQ(metrics.popupRect.x, 120.0F);
    EXPECT_FLOAT_EQ(metrics.popupRect.y, 26.0F);
    EXPECT_FLOAT_EQ(metrics.popupRect.height, 40.0F);

    assertOk(updater.setPopupOpen(*popup, false));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    EXPECT_FALSE(updater.popupMetrics(*popup).value().open);
    const auto* collapsedPopup = findLayoutEntry(context->publication().committedLayout(), *popup);
    ASSERT_NE(collapsedPopup, nullptr);
    EXPECT_EQ(collapsedPopup->effectiveVisibility, UI::UIVisibility::Collapsed);
}

TEST_F(UIPopupDropdownTest, PointerTogglesSelectsAndDismissesWithoutClickThrough)
{
    const DropdownTree tree = createDropdownTree(updater, root.rootNodeId());
    ASSERT_TRUE(tree.after.hasValue());
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));

    const auto* dropdownLayout = findLayoutEntry(context->publication().committedLayout(), tree.dropdown);
    ASSERT_NE(dropdownLayout, nullptr);
    const UI::UILogicalPoint dropdownCenter = centerOf(*dropdownLayout);
    auto down = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, dropdownCenter));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(updater.isButtonPressed(tree.dropdown).value());
    auto up = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, dropdownCenter));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_TRUE(updater.isDropdownOpen(tree.dropdown).value());
    EXPECT_EQ(context->input().activePopup(), tree.popup);

    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    const auto* secondLayout = findLayoutEntry(context->publication().committedLayout(), tree.secondItem);
    ASSERT_NE(secondLayout, nullptr);
    const UI::UILogicalPoint secondCenter = centerOf(*secondLayout);
    down = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 3, secondCenter));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    up = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 4, secondCenter));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_EQ(updater.dropdownSelectedItem(tree.dropdown).value(), tree.secondItem);
    EXPECT_FALSE(updater.isDropdownOpen(tree.dropdown).value());
    EXPECT_EQ(context->input().defaultActionFocus(), tree.dropdown);

    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    assertOk(updater.setDropdownOpen(tree.dropdown, true));
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    const auto* beforeLayout = findLayoutEntry(context->publication().committedLayout(), tree.before);
    ASSERT_NE(beforeLayout, nullptr);
    const UI::UILogicalPoint outside = centerOf(*beforeLayout);
    down = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 5, outside));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_FALSE(updater.isDropdownOpen(tree.dropdown).value());
    EXPECT_FALSE(context->input().pointerCapture().hasValue());
    up = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 6, outside));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(updater.isButtonPressed(tree.before).value());
    EXPECT_EQ(context->input().defaultActionFocus(), tree.dropdown);
}

TEST_F(UIPopupDropdownTest, AcceptArrowEscapeAndTabExitShareOneStateMachine)
{
    const DropdownTree tree = createDropdownTree(updater, root.rootNodeId());
    ASSERT_TRUE(tree.after.hasValue());
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    assertOk(context->input().requestFocus(tree.dropdown));

    auto accept = context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{1}, 1, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(accept.has_value()) << (accept ? "" : accept.error().message);
    EXPECT_TRUE(accept->consumed);
    EXPECT_TRUE(accept->activated);
    EXPECT_EQ(context->input().activePopup(), tree.popup);
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));

    auto next = context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, true);
    ASSERT_TRUE(next.has_value()) << (next ? "" : next.error().message);
    EXPECT_TRUE(next->consumed);
    EXPECT_TRUE(next->changed);
    EXPECT_EQ(next->focus, tree.firstItem);
    EXPECT_TRUE(context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, false)->consumed);
    next = context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, true);
    ASSERT_TRUE(next.has_value()) << (next ? "" : next.error().message);
    EXPECT_EQ(next->focus, tree.secondItem);
    EXPECT_TRUE(context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, false)->consumed);

    auto dismiss = context->input().routeDropdownCommand(UI::UIDropdownCommand::Dismiss, true);
    ASSERT_TRUE(dismiss.has_value()) << (dismiss ? "" : dismiss.error().message);
    EXPECT_TRUE(dismiss->consumed);
    EXPECT_TRUE(dismiss->changed);
    EXPECT_EQ(dismiss->focus, tree.dropdown);
    EXPECT_FALSE(context->input().activePopup().hasValue());
    auto dismissRelease = context->input().routeDropdownCommand(UI::UIDropdownCommand::Dismiss, false);
    ASSERT_TRUE(dismissRelease.has_value());
    EXPECT_TRUE(dismissRelease->consumed);

    accept = context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{2}, 2, UI::UIButtonActivationSource::Gamepad);
    ASSERT_TRUE(accept.has_value()) << (accept ? "" : accept.error().message);
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    next = context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, true);
    ASSERT_TRUE(next.has_value()) << (next ? "" : next.error().message);
    EXPECT_EQ(next->focus, tree.firstItem);
    EXPECT_TRUE(context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, false)->consumed);
    accept = context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{3}, 3, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(accept.has_value()) << (accept ? "" : accept.error().message);
    EXPECT_TRUE(accept->activated);
    EXPECT_EQ(updater.dropdownSelectedItem(tree.dropdown).value(), tree.firstItem);
    EXPECT_FALSE(context->input().activePopup().hasValue());
    EXPECT_EQ(context->input().defaultActionFocus(), tree.dropdown);

    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    accept = context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{4}, 4, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(accept.has_value()) << (accept ? "" : accept.error().message);
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    next = context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, true);
    ASSERT_TRUE(next.has_value()) << (next ? "" : next.error().message);
    EXPECT_EQ(next->focus, tree.firstItem);
    EXPECT_TRUE(context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, false)->consumed);
    auto exit = context->input().routeDropdownCommand(UI::UIDropdownCommand::ExitNext, true);
    ASSERT_TRUE(exit.has_value()) << (exit ? "" : exit.error().message);
    EXPECT_TRUE(exit->consumed);
    EXPECT_TRUE(exit->changed);
    EXPECT_EQ(exit->focus, tree.after);
    EXPECT_FALSE(context->input().activePopup().hasValue());
    EXPECT_TRUE(context->input().routeDropdownCommand(UI::UIDropdownCommand::ExitNext, false)->consumed);
}

TEST_F(UIPopupDropdownTest, DisableAndDestroyClosePopupAndCleanSelection)
{
    const DropdownTree tree = createDropdownTree(updater, root.rootNodeId());
    assertOk(updater.setDropdownSelectedItem(tree.dropdown, tree.firstItem));
    assertOk(updater.setDropdownOpen(tree.dropdown, true));
    EXPECT_EQ(context->input().activePopup(), tree.popup);

    assertOk(updater.setEnabled(tree.dropdown, false));
    EXPECT_FALSE(updater.isDropdownOpen(tree.dropdown).value());
    EXPECT_FALSE(context->input().activePopup().hasValue());
    assertOk(updater.setEnabled(tree.dropdown, true));
    assertOk(updater.setEnabled(tree.popup, false));
    const Core::Status disabledOpen = updater.setDropdownOpen(tree.dropdown, true);
    ASSERT_FALSE(disabledOpen.has_value());
    EXPECT_EQ(disabledOpen.error().code, UI::UIErrorCode::InvalidControlValue);
    assertOk(updater.setEnabled(tree.popup, true));

    assertOk(updater.destroy(tree.firstItem));
    EXPECT_FALSE(updater.dropdownSelectedItem(tree.dropdown).value().hasValue());
    assertOk(updater.setDropdownOpen(tree.dropdown, true));
    assertOk(updater.destroy(tree.popup));
    EXPECT_FALSE(context->input().activePopup().hasValue());
    EXPECT_FALSE(updater.isDropdownOpen(tree.dropdown).value());
}

TEST_F(UIPopupDropdownTest, ThemeAndSemanticsExposeComboListValueAndSelection)
{
    const DropdownTree tree = createDropdownTree(updater, root.rootNodeId());
    assertOk(updater.setDropdownSelectedItem(tree.dropdown, tree.secondItem));
    assertOk(updater.setDropdownOpen(tree.dropdown, true));
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));

    const auto* dropdownSemantics = findSemanticsEntry(context->publication().committedSemantics(), tree.dropdown);
    const auto* popupSemantics = findSemanticsEntry(context->publication().committedSemantics(), tree.popup);
    const auto* firstSemantics = findSemanticsEntry(context->publication().committedSemantics(), tree.firstItem);
    const auto* secondSemantics = findSemanticsEntry(context->publication().committedSemantics(), tree.secondItem);
    ASSERT_NE(dropdownSemantics, nullptr);
    ASSERT_NE(popupSemantics, nullptr);
    ASSERT_NE(firstSemantics, nullptr);
    ASSERT_NE(secondSemantics, nullptr);
    EXPECT_EQ(dropdownSemantics->role, UI::UISemanticsRole::ComboBox);
    EXPECT_EQ(dropdownSemantics->valueText, "Second");
    EXPECT_EQ(popupSemantics->role, UI::UISemanticsRole::List);
    EXPECT_EQ(firstSemantics->role, UI::UISemanticsRole::ListItem);
    EXPECT_FALSE(firstSemantics->selected);
    EXPECT_TRUE(secondSemantics->selected);

    const UI::UITheme light = UI::makeModernDesktopTheme(UI::UIColorScheme::Light);
    assertOk(context->style().setProductTheme(light));
    EXPECT_EQ(updater.buttonPaint(tree.dropdown).value(), UI::makeDropdownChrome(light).states);
    EXPECT_EQ(updater.dropdownPaint(tree.dropdown).value(), UI::makeDropdownChrome(light).dropdown);
    EXPECT_EQ(updater.buttonPaint(tree.firstItem).value(), UI::makeDropdownItemChrome(light).states);
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    EXPECT_GT(context->publication().committedPaint().paintRevision(), 0U);
}

TEST(UIPopupDropdownCapacityTest, ActivationFailureIsAtomicAndSteadyStateDoesNotAllocate)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    auto windows = std::make_unique<WindowPool>(std::move(*windowsResult));
    auto windowResult = windows->tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    ObservingMemoryResource resource;
    auto context = createContext(
        *windowResult,
        {
            .nodeCapacity = 128,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 8,
            .routePathCapacity = 16,
        },
        resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const DropdownTree tree = createDropdownTree(updater, root.rootNodeId());
    auto extraOne = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    auto extraTwo = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(extraOne.has_value()) << (extraOne ? "" : extraOne.error().message);
    ASSERT_TRUE(extraTwo.has_value()) << (extraTwo ? "" : extraTwo.error().message);
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    assertOk(context->input().requestFocus(tree.dropdown));
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));

    const UI::UIBoxPaint blockerPaint = UI::makeSolidBox(UI::rgb(0x123456));
    assertOk(updater.setBoxPaint(tree.before, blockerPaint));
    assertOk(updater.setBoxPaint(tree.firstItem, blockerPaint));
    assertOk(updater.setBoxPaint(tree.secondItem, blockerPaint));
    assertOk(updater.setBoxPaint(tree.after, blockerPaint));
    assertOk(updater.setBoxPaint(*extraOne, blockerPaint));
    assertOk(updater.setBoxPaint(*extraTwo, blockerPaint));
    const auto rejected = context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{1}, 1, UI::UIButtonActivationSource::Keyboard);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(updater.isDropdownOpen(tree.dropdown).value());
    EXPECT_FALSE(context->input().activePopup().hasValue());
    EXPECT_EQ(context->input().defaultActionFocus(), tree.dropdown);

    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    auto opened = context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{2}, 2, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(opened.has_value()) << (opened ? "" : opened.error().message);
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    const usize allocationCount = resource.allocationCount();
    for (u64 sequence = 3; sequence < 35; ++sequence)
    {
        auto next = context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, true);
        ASSERT_TRUE(next.has_value()) << (next ? "" : next.error().message);
        auto release = context->input().routeDropdownCommand(UI::UIDropdownCommand::NextItem, false);
        ASSERT_TRUE(release.has_value()) << (release ? "" : release.error().message);
        auto dismiss = context->input().routeDropdownCommand(UI::UIDropdownCommand::Dismiss, true);
        ASSERT_TRUE(dismiss.has_value()) << (dismiss ? "" : dismiss.error().message);
        release = context->input().routeDropdownCommand(UI::UIDropdownCommand::Dismiss, false);
        ASSERT_TRUE(release.has_value()) << (release ? "" : release.error().message);
        assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
        opened = context->input().routeDefaultActionActivate(
            Platform::PlatformFrameId{sequence}, sequence, UI::UIButtonActivationSource::Keyboard);
        ASSERT_TRUE(opened.has_value()) << (opened ? "" : opened.error().message);
        assertOk(context->publication().commitLayout({.width = 180.0F, .height = 180.0F}));
    }
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

} // namespace
} // namespace Tina::Tests
