#include <tina/ui/UIListView.hpp>

constexpr Tina::UI::UIListViewStyle DefaultListViewStyle{};
static_assert(DefaultListViewStyle.rowTextOverflow == Tina::UI::UITextOverflow::Clip);
