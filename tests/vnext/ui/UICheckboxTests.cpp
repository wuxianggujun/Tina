#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 8,
        .buttonActionCapacity = 8,
    },
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UINodeId createCheckbox(UI::UIContext& context, UI::UINodeId parent)
{
    auto result = context.rootBuilder().createCheckbox(parent);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIPointerInputEvent makePointerInput(
    Platform::WindowId window,
    UI::UIRoutedPointerEventKind kind,
    u64 sequence,
    UI::UILogicalPoint position = {.x = 10.0F, .y = 10.0F}) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = static_cast<usize>(sequence - 1),
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = position,
        .delta = kind == UI::UIRoutedPointerEventKind::Move
            ? UI::UILogicalPoint{.x = 1.0F, .y = 1.0F}
            : UI::UILogicalPoint{},
        .button = Platform::PointerButton::Primary,
    };
}

void publishLayout(UI::UIContext& context, float width = 100.0F, float height = 100.0F)
{
    assertOk(context.commitLayout(UI::UILogicalSize{.width = width, .height = height}));
}

TEST(UICheckboxTest, KindIsTargetableAndDefaultsUnchecked)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value()) << (checked ? "" : checked.error().message);
    EXPECT_FALSE(*checked);

    assertOk(context->commitStructure());
    const auto structure = context->committedStructure();
    bool found = false;
    for (const auto& entry : structure.entries()) {
        if (entry.node == checkbox) {
            found = true;
            EXPECT_EQ(entry.kind, UI::UIWidgetKind::Checkbox);
        }
    }
    EXPECT_TRUE(found);
}

TEST(UICheckboxTest, PrimaryClickTogglesAndFiresAction)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    int activations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{[&activations](const UI::UIButtonActionEvent&) noexcept {
            ++activations;
        }}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    publishLayout(*context);

    auto down = context->routePointerInput(
        makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto up = context->routePointerInput(
        makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);

    EXPECT_EQ(activations, 1);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_TRUE(*checked);

    auto down2 = context->routePointerInput(
        makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 3, {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down2.has_value());
    auto up2 = context->routePointerInput(
        makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 4, {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up2.has_value());
    EXPECT_EQ(activations, 2);
    checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_FALSE(*checked);
}

TEST(UICheckboxTest, PrimaryUpOutsideDoesNotToggle)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    int activations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{[&activations](const UI::UIButtonActionEvent&) noexcept {
            ++activations;
        }}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    publishLayout(*context);

    {
        auto down = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 10.0F, .y = 10.0F}));
        ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    }
    // Up far outside.
    {
        auto up = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, {.x = 90.0F, .y = 90.0F}));
        ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    }
    EXPECT_EQ(activations, 0);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_FALSE(*checked);
}

TEST(UICheckboxTest, SetCheckedSilentAndRejectsWrongKind)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    auto button = context->rootBuilder().createButton(root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());
    ASSERT_TRUE(button.has_value());

    auto updater = createUpdater(*context, root);
    int activations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{[&activations](const UI::UIButtonActionEvent&) noexcept {
            ++activations;
        }}));
    assertOk(updater.setChecked(checkbox, true));
    EXPECT_EQ(activations, 0);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_TRUE(*checked);

    EXPECT_FALSE(updater.setChecked(*button, true).has_value());
    EXPECT_FALSE(updater.isChecked(*button).has_value());
}

TEST(UICheckboxTest, KeyboardAcceptTogglesDefaultFocusedCheckbox)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    publishLayout(*context);

    // Arm via pointer to set default-action focus, then keyboard Accept.
    {
        auto down = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 10.0F, .y = 10.0F}));
        ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
        auto up = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, {.x = 10.0F, .y = 10.0F}));
        ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    }
    auto checkedAfterClick = updater.isChecked(checkbox);
    ASSERT_TRUE(checkedAfterClick.has_value());
    EXPECT_TRUE(*checkedAfterClick);

    auto activate = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{3}, 3, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(activate.has_value()) << (activate ? "" : activate.error().message);
    EXPECT_TRUE(activate->consumed);
    auto checkedAfterKey = updater.isChecked(checkbox);
    ASSERT_TRUE(checkedAfterKey.has_value());
    EXPECT_FALSE(*checkedAfterKey);
}

} // namespace
} // namespace Tina::Tests
