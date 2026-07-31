#include <tina/ui/UITextEdit.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::UI::UITextSelection>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UITextEditPaint>);
static_assert(Tina::UI::UITextSelection{}.isCollapsed());
