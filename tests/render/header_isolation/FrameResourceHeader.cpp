#include <tina/render/FrameResource.hpp>

#include <type_traits>

namespace {
static_assert(std::is_copy_constructible_v<Tina::Render::FrameResourceRef>);
[[maybe_unused]] constexpr Tina::Render::FrameResourceDescriptor kDescriptor{
    Tina::Render::FrameResourceKind::Texture2D,
    1,
};
} // namespace
