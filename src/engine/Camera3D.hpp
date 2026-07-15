#pragma once

#include <cstdint>

namespace Tina::Engine {

// Minimal right-handed perspective camera used by the runtime 3D smoke path.
// World convention: Y-up, camera forward points toward -Z, units are meters.
class Camera3D final {
public:
    void setViewportPixels(int width, int height) noexcept;
    void setPerspective(float verticalFovDegrees, float nearPlane, float farPlane) noexcept;
    void lookAt(float eyeX, float eyeY, float eyeZ,
                float targetX, float targetY, float targetZ) noexcept;

    void applyToView(uint16_t viewId) const;

private:
    int m_viewportWidth = 1;
    int m_viewportHeight = 1;
    float m_verticalFovDegrees = 60.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 100.0f;
    float m_eye[3] = {0.0f, 1.5f, 6.0f};
    float m_target[3] = {0.0f, 0.0f, 0.0f};
};

} // namespace Tina::Engine
