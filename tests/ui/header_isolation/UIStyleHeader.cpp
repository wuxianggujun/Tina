#include <tina/ui/UIStyle.hpp>

static_assert(!Tina::UI::UIStyleClassId{}.hasValue());
static_assert(Tina::UI::UIStyleClassId{1}.hasValue());
static_assert(!Tina::UI::UIStyleTokenId{}.hasValue());
static_assert(Tina::UI::UIStyleTokenId{1}.hasValue());
static_assert(Tina::UI::UIStyleBoxFillRule{
                  .colorToken = Tina::UI::UIStyleTokenId{1},
              }
                  .colorToken.hasValue());
static_assert(Tina::UI::hasStyleState(
    Tina::UI::UIStyleState::Hovered | Tina::UI::UIStyleState::Focused,
    Tina::UI::UIStyleState::Focused));
static_assert(Tina::UI::hasStyleOverride(
    Tina::UI::UIStyleOverride::BoxPaint | Tina::UI::UIStyleOverride::TextStyle,
    Tina::UI::UIStyleOverride::TextStyle));
