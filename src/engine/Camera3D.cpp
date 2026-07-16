#include "Camera3D.hpp"
#include "PerspectiveProjection.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>

namespace Tina::Engine {

void Camera3D::setViewportPixels(int width, int height) noexcept
{
    m_viewportWidth = std::max(width, 1);
    m_viewportHeight = std::max(height, 1);
}

void Camera3D::setPerspective(float verticalFovDegrees, float nearPlane, float farPlane) noexcept
{
    m_verticalFovDegrees = std::clamp(verticalFovDegrees, 1.0f, 179.0f);
    m_nearPlane = std::max(nearPlane, 0.001f);
    m_farPlane = std::max(farPlane, m_nearPlane + 0.001f);
}

void Camera3D::lookAt(float eyeX, float eyeY, float eyeZ,
                      float targetX, float targetY, float targetZ) noexcept
{
    m_eye[0] = eyeX;
    m_eye[1] = eyeY;
    m_eye[2] = eyeZ;
    m_target[0] = targetX;
    m_target[1] = targetY;
    m_target[2] = targetZ;
}

void Camera3D::applyToView(uint16_t viewId) const
{
    const bx::Vec3 eye{m_eye[0], m_eye[1], m_eye[2]};
    const bx::Vec3 target{m_target[0], m_target[1], m_target[2]};
    const bx::Vec3 up{0.0f, 1.0f, 0.0f};
    const float aspect = static_cast<float>(m_viewportWidth) /
                         static_cast<float>(m_viewportHeight);

    float view[16];
    float projection[16];
    bx::mtxLookAt(view, eye, target, up, bx::Handedness::Right);
    buildRightHandedPerspective(
        projection, m_verticalFovDegrees, aspect, m_nearPlane, m_farPlane,
        bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(viewId, view, projection);
}

} // namespace Tina::Engine
