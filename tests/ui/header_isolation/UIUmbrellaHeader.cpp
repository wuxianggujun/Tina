#include <tina/ui/UI.hpp>

static_assert(Tina::UI::UIStyleRoleId::PanelSurface != Tina::UI::UIStyleRoleId::ButtonPrimary);
static_assert(Tina::UI::hasBehavior(Tina::UI::UIElementBehavior::Focusable |
                                        Tina::UI::UIElementBehavior::Activate,
                                    Tina::UI::UIElementBehavior::Activate));
static_assert(!Tina::UI::UINodeId{}.hasValue());
