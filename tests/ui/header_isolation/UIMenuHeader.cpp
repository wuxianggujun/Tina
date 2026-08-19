#include <tina/ui/UIMenu.hpp>

static_assert(Tina::UI::UIMenuConfig{}.placement == Tina::UI::UIMenuPlacement::Auto);
static_assert(Tina::UI::UIMenuItemConfig{}.kind == Tina::UI::UIMenuItemKind::Command);
