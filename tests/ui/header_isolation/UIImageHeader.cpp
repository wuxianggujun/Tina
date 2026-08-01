#include <tina/ui/UIImage.hpp>

namespace {

constexpr Tina::UI::UIImageContent Image{};
static_assert(Image.fit == Tina::UI::UIImageFit::Contain);
static_assert(Image.sampling == Tina::UI::UIImageSampling::Linear);

} // namespace
