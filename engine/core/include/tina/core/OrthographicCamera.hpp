//
// Created by wuxianggujun on 2025/2/11.
//

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <bx/math.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/type_ptr.hpp>
#include "tina/event/Event.hpp"
#include "tina/log/Logger.hpp"

namespace Tina
{
    class OrthographicCamera
    {
    public:
        enum class ProjectionType
        {
            Centered, // 中心点为原点的投影
            ScreenSpace // 屏幕空间投影(左上角为原点)
        };


        OrthographicCamera(float left, float right, float bottom, float top)
            : m_ProjectionMatrix(glm::ortho(left, right, bottom, top, -1.0f, 1.0f))
              , m_ViewMatrix(1.0f)
              , m_Position(0.0f)
              , m_Rotation(0.0f),
              m_ProjectionType(ProjectionType::ScreenSpace)
        {
            m_Left = left;
            m_Right = right;
            m_Bottom = bottom;
            m_Top = top;

            m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
        }


        void setProjection(float left, float right, float bottom, float top)
        {
            m_Left = left;
            m_Right = right;
            m_Bottom = bottom;
            m_Top = top;

            if (m_ProjectionType == ProjectionType::ScreenSpace)
            {
                // 屏幕空间投影(左上角为原点)
                m_ProjectionMatrix = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
            }
            else
            {
                // 中心点为原点的投影
                float halfWidth = (right - left) * 0.5f;
                float halfHeight = (top - bottom) * 0.5f;
                m_ProjectionMatrix = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -1.0f, 1.0f);
            }

            m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
            TINA_CORE_LOG_DEBUG("Camera projection updated: L={}, R={}, B={}, T={}", left, right, bottom, top);
        }

        void setProjectionType(ProjectionType type)
        {
            if (m_ProjectionType != type)
            {
                m_ProjectionType = type;
                // 重新计算投影矩阵
                setProjection(m_Left, m_Right, m_Bottom, m_Top);
            }
        }

        void onUpdate(float deltaTime)
        {
            // 在这里更新相机的位置、旋转等
            if (m_Position != m_LastPosition || m_Rotation != m_LastRotation)
            {
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position) *
                    glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation), glm::vec3(0, 0, 1));

                m_ViewMatrix = glm::inverse(transform);
                m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;

                m_LastPosition = m_Position;
                m_LastRotation = m_Rotation;
            }
        }

        const glm::mat4& getProjectionMatrix() const { return m_ProjectionMatrix; }
        const glm::mat4& getViewMatrix() const { return m_ViewMatrix; }
        const glm::mat4& getViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

        const glm::vec3& getPosition() const { return m_Position; }
        void setPosition(const glm::vec3& position) { m_Position = position; }

        float getRotation() const { return m_Rotation; }
        void setRotation(float rotation) { m_Rotation = rotation; }

        float getZoomLevel() const { return m_ZoomLevel; }
        void setZoomLevel(float level) { m_ZoomLevel = level; }

    private:
        ProjectionType m_ProjectionType;
        float m_ZoomLevel = 1.0f;

        glm::mat4 m_ProjectionMatrix;
        glm::mat4 m_ViewMatrix;
        glm::mat4 m_ViewProjectionMatrix;

        glm::vec3 m_Position;
        float m_Rotation;

        glm::vec3 m_LastPosition;
        float m_LastRotation;

        // 保存投影参数
        float m_Left = 0.0f;
        float m_Right = 0.0f;
        float m_Bottom = 0.0f;
        float m_Top = 0.0f;
    };
} // namespace Tina
