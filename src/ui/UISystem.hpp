//
// UI System using RmlUI
//

#pragma once

#include <RmlUi/Core.h>
#include <bgfx/bgfx.h>
#include "../renderer/ShaderManager.hpp"
#include "../core/Core.hpp"
#include "../core/Container.hpp"
#include "../os/OS.hpp"

namespace Tina::renderer { class ShaderManager; }

namespace Tina::UI {

// RmlUI System Interface for Tina Engine
class SystemInterface : public Rml::SystemInterface {
public:
    explicit SystemInterface();
    ~SystemInterface() override;

    // Time interface
    double GetElapsedTime() override;
    
    // Logging interface
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

private:
    double m_startTime;
};

// RmlUI Render Interface for bgfx integration
class RenderInterface : public Rml::RenderInterface {
public:
    explicit RenderInterface();
    ~RenderInterface() override;

    // Viewport and scissor - SetViewport is not part of RmlUI API
    void SetViewport(int width, int height);
    void SetScissorRegion(Rml::Rectanglei region) override;
    void EnableScissorRegion(bool enable) override;

    // Rendering primitives - updated to match RmlUI API
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    // Texture management
    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
                                  const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                      Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    // Transform matrix
    void SetTransform(const Rml::Matrix4f* transform) override;

private:
    struct TextureData {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;
    };

    struct GeometryData {
        bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
    };

    Tina::Container::HashMap<Rml::TextureHandle, TextureData> m_textures;
    Tina::Container::HashMap<Rml::CompiledGeometryHandle, GeometryData> m_geometries;
    Rml::TextureHandle m_nextTextureId = 1;
    Rml::CompiledGeometryHandle m_nextGeometryId = 1;
    
    bgfx::ProgramHandle m_program;
    bgfx::UniformHandle m_uniformTexture;
    bgfx::UniformHandle m_uniformTransform;
    bgfx::VertexLayout m_layout;
    Tina::renderer::ShaderManager* m_shaderManager = nullptr;
    
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
    bool m_scissorEnabled = false;
    Rml::Rectanglei m_scissorRegion;
};

// Main UI System class
class UISystem {
public:
    UISystem();
    ~UISystem();

    // Initialization and cleanup
    bool initialize(int viewportWidth, int viewportHeight);
    void shutdown();

    // Event handling
    void handleEvent(const Tina::os::Event& event);
    
    // Update and render
    void update(float deltaTime);
    void render();
    
    // Window management
    void setViewportSize(int width, int height);
    
    // Context access
    Rml::Context* getContext() const { return m_context; }
    
    // Load document
    Rml::ElementDocument* loadDocument(const Rml::String& path);

private:
    bool initializeRmlUI();
    void shutdownRmlUI();
    
    // Convert OS events to RmlUI events
    Rml::Input::KeyIdentifier convertKeyCode(Tina::os::KeyCode keyCode);
    int convertMouseButton(Tina::os::MouseButton button);

private:
    SystemInterface* m_systemInterface = nullptr;
    RenderInterface* m_renderInterface = nullptr;
    Rml::Context* m_context = nullptr;
    
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
    bool m_initialized = false;
};

} // namespace Tina::UI
