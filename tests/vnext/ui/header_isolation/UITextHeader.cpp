#include <tina/ui/UIText.hpp>

#include <type_traits>

static_assert(std::is_aggregate_v<Tina::UI::UITextStyle>);
static_assert(std::is_aggregate_v<Tina::UI::UITextMetrics>);
