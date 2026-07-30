#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <tuple>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 32,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 32,
        .routePathCapacity = 32,
        .routedPointerListenerCapacity = 8,
        .buttonActionCapacity = 16,
        .textByteCapacity = 1024,
    })
{
    capacities.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(window, capacities);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
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

[[nodiscard]] UI::UIBoxPaint solidFill(
    u8 red,
    u8 green,
    u8 blue,
    u8 alpha = 255) noexcept
{
    UI::UIBoxPaint paint{};
    paint.solidFill = UI::UISolidFill{
        .color = {
            .red = red,
            .green = green,
            .blue = blue,
            .alpha = alpha,
        },
    };
    return paint;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] bool isEnabled(
    UI::UITreeUpdater& updater,
    UI::UINodeId node)
{
    auto result = updater.isEnabled(node);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : false;
}

[[nodiscard]] UI::UIPointerInputEvent makePointerInput(
    Platform::WindowId window,
    UI::UIRoutedPointerEventKind kind,
    u64 sequence,
    UI::UILogicalPoint position) noexcept
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

[[nodiscard]] const UI::UISemanticsEntry* findSemanticsEntry(
    UI::UICommittedSemanticsView view,
    UI::UINodeId node) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries()) {
        if (entry.node == node) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr UI::UIPremultipliedRgba8Color applyOpacity(
    UI::UIPremultipliedRgba8Color color,
    u8 opacity) noexcept
{
    const auto scale = [opacity](u8 channel) constexpr noexcept -> u8 {
        return static_cast<u8>(
            (static_cast<u16>(channel) * static_cast<u16>(opacity) + u16{127})
            / u16{255});
    };
    return {
        .red = scale(color.red),
        .green = scale(color.green),
        .blue = scale(color.blue),
        .alpha = scale(color.alpha),
    };
}

class UIEnabledStateTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(2);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto firstWindowResult = windows->tryEmplace(1);
        auto secondWindowResult = windows->tryEmplace(2);
        ASSERT_TRUE(firstWindowResult.has_value());
        ASSERT_TRUE(secondWindowResult.has_value());
        firstWindow = *firstWindowResult;
        secondWindow = *secondWindowResult;

        context = createContext(firstWindow);
        ASSERT_NE(context, nullptr);
        root = createRoot(*context);
        ASSERT_TRUE(root);
        updater = createUpdater(*context, root);
        assertOk(updater.setLayoutStyle(
            root.rootNodeId(),
            fixedSize(320.0F, 240.0F)));
    }

    [[nodiscard]] UI::UINodeId createLabel(
        float width = 120.0F,
        float height = 24.0F)
    {
        auto result = updater.createElement(root.rootNodeId(), UI::makeLabelElement());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        expectOk(updater.setLayoutStyle(*result, fixedSize(width, height)));
        return *result;
    }

    [[nodiscard]] UI::UINodeId createButton(
        float width = 120.0F,
        float height = 32.0F)
    {
        auto result = updater.createElement(root.rootNodeId(), UI::makeButtonElement());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        expectOk(updater.setLayoutStyle(*result, fixedSize(width, height)));
        return *result;
    }

    [[nodiscard]] UI::UINodeId createCheckbox(
        float width = 120.0F,
        float height = 24.0F)
    {
        auto result = updater.createElement(root.rootNodeId(), UI::makeCheckboxElement());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        expectOk(updater.setLayoutStyle(*result, fixedSize(width, height)));
        return *result;
    }

    [[nodiscard]] UI::UINodeId createSlider(
        float width = 120.0F,
        float height = 24.0F)
    {
        auto result = updater.createElement(root.rootNodeId(), UI::makeSliderElement());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        expectOk(updater.setLayoutStyle(*result, fixedSize(width, height)));
        return *result;
    }

    [[nodiscard]] UI::UINodeId createTextEdit(
        float width = 200.0F,
        float height = 32.0F)
    {
        auto result = updater.createElement(root.rootNodeId(), UI::makeTextEditElement());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        expectOk(updater.setLayoutStyle(*result, fixedSize(width, height)));
        return *result;
    }

    [[nodiscard]] UI::UINodeId createProgressBar(
        float width = 120.0F,
        float height = 16.0F)
    {
        auto result = updater.createElement(root.rootNodeId(), UI::makeProgressBarElement());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        expectOk(updater.setLayoutStyle(*result, fixedSize(width, height)));
        return *result;
    }

    [[nodiscard]] UI::UINodeId createRadioButton(
        float width = 120.0F,
        float height = 24.0F)
    {
        auto result = updater.createElement(root.rootNodeId(), UI::makeRadioButtonElement());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        expectOk(updater.setLayoutStyle(*result, fixedSize(width, height)));
        return *result;
    }

    void publishLayout()
    {
        assertOk(context->commitLayout({.width = 320.0F, .height = 240.0F}));
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId firstWindow{};
    Platform::WindowId secondWindow{};
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
};

