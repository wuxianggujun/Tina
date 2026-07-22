#include <tina/ui/UIWidgetKind.hpp>

#include <type_traits>

static_assert(std::is_enum_v<Tina::UI::UIWidgetKind>);
static_assert(std::is_same_v<std::underlying_type_t<Tina::UI::UIWidgetKind>, Tina::u8>);
static_assert(Tina::UI::UIWidgetKind::Root != Tina::UI::UIWidgetKind::Button);
