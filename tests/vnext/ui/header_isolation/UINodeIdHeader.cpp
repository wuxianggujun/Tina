#include <tina/ui/UINodeId.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::UI::UINodeId>);
static_assert(std::is_default_constructible_v<Tina::UI::UINodeId>);
static_assert(!std::is_constructible_v<Tina::UI::UINodeId, Tina::Platform::WindowId, Tina::u32, Tina::u32>);

constexpr Tina::UI::UINodeId EmptyUINode{};
static_assert(!EmptyUINode.hasValue());
static_assert(!static_cast<bool>(EmptyUINode));
