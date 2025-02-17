#pragma once

#include <glm/vec4.hpp>
#include "tina/core/Core.hpp"

namespace Tina {

using UIColor = glm::vec4;

/**
 * @brief 线条样式
 */
struct LineStyle {
    UIColor color{1.0f};     ///< 线条颜色
    float thickness = 1.0f;   ///< 线条粗细
};

/**
 * @brief 矩形样式
 */
struct RectStyle {
    UIColor fillColor{1.0f};     ///< 填充颜色
    UIColor borderColor{1.0f};   ///< 边框颜色
    float borderWidth = 0.0f;    ///< 边框宽度
    float cornerRadius = 0.0f;   ///< 圆角半径
    bool filled = true;          ///< 是否填充
    float borderThickness = 1.0f; ///< 边框粗细
};

} // namespace Tina 