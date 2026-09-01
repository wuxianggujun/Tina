#include <tina/animation3d/PoseBlend3D.hpp>

// Header-only surface: this TU exists to prove the header compiles with nothing else
// included, and that its free functions are declared noexcept as the blend contract states.
static_assert(noexcept(Tina::Animation3D::isPoseFinite(
    *static_cast<const Tina::Animation3D::Pose3D*>(nullptr))));
