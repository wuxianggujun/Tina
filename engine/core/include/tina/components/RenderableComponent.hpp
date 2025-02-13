#pragma once

#include <bgfx/bgfx.h>
#include "tina/renderer/Color.hpp"
#include <memory>

namespace Tina {

class RenderableComponent {
public:
    virtual ~RenderableComponent() = default;

    // 设置颜色
    void setColor(const Color& color) { m_Color = color; }
    const Color& getColor() const { return m_Color; }

    // 设置是否可见
    void setVisible(bool visible) { m_Visible = visible; }
    bool isVisible() const { return m_Visible; }

    // 设置渲染层级
    void setLayer(int layer) { m_Layer = layer; }
    int getLayer() const { return m_Layer; }

    // 获取顶点数量
    virtual uint32_t getVertexCount() const = 0;
    
    // 获取索引数量
    virtual uint32_t getIndexCount() const = 0;

    // 获取顶点布局
    virtual const bgfx::VertexLayout& getVertexLayout() const = 0;

    // 更新顶点数据
    virtual void updateVertexBuffer(void* buffer, uint32_t size) = 0;

    // 更新索引数据
    virtual void updateIndexBuffer(void* buffer, uint32_t size) = 0;

protected:
    Color m_Color = Color::White;
    bool m_Visible = true;
    int m_Layer = 0;
};

} // namespace Tina 