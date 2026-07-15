#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Tina::Renderer {

class ShaderManager;

// Small bgfx backend primitive for validating the 3D runtime path.
// Shader programs stay owned by ShaderManager; this object owns its GPU buffers.
class SimpleMeshRenderer final {
public:
    SimpleMeshRenderer() = default;
    ~SimpleMeshRenderer();

    SimpleMeshRenderer(const SimpleMeshRenderer&) = delete;
    SimpleMeshRenderer& operator=(const SimpleMeshRenderer&) = delete;

    bool initialize(ShaderManager& shaders);
    void shutdown();
    void drawCube(uint16_t viewId, float rotationRadians) const;

    [[nodiscard]] bool isInitialized() const noexcept;

private:
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_vertexBuffer = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_indexBuffer = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout{};
};

} // namespace Tina::Renderer
