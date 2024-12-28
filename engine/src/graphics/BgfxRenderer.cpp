#include "BgfxRenderer.hpp"

#include "resource/ShaderResource.hpp"
#include "resource/TextureResource.hpp"

namespace Tina {
    BgfxRenderer::BgfxRenderer(IWindow *window):m_window(window),m_resourceManager(createScopePtr<ResourceManager>()) {
    }

    BgfxRenderer::~BgfxRenderer() {
        shutdown();
    }

    void BgfxRenderer::init(Vector2i resolution) {
        m_resolution = resolution;

        m_vbh.getLayout().begin()
             .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
             .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Int16, true, true)
             .end();

        m_vbh.init(s_cubeVertices, sizeof(s_cubeVertices));
        m_ibh.init(s_cubeIndices, sizeof(s_cubeIndices));

        bgfx::setViewClear(0,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, m_resolution.width, m_resolution.height);
    }

    void BgfxRenderer::shutdown() {
        m_vbh.free();
        m_ibh.free();
        
        // 强制 BGFX 等待当前帧渲染完成
        bgfx::frame(true);
        
        m_resourceManager->unloadAllResources();
        bgfx::shutdown();
    }


    void BgfxRenderer::render() {
        const bx::Vec3 at  = { 0.0f, 0.0f,   0.0f };
        const bx::Vec3 eye = { 5.0f, 5.0f, -5.0f };

        // Set view and projection matrix for view 0.
        {
            float view[16];
            bx::mtxLookAt(view, eye, at);

            float proj[16];
            bx::mtxProj(proj, 60.0f, static_cast<float>(m_resolution.width)/static_cast<float>(m_resolution.height), 0.1f, 100.0f, bgfx::getCaps()->homogeneousDepth);
            bgfx::setViewTransform(0, view, proj);

            // Set view 0 default viewport.
            bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(m_resolution.width), static_cast<uint16_t>(m_resolution.height) );
        }

        bgfx::touch(0);

        // Set vertex and index buffer.
        m_vbh.enable();
        m_ibh.enable();

        // Bind textures.
        // bgfx::setTexture(0, s_texColor, m_textureColor);

        // Set render states.
        bgfx::setState(BGFX_STATE_WRITE_RGB
                       | BGFX_STATE_WRITE_A
                       | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS
                       | BGFX_STATE_MSAA
        );

        auto shaderHandle = m_resourceManager->loadResource<ShaderResource>("bump");
        auto textureHandle = m_resourceManager->loadResource<TextureResource>("../resources/textures/grassland.png");
        auto s_texColorHandle = createRefPtr<ShaderUniform>();
        s_texColorHandle->init("s_texColor", bgfx::UniformType::Sampler);
        setTexture(0, *s_texColorHandle, textureHandle->getHandle());
        // Submit primitive for rendering to view 0.
        submit(0, shaderHandle->getHandle());
    }

    void BgfxRenderer::frame() {
        bgfx::frame();
    }

    Vector2i BgfxRenderer::getResolution() const {
        return m_resolution;
    }
    
    void BgfxRenderer::setTexture(uint8_t stage, const ShaderUniform &uniform, const ResourceHandle &textureHandle) {
        auto textureResource = m_resourceManager->getResource<TextureResource>(textureHandle);
        if (textureResource && textureResource->isLoaded()) {
            bgfx::setTexture(stage, uniform.getHandle(), textureResource->getTexture().getTextureHandle());
        }
    }

    void BgfxRenderer::submit(uint8_t viewId, const ResourceHandle &shaderHandle) {
        auto shaderResource = m_resourceManager->getResource<ShaderResource>(shaderHandle);
        if (shaderResource && shaderResource->isLoaded()) {
            bgfx::submit(viewId, shaderResource->getShader().getProgram());
        }
    }
    
}
