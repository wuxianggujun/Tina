#include "Layer.hpp"

namespace Tina {

    const bgfx::EmbeddedShader shaders[3] = {
        BGFX_EMBEDDED_SHADER(vs_basic),
        BGFX_EMBEDDED_SHADER(fs_basic),
        BGFX_EMBEDDED_SHADER_END()
    };

    Layer::Layer(GlfwWindow window, std::vector<ColorVertex>& vertices, std::vector<uint16_t>& indices) {
        this->vertices = vertices;

        colorVertexLayout.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();

        vertexBuffer = bgfx::createDynamicVertexBuffer(bgfx::makeRef(this->vertices.data(), sizeof(ColorVertex) * this->vertices.size()),colorVertexLayout);
        indexBuffer = bgfx::createDynamicIndexBuffer(bgfx::makeRef(indices.data(),sizeof(uint16_t) * indices.size()));

        bgfx::RendererType::Enum renderer_type = bgfx::getRendererType();
        program = bgfx::createProgram(
            bgfx::createEmbeddedShader(shaders,renderer_type,"vs_basic"),
            bgfx::createEmbeddedShader(shaders,renderer_type,"fs_basic"));
    }

    void Layer::updateGeometry(std::vector<ColorVertex>& vertices) {
        this->vertices.clear();
        this->vertices = vertices;
        scaleVertices(xScale, yScale);
        bgfx::update(vertexBuffer, 0, bgfx::makeRef(this->vertices.data(), sizeof(ColorVertex) * this->vertices.size()));

    }

    void Layer::updateGeometry(std::vector<uint16_t>& indices) {
        this->indices.clear();
        this->indices = indices;
        bgfx::update(indexBuffer, 0, bgfx::makeRef(this->indices.data(), sizeof(uint16_t) * this->indices.size()));

    }

    void Layer::updateGeometry(std::vector<ColorVertex>& vertices, std::vector<uint16_t>& indices) {
        updateGeometry(vertices);
        updateGeometry(indices);
    }

    void Layer::setScaleFactor(double xScale, double yScale) {
        double oldXScale = this->xScale;
        double oldYScale = this->yScale;

        this->xScale = xScale;
        this->yScale = yScale;

        double xScaleAmount = this->xScale / oldXScale;
        double yScaleAmount = this->yScale / oldYScale;

        scaleVertices(xScaleAmount, yScaleAmount);

        bgfx::update(vertexBuffer, 0, bgfx::makeRef(this->vertices.data(),sizeof(ColorVertex) * this->vertices.size()));
    }


    void Layer::scaleVertices(double xScale, double yScale) {
        for (int i = 0;i<this->vertices.size();i++)
        {
            this->vertices.at(i).xPos *= xScale;
            this->vertices.at(i).yPos *= yScale;
        }
    }

    void Layer::draw(bgfx::ViewId view) {
        bgfx::setVertexBuffer(0, vertexBuffer);
        bgfx::setIndexBuffer(indexBuffer);
        bgfx::submit(view, program);
    }

}
