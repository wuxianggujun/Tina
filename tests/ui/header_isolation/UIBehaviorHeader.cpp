#include <tina/ui/UIBehavior.hpp>

static_assert(Tina::UI::hasBehavior(
    Tina::UI::UIElementBehavior::Focusable | Tina::UI::UIElementBehavior::Activate,
    Tina::UI::UIElementBehavior::Activate));
