//
// Camera2D 实现

#include "Camera2D.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace Tina::Game {

void Camera2D::buildViewProj(float* outView16, float* outProj16) const
{
    // 视图矩阵：把世界平移到以相机左下角为原点
    bx::mtxTranslate(outView16, -m_x, -m_y, 0.0f);

    // 正交投影：原点左下，y 向上
    const float vw = viewW();
    const float vh = viewH();
    const bool homogeneous = bgfx::getCaps()->homogeneousDepth;
    bx::mtxOrtho(outProj16,
                 0.0f, vw,       // left, right
                 0.0f, vh,       // bottom, top
                 0.0f, 1000.0f,  // z near, z far
                 0.0f,           // offset
                 homogeneous);
}

} // namespace Tina::Game
