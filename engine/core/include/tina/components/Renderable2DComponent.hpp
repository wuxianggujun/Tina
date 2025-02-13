#pragma once

#include <glm/glm.hpp>
#include "tina/renderer/Color.hpp"

namespace Tina {

class Renderable2DComponent {
public:
    virtual ~Renderable2DComponent() = default;

    // 基本属性
    void setVisible(bool visible) { m_Visible = visible; }
    bool isVisible() const { return m_Visible; }

    void setColor(const Color& color) { m_Color = color; }
    const Color& getColor() const { return m_Color; }

    void setLayer(int layer) { m_Layer = layer; }
    int getLayer() const { return m_Layer; }

    void setSize(const glm::vec2& size) { m_Size = size; }
    const glm::vec2& getSize() const { return m_Size; }

    // 渲染数据接口
    virtual void updateRenderData() = 0;
    virtual bool isTextured() const = 0;
    
    // 获取渲染数据
    virtual const void* getVertexData() const = 0;
    virtual uint32_t getVertexDataSize() const = 0;
    virtual const void* getInstanceData() const = 0;
    virtual uint32_t getInstanceDataSize() const = 0;

protected:
    bool m_Visible = true;
    Color m_Color = Color::White;
    int m_Layer = 0;
    glm::vec2 m_Size = {1.0f, 1.0f};
};

} // namespace Tina 