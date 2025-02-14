#pragma once

#include "tina/layer/Layer.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/renderer/ShaderManager.hpp"
#include "tina/log/Logger.hpp"
#include "tina/core/Engine.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include "bx/math.h"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/event/Event.hpp"
#include "tina/renderer/Texture2D.hpp"
#include "tina/components/SpriteComponent.hpp"
#include "tina/components/Transform2DComponent.hpp"
#include "tina/scene/Scene.hpp"
#include <unordered_map>
#include "tina/utils/BgfxUtils.hpp"
#include <vector>
#include <algorithm>

#include "bgfx/platform.h"

namespace Tina
{
    class Render2DLayer : public Layer
    {
    public:
        Render2DLayer();

        ~Render2DLayer() override;

        void onAttach() override;
        void onDetach() override;

        void onUpdate(float deltaTime) override;
        void onRender() override;
        void onEvent(Event& event) override;


        // 加载纹理
        std::shared_ptr<Texture2D> loadTexture(const std::string& name, const std::string& path);

        // 创建矩形
        void createRectangle(const glm::vec2& position, const glm::vec2& size, const Color& color);


        // 创建带纹理的矩形
        void createTexturedRectangle(const glm::vec2& position, const glm::vec2& size, 
                                   const std::string& textureName,
                                   const glm::vec4& textureCoords = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                                   const Color& tint = Color::White);

    private:
        void initShaders();

        void initRenderer();

        void initCamera();

        UniquePtr<OrthographicCamera> m_camera;
        bgfx::ProgramHandle m_shaderProgram = BGFX_INVALID_HANDLE;
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_textures;
        bool m_initialized = false;
        UniquePtr<BatchRenderer2D> m_batchRenderer;
        std::vector<BatchRenderer2D::InstanceData> m_rectangles;
        
        // 存储带纹理的矩形数据
        struct TexturedRectangle {
            BatchRenderer2D::InstanceData instance;
            std::shared_ptr<Texture2D> texture;
        };
        std::vector<TexturedRectangle> m_texturedRectangles;
    };
} // namespace Tina
