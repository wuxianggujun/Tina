#include <tina/animation3d/BlendTree3D.hpp>

#include <type_traits>

static_assert(Tina::Animation3D::MaximumBlendTreeNodeCount == 64);
static_assert(Tina::Animation3D::BlendTreeNodeNone == 0xFFFF);
static_assert(!std::is_copy_constructible_v<Tina::Animation3D::BlendTree3D>);
static_assert(std::is_trivially_copyable_v<Tina::Animation3D::BlendTreeNodeDesc>);
