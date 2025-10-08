//
// PauseScene 实现
//

#include "PauseScene.hpp"
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"
#include "../core/Log.hpp"
#include "../ui/UIComponents.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace Tina::Game {

PauseScene::PauseScene() = default;
PauseScene::~PauseScene() = default;

void PauseScene::onEnter()
{
    TINA_INFO("PauseScene::onEnter - 进入暂停菜单");

    // 获取窗口尺寸
    app()->getPixelSize(m_pixelWidth, m_pixelHeight);

    // 初始化渲染资源（使用全局 ShaderManager）
    m_progColor = app()->shaders().loadProgram("color", "color");

    // 文本渲染器
    m_textRenderer = Memory::MakeUnique<UI::TextRenderer>();
    if (!m_textRenderer->initialize(app()->shaders(), app()->resources())) {
        TINA_ERROR("TextRenderer 初始化失败");
    } else {
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 32);
    }

    // UI 渲染器
    m_uiRenderer = Memory::MakeUnique<UI::UIRenderer>();
    m_uiRenderer->initialize(app()->shaders(), m_textRenderer.get());

    // 初始化顶点布局
    m_colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    // 创建 UI
    createUI();
    // 绑定事件系统根节点，统一处理 hover/click
    m_events.setRoot(m_rootNode.get());

    TINA_INFO("PauseScene: 初始化完成");
}

void PauseScene::onExit()
{
    TINA_INFO("PauseScene::onExit - 退出暂停菜单");

    // 断开 Signal 连接
    m_continueConnection.disconnect();
    m_quitConnection.disconnect();

    // 清理 UI（必须在渲染资源之前清理）
    m_rootNode.reset();
    m_btnContinue = nullptr;
    m_btnQuit = nullptr;

    // 清理渲染资源（按逆序）
    m_uiRenderer.reset();
    m_textRenderer.reset();
    
    // 不手动销毁 m_progColor，让全局 ShaderManager 自动管理（避免双重释放）
    m_progColor = BGFX_INVALID_HANDLE;
}

void PauseScene::update(float dt)
{
    if (m_rootNode) {
        m_rootNode->update(dt);
    }
}

void PauseScene::render()
{
    // 1. 确保 view 3 被清理（不清屏，只是触摸）
    bgfx::touch(3);

    // 2. 渲染半透明遮罩
    renderOverlay();

    // 3. 渲染 UI（按钮和文本）
    if (m_rootNode && m_uiRenderer) {
        m_rootNode->render(3, *m_uiRenderer);
    }
}

void PauseScene::handleEvent(const Tina::os::Event& event)
{
    using E = Tina::os::Event;

    // ESC 键：返回游戏
    if (event.type == E::Type::KEY && event.key.down) {
        if (event.key.key_code == os::KeyCode::ESCAPE) {
            onContinueClicked();
            return;
        }
    }

    // 鼠标移动：更新按钮悬停状态
    // 使用 UIEventSystem 统一处理 hover/click
    if (event.type == E::Type::MOUSE_MOVE || event.type == E::Type::MOUSE_BUTTON) {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        bool leftDown = false;
        if (event.type == E::Type::MOUSE_BUTTON) {
            leftDown = (event.mouse_button.button == os::MouseButton::LEFT) && event.mouse_button.down;
        } else {
            uint32_t mask = (uint32_t)SDL_GetMouseState(nullptr, nullptr);
#ifdef SDL_BUTTON_MASK
            leftDown = (mask & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
#else
            leftDown = (mask & SDL_BUTTON_LMASK) != 0;
#endif
        }
        m_events.updateMouse(mx, my, leftDown);
        m_events.processEvents();
    }

    #if 0
    if (event.type == E::Type::MOUSE_MOVE) {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        
        if (m_btnContinue) {
            bool hovered = m_btnContinue->containsPoint(mx, my);
            if (hovered && !m_btnContinue->isHovered()) {
                m_btnContinue->setHovered(true);
                m_btnContinue->onMouseEnter();
            } else if (!hovered && m_btnContinue->isHovered()) {
                m_btnContinue->setHovered(false);
                m_btnContinue->onMouseLeave();
            }
        }
        
        if (m_btnQuit) {
            bool hovered = m_btnQuit->containsPoint(mx, my);
            if (hovered && !m_btnQuit->isHovered()) {
                m_btnQuit->setHovered(true);
                m_btnQuit->onMouseEnter();
            } else if (!hovered && m_btnQuit->isHovered()) {
                m_btnQuit->setHovered(false);
                m_btnQuit->onMouseLeave();
            }
        }
    }

    // 鼠标点击：处理按钮
    if (event.type == E::Type::MOUSE_BUTTON && event.mouse_button.down) {
        if (event.mouse_button.button == os::MouseButton::LEFT) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            
            TINA_INFO("PauseScene: 鼠标点击位置 ({}, {})", mx, my);
            
            // 检测按钮点击
            if (m_btnContinue && m_btnContinue->containsPoint(mx, my)) {
                TINA_INFO("PauseScene: 点击了继续按钮");
                m_btnContinue->onClick.emit();
            } else if (m_btnQuit && m_btnQuit->containsPoint(mx, my)) {
                TINA_INFO("PauseScene: 点击了退出按钮");
                m_btnQuit->onClick.emit();
            } else {
                TINA_INFO("PauseScene: 点击了空白区域");
            }
        }
    }
    #endif

    // 窗口调整大小
    if (event.type == E::Type::WINDOW_SIZE) {
        m_pixelWidth = event.win_size.w;
        m_pixelHeight = event.win_size.h;
        
        // 重新布局 UI
        if (m_rootNode) {
            createUI();  // 简单重建
        }
    }
}

