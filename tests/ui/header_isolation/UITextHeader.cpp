#include <tina/ui/UIText.hpp>

#include <type_traits>

static_assert(std::is_aggregate_v<Tina::UI::UITextStyle>);
static_assert(std::is_aggregate_v<Tina::UI::UITextMetrics>);
static_assert(std::is_aggregate_v<Tina::UI::UITextLineClamp>);
static_assert(!Tina::UI::UITextLineClamp{}.enabled());
static_assert(Tina::UI::UITextLineClamp{.maximumLines = 2}.enabled());

// Overflow is a plain authoring enum; the ellipsis run must stay a compile-time
// UTF-8 constant so truncation never allocates or scans for it at paint time.
static_assert(std::is_same_v<std::underlying_type_t<Tina::UI::UITextOverflow>, Tina::u8>);
static_assert(Tina::UI::UITextOverflow{} == Tina::UI::UITextOverflow::Clip);
static_assert(Tina::UI::UITextEllipsisUtf8.size() == 3);
static_assert(Tina::UI::UITextEllipsisUtf8[0] == '\xE2');
