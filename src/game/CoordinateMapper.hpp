//
// 屏幕/世界/瓦片 坐标映射工具
// - 提供从屏幕像素坐标转换到世界坐标，及世界->瓦片的整数网格映射
//

#pragma once

#include <cmath>

namespace Tina::Game {

// 将屏幕像素坐标 (mx,my) 映射到世界坐标 (wx,wy)
// 说明：
// - 屏幕坐标原点在左上；世界坐标 y 轴向上；
// - 传入相机窗口 (camX,camY) 以及视口尺寸 (viewW,viewH)
inline void screenToWorld(float mx, float my,
                          int pxW, int pxH,
                          float camX, float camY,
                          float viewW, float viewH,
                          float& wx, float& wy)
{
    float u = (pxW > 0) ? (mx / (float)pxW) : 0.0f;
    float v = (pxH > 0) ? (1.0f - my / (float)pxH) : 0.0f; // 屏幕向下 -> 世界向上
    wx = camX + u * viewW;
    wy = camY + v * viewH;
}

// 世界坐标 -> 瓦片网格坐标（向下取整到网格）
inline void worldToTile(float wx, float wy, int& tx, int& ty)
{
    tx = (int)std::floor(wx);
    ty = (int)std::floor(wy);
}

} // namespace Tina::Game

