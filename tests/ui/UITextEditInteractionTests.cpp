#include "UITextEditTestSupport.hpp"

namespace Tina::Tests {
namespace {

using namespace UITextEditTestSupport;

TEST_F(UITextEditTest, CancelPointerInteractionClearsTextEditFocusAndAccept)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "ABC"));
    publishLayout();

    auto down = context->input().routePointerInput(makePrimaryPointerDown(window, 1));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->input().defaultActionFocus(), textEdit);
    EXPECT_EQ(context->text().imeFocus(), textEdit);

    assertOk(context->input().cancelPointerInteraction(window));
    EXPECT_FALSE(context->input().defaultActionFocus().hasValue());
    EXPECT_FALSE(context->text().imeFocus().hasValue());

    auto accept = context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{2},
        2,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(accept.has_value()) << (accept ? "" : accept.error().message);
    EXPECT_FALSE(accept->consumed);
    EXPECT_FALSE(accept->activated);
}

TEST_F(UITextEditTest, CommitClearsTextEditFocusWhenCollapsedOrIgnored)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "Stable"));
    focusWithTab(textEdit);

    UI::UILayoutStyle collapsedStyle = fixedSize(240.0F, 32.0F);
    collapsedStyle.visibility = UI::UIVisibility::Collapsed;
    assertOk(updater.setLayoutStyle(textEdit, collapsedStyle));
    publishLayout();
    EXPECT_FALSE(context->input().defaultActionFocus().hasValue());
    EXPECT_FALSE(context->text().imeFocus().hasValue());

    auto inputAfterCollapse = context->text().routeTextInput(
        window,
        Platform::PlatformFrameId{1},
        1,
        "X");
    ASSERT_TRUE(inputAfterCollapse.has_value())
        << (inputAfterCollapse ? "" : inputAfterCollapse.error().message);
    EXPECT_FALSE(inputAfterCollapse->consumed);
    EXPECT_FALSE(inputAfterCollapse->applied);
    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Stable");

    assertOk(updater.setLayoutStyle(textEdit, fixedSize(240.0F, 32.0F)));
    assertOk(updater.setPointerHitPolicy(textEdit, UI::UIPointerHitPolicy::Targetable));
    focusWithTab(textEdit);

    assertOk(updater.setPointerHitPolicy(textEdit, UI::UIPointerHitPolicy::Ignore));
    publishLayout();
    EXPECT_FALSE(context->input().defaultActionFocus().hasValue());
    EXPECT_FALSE(context->text().imeFocus().hasValue());

    auto inputAfterIgnore = context->text().routeTextInput(
        window,
        Platform::PlatformFrameId{2},
        2,
        "Y");
    ASSERT_TRUE(inputAfterIgnore.has_value())
        << (inputAfterIgnore ? "" : inputAfterIgnore.error().message);
    EXPECT_FALSE(inputAfterIgnore->consumed);
    EXPECT_FALSE(inputAfterIgnore->applied);
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Stable");
}

TEST_F(UITextEditTest, NestedInButtonPrimaryUpAndStrayUpDoNotActivateParent)
{
    auto buttonResult = updater.createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(buttonResult.has_value()) << (buttonResult ? "" : buttonResult.error().message);
    const UI::UINodeId button = *buttonResult;
    auto textEditResult = updater.createElement(button, UI::makeTextEditElement());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;

    int activations = 0;
    assertOk(updater.setLayoutStyle(button, fixedSize(160.0F, 48.0F)));
    assertOk(updater.setLayoutStyle(textEdit, fixedSize(120.0F, 32.0F)));
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout();

    auto down = context->input().routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_EQ(down->pointQuery.target.node, textEdit);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->input().defaultActionFocus(), textEdit);
    EXPECT_EQ(context->text().imeFocus(), textEdit);
    auto pressed = updater.isButtonPressed(button);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    EXPECT_FALSE(*pressed);

    auto up = context->input().routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_EQ(up->pointQuery.target.node, textEdit);
    EXPECT_TRUE(up->consumed);
    pressed = updater.isButtonPressed(button);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_FALSE(*pressed);
    EXPECT_EQ(activations, 0);

    auto strayUp = context->input().routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        3));
    ASSERT_TRUE(strayUp.has_value()) << (strayUp ? "" : strayUp.error().message);
    EXPECT_EQ(strayUp->pointQuery.target.node, textEdit);
    EXPECT_FALSE(strayUp->consumed);
    EXPECT_EQ(activations, 0);
    EXPECT_EQ(context->input().defaultActionFocus(), textEdit);
    EXPECT_EQ(context->text().imeFocus(), textEdit);
}

TEST_F(UITextEditTest, NestedInSliderPrimaryUpAndStrayUpDoNotDragParent)
{
    auto sliderResult = updater.createElement(root.rootNodeId(), UI::makeSliderElement());
    ASSERT_TRUE(sliderResult.has_value()) << (sliderResult ? "" : sliderResult.error().message);
    const UI::UINodeId slider = *sliderResult;
    auto textEditResult = updater.createElement(slider, UI::makeTextEditElement());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;

    int changes = 0;
    assertOk(updater.setLayoutStyle(slider, fixedSize(160.0F, 48.0F)));
    assertOk(updater.setLayoutStyle(textEdit, fixedSize(120.0F, 32.0F)));
    assertOk(updater.setSliderRange(slider, 0.0F, 100.0F, 1.0F));
    assertOk(updater.setSliderValue(slider, 25.0F));
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{
            [&changes](const UI::UISliderChangeEvent&) noexcept {
                ++changes;
            }}));
    publishLayout();

    auto down = context->input().routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        10.0F,
        10.0F));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_EQ(down->pointQuery.target.node, textEdit);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->input().defaultActionFocus(), textEdit);
    EXPECT_EQ(context->text().imeFocus(), textEdit);
    auto dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value()) << (dragging ? "" : dragging.error().message);
    EXPECT_FALSE(*dragging);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value()) << (value ? "" : value.error().message);
    EXPECT_FLOAT_EQ(*value, 25.0F);
    EXPECT_EQ(changes, 0);

    auto up = context->input().routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        100.0F,
        10.0F));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_EQ(up->pointQuery.target.node, textEdit);
    EXPECT_TRUE(up->consumed);
    dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    EXPECT_FALSE(*dragging);
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 25.0F);
    EXPECT_EQ(changes, 0);

    auto strayUp = context->input().routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        3,
        100.0F,
        10.0F));
    ASSERT_TRUE(strayUp.has_value()) << (strayUp ? "" : strayUp.error().message);
    EXPECT_EQ(strayUp->pointQuery.target.node, textEdit);
    EXPECT_FALSE(strayUp->consumed);
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 25.0F);
    EXPECT_EQ(changes, 0);
    EXPECT_EQ(context->input().defaultActionFocus(), textEdit);
    EXPECT_EQ(context->text().imeFocus(), textEdit);
}

} // namespace
} // namespace Tina::Tests
