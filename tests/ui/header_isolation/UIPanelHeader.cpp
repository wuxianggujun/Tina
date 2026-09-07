#include <tina/ui/UIPanel.hpp>
#include <type_traits>

static_assert(std::is_copy_constructible_v<Tina::UI::UIPanel>);
static_assert(!std::is_polymorphic_v<Tina::UI::UIPanel>);
