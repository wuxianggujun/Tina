#include <tina/core/color/BlendMode.hpp>

static_assert(Tina::Core::isSupportedBlendMode(Tina::Core::BlendMode::PremultipliedAlpha));
static_assert(Tina::Core::isSupportedBlendMode(Tina::Core::BlendMode::Additive));
static_assert(!Tina::Core::isSupportedBlendMode(static_cast<Tina::Core::BlendMode>(2)));
