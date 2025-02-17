#pragma once

#include "tina/core/Engine.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/view/View.hpp"
#include "tina/renderer/Texture2D.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Tina {

    // 定义UI元素的基本颜色结构
    struct UIColor {
        float r, g, b, a;
        
        UIColor(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
            : r(r), g(g), b(b), a(a) {}
    };

    // 定义线条样式
    struct LineStyle {
        float thickness = 1.0f;
        UIColor color;
    };

    // 定义矩形样式
    struct RectStyle {
        UIColor fillColor;
        UIColor borderColor;
        float borderThickness = 0.0f;
        bool filled = true;
    };

    class UIView : public View {
    public:
        UIView();
        ~UIView() override = default;

        // 基础绘制方法
        void drawLine(const glm::vec2& start, const glm::vec2& end, const LineStyle& style = LineStyle());
        void drawRect(const glm::vec2& position, const glm::vec2& size, const RectStyle& style = RectStyle());
        void drawCircle(const glm::vec2& center, float radius, const RectStyle& style = RectStyle());
        void drawTexture(const std::shared_ptr<Texture2D>& texture, const glm::vec2& position, const glm::vec2& size, const UIColor& tint = UIColor());

        // View接口实现
        void onAttach() override;
        void onDetach() override;
        void onUpdate(float deltaTime) override;
        void beginRender() override;
        void endRender() override;
        void render(Scene* scene) override;
        bool onEvent(Event& event) override;

        // 设置背景颜色
        void setBackgroundColor(const UIColor& color);

    private:
        void initShaders();
        void initRenderer();
        void initCamera();

        UniquePtr<Renderer2D> m_renderer;
        SharedPtr<OrthographicCamera> m_camera;
        bgfx::ProgramHandle m_shaderProgram = BGFX_INVALID_HANDLE;
        bool m_initialized = false;

        // UI状态
        UIColor m_backgroundColor{0.18f, 0.18f, 0.18f, 1.0f}; // 默认深灰色背景
        std::vector<std::function<void()>> m_drawCommands; // 存储绘制命令
    };

} // namespace Tina 