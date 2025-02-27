//
// Created by wuxianggujun on 2025/2/27.
//

#include "tina/core/Transform.hpp"
#include "tina/core/Node.hpp"
#include "tina/log/Log.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Tina
{
    glm::mat4 Transform::getWorldMatrix() const
    {
        // 记录当前变换状态
        TINA_ENGINE_TRACE("Transform状态 - 位置: ({}, {}, {}), 缩放: ({}, {}, {})",
                          m_position.x, m_position.y, m_position.z,
                          m_scale.x, m_scale.y, m_scale.z);
        
        // 创建变换矩阵
        glm::mat4 matrix = glm::mat4(1.0f);
        
        // 应用变换 (顺序: 缩放 -> 旋转 -> 平移)
        matrix = glm::scale(matrix, m_scale);                         // 1. 缩放
        matrix = matrix * glm::mat4_cast(m_rotation);                 // 2. 旋转
        matrix = glm::translate(matrix, m_position);                  // 3. 平移
        
        // 输出转换后矩阵以进行调试
        const float* matData = glm::value_ptr(matrix);
        TINA_ENGINE_TRACE("Transform矩阵: [{:.2f}, {:.2f}, {:.2f}, {:.2f}, {:.2f}, {:.2f}, {:.2f}, {:.2f}...]",
                         matData[0], matData[1], matData[2], matData[3], 
                         matData[4], matData[5], matData[6], matData[7]);
        
        return matrix;
    }
}