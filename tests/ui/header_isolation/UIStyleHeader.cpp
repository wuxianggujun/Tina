#include <tina/ui/UIStyle.hpp>

static_assert(!Tina::UI::UIStyleClassId{}.hasValue());
static_assert(Tina::UI::UIStyleClassId{1}.hasValue());
static_assert(!Tina::UI::UIStyleTokenId{}.hasValue());
static_assert(Tina::UI::UIStyleTokenId{1}.hasValue());
static_assert(Tina::UI::UIStyleBoxFillRule{
                  .colorToken = Tina::UI::UIStyleTokenId{1},
              }
                  .colorToken.hasValue());
static_assert(Tina::UI::UIStyleBoxFillRule{
                  .imageTintToken = Tina::UI::UIStyleTokenId{2},
              }
                  .imageTintToken.hasValue());
static_assert(Tina::UI::hasStyleState(
    Tina::UI::UIStyleState::Hovered | Tina::UI::UIStyleState::Focused,
    Tina::UI::UIStyleState::Focused));
static_assert(Tina::UI::hasStyleOverride(
    Tina::UI::UIStyleOverride::BoxPaint | Tina::UI::UIStyleOverride::TextStyle,
    Tina::UI::UIStyleOverride::TextStyle));
static_assert(Tina::UI::hasStyleOverride(Tina::UI::UIStyleOverride::All,
                                           Tina::UI::UIStyleOverride::ImageTint));
static_assert(!Tina::UI::stylePropertyDirtiesLayout(Tina::UI::UIStylePropertyKind::ColorOrOpacity));
static_assert(Tina::UI::stylePropertyDirtiesPaint(Tina::UI::UIStylePropertyKind::ColorOrOpacity));
static_assert(!Tina::UI::stylePropertyDirtiesLayout(Tina::UI::UIStylePropertyKind::ColorToken));
static_assert(Tina::UI::stylePropertyDirtiesPaint(Tina::UI::UIStylePropertyKind::ColorToken));
static_assert(Tina::UI::stylePropertyDirtiesLayout(Tina::UI::UIStylePropertyKind::TextStyle));
static_assert(Tina::UI::stylePropertyDirtiesLayout(Tina::UI::UIStylePropertyKind::TextWrap));
static_assert(Tina::UI::stylePropertyDirtiesPaint(Tina::UI::UIStylePropertyKind::TextWrap));
static_assert(Tina::UI::stylePropertyDirtiesLayout(Tina::UI::UIStylePropertyKind::TextLineClamp));
static_assert(Tina::UI::stylePropertyDirtiesPaint(Tina::UI::UIStylePropertyKind::TextLineClamp));
static_assert(Tina::UI::stylePropertyDirtiesHit(Tina::UI::UIStylePropertyKind::PointerHitPolicy));
static_assert(!Tina::UI::stylePropertyDirtiesPaint(Tina::UI::UIStylePropertyKind::PointerHitPolicy));
static_assert(Tina::UI::stylePropertyDirtiesLayout(Tina::UI::UIStylePropertyKind::LayoutStyle));
static_assert(!Tina::UI::stylePropertyDirtiesPaint(Tina::UI::UIStylePropertyKind::LayoutStyle));
static_assert(!Tina::UI::stylePropertyDirtiesLayout(Tina::UI::UIStylePropertyKind::TextOverflow));
static_assert(Tina::UI::stylePropertyDirtiesPaint(Tina::UI::UIStylePropertyKind::TextOverflow));
static_assert(Tina::UI::dirtyFlagsForStyleOverride(Tina::UI::UIStyleOverride::ImageTint) ==
              Tina::UI::dirtyFlagsForStyleProperty(Tina::UI::UIStylePropertyKind::ColorOrOpacity));
