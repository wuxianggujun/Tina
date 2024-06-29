//
// Created by wuxianggujun on 2024/6/29.
//

#include "Layer.hpp"

namespace Tina {
    const bgfx::EmbeddedShader shaders[3] = {
        BGFX_EMBEDDED_SHADER(vs_basic),
        BGFX_EMBEDDED_SHADER(fs_basic),
        BGFX_EMBEDDED_SHADER_END()
    };

    // 构造函数，初始化Layer对象并创建顶点和索引缓冲区
    Layer::Layer(std::vector<ColorVertex> &vertices, std::vector<uint16_t> &indices) {
        this->vertices = vertices;

        colorVertexLayout.begin()
                .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
                .end();

        vertexBuffer = bgfx::createDynamicVertexBuffer(
            bgfx::makeRef(this->vertices.data(), sizeof(ColorVertex) * this->vertices.size()), colorVertexLayout,
            BGFX_BUFFER_ALLOW_RESIZE);

        indexBuffer = bgfx::createDynamicIndexBuffer(bgfx::makeRef(indices.data(), sizeof(uint16_t) * indices.size()),
                                                     BGFX_BUFFER_ALLOW_RESIZE);
        bgfx::RendererType::Enum rendererType = bgfx::getRendererType();

        program = bgfx::createProgram(
            bgfx::createEmbeddedShader(shaders, rendererType, "vs_basic"),
            bgfx::createEmbeddedShader(shaders, rendererType, "fs_basic"),
            true);
    }

    // 默认构造函数，初始化Layer对象并创建顶点和索引缓冲区
    Layer::Layer() {
        colorVertexLayout.begin()
                .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
                .end();

        vertexBuffer = bgfx::createDynamicVertexBuffer(1, colorVertexLayout, BGFX_BUFFER_ALLOW_RESIZE);
        indexBuffer = bgfx::createDynamicIndexBuffer(1, BGFX_BUFFER_ALLOW_RESIZE);

        bgfx::RendererType::Enum renderer_type = bgfx::getRendererType();
        program = bgfx::createProgram(
            bgfx::createEmbeddedShader(shaders, renderer_type, "vs_basic"),
            bgfx::createEmbeddedShader(shaders, renderer_type, "fs_basic"),
            true
        );
    }

    // 更新顶点数据
    void Layer::updateGeometry(std::vector<ColorVertex> &vertices) {
        this->vertices.clear();
        this->vertices = vertices;
        bgfx::update(vertexBuffer, 0,
                     bgfx::makeRef(this->vertices.data(), sizeof(ColorVertex) * this->vertices.size()));
    }

    // 更新索引数据
    void Layer::updateGeometry(std::vector<uint16_t> &indices) {
        this->indices.clear();
        this->indices = indices;
        bgfx::update(indexBuffer, 0, bgfx::makeRef(this->indices.data(), sizeof(uint16_t) * this->indices.size()));
    }

    // 同时更新顶点和索引数据
    void Layer::updateGeometry(std::vector<ColorVertex> &vertices, std::vector<uint16_t> &indices) {
        updateGeometry(vertices);
        updateGeometry(indices);
    }

    // 绘制图层
    void Layer::draw(bgfx::ViewId viewId) {
        bgfx::setVertexBuffer(0, vertexBuffer);
        bgfx::setIndexBuffer(indexBuffer);
        bgfx::submit(viewId, program);
    }
} // Tina