TEST_F(UIEnabledStateTest, PublishedWidgetsDefaultEnabledAndPublishDisabledSemantics)
{
    const std::array<UI::UINodeId, 7> widgets{
        createLabel(),
        createButton(),
        createCheckbox(),
        createSlider(),
        createTextEdit(),
        createProgressBar(),
        createRadioButton(),
    };
    for (const UI::UINodeId widget : widgets) {
        ASSERT_TRUE(widget.hasValue());
        EXPECT_TRUE(isEnabled(updater, widget));
        assertOk(updater.setEnabled(widget, false));
        EXPECT_FALSE(isEnabled(updater, widget));
    }

    publishLayout();
    const UI::UICommittedSemanticsView semantics = context->committedSemantics();
    ASSERT_EQ(semantics.size(), widgets.size());
    for (const UI::UINodeId widget : widgets) {
        const UI::UISemanticsEntry* const entry = findSemanticsEntry(semantics, widget);
        ASSERT_NE(entry, nullptr);
        EXPECT_FALSE(entry->enabled);
        EXPECT_FALSE(entry->focused);
    }

    const u64 paintRevision = context->committedPaint().paintRevision();
    const u64 semanticsRevision = semantics.semanticsRevision();
    assertOk(updater.setEnabled(widgets[1], false));
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 0U);
    EXPECT_FALSE(context->statistics().paintDirty);
    EXPECT_FALSE(context->statistics().semanticsDirty);
    publishLayout();
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision);
    EXPECT_EQ(context->committedSemantics().semanticsRevision(), semanticsRevision);
}

