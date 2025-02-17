#pragma once

#include "tina/view/View.hpp"
#include "tina/ui/UITypes.hpp"
#include "tina/math/Rect.hpp"
#include "tina/renderer/Color.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/renderer/Texture2D.hpp"
#include "tina/renderer/RenderCommand.hpp"
#include "tina/core/Core.hpp"
#include "tina/scene/Scene.hpp"
#include "tina/event/Event.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include <bgfx/bgfx.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <functional>
#include <memory>

namespace Tina {

class TINA_CORE_API UIView : public View {
public:
    explicit UIView(const std::string& name = "UIView");
    ~UIView() override;

    // 绘制相关方法
    void drawLine(const glm::vec2& start, const glm::vec2& end, const LineStyle& style = {});
    void drawRect(const Rect& rect, const RectStyle& style = {});
    void drawCircle(const glm::vec2& center, float radius, const RectStyle& style = {});
    void drawEllipse(const Rect& bounds, const RectStyle& style = {});
    void drawRoundedRect(const Rect& rect, float radius, const RectStyle& style = {});
    void drawTexture(const Rect& rect, const std::shared_ptr<Texture2D>& texture, const Color& tint = Color::White);

    // 设置背景颜色
    void setBackgroundColor(const UIColor& color);
    [[nodiscard]] const UIColor& getBackgroundColor() const { return m_backgroundColor; }

    // 获取视图边界
    [[nodiscard]] Rect getBounds() const { return bounds; }
    void setBounds(const Rect& rect) { 
        bounds = rect;
        setPosition(rect.getPosition());
        setSize(rect.getSize());
    }

    // 设置位置和大小
    void setPosition(const glm::vec2& pos) { position = pos; }
    void setSize(const glm::vec2& sz) { size = sz; }
    [[nodiscard]] const glm::vec2& getPosition() const { return position; }
    [[nodiscard]] const glm::vec2& getSize() const { return size; }

    // View接口实现
    void onAttach() override;
    void onDetach() override;
    void onUpdate(float deltaTime) override;
    void beginRender() override;
    void endRender() override;
    void render(Scene* scene) override;
    bool onEvent(Event& event) override;

protected:
    // 绘制背景
    virtual void drawBackground();

    // 事件处理
    bool handleMouseMove(Event& event) override;
    bool handleMouseButton(Event& event) override;
    bool handleWindowResize(Event& event) override;

private:
    void initShaders();
    void initRenderer();
    void initCamera();

    UIColor m_backgroundColor{0.0f, 0.0f, 0.0f, 0.0f}; // 背景颜色
    std::unique_ptr<BatchRenderer2D> m_renderer;
    SharedPtr<OrthographicCamera> m_camera;
    std::vector<std::function<void()>> m_drawCommands;
    bgfx::ProgramHandle m_shaderProgram{BGFX_INVALID_HANDLE};
};

} // namespace Tina 