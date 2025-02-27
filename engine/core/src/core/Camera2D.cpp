//
// Created by wuxianggujun on 2025/2/26.
//

#include "tina/core/Camera2D.hpp"
#include "tina/log/Log.hpp"
#include <bgfx/bgfx.h>

namespace Tina
{
    Camera2D::Camera2D(float width, float height) : m_width(width), m_height(height)
    {
        updateMatrices();
    }

    void Camera2D::setPosition(const Math::Vec2f& position)
    {
        if (m_position != position)
        {
            m_position = position;
            m_dirty = true;
        }
    }

    void Camera2D::setZoom(float zoom)
    {
        // 防止缩放值为0或负值
        if (zoom <= 0.0f)
        {
            TINA_ENGINE_WARN("Camera2D::setZoom - Invalid zoom value ({}), must be greater than 0.0", zoom);
            zoom = 0.01f;
        }

        if (m_zoom != zoom)
        {
            m_zoom = zoom;
            m_dirty = true;
        }
    }

    void Camera2D::setRotation(float rotation)
    {
        if (m_rotation != rotation)
        {
            m_rotation = rotation;
            m_dirty = true;
        }
    }

    void Camera2D::setViewport(float width, float height)
    {
        if (m_width != width || m_height != height)
        {
            m_width = width;
            m_height = height;
            m_dirty = true;
        }
    }

    const float* Camera2D::getViewMatrix() const
    {
        return m_viewMatrix;
    }

    const float* Camera2D::getProjectionMatrix() const
    {
        return m_projMatrix;
    }

    void Camera2D::updateMatrices()
    {
        if (!m_dirty)
        {
            return;
        }

        // 重置矩阵
        bx::mtxIdentity(m_viewMatrix);
        
        // 设置视图矩阵 (转换顺序：缩放 -> 旋转 -> 平移)
        float tmpMatrix[16];
        
        // 平移（需要取反，因为是视图矩阵）
        bx::mtxTranslate(m_viewMatrix, -m_position.x, -m_position.y, 0.0f);
        
        // 旋转（围绕Z轴）
        if (m_rotation != 0.0f)
        {
            bx::mtxRotateZ(tmpMatrix, m_rotation);
            bx::mtxMul(m_viewMatrix, m_viewMatrix, tmpMatrix);
        }
        
        // 缩放
        if (m_zoom != 1.0f)
        {
            bx::mtxScale(tmpMatrix, m_zoom, m_zoom, 1.0f);
            bx::mtxMul(m_viewMatrix, m_viewMatrix, tmpMatrix);
        }
        
        // 设置正交投影矩阵，使用左上角作为原点，Y轴向下
        bx::mtxOrtho(m_projMatrix, 
            0.0f, m_width,           // left, right
            m_height, 0.0f,          // bottom, top (翻转Y轴，使Y轴向下为正)
            -1.0f, 1.0f,             // near, far
            0.0f, bgfx::getCaps()->homogeneousDepth);
            
        m_dirty = false;
    }
} 