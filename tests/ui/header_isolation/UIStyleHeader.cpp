#include <tina/ui/UIStyle.hpp>

static_assert(Tina::UI::hasStyleOverride(
    Tina::UI::UIStyleOverride::BoxPaint | Tina::UI::UIStyleOverride::TextStyle,
    Tina::UI::UIStyleOverride::TextStyle));
