
//
// MenuScene 实现
//

#include "MenuScene.hpp"
#include "GameScene.hpp"
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"
#include "../core/Log.hpp"
#include "../game/GameConfig.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <cstdlib>
#include <ctime>

namespace Tina::Game {

MenuScene::MenuScene() {
    // 初始化随机数种子（用于粒子生成）
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
}

MenuScene::~MenuScene() = default;

void MenuScene::onEnter() {
    TINA_INFO("MenuScene::onEnter - 进入主菜单");
    
    // 获取窗口尺寸
    app()->getPixelSize(m_pixelWidth, m_pixelHeight);
    
    // 初始化渲染资源
    m_progColor = app()->shaders().loadProgram("color", "color");
    
    // 文本渲染器（使用较大字体）
    m_textRenderer = Memory::MakeUnique<UI::TextRenderer>();
    if (!m_textRenderer->initialize(app()->shaders(), app()->resources())) {
        TINA_ERROR("TextRenderer 初始化失败");
    } else {
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 48);
    }
    
    // UI 渲染器
    m_uiRenderer = Memory::MakeUnique<UI::UIRenderer>();
    m_uiRenderer->initialize(app()->shaders(), m_textRenderer.get());
    
    // 初始化顶点布局
    m_colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    
    // 创建背景粒子系统
    m_bgParticles = Memory::MakeUnique<Particles::ParticleSystem2D>();
    if (m_bgParticles->initialize(app()->shaders())) {
        m_bgParticles->setGlobalAcceleration(0.0f, 50.0f);  // 缓慢下落
        m_bgParticles->setDrag(0.0f);
    }
    
    // 创建 UI
    createUI();
    m_events.setRoot(m_rootNode.get());
    
    // 设置 UI 视图（view 3）
    bgfx::setViewRect(3, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);
    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(ortho, 0.0f, (float)m_pixelWidth, (float)m_pixelHeight, 0.0f,
                 -1.0f, 1.0f, 0.0f, caps ? caps->homogeneousDepth : false);
    bgfx::setViewTransform(3, nullptr, ortho);
    
    TINA_INFO("MenuScene: 初始化完成");
}

void MenuScene::onExit() {
    TINA_INFO("MenuScene::onExit - 退出主菜单");
    
    // 断开 Signal 连接
    m_startConnection.disconnect();
    m_settingsConnection.disconnect();
    m_quitConnection.disconnect();
    
    // 清理 UI
    m_rootNode.reset();
    m_buttons.clear();
    m_btnStart = nullptr;
    m_btnSettings = nullptr;
    m_btnQuit = nullptr;
    
    // 清理渲染资源
    m_bgParticles.reset();
    m_uiRenderer.reset();
    m_textRenderer.reset();
    
    m_progColor = BGFX_INVALID_HANDLE;
}

void MenuScene::update(float dt) {
    // 标题淡入动画
    if (m_titleAlpha < 1.0f) {
        m_titleAlpha += dt * 2.0f;  // 0.5秒淡入
        m_titleAlpha = std::min(m_titleAlpha, 1.0f);
    }
    
    // 标题缩放动画
    if (m_titleScale < 1.0f) {
        m_titleScale += dt * 0.4f;  // 0.5秒缩放
        m_titleScale = std::min(m_titleScale, 1.0f);
    }
    
    // 更新背景粒子
    if (m_bgParticles) {
        m_particleTimer += dt;
        if (m_particleTimer > 0.5f) {
            m_particleTimer = 0.0f;
            
            // 在屏幕顶部随机位置生成粒子
            float x = static_cast<float>(std::rand() % m_pixelWidth);
            // 使用 explode API 生成 1 个缓慢下落的小粒子
            m_bgParticles->explode(
                x, -10.0f,
                1,                 // count
                5.0f, 12.0f,       // speedMin/Max（较慢下落）
                0.2f, 0.3f,        // sizeMin/Max（小方块）
                5.0f, 10.0f,       // lifeMin/Max（长寿命）
                Core::Color(0.5f, 0.5f, 0.5f, 0.3f) // 半透明灰色
            );
        }
        m_bgParticles->update(dt);
    }
    
    // 更新 UI
    if (m_rootNode) {
        m_rootNode->update(dt);
    }
}

