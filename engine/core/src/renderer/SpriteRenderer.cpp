#include "tina/renderer/SpriteRenderer.hpp"
#include "tina/core/Node.hpp"
#include "tina/core/Scene.hpp"
#include "tina/log/Log.hpp"
#include <bx/math.h>
#include <glm/gtc/matrix_transform.hpp>

#include "tina/core/Engine.hpp"
#include "tina/resource/ShaderResource.hpp"

namespace Tina {

struct PosTexcoordVertex {
    float x;
    float y;
    float u;
    float v;

    static void init() {
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
    }

    static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout PosTexcoordVertex::ms_layout;

SpriteRenderer::SpriteRenderer() {
    // 初始化顶点布局
    static bool layoutInitialized = false;
    if (!layoutInitialized) {
        PosTexcoordVertex::init();
        layoutInitialized = true;
    }

    // 加载着色器程序
    ResourceManager* resourceManager = Engine::getInstance()->getResourceManager();
    auto shader = resourceManager->loadSync<ShaderResource>("sprite", "resources/shaders/");
    if (!shader || !shader->isLoaded()) {
        TINA_ENGINE_ERROR("Failed to load sprite shader");
        return;
    }
    m_program = shader->getProgram();

    // 创建纹理采样器
    m_textureSampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    // 创建顶点和索引缓冲
    createBuffers();
}

SpriteRenderer::~SpriteRenderer() {
    // 销毁BGFX资源
    if (bgfx::isValid(m_vbh)) {
        bgfx::destroy(m_vbh);
    }
    if (bgfx::isValid(m_ibh)) {
        bgfx::destroy(m_ibh);
    }
    if (bgfx::isValid(m_textureSampler)) {
        bgfx::destroy(m_textureSampler);
    }
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
    }
}

void SpriteRenderer::setTexture(const RefPtr<TextureResource>& texture) {
    m_texture = texture;
    if (m_texture && m_texture->isLoaded()) {
        // 更新精灵大小为纹理大小
        m_size = glm::vec2(m_texture->getWidth(), m_texture->getHeight());
        updateVertexBuffer();
    }
}

void SpriteRenderer::render() {
    if (!m_texture || !m_texture->isLoaded() || !bgfx::isValid(m_program)) {
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

    // 设置变换矩阵
    float mtx[16];
    bx::mtxIdentity(mtx);
    bx::mtxTranslate(mtx, m_position.x, m_position.y, 0.0f);
    if (m_rotation != 0.0f) {
        float rot[16];
        bx::mtxRotateZ(rot, m_rotation);
        bx::mtxMul(mtx, mtx, rot);
    }

    // 提交渲染命令
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setTransform(mtx);
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

void SpriteRenderer::createBuffers() {
    // 获取窗口尺寸
    auto* window = Engine::getInstance()->getMainWindow();
    float windowWidth = static_cast<float>(window->getWidth());
    float windowHeight = static_cast<float>(window->getHeight());

    // 计算居中位置
    float posX = (windowWidth - m_size.x) * 0.5f;
    float posY = (windowHeight - m_size.y) * 0.5f;

    // 创建顶点数据（使用屏幕坐标系）
    PosTexcoordVertex vertices[4] = {
        {posX,          posY,           0.0f, 0.0f}, // 左上
        {posX + m_size.x, posY,         1.0f, 0.0f}, // 右上
        {posX + m_size.x, posY + m_size.y, 1.0f, 1.0f}, // 右下
        {posX,          posY + m_size.y, 0.0f, 1.0f}  // 左下
    };

    // 创建顶点缓冲
    m_vbh = bgfx::createDynamicVertexBuffer(
        bgfx::copy(vertices, sizeof(vertices)),
        PosTexcoordVertex::ms_layout
    );

    // 创建索引数据
    uint16_t indices[] = {
        0, 1, 2,  // 第一个三角形
        2, 3, 0   // 第二个三角形
    };

    // 创建索引缓冲
    m_ibh = bgfx::createIndexBuffer(
        bgfx::copy(indices, sizeof(indices))
    );
}

void SpriteRenderer::updateVertexBuffer() {
    if (!bgfx::isValid(m_vbh)) {
        return;
    }

    // 获取窗口尺寸
    auto* window = Engine::getInstance()->getMainWindow();
    float windowWidth = static_cast<float>(window->getWidth());
    float windowHeight = static_cast<float>(window->getHeight());

    // 计算居中位置
    float posX = (windowWidth - m_size.x) * 0.5f;
    float posY = (windowHeight - m_size.y) * 0.5f;

    // 更新顶点位置（使用屏幕坐标系）
    PosTexcoordVertex vertices[4] = {
        {posX,          posY,           0.0f, 0.0f}, // 左上
        {posX + m_size.x, posY,         1.0f, 0.0f}, // 右上
        {posX + m_size.x, posY + m_size.y, 1.0f, 1.0f}, // 右下
        {posX,          posY + m_size.y, 0.0f, 1.0f}  // 左下
    };

    // 更新顶点缓冲
    bgfx::update(m_vbh, 0, bgfx::copy(vertices, sizeof(vertices)));
}

} // namespace Tina