TEST_F(UIEnabledStateTest, RejectsDecorativeForeignContextForeignRootAndStaleNodes)
{
    auto localContext = createContext(
        firstWindow,
        {.nodeCapacity = 8, .rootCapacity = 2});
    ASSERT_NE(localContext, nullptr);
    auto firstRoot = createRoot(*localContext);
    auto secondRoot = createRoot(*localContext);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);
    auto firstUpdater = createUpdater(*localContext, firstRoot);
    auto panel = firstUpdater.createElement(firstRoot.rootNodeId(), UI::makePanelElement());
    auto stale = firstUpdater.createElement(firstRoot.rootNodeId(), UI::makeLabelElement());
    auto foreignRootButton = localContext->rootBuilder().createElement(
        secondRoot.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(panel.has_value());
    ASSERT_TRUE(stale.has_value());
    ASSERT_TRUE(foreignRootButton.has_value());

    for (const UI::UINodeId decorative : {firstRoot.rootNodeId(), *panel}) {
        const Core::Status rejected = firstUpdater.setEnabled(decorative, false);
        ASSERT_FALSE(rejected.has_value());
        EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
        auto query = firstUpdater.isEnabled(decorative);
        ASSERT_FALSE(query.has_value());
        EXPECT_EQ(query.error().code, UI::UIErrorCode::InvalidControlValue);
    }

    const Core::Status wrongRoot = firstUpdater.setEnabled(*foreignRootButton, false);
    ASSERT_FALSE(wrongRoot.has_value());
    EXPECT_EQ(wrongRoot.error().code, UI::UIErrorCode::InvalidNode);

    auto sameWindowContext = createContext(firstWindow);
    ASSERT_NE(sameWindowContext, nullptr);
    auto sameWindowRoot = createRoot(*sameWindowContext);
    ASSERT_TRUE(sameWindowRoot);
    auto foreignContextButton = sameWindowContext->rootBuilder().createElement(
        sameWindowRoot.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(foreignContextButton.has_value());
    const Core::Status wrongContext =
        firstUpdater.setEnabled(*foreignContextButton, false);
    ASSERT_FALSE(wrongContext.has_value());
    EXPECT_EQ(wrongContext.error().code, UI::UIErrorCode::WrongContext);

    assertOk(firstUpdater.destroy(*stale));
    const Core::Status staleSet = firstUpdater.setEnabled(*stale, false);
    ASSERT_FALSE(staleSet.has_value());
    EXPECT_EQ(staleSet.error().code, UI::UIErrorCode::InvalidNode);
    auto staleQuery = firstUpdater.isEnabled(*stale);
    ASSERT_FALSE(staleQuery.has_value());
    EXPECT_EQ(staleQuery.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(UIEnabledStateTest, ReusedNodeSlotRestoresEnabledDefault)
{
    auto localContext = createContext(
        firstWindow,
        {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot);
    auto localUpdater = createUpdater(*localContext, localRoot);
    auto originalResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(originalResult.has_value());
    const UI::UINodeId original = *originalResult;

    assertOk(localUpdater.setEnabled(original, false));
    EXPECT_FALSE(isEnabled(localUpdater, original));
    assertOk(localUpdater.destroy(original));

    auto replacementResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makeLabelElement());
    ASSERT_TRUE(replacementResult.has_value());
    const UI::UINodeId replacement = *replacementResult;
    EXPECT_EQ(replacement.index(), original.index());
    EXPECT_EQ(replacement.generation(), original.generation() + 1U);
    EXPECT_TRUE(isEnabled(localUpdater, replacement));
}

TEST_F(UIEnabledStateTest, TabSkipsDisabledWidgetsAndDisablingFocusClearsActivation)
{
    const UI::UINodeId disabledButton = createButton();
    const UI::UINodeId checkbox = createCheckbox();
    ASSERT_TRUE(disabledButton.hasValue());
    ASSERT_TRUE(checkbox.hasValue());
    int checkboxActivations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{
            [&checkboxActivations](const UI::UIButtonActionEvent&) noexcept {
                ++checkboxActivations;
            }}));
    assertOk(updater.setEnabled(disabledButton, false));
    publishLayout();

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_TRUE(focus->consumed);
    EXPECT_TRUE(focus->moved);
    EXPECT_EQ(focus->focus, checkbox);

    assertOk(updater.setEnabled(checkbox, false));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    auto activation = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        1,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(activation.has_value())
        << (activation ? "" : activation.error().message);
    EXPECT_FALSE(activation->consumed);
    EXPECT_FALSE(activation->activated);
    EXPECT_EQ(checkboxActivations, 0);
}

TEST_F(UIEnabledStateTest, DisablingArmedButtonClearsPressedAndPreventsAction)
{
    const UI::UINodeId button = createButton();
    ASSERT_TRUE(button.hasValue());
    int activations = 0;
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout();

    auto down = context->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(down->pointQuery.target.node, button);
    auto pressed = updater.isButtonPressed(button);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_TRUE(*pressed);
    EXPECT_EQ(context->defaultActionFocus(), button);

    assertOk(updater.setEnabled(button, false));
    pressed = updater.isButtonPressed(button);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_FALSE(*pressed);
    EXPECT_FALSE(context->defaultActionFocus().hasValue());

    auto activation = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{2},
        2,
        UI::UIButtonActivationSource::Gamepad);
    ASSERT_TRUE(activation.has_value());
    EXPECT_FALSE(activation->consumed);
    EXPECT_FALSE(activation->activated);
    auto up = context->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonUp,
        3,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_FALSE(up->consumed);
    EXPECT_EQ(activations, 0);
}

