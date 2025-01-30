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
            m_viewMatrix = glm::lookAt(
                glm::vec3(m_position.x, m_position.y, m_position.z),
                glm::vec3(m_target.x, m_target.y, m_target.z),
                glm::vec3(0.0f, 1.0f, 0.0f)
            );
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
            m_projectionMatrix = glm::ortho(m_left, m_right, m_bottom, m_top, m_near, m_far);
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