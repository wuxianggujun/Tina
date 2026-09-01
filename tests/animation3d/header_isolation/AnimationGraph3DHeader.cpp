#include <tina/animation3d/AnimationGraph3D.hpp>

#include <type_traits>

static_assert(Tina::Animation3D::MaximumLayerCount == 8);
// A default handle addresses nothing: a graph must reject it rather than treating index 0
// as a valid layer.
static_assert(!Tina::Animation3D::LayerId{}.hasValue());
static_assert(!Tina::Animation3D::StateId{}.hasValue());
static_assert(!std::is_copy_constructible_v<Tina::Animation3D::AnimationGraph3D>);
static_assert(std::is_move_constructible_v<Tina::Animation3D::AnimationGraph3D>);
static_assert(std::is_trivially_copyable_v<Tina::Animation3D::RootMotionDelta3D>);