TEST_F(UIEnabledStateTest, EnablingFromPointerListenerTakesEffectOnNextRoute)
{
    const UI::UINodeId button = createButton();
    ASSERT_TRUE(button.hasValue());
    int activations = 0;
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    assertOk(updater.setEnabled(button, false));
    publishLayout();

    auto listener = updater.addRoutedPointerListener(
        UI::UIRoutedPointerListenerDesc{
            .node = button,
            .kind = UI::UIRoutedPointerEventKind::ButtonDown,
            .phases = UI::UIEventPhaseMask::Target,
        },
        UI::UIRoutedPointerCallback{
            [this, button](UI::UIRoutedPointerEvent&) noexcept {
                expectOk(updater.setEnabled(button, true));
            }});
    ASSERT_TRUE(listener.has_value())
        << (listener ? "" : listener.error().message);

    auto firstDown = context->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(firstDown.has_value())
        << (firstDown ? "" : firstDown.error().message);
    EXPECT_FALSE(firstDown->consumed);
    EXPECT_TRUE(isEnabled(updater, button));
    auto pressed = updater.isButtonPressed(button);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_FALSE(*pressed);

    auto firstUp = context->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(firstUp.has_value())
        << (firstUp ? "" : firstUp.error().message);
    EXPECT_FALSE(firstUp->consumed);
    EXPECT_EQ(activations, 0);

    auto secondDown = context->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonDown,
        3,
        {.x = 10.0F, .y = 10.0F}));
    auto secondUp = context->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonUp,
        4,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(secondDown.has_value());
    ASSERT_TRUE(secondUp.has_value());
    EXPECT_TRUE(secondDown->consumed);
    EXPECT_TRUE(secondUp->consumed);
    EXPECT_EQ(activations, 1);
}

TEST_F(UIEnabledStateTest, DisablingFromPointerListenerBlocksCurrentDefaultAction)
{
    const UI::UINodeId button = createButton();
    ASSERT_TRUE(button.hasValue());
    int activations = 0;
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout();

    auto listener = updater.addRoutedPointerListener(
        UI::UIRoutedPointerListenerDesc{
            .node = button,
            .kind = UI::UIRoutedPointerEventKind::ButtonDown,
            .phases = UI::UIEventPhaseMask::Target,
        },
        UI::UIRoutedPointerCallback{
            [this, button](UI::UIRoutedPointerEvent&) noexcept {
                expectOk(updater.setEnabled(button, false));
            }});
    ASSERT_TRUE(listener.has_value())
        << (listener ? "" : listener.error().message);

    auto down = context->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_FALSE(down->consumed);
    EXPECT_FALSE(isEnabled(updater, button));
    auto pressed = updater.isButtonPressed(button);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_FALSE(*pressed);

    auto up = context->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_FALSE(up->consumed);
    EXPECT_EQ(activations, 0);
}

TEST_F(UIEnabledStateTest, DisabledCheckboxAndRadioIgnorePointerAndKeyboard)
{
    const UI::UINodeId checkbox = createCheckbox();
    const UI::UINodeId radioButton = createRadioButton();
    ASSERT_TRUE(checkbox.hasValue());
    ASSERT_TRUE(radioButton.hasValue());
    int checkboxActions = 0;
    int radioActions = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{
            [&checkboxActions](const UI::UIButtonActionEvent&) noexcept {
                ++checkboxActions;
            }}));
    assertOk(updater.setRadioButtonAction(
        radioButton,
        UI::UIButtonActionCallback{
            [&radioActions](const UI::UIButtonActionEvent&) noexcept {
                ++radioActions;
            }}));
    assertOk(updater.setEnabled(checkbox, false));
    assertOk(updater.setEnabled(radioButton, false));
    publishLayout();

    u64 sequence = 1;
    for (const UI::UILogicalPoint position : {
             UI::UILogicalPoint{.x = 10.0F, .y = 10.0F},
             UI::UILogicalPoint{.x = 10.0F, .y = 30.0F},
         }) {
        auto down = context->routePointerInput(makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::ButtonDown,
            sequence++,
            position));
        ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
        EXPECT_FALSE(down->consumed);
        auto up = context->routePointerInput(makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::ButtonUp,
            sequence++,
            position));
        ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
        EXPECT_FALSE(up->consumed);
    }

    auto checked = updater.isChecked(checkbox);
    auto checkboxPressed = updater.isCheckboxPressed(checkbox);
    auto selected = updater.isRadioButtonSelected(radioButton);
    auto radioPressed = updater.isRadioButtonPressed(radioButton);
    ASSERT_TRUE(checked.has_value());
    ASSERT_TRUE(checkboxPressed.has_value());
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(radioPressed.has_value());
    EXPECT_FALSE(*checked);
    EXPECT_FALSE(*checkboxPressed);
    EXPECT_FALSE(*selected);
    EXPECT_FALSE(*radioPressed);
    EXPECT_EQ(checkboxActions, 0);
    EXPECT_EQ(radioActions, 0);

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_FALSE(focus->consumed);
    EXPECT_FALSE(focus->focus.hasValue());
    auto activation = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{5},
        5,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(activation.has_value());
    EXPECT_FALSE(activation->consumed);
    EXPECT_FALSE(activation->activated);
}

