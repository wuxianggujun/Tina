#include <tina/render/Camera2DProjection.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Render::FixedWorldHeight2D>);
static_assert(std::is_trivially_copyable_v<Tina::Render::PixelPerfect2D>);
static_assert(std::is_trivially_copyable_v<Tina::Render::Camera2DSurfaceViewport>);
