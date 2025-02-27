//
// Created by wuxianggujun on 2025/2/26.
//

#pragma once

#include "tina/core/Core.hpp"
#include "tina/math/Vec.hpp"
#include <bx/math.h>

namespace Tina
{
    /**
     * @brief 2D相机类，用于处理2D场景的视图和投影
     */
    class TINA_CORE_API Camera2D
    {
    public:
        /**
         * @brief 构造函数
         * @param width 视口宽度
         * @param height 视口高度
         */
        Camera2D(float width, float height);
        ~Camera2D() = default;

        /**
         * @brief 设置相机位置
         * @param position 新位置
         */
        void setPosition(const Math::Vec2f& position);

        /**
         * @brief 设置相机缩放
         * @param zoom 缩放值
         */
        void setZoom(float zoom);

        /**
         * @brief 设置相机旋转角度
         * @param rotation 旋转角度（弧度）
         */
        void setRotation(float rotation);

        /**
         * @brief 设置视口大小
         * @param width 视口宽度
         * @param height 视口高度
         */
        void setViewport(float width, float height);

        /**
         * @brief 获取视图矩阵
         * @return 视图矩阵
         */
        const float* getViewMatrix() const;

        /**
         * @brief 获取投影矩阵
         * @return 投影矩阵
         */
        const float* getProjectionMatrix() const;

        /**
         * @brief 获取相机位置
         * @return 相机位置
         */
        const Math::Vec2f& getPosition() const { return m_position; }

        /**
         * @brief 获取相机缩放
         * @return 缩放值
         */
        float getZoom() const { return m_zoom; }

        /**
         * @brief 获取相机旋转角度
         * @return 旋转角度（弧度）
         */
        float getRotation() const { return m_rotation; }

        /**
         * @brief 更新相机矩阵
         */
        void updateMatrices();

    private:
        Math::Vec2f m_position{0.0f, 0.0f}; // 相机位置
        float m_rotation{0.0f};       // 相机旋转（弧度）
        float m_zoom{1.0f};           // 相机缩放
        float m_width{800.0f};        // 视口宽度
        float m_height{600.0f};       // 视口高度

        float m_viewMatrix[16]{};     // 视图矩阵
        float m_projMatrix[16]{};     // 投影矩阵
        bool m_dirty{true};           // 矩阵是否需要更新
    };
} 