TEST_F(UIEnabledStateTest, DisabledSliderDoesNotDragChangeOrInvokeCallback)
{
    const UI::UINodeId slider = createSlider();
    ASSERT_TRUE(slider.hasValue());
    assertOk(updater.setSliderRange(slider, 0.0F, 100.0F, 1.0F));
    assertOk(updater.setSliderValue(slider, 25.0F));
    int changes = 0;
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{
            [&changes](const UI::UISliderChangeEvent&) noexcept {
                ++changes;
            }}));
    assertOk(updater.setEnabled(slider, false));
    publishLayout();

    for (const auto [kind, sequence, x] : {
             std::tuple{UI::UIRoutedPointerEventKind::ButtonDown, u64{1}, 90.0F},
             std::tuple{UI::UIRoutedPointerEventKind::Move, u64{2}, 110.0F},
             std::tuple{UI::UIRoutedPointerEventKind::ButtonUp, u64{3}, 119.0F},
         }) {
        auto routed = context->routePointerInput(makePointerInput(
            firstWindow,
            kind,
            sequence,
            {.x = x, .y = 10.0F}));
        ASSERT_TRUE(routed.has_value())
            << (routed ? "" : routed.error().message);
        EXPECT_FALSE(routed->consumed);
    }

    auto value = updater.sliderValue(slider);
    auto dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(value.has_value());
    ASSERT_TRUE(dragging.has_value());
    EXPECT_FLOAT_EQ(*value, 25.0F);
    EXPECT_FALSE(*dragging);
    EXPECT_EQ(changes, 0);
}

TEST_F(UIEnabledStateTest, DisablingTextEditClearsImeAndRejectsFurtherInput)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "Stable"));
    publishLayout();

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, textEdit);
    auto composition = context->routeTextComposition(
        firstWindow,
        Platform::PlatformFrameId{1},
        1,
        "preedit",
        3,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value())
        << (composition ? "" : composition.error().message);
    ASSERT_TRUE(context->imeCompositionActive());

    assertOk(updater.setEnabled(textEdit, false));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_FALSE(context->imeFocus().hasValue());
    EXPECT_FALSE(context->imeCompositionActive());
    EXPECT_TRUE(context->imePreeditUtf8().empty());

    auto input = context->routeTextInput(
        firstWindow,
        Platform::PlatformFrameId{2},
        2,
        "X");
    auto ignoredComposition = context->routeTextComposition(
        firstWindow,
        Platform::PlatformFrameId{3},
        3,
        "new",
        1,
        Platform::TextCompositionStage::Started);
    auto command = context->routeTextEditCommand(
        firstWindow,
        Platform::PlatformFrameId{4},
        4,
        UI::UITextEditCommand::Delete,
        false);
    ASSERT_TRUE(input.has_value());
    ASSERT_TRUE(ignoredComposition.has_value());
    ASSERT_TRUE(command.has_value());
    EXPECT_FALSE(input->consumed);
    EXPECT_FALSE(input->applied);
    EXPECT_FALSE(ignoredComposition->consumed);
    EXPECT_FALSE(ignoredComposition->applied);
    EXPECT_FALSE(command->consumed);
    EXPECT_FALSE(command->applied);

    auto down = context->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonDown,
        5,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_FALSE(down->consumed);
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_FALSE(context->imeFocus().hasValue());
    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Stable");
}

