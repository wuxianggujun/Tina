#include <tina/ui/text/TextShaper.h>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::UI::TextShaper>);
static_assert(std::is_aggregate_v<Tina::UI::GlyphRun>);
