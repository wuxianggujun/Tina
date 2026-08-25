#include <tina/ui/UIElement.hpp>

namespace {

[[maybe_unused]] constexpr auto Panel = Tina::UI::makePanelElement();
[[maybe_unused]] constexpr auto Button = Tina::UI::makeButtonElement("Run");
[[maybe_unused]] constexpr auto Label = Tina::UI::makeLabelElement("Wrapped");

static_assert(Panel.behaviors == Tina::UI::UIElementBehavior::None);
static_assert(Tina::UI::hasBehavior(Button.behaviors, Tina::UI::UIElementBehavior::Activate));
static_assert(Button.visual.styleRole == Tina::UI::UIStyleRoleId::ButtonTonal);
static_assert(Button.semantics.role == Tina::UI::UISemanticsRole::Button);
static_assert(Button.text.has_value() && *Button.text == "Run");
static_assert(Button.contentAlignment.horizontal == Tina::UI::UIAxisAlignment::Center);
static_assert(Button.contentAlignment.vertical == Tina::UI::UIAxisAlignment::Center);
static_assert(Label.textWrapMode == Tina::UI::UITextWrapMode::Words);
static_assert(!Label.textLineClamp.enabled());

} // namespace
