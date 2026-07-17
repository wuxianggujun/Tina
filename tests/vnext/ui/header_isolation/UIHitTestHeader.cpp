#include <tina/ui/UIHitTest.hpp>

#include <type_traits>

static_assert(std::is_enum_v<Tina::UI::UIPointerHitPolicy>);
static_assert(std::is_same_v<
              std::underlying_type_t<Tina::UI::UIPointerHitPolicy>,
              Tina::u8>);
static_assert(
    Tina::UI::UIPointerHitPolicy::Ignore != Tina::UI::UIPointerHitPolicy::Targetable);
