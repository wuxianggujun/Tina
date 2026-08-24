#include <tina/ui/UIRoutedPointerListenerToken.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::UI::UIRoutedPointerListenerToken>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UIRoutedPointerListenerToken>);
