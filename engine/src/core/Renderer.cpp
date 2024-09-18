#include "Renderer.hpp"
#include <fstream>

#include "bgfx/platform.h"
#include "bx/math.h"
#include "stb/stb_image.h"

namespace Tina {
    static Renderer::PosColorVertex s_cubeVertices[] =
    {
        {-1.0f, 1.0f, 1.0f, 0xff000000},
        {1.0f, 1.0f, 1.0f, 0xff0000ff},
        {-1.0f, -1.0f, 1.0f, 0xff00ff00},
        {1.0f, -1.0f, 1.0f, 0xff00ffff}
    };

    static const uint16_t s_cubeTriList[] =
    {
        0, 1, 2, // 0
        1, 3, 2
    };


    Renderer::Renderer(Vector2i size, int viewId): _resolution(size), _viewId(viewId) {
        // bgfx::setDebug(BGFX_DEBUG_TEXT);
        // bgfx::setDebug(BGFX_DEBUG_WIREFRAME);
        // 在某些渲染逻辑之前设置标记，便于调试
        // bgfx::setDebug(BGFX_DEBUG_IFH);

        s_texColor = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
        m_texture = loadTexture("../resources/textures/player.png");

        m_vertexBuffer.getLayout().begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                // .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
                .end();


        m_vertexBuffer.init(s_cubeVertices, sizeof(s_cubeVertices));

        m_indexBuffer.init(s_cubeTriList, sizeof(s_cubeTriList));


        m_shader.loadFromFile("cubes");

        bgfx::setViewClear(_viewId,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(_viewId, 0, 0, bgfx::BackbufferRatio::Equal);
    }

    void Renderer::render() {
        constexpr bx::Vec3 at = {0.0f, 0.0f, 0.0f};
        constexpr bx::Vec3 eye = {0.0f, 0.0f, -35.0f};

        // Set view and projection matrix for view 0.
        float view[16];
        bx::mtxLookAt(view, eye, at);

        float proj[16];
        bx::mtxProj(proj,
                    60.0f,
                    float(_resolution.width) / float(_resolution.height),
                    0.1f, 100.0f,
                    bgfx::getCaps()->homogeneousDepth);

        bgfx::setViewTransform(_viewId, view, proj);

        // Set view 0 default viewport.
        bgfx::setViewRect(0, 0, 0,
                          _resolution.width,
                          _resolution.height);

        bgfx::touch(_viewId);

        float mtx[16];
        bx::mtxRotateXY(mtx, 0.21f, 0.37f);
        // position x,y,z
        mtx[12] = 0.0f;
        mtx[13] = 0.0f;
        mtx[14] = 0.0f;

        // Set model matrix for rendering.
        bgfx::setTransform(mtx);

        m_vertexBuffer.enable();
        m_indexBuffer.enable();

        bgfx::setTexture(0, s_texColor, m_texture);

        // Set render states.
        bgfx::setState(BGFX_STATE_DEFAULT);

        // Submit primitive for rendering to view 0.
        bgfx::submit(_viewId, m_shader.getProgram());
        bgfx::frame();
    }

    void Renderer::shutdown() {
        m_indexBuffer.free();
        m_vertexBuffer.free();
    }

    bgfx::TextureHandle Renderer::loadTexture(const char *filepath) {
        // Opening the file in binary mode
        std::ifstream file(filepath, std::ios::binary);

        if (!file.is_open()) {
            std::cerr << "Failed to open file at filepath: " << filepath << std::endl;
        }

        // Getting the filesize
        file.seekg(0, std::ios::end);
        std::streampos size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Storing the data into data
        char *data = new char[size];
        file.read(data, size);

        // Closing the file
        file.close();

        bx::DefaultAllocator allocator;
        bimg::ImageContainer *img_container = bimg::imageParse(&allocator, data, size);

        if (img_container == nullptr)
            return BGFX_INVALID_HANDLE;

        const bgfx::Memory *mem = bgfx::makeRef(img_container->m_data, img_container->m_size, 0, img_container);
        delete[] data;

        bgfx::TextureHandle handle = bgfx::createTexture2D(static_cast<uint16_t>(img_container->m_width),
                                                           static_cast<uint16_t>(img_container->m_height),
                                                           1 < img_container->m_numMips, img_container->m_numLayers,
                                                           static_cast<bgfx::TextureFormat::Enum>(img_container->
                                                               m_format),
                                                           BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR, mem
        );

        std::cout << "Image width: " << static_cast<uint16_t>(img_container->m_width) <<
                ", Image height: " << static_cast<uint16_t>(img_container->m_height) << std::endl;

        return handle;
    }
}
