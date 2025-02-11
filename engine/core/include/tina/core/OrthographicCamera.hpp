//
// Created by wuxianggujun on 2025/2/11.
//

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <bx/math.h>
#include <bgfx/bgfx.h>


namespace Tina
{
    class OrthographicCamera
    {
    public:
        explicit OrthographicCamera(float left, float right, float bottom, float top)
        {
            setProjection(left, right, bottom, top);
        }

        void setProjection(float left, float right, float bottom, float top)
        {
            // 计算投影矩阵
            bx::mtxOrtho(m_projectionMatrix,
                         left, right, // left, right
                         bottom, top, // bottom, top (反转Y轴)
                         -1.0f, 1.0f, // near, far
                         0.0f, // offset
                         bgfx::getCaps()->homogeneousDepth);
            updateViewProjection();
        }

        void setPosition(const glm::vec3& position)
        {
            m_position = position;
            updateView();
        }

        void setRotation(float rotation)
        {
            m_rotation = rotation;
            updateView();
        }

        [[nodiscard]] const glm::vec3& getPosition() const { return m_position; }
        [[nodiscard]] float getRotation() const { return m_rotation; }

        [[nodiscard]] const float* getProjection() const { return m_projectionMatrix; }
        [[nodiscard]] const float* getView() const { return m_viewMatrix; }
        [[nodiscard]] const float* getViewProjection() const { return m_viewProjectionMatrix; }

        // 添加这个方法来获取GLM格式的投影矩阵
        [[nodiscard]] glm::mat4 getProjectionMatrix() const {
            return glm::make_mat4(m_projectionMatrix);
        }
    private:
        void updateView()
        {
            // 计算视图矩阵
            bx::mtxIdentity(m_viewMatrix);

            // 应用旋转
            if (m_rotation != 0.0f)
            {
                float rotationMatrix[16];
                bx::mtxRotateZ(rotationMatrix, m_rotation);
                bx::mtxMul(m_viewMatrix, m_viewMatrix, rotationMatrix);
            }

            // 应用位移
            float translation[16];
            bx::mtxTranslate(translation, -m_position.x, -m_position.y, -m_position.z);
            bx::mtxMul(m_viewMatrix, m_viewMatrix, translation);

            updateViewProjection();
        }

        void updateViewProjection()
        {
            // 计算视图投影矩阵
            bx::mtxMul(m_viewProjectionMatrix, m_viewMatrix, m_projectionMatrix);
        }

        float m_projectionMatrix[16]{};
        float m_viewMatrix[16]{};
        float m_viewProjectionMatrix[16]{};

        glm::vec3 m_position = {0.0f, 0.0f, 0.0f};
        float m_rotation = 0.0f;
    };
} // Tina
