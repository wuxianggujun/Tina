#pragma once

#include "math/Vector.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace Tina {
    class Camera {
    public:
        Camera() = default;
        virtual ~Camera() = default;

        // 获取视图和投影矩阵
        const glm::mat4& getViewMatrix() const { return m_viewMatrix; }
        const glm::mat4& getProjectionMatrix() const { return m_projectionMatrix; }
        
        // 设置相机位置和目标
        void setPosition(const Vector3f& position) { 
            m_position = position; 
            updateViewMatrix();
        }
        
        void setTarget(const Vector3f& target) { 
            m_target = target; 
            updateViewMatrix();
        }

        // 获取相机属性
        const Vector3f& getPosition() const { return m_position; }
        const Vector3f& getTarget() const { return m_target; }

    protected:
        // 更新视图矩阵
        virtual void updateViewMatrix() {
            // 在2D渲染中，我们只需要一个简单的平移矩阵
            m_viewMatrix = glm::mat4(1.0f);
            m_viewMatrix = glm::translate(m_viewMatrix, glm::vec3(-m_position.x, -m_position.y, -m_position.z));
        }

        // 更新投影矩阵（由派生类实现）
        virtual void updateProjectionMatrix() = 0;

        Vector3f m_position{0.0f, 0.0f, 0.0f};
        Vector3f m_target{0.0f, 0.0f, 0.0f};
        glm::mat4 m_viewMatrix{1.0f};
        glm::mat4 m_projectionMatrix{1.0f};
    };

    class OrthographicCamera : public Camera {
    public:
        OrthographicCamera(float left, float right, float bottom, float top, float near = 0.0f, float far = 100.0f)
            : m_left(left), m_right(right), m_bottom(bottom), m_top(top), m_near(near), m_far(far) {
            updateProjectionMatrix();
        }

        void setProjection(float left, float right, float bottom, float top, float near = 0.0f, float far = 100.0f) {
            m_left = left;
            m_right = right;
            m_bottom = bottom;
            m_top = top;
            m_near = near;
            m_far = far;
            updateProjectionMatrix();
        }

    protected:
        void updateProjectionMatrix() override {
            // 创建正交投影矩阵
            m_projectionMatrix = glm::ortho(
                m_left,                 // left
                m_right,                // right
                m_bottom,               // bottom
                m_top,                  // top
                m_near,                 // near
                m_far                   // far
            );

            // 调整深度范围从[-1,1]到[0,1]（bgfx要求）
            glm::mat4 depthAdjust = glm::mat4(1.0f);
            depthAdjust[2][2] = 0.5f;   // 缩放Z到[0,1]范围
            depthAdjust[3][2] = 0.5f;   // 平移Z到[0,1]范围

            m_projectionMatrix = depthAdjust * m_projectionMatrix;
        }

    private:
        float m_left, m_right, m_bottom, m_top, m_near, m_far;
    };

    class PerspectiveCamera : public Camera {
    public:
        PerspectiveCamera(float fov, float aspect, float near = 0.1f, float far = 1000.0f)
            : m_fov(fov), m_aspect(aspect), m_near(near), m_far(far) {
            updateProjectionMatrix();
        }

        void setProjection(float fov, float aspect, float near = 0.1f, float far = 1000.0f) {
            m_fov = fov;
            m_aspect = aspect;
            m_near = near;
            m_far = far;
            updateProjectionMatrix();
        }

    protected:
        void updateProjectionMatrix() override {
            m_projectionMatrix = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
            
            // 调整深度范围从[-1,1]到[0,1]（bgfx要求）
            glm::mat4 depthAdjust = glm::mat4(1.0f);
            depthAdjust[2][2] = 0.5f;
            depthAdjust[3][2] = 0.5f;
            
            m_projectionMatrix = depthAdjust * m_projectionMatrix;
        }

    private:
        float m_fov, m_aspect, m_near, m_far;
    };
} 