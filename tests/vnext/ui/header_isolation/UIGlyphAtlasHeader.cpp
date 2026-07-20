#include <tina/ui/text/UIGlyphAtlas.hpp>

#include <type_traits>

static_assert(std::is_aggregate_v<Tina::UI::UIGlyphAtlasCapacity>);
static_assert(std::is_aggregate_v<Tina::UI::UIGlyphKey>);
static_assert(std::is_aggregate_v<Tina::UI::UIGlyphPlacement>);
static_assert(!std::is_default_constructible_v<Tina::UI::UIGlyphAtlas>);
