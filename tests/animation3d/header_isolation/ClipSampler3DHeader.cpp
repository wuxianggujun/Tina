#include <tina/animation3d/ClipSampler3D.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Animation3D::ClipSampler3D>);
static_assert(std::is_move_constructible_v<Tina::Animation3D::ClipSampler3D>);
// A plain view handed to the callback-free advance path.
static_assert(std::is_trivially_copyable_v<Tina::Animation3D::ClipPlayhead3D>);
