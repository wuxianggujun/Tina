#pragma once
#include <entt/entt.hpp>
#include <bgfx/bgfx.h>
#include "GuiComponent.hpp"
#include <vector>

namespace Tina {

/*
struct PosColorVertex {
    float x;
    float y;
    float z;
    uint32_t abgr;

    static void init() {
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    }

    static bgfx::VertexLayout ms_layout;
};
*/

class GuiSystem {
public:
    GuiSystem() = default;
    ~GuiSystem() = default;

    void Initialize(uint16_t width, uint16_t height);
    void Shutdown();
    
    void Update(entt::registry& registry, float deltaTime);
    void Render(entt::registry& registry);
    
    void Resize(uint16_t width, uint16_t height);

private:
    bgfx::ProgramHandle m_program{BGFX_INVALID_HANDLE};
    bgfx::DynamicVertexBufferHandle m_vbh{BGFX_INVALID_HANDLE};
    bgfx::DynamicIndexBufferHandle m_ibh{BGFX_INVALID_HANDLE};
    
    uint16_t m_width{0};
    uint16_t m_height{0};
    
    void InitializeShaders();
    void CreateBuffers();
    
    // std::vector<PosColorVertex> m_vertices;
    std::vector<uint16_t> m_indices;
};

}  // namespace Tina