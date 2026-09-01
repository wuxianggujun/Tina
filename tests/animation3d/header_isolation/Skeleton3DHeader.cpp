#include <tina/animation3d/Skeleton3D.hpp>

#include <type_traits>

static_assert(Tina::Animation3D::MaximumJointCount == 256);
static_assert(Tina::Animation3D::JointIndexNone == 0xFFFF);
// A default mask means "every joint", not "no joint": a layer whose caller did not ask for
// masking has to animate the whole skeleton.
static_assert(Tina::Animation3D::JointMask{}.includesEveryJoint());
static_assert(Tina::Animation3D::JointMask{}.includes(0));
static_assert(Tina::Animation3D::JointMask{}.includes(255));
static_assert(!std::is_copy_constructible_v<Tina::Animation3D::Pose3D>);
static_assert(std::is_move_constructible_v<Tina::Animation3D::Pose3D>);
static_assert(!std::is_copy_constructible_v<Tina::Animation3D::Skeleton3D>);
