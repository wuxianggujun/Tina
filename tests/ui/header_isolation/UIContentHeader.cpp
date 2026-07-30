#include <tina/ui/UIContent.hpp>

namespace {

constexpr Tina::UI::UIContentAlignment DefaultAlignment{};
constexpr Tina::UI::UICommittedContentPlacement EmptyPlacement{};

static_assert(DefaultAlignment.horizontal == Tina::UI::UIAxisAlignment::Start);
static_assert(DefaultAlignment.vertical == Tina::UI::UIAxisAlignment::Start);
static_assert(!EmptyPlacement.hasIntrinsicContent);

} // namespace