void MenuScene::render() {
    // 触摸 view 3
    bgfx::touch(3);
    
    // 渲染背景
    renderGradientBackground();
    
    // 渲染粒子背景
    renderParticleBackground();
    
    // === UI渲染开始（批处理模式） ===
    if (m_uiRenderer) {
        m_uiRenderer->beginFrame(3);
    }
    
    // 渲染标题
    if (m_textRenderer && m_uiRenderer && m_titleAlpha > 0.0f) {
        float titleX = (float)m_pixelWidth / 2.0f;
        float titleY = (float)m_pixelHeight * 0.25f;
        
        // 主标题
        m_uiRenderer->drawTextEx(3, titleX - 200.0f, titleY - 40.0f, 400, 80,
                                 1.0f, 1.0f, 1.0f, m_titleAlpha,
                                 "TINA GAME",
                                 UI::UIRenderer::AlignH::Center,
                                 UI::UIRenderer::AlignV::Center,
                                 0.0f, 0.0f);
        
        // 副标题（使用较小的透明度）
        m_uiRenderer->drawTextEx(3, titleX - 200.0f, titleY + 60 - 20.0f, 400, 40,
                                 0.7f, 0.7f, 0.7f, m_titleAlpha * 0.8f,
                                 "2D Sandbox Adventure",
                                 UI::UIRenderer::AlignH::Center,
                                 UI::UIRenderer::AlignV::Center,
                                 0.0f, 0.0f);
    }
    
    // 渲染 UI 按钮
    if (m_rootNode && m_uiRenderer) {
        m_rootNode->render(3, *m_uiRenderer);
    }
    
    // 渲染版本号
    if (m_textRenderer && m_uiRenderer) {
        m_uiRenderer->drawTextEx(3, (float)m_pixelWidth - 10 - 100, (float)m_pixelHeight - 10 - 30,
                                 100, 30, 0.5f, 0.5f, 0.5f, 0.5f,
                                 "v1.0.0",
                                 UI::UIRenderer::AlignH::Right,
                                 UI::UIRenderer::AlignV::Bottom,
                                 0.0f, 0.0f);
    }
    
    // === UI渲染结束（提交批次） ===
    if (m_uiRenderer) {
        m_uiRenderer->flush();
    }
}

