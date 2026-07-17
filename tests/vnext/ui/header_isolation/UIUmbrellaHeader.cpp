#include <tina/ui/UI.hpp>

static_assert(Tina::UI::UIWidgetKind::Root != Tina::UI::UIWidgetKind::Panel);
static_assert(!Tina::UI::UINodeId{}.hasValue());
