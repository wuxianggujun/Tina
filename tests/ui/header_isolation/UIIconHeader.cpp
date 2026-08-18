#include <tina/ui/UIIcon.hpp>

namespace {

constexpr Tina::UI::UIIconContent Icon{};
static_assert(Icon.sampling == Tina::UI::UIImageSampling::Linear);
static_assert(Icon.alignment.horizontal == Tina::UI::UIAxisAlignment::Center);
static_assert(Icon.alignment.vertical == Tina::UI::UIAxisAlignment::Center);

} // namespace
