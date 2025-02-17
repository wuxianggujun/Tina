#pragma once

#include "tina/core/Engine.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/view/View.hpp"
#include "tina/resources/ResourceManager.hpp"
#include "tina/resources/TextureLoader.hpp"
#include "tina/renderer/Texture2D.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include "tina/renderer/GameViewRenderer.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Tina {
    class Scene;

    class GameView : public View {
    public:
        GameView();
        ~GameView() override = default;

        // 加载纹理
        std::shared_ptr<Texture2D> loadTexture(const std::string& name, const std::string& path);

        void onAttach() override;
        void onDetach() override;
        void onUpdate(float deltaTime) override;
        void beginRender() override;
        void endRender() override;
        void render(Scene* scene) override;
        bool onEvent(Event& event) override;

    private:
        void initShaders();
        void initRenderer();
        void initCamera();

        UniquePtr<Renderer2D> m_renderer;
        SharedPtr<OrthographicCamera> m_camera;
        bgfx::ProgramHandle m_shaderProgram = BGFX_INVALID_HANDLE;
        bool m_initialized = false;
        
        // 组件渲染器
        GameViewRenderer m_viewRenderer;
    };

} // namespace Tina
