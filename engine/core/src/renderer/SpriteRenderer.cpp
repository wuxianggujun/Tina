#include "tina/renderer/SpriteRenderer.hpp"
#include "tina/core/Node.hpp"
#include "tina/core/Scene.hpp"
#include "tina/log/Log.hpp"
#include <bx/math.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "tina/core/Engine.hpp"
#include "tina/core/Transform.hpp"
#include "tina/resource/ShaderResource.hpp"
#include "tina/resource/ResourceManager.hpp"

namespace Tina
{
    struct PosTexcoordVertex
    {
        float x;
        float y;
        float u;
        float v;

        static void init()
        {
            ms_layout
                .begin()
                .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .end();
        }

        static bgfx::VertexLayout ms_layout;
    };

    bgfx::VertexLayout PosTexcoordVertex::ms_layout;

    SpriteRenderer::SpriteRenderer()
    {
        // 初始化顶点布局
        static bool layoutInitialized = false;
        if (!layoutInitialized)
        {
            PosTexcoordVertex::init();
            layoutInitialized = true;
        }

        // 加载着色器程序
        ResourceManager* resourceManager = Engine::getInstance()->getResourceManager();
        auto shader = resourceManager->loadSync<ShaderResource>("sprite", "resources/shaders/");
        if (!shader || !shader->isLoaded())
        {
            TINA_ENGINE_ERROR("Failed to load sprite shader");
            return;
        }
        m_program = shader->getProgram();

        // 创建纹理采样器
        m_textureSampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

        // 创建顶点和索引缓冲
        createBuffers();
    }

    SpriteRenderer::~SpriteRenderer()
    {
        // 销毁BGFX资源
        if (bgfx::isValid(m_vbh))
        {
            bgfx::destroy(m_vbh);
        }
        if (bgfx::isValid(m_ibh))
        {
            bgfx::destroy(m_ibh);
        }
        if (bgfx::isValid(m_textureSampler))
        {
            bgfx::destroy(m_textureSampler);
        }
        if (bgfx::isValid(m_program))
        {
            bgfx::destroy(m_program);
        }
    }

    void SpriteRenderer::setTexture(const RefPtr<TextureResource>& texture)
    {
        m_texture = texture;
        if (m_texture && m_texture->isLoaded())
        {
            // 更新精灵大小为纹理大小
            m_size = glm::vec2(m_texture->getWidth(), m_texture->getHeight());
            updateVertexBuffer();
        }
    }

    void SpriteRenderer::render()
    {
        if (!m_texture || !m_texture->isLoaded() || !bgfx::isValid(m_program))
        {
            return;
        }

        // 设置渲染状态
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_MSAA
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        bgfx::setState(state);

        // 设置纹理
        bgfx::setTexture(0, m_textureSampler, m_texture->getHandle());

        // 使用纹理的原始尺寸
        float finalWidth = m_size.x;
        float finalHeight = m_size.y;

        // 计算模型变换矩阵
        float modelMatrix[16];
        bx::mtxIdentity(modelMatrix);

        // 获取节点的Transform组件
        Node* node = getNode();
        if (!node)
        {
            TINA_ENGINE_ERROR("SpriteRenderer没有关联节点!");
            return;
        }

        Transform* transform = node->getComponent<Transform>();
        if (!transform)
        {
            TINA_ENGINE_ERROR("节点{}没有Transform组件!", node->getName());
        }
        else
        {
            TINA_ENGINE_INFO("使用Transform组件渲染!");
        }

        if (transform)
        {
            // 使用节点的Transform组件信息渲染
            glm::mat4 worldMatrix = transform->getWorldMatrix();

            // 应用精灵自身的尺寸
            glm::mat4 spriteScale = glm::scale(glm::mat4(1.0f), glm::vec3(finalWidth, finalHeight, 1.0f));
            worldMatrix = worldMatrix * spriteScale;

            // 从glm矩阵复制到BGFX矩阵
            const float* glmData = glm::value_ptr(worldMatrix);
            std::memcpy(modelMatrix, glmData, sizeof(float) * 16);
        }
        else
        {
            // 如果没有Transform组件，使用自身的属性（向后兼容）
            // 构建完整的变换矩阵：平移 * (旋转 * 缩放)
            float translation[16];
            float rotation[16];
            float scale[16];

            // 在左上角坐标系中进行变换
            bx::mtxTranslate(translation, m_position.x, m_position.y, 0.0f);
            bx::mtxRotateZ(rotation, bx::toRad(m_rotation));
            bx::mtxScale(scale, finalWidth, finalHeight, 1.0f);

            // 组合变换：最终变换 = 平移 * (旋转 * 缩放)
            float temp[16];
            bx::mtxMul(temp, rotation, scale);
            bx::mtxMul(modelMatrix, translation, temp);
        }
        // 获取相机
        Camera2D* camera = m_camera;
        if (!camera)
        {
            // 如果没有设置相机，则使用Engine的主相机
            camera = Engine::getInstance()->getMainCamera();
        }

        // 如果有相机，确保其矩阵是最新的
        if (camera)
        {
            camera->updateMatrices();
        }

        // 提交渲染命令
        bgfx::setVertexBuffer(0, m_vbh);
        bgfx::setIndexBuffer(m_ibh);
        bgfx::setTransform(modelMatrix);
        bgfx::submit(0, m_program);
    }

    void SpriteRenderer::onAttach(Node* node)
    {
        Component::onAttach(node);
        // 组件被添加到节点时的初始化
        updateVertexBuffer();
    }

    void SpriteRenderer::onDetach(Node* node)
    {
        Component::onDetach(node);
    }

    void SpriteRenderer::createBuffers()
    {
        // 初始化时使用一个单位大小的四边形，实际大小由变换矩阵控制
        // 在左上角坐标系中，Y轴向下为正
        PosTexcoordVertex vertices[4] = {
            {0.0f, 0.0f, 0.0f, 0.0f}, // 左上
            {1.0f, 0.0f, 1.0f, 0.0f}, // 右上
            {1.0f, 1.0f, 1.0f, 1.0f}, // 右下
            {0.0f, 1.0f, 0.0f, 1.0f} // 左下
        };

        // 创建顶点缓冲
        m_vbh = bgfx::createDynamicVertexBuffer(
            bgfx::copy(vertices, sizeof(vertices)),
            PosTexcoordVertex::ms_layout
        );

        // 创建索引数据 - 保持顺时针绕序
        uint16_t indices[] = {
            0, 1, 2, // 第一个三角形
            2, 3, 0 // 第二个三角形
        };

        // 创建索引缓冲
        m_ibh = bgfx::createIndexBuffer(
            bgfx::copy(indices, sizeof(indices))
        );
    }

    void SpriteRenderer::updateVertexBuffer()
    {
        if (!bgfx::isValid(m_vbh))
        {
            return;
        }

        // 使用单位大小的四边形，实际大小由变换矩阵控制
        PosTexcoordVertex vertices[4] = {
            {0.0f, 0.0f, 0.0f, 0.0f}, // 左上
            {1.0f, 0.0f, 1.0f, 0.0f}, // 右上
            {1.0f, 1.0f, 1.0f, 1.0f}, // 右下
            {0.0f, 1.0f, 0.0f, 1.0f} // 左下
        };

        // 更新顶点缓冲
        bgfx::update(m_vbh, 0, bgfx::copy(vertices, sizeof(vertices)));
    }
} // namespace Tina
