#include <tina/render/UIDisplayList.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Render::UIPixelRect>);
static_assert(std::is_trivially_copyable_v<Tina::Render::UIPremultipliedRgba8>);
static_assert(!Tina::Render::UIClipId{}.hasClip());
static_assert(!std::is_constructible_v<Tina::Render::UIClipId, Tina::u32>);
