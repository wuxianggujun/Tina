
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
    
    // 统一使用 Application 初始化时的默认字号（32），避免切换字号导致异步等待
    // 如需不同字号，可改为在 Application::prewarmCommonAssets 中确保 48/32/24 Face 并切换前等待 READY
    
    // 兼容旧代码：屏蔽本地 TextRenderer 初始化块
    #if 0
    // 文本渲染器（使用较大字体）
    app()->textRenderer().loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 48);
    if (!m_textRenderer->initialize(app()->shaders(), app()->resources())) {
        TINA_ERROR("TextRenderer 初始化失败");
    } else {
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 48);
    }
    #endif
    
    // ✅ 使用 Scene 基类提供的 ui() 方法，无需手动创建
    // m_uiRenderer = Memory::MakeUnique<UI::UIRenderer>();
    // m_uiRenderer->initialize(app()->shaders(), &app()->textRenderer());
    
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
    
    // ✅ 使用 Scene 基类提供的便捷方法
    setupUIView(uiViewId(), m_pixelWidth, m_pixelHeight);
    
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
    // ✅ m_uiRenderer 由 Scene 基类管理，无需手动清理
    
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
    // ✅ 使用 Scene 基类方法设置视图（只在窗口大小变化时设置）
    if (m_viewDirty) {
        setupUIView(uiViewId(), m_pixelWidth, m_pixelHeight);
        m_viewDirty = false;
    }
    
    // 触摸 view
    bgfx::touch(uiViewId());
    
    // 渲染背景
    renderGradientBackground();
    
    // 渲染粒子背景
    renderParticleBackground();
    
    // ✅ 使用 Scene 基类的 ui() 方法和 RAII 作用域
    auto scope = ui().beginRender(uiViewId());
    
    // 渲染标题
    if (m_titleAlpha > 0.0f) {
        float titleX = (float)m_pixelWidth / 2.0f;
        float titleY = (float)m_pixelHeight * 0.25f;
        
        // 主标题
        {
            UI::UIRenderer::TextOptions to{};
            to.r = 1.0f; to.g = 1.0f; to.b = 1.0f; to.a = m_titleAlpha;
            to.hAlign = UI::UIRenderer::AlignH::Center;
            to.vAlign = UI::UIRenderer::AlignV::Center;
            ui().drawTextBox(uiViewId(), titleX - 200.0f, titleY - 40.0f, 400, 80,
                                      "TINA GAME", to);
        }
        
        // 副标题（使用较小的透明度）
        {
            UI::UIRenderer::TextOptions to{};
            to.r = 0.7f; to.g = 0.7f; to.b = 0.7f; to.a = m_titleAlpha * 0.8f;
            to.hAlign = UI::UIRenderer::AlignH::Center;
            to.vAlign = UI::UIRenderer::AlignV::Center;
            ui().drawTextBox(uiViewId(), titleX - 200.0f, titleY + 60 - 20.0f, 400, 40,
                                      "2D Sandbox Adventure", to);
        }
    }
    
    // 渲染 UI 按钮
    if (m_rootNode) {
        m_rootNode->render(uiViewId(), ui());
        // 再次绘制标题，确保位于 UI 面板之上
        if (m_titleAlpha > 0.0f) {
            float titleX = (float)m_pixelWidth / 2.0f;
            float titleY = (float)m_pixelHeight * 0.25f;
            {
                UI::UIRenderer::TextOptions to{};
                to.r = 1.0f; to.g = 1.0f; to.b = 1.0f; to.a = m_titleAlpha;
                to.hAlign = UI::UIRenderer::AlignH::Center;
                to.vAlign = UI::UIRenderer::AlignV::Center;
                ui().drawTextBox(uiViewId(), titleX - 200.0f, titleY - 40.0f, 400, 80,
                                          "TINA GAME", to);
            }
            {
                UI::UIRenderer::TextOptions to{};
                to.r = 0.7f; to.g = 0.7f; to.b = 0.7f; to.a = m_titleAlpha * 0.8f;
                to.hAlign = UI::UIRenderer::AlignH::Center;
                to.vAlign = UI::UIRenderer::AlignV::Center;
                ui().drawTextBox(uiViewId(), titleX - 200.0f, titleY + 60 - 20.0f, 400, 40,
                                          "2D Sandbox Adventure", to);
            }
        }
    }
    
    // 渲染版本号
    {
        UI::UIRenderer::TextOptions to{};
        to.r = 0.5f; to.g = 0.5f; to.b = 0.5f; to.a = 0.5f;
        to.hAlign = UI::UIRenderer::AlignH::Right; to.vAlign = UI::UIRenderer::AlignV::Bottom;
        ui().drawTextBox(uiViewId(), (float)m_pixelWidth - 10 - 100, (float)m_pixelHeight - 10 - 30,
                                  100, 30, "v1.0.0", to);
    }
    
    // ✅ RAII 作用域自动 flush，无需手动调用
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
        
        // ✅ 标记视图为脏，下一帧重新设置
        m_viewDirty = true;
        
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

    // 使用新的API：createChild
    auto* panel = m_rootNode->createChild<UI::UIPanel>("MenuPanel");
    panel->setColor(0.10f,0.10f,0.12f,0.90f);
    panel->setSize(panelW, 1.0f);
    panel->setHeightWrap();

    auto* vbox = panel->createChild<UI::UIVStack>("VBox");
    vbox->setSize(panelW, 0.0f);
    vbox->setHeightWrap();
    vbox->setPadding(pad, pad);
    vbox->setSpacing(spacing);

    // 标题（增加高度以适配 48 号字体）
    auto* title = vbox->createChild<UI::UILabel>();
    title->setText("Tina - 主菜单");
    title->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    title->setSize(panelW - pad*2, 60.0f);  // 从 44 增加到 60，适配 48 号字体

    // 三个按钮（占满行宽）
    auto addButton = [&](const char* text, const Tina::Core::Color& n, const Tina::Core::Color& h, const Tina::Core::Color& p, auto&& onClick){
        auto* row = vbox->createChild<UI::UIHStack>("Row");
        row->setSize(panelW - pad*2, btnH);
        row->setPadding(0,0);
        auto* btn = row->createChild<UI::UIButton>();
        btn->setText(text);
        btn->setWidthMatch();
        btn->setHeight(btnH);
        btn->setNormalColor(n);
        btn->setHoverColor(h);
        btn->setPressedColor(p);
        return btn;
    };

    m_buttons.clear();
    m_btnStart = addButton("开始游戏",
                           Tina::Core::Color(0.15f,0.68f,0.38f,0.9f),
                           Tina::Core::Color(0.18f,0.80f,0.44f,1.0f),
                           Tina::Core::Color(0.12f,0.52f,0.29f,1.0f),
                           [this]{ onStartClicked(); });
    m_startConnection = m_btnStart->onClick.connect(this, &MenuScene::onStartClicked);
    m_buttons.push_back(m_btnStart);

    m_btnSettings = addButton("设置",
                           Tina::Core::Color(0.20f,0.60f,0.86f,0.9f),
                           Tina::Core::Color(0.36f,0.68f,0.89f,1.0f),
                           Tina::Core::Color(0.16f,0.45f,0.65f,1.0f),
                           [this]{ onSettingsClicked(); });
    m_settingsConnection = m_btnSettings->onClick.connect(this, &MenuScene::onSettingsClicked);
    m_buttons.push_back(m_btnSettings);

    m_btnQuit = addButton("退出游戏",
                           Tina::Core::Color(0.91f,0.30f,0.24f,0.9f),
                           Tina::Core::Color(0.93f,0.44f,0.39f,1.0f),
                           Tina::Core::Color(0.75f,0.22f,0.17f,1.0f),
                           [this]{ onQuitClicked(); });
    m_quitConnection = m_btnQuit->onClick.connect(this, &MenuScene::onQuitClicked);
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

