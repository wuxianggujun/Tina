#pragma once

#include "math/Vector.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
            // 计算相机的前、右、上向量
            glm::vec3 pos(m_position.x, m_position.y, m_position.z);
            glm::vec3 target(m_target.x, m_target.y, m_target.z);
            glm::vec3 up(0.0f, -1.0f, 0.0f);  // Y轴向下

            glm::vec3 forward = glm::normalize(target - pos);
            glm::vec3 right = glm::normalize(glm::cross(forward, up));
            glm::vec3 newUp = glm::cross(right, forward);

            // 手动构建视图矩阵
            m_viewMatrix[0][0] = right.x;
            m_viewMatrix[0][1] = newUp.x;
            m_viewMatrix[0][2] = -forward.x;
            m_viewMatrix[0][3] = 0.0f;

            m_viewMatrix[1][0] = right.y;
            m_viewMatrix[1][1] = newUp.y;
            m_viewMatrix[1][2] = -forward.y;
            m_viewMatrix[1][3] = 0.0f;

            m_viewMatrix[2][0] = right.z;
            m_viewMatrix[2][1] = newUp.z;
            m_viewMatrix[2][2] = -forward.z;
            m_viewMatrix[2][3] = 0.0f;

            m_viewMatrix[3][0] = -glm::dot(right, pos);
            m_viewMatrix[3][1] = -glm::dot(newUp, pos);
            m_viewMatrix[3][2] = glm::dot(forward, pos);
            m_viewMatrix[3][3] = 1.0f;
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
            OrthographicCamera::updateProjectionMatrix();
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
            // 使用bgfx的坐标系统：Y轴向下，Z轴向内
            float width = m_right - m_left;
            float height = m_bottom - m_top;

            // 使用列主序矩阵（bgfx和OpenGL使用的格式）
            m_projectionMatrix[0][0] = 2.0f / width;
            m_projectionMatrix[0][1] = 0.0f;
            m_projectionMatrix[0][2] = 0.0f;
            m_projectionMatrix[0][3] = 0.0f;

            m_projectionMatrix[1][0] = 0.0f;
            m_projectionMatrix[1][1] = -2.0f / height;  // 翻转Y轴
            m_projectionMatrix[1][2] = 0.0f;
            m_projectionMatrix[1][3] = 0.0f;

            m_projectionMatrix[2][0] = 0.0f;
            m_projectionMatrix[2][1] = 0.0f;
            m_projectionMatrix[2][2] = 2.0f / (m_far - m_near);
            m_projectionMatrix[2][3] = 0.0f;

            m_projectionMatrix[3][0] = -(m_right + m_left) / width;
            m_projectionMatrix[3][1] = (m_bottom + m_top) / height;  // 注意这里的符号
            m_projectionMatrix[3][2] = -(m_far + m_near) / (m_far - m_near);
            m_projectionMatrix[3][3] = 1.0f;
        }

    private:
        float m_left, m_right, m_bottom, m_top, m_near, m_far;
    };

    class PerspectiveCamera : public Camera {
    public:
        PerspectiveCamera(float fov, float aspect, float near = 0.1f, float far = 1000.0f)
            : m_fov(fov), m_aspect(aspect), m_near(near), m_far(far) {
            PerspectiveCamera::updateProjectionMatrix();
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
        }

    private:
        float m_fov, m_aspect, m_near, m_far;
    };
} 