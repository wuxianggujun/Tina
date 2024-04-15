#ifndef TINA_LAYER_HPP
#define TINA_LAYER_HPP

#include <bgfx/embedded_shader.h>
#include <bgfx/bgfx.h>
#include <vector>
#include "GlfwWindow.hpp"
#include "generated/shaders/Tina/all.h"

namespace Tina {

    struct ColorVertex
    {
        float xPos;
        float yPos;
        uint32_t color;
    };

    class Layer {
    public:
        Layer(GlfwWindow window, std::vector<ColorVertex>& vertices, std::vector<uint16_t>& indices);
        void setScaleFactor(double xScale, double yScale);
        void updateGeometry(std::vector<ColorVertex>& vertices);
        void updateGeometry(std::vector<uint16_t>& indices);
        void updateGeometry(std::vector<ColorVertex>& vertices,std::vector<uint16_t> &indices);
        void draw(bgfx::ViewId view);

        std::vector<ColorVertex> vertices;
        std::vector<uint16_t> indices;

        double xScale = 1;
        double yScale = 1;

    private:

        void scaleVertices(double xScale, double yScale);

        bgfx::DynamicVertexBufferHandle vertexBuffer;
        bgfx::DynamicIndexBufferHandle indexBuffer;

        bgfx::ProgramHandle program;
        bgfx::VertexLayout colorVertexLayout;
    };

}


#endif
