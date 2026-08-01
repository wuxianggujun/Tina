#include <tina/ui/UIImageSource.hpp>

constexpr Tina::UI::UIImagePixelInsets Insets{.left = 1, .top = 2, .right = 3, .bottom = 4};
static_assert(Insets.left == 1);
static_assert(Tina::UI::UIImageSampling::Linear != Tina::UI::UIImageSampling::Nearest);
