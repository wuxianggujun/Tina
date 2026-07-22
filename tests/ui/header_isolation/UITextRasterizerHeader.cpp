#include <tina/ui/text/UITextRasterizer.hpp>

#include <type_traits>

static_assert(!std::is_default_constructible_v<Tina::UI::IUITextRasterizer>);
static_assert(std::is_aggregate_v<Tina::UI::UITextRasterizerCapacity>);
static_assert(std::is_aggregate_v<Tina::UI::UITextGlyphRaster>);
