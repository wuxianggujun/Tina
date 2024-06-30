//
// Created by wuxianggujun on 2024/6/29.
//

#ifndef LAYER_HPP
#define LAYER_HPP

#include <cstdint>
#include <vector>

#include "generated/shaders/engine/all.h"
#include <bgfx/embedded_shader.h>
#include <bgfx/bgfx.h>

namespace Tina {
    struct ColorVertex {
        float xPos; // 顶点的x坐标
        float yPos; // 顶点的y坐标
        uint32_t color; // 顶点的颜色
    };

    class Layer {
    public:
       explicit  Layer(std::vector<ColorVertex> &vertices, std::vector<uint16_t> &indices); // 构造函数，初始化图层并设置顶点和索引

       explicit Layer(); // 默认构造函数

        virtual ~Layer() = default; // 析构函数

        void updateGeometry(std::vector<ColorVertex> &vertices); // 更新顶点数据

        void updateGeometry(std::vector<uint16_t> &indices); // 更新索引数据

        void updateGeometry(std::vector<ColorVertex> &vertices, std::vector<uint16_t> &indices); // 更新顶点和索引数据

        void draw(bgfx::ViewId viewId); // 绘制图层

    private:
        bgfx::DynamicVertexBufferHandle vertexBuffer; // 动态顶点缓冲区句柄
        bgfx::DynamicIndexBufferHandle indexBuffer; // 动态索引缓冲区句柄

        bgfx::ProgramHandle program; // 着色器程序句柄
        bgfx::VertexLayout colorVertexLayout; // 顶点布局

        std::vector<ColorVertex> vertices; // 顶点数据
        std::vector<uint16_t> indices; // 索引数据
    };
} // Tina

#endif //LAYER_HPP