TEST_F(UIEnabledStateTest, DirtyQueueFailurePreservesEnabledFocusAndArmAtomically)
{
    auto localContext = createContext(
        firstWindow,
        {
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 2,
            .paintSnapshotCapacity = 4,
            .routePathCapacity = 4,
            .buttonActionCapacity = 2,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot);
    auto localUpdater = createUpdater(*localContext, localRoot);
    auto buttonResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(buttonResult.has_value());
    const UI::UINodeId button = *buttonResult;
    assertOk(localUpdater.setLayoutStyle(
        localRoot.rootNodeId(),
        fixedSize(100.0F, 80.0F)));
    assertOk(localUpdater.setLayoutStyle(button, fixedSize(40.0F, 30.0F)));
    assertOk(localContext->commitLayout({.width = 100.0F, .height = 80.0F}));

    auto down = localContext->routePointerInput(makePointerInput(
        firstWindow,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    ASSERT_TRUE(down->consumed);
    assertOk(localContext->commitLayout({.width = 100.0F, .height = 80.0F}));

    auto firstBlocker = localUpdater.createElement(localRoot.rootNodeId(), UI::makePanelElement());
    auto secondBlocker = localUpdater.createElement(localRoot.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(firstBlocker.has_value());
    ASSERT_TRUE(secondBlocker.has_value());
    assertOk(localUpdater.setBoxPaint(*firstBlocker, solidFill(1, 2, 3)));
    assertOk(localUpdater.setBoxPaint(*secondBlocker, solidFill(4, 5, 6)));
    ASSERT_EQ(localContext->statistics().dirtyQueuePendingCount, 2U);

    assertOk(localUpdater.setEnabled(button, true));
    const Core::Status rejected = localUpdater.setEnabled(button, false);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_TRUE(isEnabled(localUpdater, button));
    auto pressed = localUpdater.isButtonPressed(button);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_TRUE(*pressed);
    EXPECT_EQ(localContext->defaultActionFocus(), button);
    EXPECT_EQ(localContext->statistics().dirtyQueuePendingCount, 2U);
}

TEST_F(UIEnabledStateTest, DisabledPaintAppliesDeterministicOpacityWithoutChangingEntries)
{
    constexpr u8 DisabledOpacity = 140;
    const UI::UINodeId button = createButton();
    ASSERT_TRUE(button.hasValue());
    assertOk(updater.setBoxPaint(button, solidFill(200, 100, 50, 128)));
    assertOk(updater.setText(button, "A"));
    UI::UITextStyle textStyle{};
    textStyle.color = {.red = 90, .green = 180, .blue = 240, .alpha = 200};
    assertOk(updater.setTextStyle(button, textStyle));
    publishLayout();

    const UI::UICommittedPaintView enabledPaint = context->committedPaint();
    ASSERT_EQ(enabledPaint.size(), 2U);
    ASSERT_EQ(enabledPaint.entries()[0].node, button);
    ASSERT_EQ(enabledPaint.entries()[1].node, button);
    const std::array<UI::UIPremultipliedRgba8Color, 2> enabledColors{
        enabledPaint.entries()[0].solidFill,
        enabledPaint.entries()[1].solidFill,
    };
    const std::array<UI::UILogicalRect, 2> enabledRects{
        enabledPaint.entries()[0].worldRect,
        enabledPaint.entries()[1].worldRect,
    };

    assertOk(updater.setEnabled(button, false));
    publishLayout();
    const UI::UICommittedPaintView disabledPaint = context->committedPaint();
    ASSERT_EQ(disabledPaint.size(), enabledColors.size());
    for (usize index = 0; index < disabledPaint.size(); ++index) {
        EXPECT_EQ(disabledPaint.entries()[index].node, button);
        EXPECT_EQ(disabledPaint.entries()[index].worldRect, enabledRects[index]);
        EXPECT_EQ(
            disabledPaint.entries()[index].solidFill,
            applyOpacity(enabledColors[index], DisabledOpacity));
    }
}

} // namespace
} // namespace Tina::Tests