void MenuScene::handleEvent(const os::Event& event) {
    using E = os::Event;
    
    // 键盘导航
    if (event.type == E::Type::KEY && event.key.down) {
        switch (event.key.key_code) {
            case os::KeyCode::UP:
                selectPreviousButton();
                break;
            case os::KeyCode::DOWN:
                selectNextButton();
                break;
            case os::KeyCode::RETURN:
            case os::KeyCode::SPACE:
                activateSelectedButton();
                break;
            case os::KeyCode::ESCAPE:
                onQuitClicked();
                break;
            default:
                break;
        }
    }
    
    // 鼠标移动/点击：统一交给 UIEventSystem
    if (event.type == E::Type::MOUSE_MOVE || event.type == E::Type::MOUSE_BUTTON) {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        bool leftDown = false;
        if (event.type == E::Type::MOUSE_BUTTON) {
            leftDown = (event.mouse_button.button == os::MouseButton::LEFT) && event.mouse_button.down;
        } else {
            // 保持 hover 精确，读取当前是否按住左键
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
    
    // 窗口调整大小
    if (event.type == E::Type::WINDOW_SIZE) {
        m_pixelWidth = event.win_size.w;
        m_pixelHeight = event.win_size.h;
        
        // 重新设置 UI 视图
        bgfx::setViewRect(3, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);
        float ortho[16];
        const bgfx::Caps* caps = bgfx::getCaps();
        bx::mtxOrtho(ortho, 0.0f, (float)m_pixelWidth, (float)m_pixelHeight, 0.0f,
                     -1.0f, 1.0f, 0.0f, caps ? caps->homogeneousDepth : false);
        bgfx::setViewTransform(3, nullptr, ortho);
        
        // 重新布局 UI（尽量避免重建；这里简单重建并重置事件根）
        createUI();
        m_events.setRoot(m_rootNode.get());
}
}

void MenuScene::createUI() {
    // 若已存在旧 UI，先断开所有信号连接并释放旧根，避免连接持有指向已销毁 Signal 的悬挂指针
    if (m_rootNode) {
        m_startConnection.disconnect();
        m_settingsConnection.disconnect();
        m_quitConnection.disconnect();
        m_rootNode.reset();
        m_buttons.clear();
        m_btnStart = nullptr;
        m_btnSettings = nullptr;
        m_btnQuit = nullptr;
    }

    // 创建根节点与居中面板（Android 风格布局：Wrap/Match）
    m_rootNode = Memory::MakeUnique<UI::UINode>();
    m_rootNode->setPosition(0, 0);
    m_rootNode->setSize((float)m_pixelWidth, (float)m_pixelHeight);

    const float panelW = 520.0f;
    const float pad    = 16.0f;
    const float spacing= 12.0f;
    const float btnH   = 60.0f;

    auto* panel = new UI::UIPanel("MenuPanel");
    panel->setColor(0.10f,0.10f,0.12f,0.90f);
    panel->setSize(panelW, 1.0f);
    panel->setHeightWrap();
    m_rootNode->addChild(panel);

    auto* vbox = new UI::UIVStack("VBox");
    vbox->setSize(panelW, 0.0f);
    vbox->setHeightWrap();
    vbox->setPadding(pad, pad);
    vbox->setSpacing(spacing);
    panel->addChild(vbox);

    // 标题
    auto* title = new UI::UILabel();
    title->setText("Tina - 主菜单");
    title->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    title->setSize(panelW - pad*2, 44.0f);
    vbox->addChild(title);

    // 三个按钮（占满行宽）
    auto addButton = [&](const char* text, const Tina::Core::Color& n, const Tina::Core::Color& h, const Tina::Core::Color& p, auto&& onClick){
        auto* row = new UI::UIHStack("Row");
        row->setSize(panelW - pad*2, btnH);
        row->setPadding(0,0);
        vbox->addChild(row);
        auto* btn = new UI::UIButton();
        btn->setText(text);
        btn->setWidthMatch();
        btn->setHeight(btnH);
        btn->setNormalColor(n);
        btn->setHoverColor(h);
        btn->setPressedColor(p);
        row->addChild(btn);
        return btn;
    };

    m_buttons.clear();
    m_btnStart = addButton("开始游戏",
                           Tina::Core::Color(0.15f,0.68f,0.38f,0.9f),
                           Tina::Core::Color(0.18f,0.80f,0.44f,1.0f),
                           Tina::Core::Color(0.12f,0.52f,0.29f,1.0f),
                           [this]{ onStartClicked(); });
    m_startConnection = m_btnStart->onClick.connect([this]{ onStartClicked(); });
    m_buttons.push_back(m_btnStart);

    m_btnSettings = addButton("设置",
                           Tina::Core::Color(0.20f,0.60f,0.86f,0.9f),
                           Tina::Core::Color(0.36f,0.68f,0.89f,1.0f),
                           Tina::Core::Color(0.16f,0.45f,0.65f,1.0f),
                           [this]{ onSettingsClicked(); });
    m_settingsConnection = m_btnSettings->onClick.connect([this]{ onSettingsClicked(); });
    m_buttons.push_back(m_btnSettings);

    m_btnQuit = addButton("退出游戏",
                           Tina::Core::Color(0.91f,0.30f,0.24f,0.9f),
                           Tina::Core::Color(0.93f,0.44f,0.39f,1.0f),
                           Tina::Core::Color(0.75f,0.22f,0.17f,1.0f),
                           [this]{ onQuitClicked(); });
    m_quitConnection = m_btnQuit->onClick.connect([this]{ onQuitClicked(); });
    m_buttons.push_back(m_btnQuit);

    // 计算与居中
    vbox->update(0.0f);
    panel->update(0.0f);
    panel->setPosition((m_pixelWidth - panelW)*0.5f, (m_pixelHeight - panel->getSize().y)*0.5f);
    
    // 默认选中第一个按钮
    m_selectedButtonIndex = 0;
    
    TINA_INFO("MenuScene: UI 创建完成");
}

void MenuScene::renderGradientBackground() {
    if (!bgfx::isValid(m_progColor)) return;
    
    // 渲染渐变背景（从深蓝灰到黑色）
    struct Vertex {
        float x, y, z;
        uint32_t abgr;
    };
    
    // 顶部：深蓝灰 #2C3E50 (ABGR: 0xFF503E2C)
    uint32_t topColor = 0xFF503E2C;
    // 底部：黑色 #1A1A1A (ABGR: 0xFF1A1A1A)
    uint32_t bottomColor = 0xFF1A1A1A;
    
    Vertex vertices[6] = {
        // 三角形 1
        {0.0f, 0.0f, 0.0f, topColor},
        {(float)m_pixelWidth, 0.0f, 0.0f, topColor},
        {(float)m_pixelWidth, (float)m_pixelHeight, 0.0f, bottomColor},
        
        // 三角形 2
        {0.0f, 0.0f, 0.0f, topColor},
        {(float)m_pixelWidth, (float)m_pixelHeight, 0.0f, bottomColor},
        {0.0f, (float)m_pixelHeight, 0.0f, bottomColor},
    };
    
    // 创建瞬态顶点缓冲区
    if (bgfx::getAvailTransientVertexBuffer(6, m_colorLayout) == 6) {
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 6, m_colorLayout);
        bx::memCopy(tvb.data, vertices, sizeof(vertices));
        
        // 设置状态
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        bgfx::setState(state);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::submit(3, m_progColor);
    }
}

void MenuScene::renderParticleBackground() {
    if (m_bgParticles) {
        m_bgParticles->render(3);
    }
}

void MenuScene::onStartClicked() {
    TINA_INFO("MenuScene: 开始游戏按钮被点击");
    // 更安全的方式：请求场景替换；SceneManager 会在事件分发安全点应用
    app()->scenes().requestReplace(Memory::MakeUnique<GameScene>());
}

void MenuScene::onSettingsClicked() {
    TINA_INFO("MenuScene: 设置按钮被点击");
    
    // TODO: 实现 SettingsScene
    // app()->scenes().push(Memory::MakeUnique<SettingsScene>());
    
    TINA_WARN("设置功能尚未实现");
}

void MenuScene::onQuitClicked() {
    TINA_INFO("MenuScene: 退出游戏按钮被点击");
    
    // 退出应用
    app()->quit();
}

void MenuScene::selectPreviousButton() {
    if (m_buttons.empty()) return;
    
    m_selectedButtonIndex--;
    if (m_selectedButtonIndex < 0) {
        m_selectedButtonIndex = static_cast<int>(m_buttons.size()) - 1;
    }
    
    TINA_INFO("MenuScene: 选中按钮 {}", m_selectedButtonIndex);
}

void MenuScene::selectNextButton() {
    if (m_buttons.empty()) return;
    
    m_selectedButtonIndex++;
    if (m_selectedButtonIndex >= static_cast<int>(m_buttons.size())) {
        m_selectedButtonIndex = 0;
    }
    
    TINA_INFO("MenuScene: 选中按钮 {}", m_selectedButtonIndex);
}

void MenuScene::activateSelectedButton() {
    if (m_buttons.empty()) return;
    if (m_selectedButtonIndex < 0 || m_selectedButtonIndex >= static_cast<int>(m_buttons.size())) return;
    
    // 触发选中按钮的点击事件
    UI::UIButton* btn = m_buttons[m_selectedButtonIndex];
    if (btn) {
        btn->onClick.emit();
    }
}

// 手工 hover/click 逻辑已由 UIEventSystem 接管

} // namespace Tina::Game
