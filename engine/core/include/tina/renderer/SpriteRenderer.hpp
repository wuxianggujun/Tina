#pragma once

#include "tina/core/Component.hpp"
#include "tina/resource/TextureResource.hpp"
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <memory>

namespace Tina {

class TINA_CORE_API SpriteRenderer : public Component {
public:
    SpriteRenderer();
    ~SpriteRenderer() override;

    // 设置纹理
    void setTexture(const RefPtr<TextureResource>& texture);
    [[nodiscard]] RefPtr<TextureResource> getTexture() const { return m_texture; }

    // 设置变换属性
    void setPosition(const glm::vec2& position) { m_position = position; }
    void setSize(const glm::vec2& size) { m_size = size; }
    void setRotation(float rotation) { m_rotation = rotation; }
    void setColor(const glm::vec4& color) { m_color = color; }

    // 获取变换属性
    const glm::vec2& getPosition() const { return m_position; }
    const glm::vec2& getSize() const { return m_size; }
    float getRotation() const { return m_rotation; }
    const glm::vec4& getColor() const { return m_color; }

    // 组件生命周期
    void onAttach(Node* node) override;
    void onDetach(Node* node) override;
    void render() override;

private:
    void createBuffers();
    void updateVertexBuffer();

    RefPtr<TextureResource> m_texture;
    glm::vec2 m_position{0.0f, 0.0f};
    glm::vec2 m_size{100.0f, 100.0f};
    float m_rotation{0.0f};
    glm::vec4 m_color{1.0f, 1.0f, 1.0f, 1.0f};

    bgfx::DynamicVertexBufferHandle m_vbh{BGFX_INVALID_HANDLE};
    bgfx::IndexBufferHandle m_ibh{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle m_textureSampler{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle m_program{BGFX_INVALID_HANDLE};
};

} // namespace Tina