void PauseScene::createUI()
{
    // 若重复创建（例如窗口尺寸变化），先断开旧连接并释放旧 UI
    m_continueConnection.disconnect();
    m_quitConnection.disconnect();
    m_rootNode.reset();
    m_btnContinue = nullptr;
    m_btnQuit = nullptr;

    // 创建根节点
    m_rootNode = Memory::MakeUnique<UI::UINode>();
    m_rootNode->setPosition(0, 0);
    m_rootNode->setSize((float)m_pixelWidth, (float)m_pixelHeight);

    // 标题文本："游戏已暂停"
    auto* title = new UI::UILabel();
    title->setText("游戏已暂停");
    title->setColor(1.0f, 1.0f, 1.0f, 1.0f);
    // 使标题文本在其矩形内水平/垂直居中
    title->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    title->setPosition(
        (float)m_pixelWidth / 2 - 200,
        (float)m_pixelHeight / 2 - 150
    );
    title->setSize(400, 60);
    m_rootNode->addChild(title);

    // 按钮样式参数
    const float btnWidth = 300.0f;
    const float btnHeight = 60.0f;
    const float btnSpacing = 20.0f;
    const float btnStartY = (float)m_pixelHeight / 2 - 30;

    // 继续游戏按钮
    m_btnContinue = new UI::UIButton();
    m_btnContinue->setText("继续游戏 (ESC)");
    m_btnContinue->setPosition(
        (float)m_pixelWidth / 2 - btnWidth / 2,
        btnStartY
    );
    m_btnContinue->setSize(btnWidth, btnHeight);
    m_btnContinue->setNormalColor(0.2f, 0.6f, 0.2f, 0.9f);   // 绿色
    m_btnContinue->setHoverColor(0.3f, 0.8f, 0.3f, 1.0f);
    m_btnContinue->setPressedColor(0.1f, 0.4f, 0.1f, 1.0f);
    m_continueConnection = m_btnContinue->onClick.connect([this]() { onContinueClicked(); });
    m_rootNode->addChild(m_btnContinue);

    // 退出按钮
    m_btnQuit = new UI::UIButton();
    m_btnQuit->setText("退出游戏");
    m_btnQuit->setPosition(
        (float)m_pixelWidth / 2 - btnWidth / 2,
        btnStartY + btnHeight + btnSpacing
    );
    m_btnQuit->setSize(btnWidth, btnHeight);
    m_btnQuit->setNormalColor(0.6f, 0.2f, 0.2f, 0.9f);   // 红色
    m_btnQuit->setHoverColor(0.8f, 0.3f, 0.3f, 1.0f);
    m_btnQuit->setPressedColor(0.4f, 0.1f, 0.1f, 1.0f);
    m_quitConnection = m_btnQuit->onClick.connect([this]() { onQuitClicked(); });
    m_rootNode->addChild(m_btnQuit);

    // 更新事件系统根节点，保证重建后事件命中正确
    m_events.setRoot(m_rootNode.get());

    TINA_INFO("PauseScene: UI 创建完成");
}

void PauseScene::renderOverlay()
{
    // 渲染半透明黑色遮罩（覆盖整个屏幕）
    // 使用 view 3（UI 层）

    bgfx::setViewRect(3, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);
    
    // 设置正交投影矩阵
    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(ortho,
                 0.0f, (float)m_pixelWidth,
                 (float)m_pixelHeight, 0.0f,
                 -1.0f, 1.0f,
                 0.0f,
                 caps ? caps->homogeneousDepth : false);
    bgfx::setViewTransform(3, nullptr, ortho);

    // 定义全屏四边形顶点（两个三角形）
    struct PosColorVertex {
        float x, y, z;
        uint32_t abgr;  // bgfx 使用 ABGR 格式
    };

    // 半透明黑色 (R=0, G=0, B=0, A=128)
    uint32_t color = 0x80000000;  // ABGR: A=0x80, B=0, G=0, R=0

    PosColorVertex vertices[6] = {
        // 三角形 1
        {0.0f,                      0.0f,                       0.0f, color},
        {(float)m_pixelWidth,       0.0f,                       0.0f, color},
        {(float)m_pixelWidth,       (float)m_pixelHeight,       0.0f, color},
        
        // 三角形 2
        {0.0f,                      0.0f,                       0.0f, color},
        {(float)m_pixelWidth,       (float)m_pixelHeight,       0.0f, color},
        {0.0f,                      (float)m_pixelHeight,       0.0f, color},
    };

    // 创建瞬态顶点缓冲区
    if (bgfx::getAvailTransientVertexBuffer(6, m_colorLayout) == 6) {
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 6, m_colorLayout);
        bx::memCopy(tvb.data, vertices, sizeof(vertices));

        // 设置状态：启用混合（半透明）
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_BLEND_ALPHA;  // Alpha 混合

        bgfx::setState(state);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::submit(3, m_progColor);
    }
}

void PauseScene::onContinueClicked()
{
    TINA_INFO("PauseScene: 继续游戏按钮被点击");
    // 使用延迟场景操作，避免在回调栈内修改场景栈
    app()->scenes().requestPop();  // 返回 GameScene
}

void PauseScene::onQuitClicked()
{
    TINA_INFO("PauseScene: 退出游戏按钮被点击");
    // 通过任务队列在主线程安全点退出（避免在回调栈内直接退出）
    auto* a = app();
    a->post([a]{ a->quit(); });
}

} // namespace Tina::Game

