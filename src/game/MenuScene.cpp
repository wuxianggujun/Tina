//
// MenuScene 实现
//

#include "MenuScene.hpp"
#include "GameScene.hpp"
#include "WorldSelectScene.hpp"
#include "SettingsScene.hpp"
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"
#include "../engine/InputSystem.hpp"  // 添加 InputSystem 头文件
#include "../core/Log.hpp"
#include "../game/GameConfig.hpp"
#include "../ui/UIConstants.hpp"

// #include <SDL3/SDL.h>  // 不再需要：使用 InputSystem
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cmath>

namespace Tina::Game {

MenuScene::MenuScene() {
    // 初始化随机数种子（用于粒子生成）
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
}

MenuScene::~MenuScene() = default;

// 视图配置（使用新架构）
Container::Vector<Engine::Scene::ViewSetup> MenuScene::getViewSetup() {
    return {
        { UI::VIEW_BACKGROUND, Engine::Scene::ViewSetup::Background2D, false },  // 背景视图
        { UI::VIEW_UI, Engine::Scene::ViewSetup::UI2D, false }                   // UI视图
    };
}

void MenuScene::onEnter() {
    TINA_INFO("MenuScene::onEnter - 进入主菜单");

    // 计算UI缩放（窗口尺寸已由基类自动设置）
    computeUIScale();

    // 创建背景粒子系统
    m_bgParticles = Memory::MakeUnique<Particles::ParticleSystem2D>();
    if (m_bgParticles->initialize(app()->shaders())) {
        m_bgParticles->setGlobalAcceleration(0.0f, 50.0f);  // 缓慢下落
        m_bgParticles->setDrag(0.0f);
    }

    // 创建 UI
    createUI();

    // 注册UI根节点到框架（框架会自动处理窗口resize）
    addUIRoot(m_rootNode.get());
    
    // ✅ 设置UI根节点到事件系统（传递 SharedPtr）
    app()->events().setUIRoot(m_rootNode);
}

void MenuScene::onExit() {
    TINA_INFO("MenuScene::onExit - 退出主菜单");

    // ✅ 清空事件系统的UI根节点（SharedPtr 会自动处理）
    if (app()) {
        app()->events().setUIRoot(Memory::SharedPtr<UI::UINode>());
    }

    // 清理 UI
    m_rootNode.reset();
    m_buttons.clear();
    m_btnStart = nullptr;
    m_btnSettings = nullptr;
    m_btnQuit = nullptr;

    // 清理渲染资源
    m_bgParticles.reset();
}

// 注意：不再需要onWindowSizeChanged()方法
// MenuRootNode会自动处理窗口resize并调用computeUIScale()和updateUILayout()

void MenuScene::update(float dt) {
    // 处理输入
    handleInput();

    // 标题淡入动画
    if (m_titleAlpha < 1.0f) {
        m_titleAlpha += dt * 2.0f;  // 0.5秒淡入
        m_titleAlpha = std::min(m_titleAlpha, 1.0f);
    }

    // 更新背景粒子
    if (m_bgParticles) {
        m_particleTimer += dt;

        // 每隔一段时间生成新粒子
        if (m_particleTimer > 0.1f) {  // 每0.1秒生成一批
            m_particleTimer = 0.0f;

            // 生成随机粒子（星星效果）
            for (int i = 0; i < 2; ++i) {
                float x = (float)(std::rand() % getPixelWidth());
                float y = -10.0f;  // 从顶部生成

                // 随机大小和亮度
                float size = 2.0f + (std::rand() % 100) / 100.0f * 3.0f;
                float brightness = 0.3f + (std::rand() % 100) / 100.0f * 0.7f;

                m_bgParticles->emit(
                    x, y,                        // 位置
                    0.0f, 20.0f,                 // 速度（缓慢下落）
                    brightness, brightness, brightness * 0.8f, brightness * 0.5f,  // 颜色
                    size,                        // 大小
                    10.0f                        // 生命周期（10秒）
                );
            }
        }

        m_bgParticles->update(dt);
    }
}

void MenuScene::render() {
    // 框架已自动处理视图设置和touch

    // 渲染渐变背景（使用 SceneRenderer 一次性绘制，无需多层矩形）
    {
        Core::Color topColor{0.1f, 0.2f, 0.4f, 1.0f};
        Core::Color bottomColor{0.05f, 0.1f, 0.2f, 1.0f};
        scene().drawGradientBackground(UI::VIEW_BACKGROUND, topColor, bottomColor);
    }

    // 渲染粒子背景
    if (m_bgParticles) {
        m_bgParticles->render(UI::VIEW_BACKGROUND);
    }

    // 使用传统UI渲染器渲染UI元素
    auto scope = ui().beginRender(uiViewId());

    // 渲染标题
    if (m_titleAlpha > 0.0f) {
        float titleX = (float)getPixelWidth() / 2.0f;
        float titleY = (float)getPixelHeight() * 0.25f;

        // 主标题（随窗口缩放字号与包围盒）
        {
            UI::UIRenderer::TextOptions to{};
            to.r = 1.0f; to.g = 1.0f; to.b = 1.0f; to.a = m_titleAlpha;
            to.hAlign = UI::UIRenderer::AlignH::Center;
            to.vAlign = UI::UIRenderer::AlignV::Center;
            to.fontPx = std::max(28, (int)std::lround(64.0f * m_uiScale));
            float halfW = 200.0f * m_uiScale;
            float halfH = 40.0f * m_uiScale;
            ui().drawTextBox(uiViewId(), titleX - halfW, titleY - halfH, halfW * 2.0f, halfH * 2.0f,
                                      "TINA GAME", to);
        }

        // 副标题（随窗口缩放字号与包围盒）
        {
            UI::UIRenderer::TextOptions to{};
            to.r = 0.7f; to.g = 0.7f; to.b = 0.7f; to.a = m_titleAlpha * 0.8f;
            to.hAlign = UI::UIRenderer::AlignH::Center;
            to.vAlign = UI::UIRenderer::AlignV::Center;
            to.fontPx = std::max(18, (int)std::lround(32.0f * m_uiScale));
            float boxW = 400.0f * m_uiScale;
            float boxH = 40.0f * m_uiScale;
            ui().drawTextBox(uiViewId(), titleX - boxW * 0.5f, titleY + (60.0f * m_uiScale) - boxH * 0.5f,
                                      boxW, boxH, "2D Sandbox Adventure", to);
        }
    }

    // 渲染 UI 按钮
    if (m_rootNode) {
        m_rootNode->render(uiViewId(), ui());
    }

    // 渲染版本号
    {
        UI::UIRenderer::TextOptions to{};
        to.r = 0.5f; to.g = 0.5f; to.b = 0.5f; to.a = 0.5f;
        to.hAlign = UI::UIRenderer::AlignH::Right; to.vAlign = UI::UIRenderer::AlignV::Bottom;
        ui().drawTextBox(uiViewId(), (float)getPixelWidth() - 10 - 100, (float)getPixelHeight() - 10 - 30,
                                  100, 30, "v1.0.0", to);
    }

    // 不再使用渲染队列提交背景，无需 endFrame()
}

// handleEvent 已删除，输入处理移至 update() 中使用 InputSystem

void MenuScene::handleInput() {
    // 使用 InputSystem 进行输入处理
    auto* appPtr = app();
    if (!appPtr) {
        TINA_WARN("MenuScene::handleInput - app() 返回 nullptr！");
        return;
    }
    auto& input = appPtr->input();
    
    static int callCount = 0;
    if (++callCount % 60 == 0) {
        TINA_DEBUG("MenuScene::handleInput 被调用 (第{}次)", callCount);
    }

    // 键盘导航
    if (input.isKeyPressed(Engine::KeyCode::Up)) {
        selectPreviousButton();
    }
    if (input.isKeyPressed(Engine::KeyCode::Down)) {
        selectNextButton();
    }
    if (input.isKeyPressed(Engine::KeyCode::Enter) || input.isKeyPressed(Engine::KeyCode::Space)) {
        activateSelectedButton();
    }
    if (input.isKeyPressed(Engine::KeyCode::Escape)) {
        onQuitClicked();
    }

    // 更新UI输入到引擎事件系统
    auto mousePos = input.getMousePosition();
    bool leftDown = input.isMouseButtonDown(Engine::MouseButton::Left);
    
    static int uiInputCount = 0;
    if (++uiInputCount % 60 == 0) {
        TINA_DEBUG("MenuScene - 调用 updateUIInput: 鼠标({}, {}), 按下: {}", 
                   mousePos.x, mousePos.y, leftDown);
    }
    
    app()->events().updateUIInput(mousePos.x, mousePos.y, leftDown);
}

void MenuScene::createUI() {
    // ✅ 使用 MakeShared 创建根节点（支持 weak_ptr 观察）
    m_rootNode = Memory::MakeShared<MenuRootNode>(this);
    m_rootNode->setPosition(0, 0);
    m_rootNode->setSize((float)getPixelWidth(), (float)getPixelHeight());

    // 计算按钮位置
    float centerX = getPixelWidth() / 2.0f;
    float startY = getPixelHeight() * 0.5f;
    float buttonWidth = 200.0f * m_uiScale;
    float buttonHeight = 50.0f * m_uiScale;
    float buttonSpacing = 20.0f * m_uiScale;
    int buttonFontPx = std::max(18, (int)std::lround(32.0f * m_uiScale));

    // 创建按钮面板（半透明背景）
    auto panel = Memory::MakeUnique<UI::UIPanel>("ButtonPanel");
    panel->setPosition(centerX - 150.0f * m_uiScale, startY - 50.0f * m_uiScale);
    panel->setSize(300.0f * m_uiScale, 250.0f * m_uiScale);
    panel->setColor(Core::Color{0.1f, 0.1f, 0.15f, 0.8f});
    panel->setInteractable(false);  // 🔧 关键修复：面板不响应事件，让按钮能被命中
    // panel->setCornerRadius(10.0f);  // UIPanel 不支持圆角

    // 创建开始按钮（直接使用屏幕坐标）
    auto btnStart = Memory::MakeUnique<UI::UIButton>("BtnStart");
    btnStart->setPosition(centerX - buttonWidth/2, startY);  // 使用屏幕绝对坐标
    btnStart->setSize(buttonWidth, buttonHeight);
    btnStart->setText("开始游戏");
    btnStart->setFontPx(buttonFontPx);
    btnStart->setNormalColor(Core::Color{0.2f, 0.3f, 0.5f, 0.9f});
    btnStart->setHoverColor(Core::Color{0.3f, 0.4f, 0.6f, 1.0f});
    btnStart->setPressedColor(Core::Color{0.1f, 0.2f, 0.4f, 1.0f});
    // btnStart->setCornerRadius(5.0f);  // UIButton 不支持圆角
    m_btnStart = btnStart.get();
    m_buttons.push_back(m_btnStart);

    // 事件系统与ID由路由器在 bind 时设置

    // 创建设置按钮
    auto btnSettings = Memory::MakeUnique<UI::UIButton>("BtnSettings");
    btnSettings->setPosition(centerX - buttonWidth/2, startY + buttonHeight + buttonSpacing);  // 使用屏幕绝对坐标
    btnSettings->setSize(buttonWidth, buttonHeight);
    btnSettings->setText("游戏设置");
    btnSettings->setFontPx(buttonFontPx);
    btnSettings->setNormalColor(Core::Color{0.2f, 0.3f, 0.5f, 0.9f});
    btnSettings->setHoverColor(Core::Color{0.3f, 0.4f, 0.6f, 1.0f});
    btnSettings->setPressedColor(Core::Color{0.1f, 0.2f, 0.4f, 1.0f});
    // btnSettings->setCornerRadius(5.0f);  // UIButton 不支持圆角
    m_btnSettings = btnSettings.get();
    m_buttons.push_back(m_btnSettings);

    // 事件系统与ID由路由器在 bind 时设置

    // 创建退出按钮
    auto btnQuit = Memory::MakeUnique<UI::UIButton>("BtnQuit");
    btnQuit->setPosition(centerX - buttonWidth/2, startY + 2 * (buttonHeight + buttonSpacing));  // 使用屏幕绝对坐标
    btnQuit->setSize(buttonWidth, buttonHeight);
    btnQuit->setText("退出游戏");
    btnQuit->setFontPx(buttonFontPx);
    btnQuit->setNormalColor(Core::Color{0.4f, 0.2f, 0.2f, 0.9f});  // 红色调
    btnQuit->setHoverColor(Core::Color{0.5f, 0.3f, 0.3f, 1.0f});
    btnQuit->setPressedColor(Core::Color{0.3f, 0.1f, 0.1f, 1.0f});
    // btnQuit->setCornerRadius(5.0f);  // UIButton 不支持圆角
    m_btnQuit = btnQuit.get();
    m_buttons.push_back(m_btnQuit);

    // 事件系统与ID由路由器在 bind 时设置

    // 调试：打印按钮信息（在 move 之前）
    TINA_INFO("MenuScene - 按钮创建完成:");
    TINA_INFO("  开始按钮: 位置({}, {}), 尺寸({}, {}), 可交互: {}", 
              m_btnStart->getPosition().x, m_btnStart->getPosition().y,
              m_btnStart->getSize().x, m_btnStart->getSize().y,
              m_btnStart->isInteractable());

    // 绑定按钮点击（内部经事件系统路由）
    if (m_btnStart)    m_btnStart->setOnClick([this]{ onStartClicked(); });
    if (m_btnSettings) m_btnSettings->setOnClick([this]{ onSettingsClicked(); });
    if (m_btnQuit)     m_btnQuit->setOnClick([this]{ onQuitClicked(); });

    // 直接添加到根节点，不使用面板作为父节点
    m_rootNode->addChild(std::move(panel));
    m_rootNode->addChild(std::move(btnStart));
    m_rootNode->addChild(std::move(btnSettings));
    m_rootNode->addChild(std::move(btnQuit));

    // 默认选中第一个按钮
    m_selectedButtonIndex = 0;
    if (!m_buttons.empty()) {
        m_buttons[0]->setHovered(true);
    }
}

void MenuScene::onStartClicked() {
    TINA_INFO("MenuScene: 开始游戏按钮被点击");
    // 进入世界选择场景
    app()->scenes().replace(Memory::MakeUnique<WorldSelectScene>());
}

void MenuScene::onSettingsClicked() {
    TINA_INFO("MenuScene: 设置按钮被点击");
    // 推入设置场景（不替换，可以返回）
    app()->scenes().push(Memory::MakeUnique<SettingsScene>());
}

void MenuScene::onQuitClicked() {
    TINA_INFO("MenuScene: 退出按钮被点击");
    app()->quit();
}

// 无需额外路由函数，事件由路由器直接回调

void MenuScene::selectPreviousButton() {
    if (m_buttons.empty()) return;

    // 取消当前按钮的选中状态
    m_buttons[m_selectedButtonIndex]->setHovered(false);

    // 选择前一个按钮
    if (m_selectedButtonIndex > 0) {
        m_selectedButtonIndex--;
    } else {
        m_selectedButtonIndex = m_buttons.size() - 1;
    }

    // 设置新按钮的选中状态
    m_buttons[m_selectedButtonIndex]->setHovered(true);
}

void MenuScene::selectNextButton() {
    if (m_buttons.empty()) return;

    // 取消当前按钮的选中状态
    m_buttons[m_selectedButtonIndex]->setHovered(false);

    // 选择下一个按钮
    if (m_selectedButtonIndex < m_buttons.size() - 1) {
        m_selectedButtonIndex++;
    } else {
        m_selectedButtonIndex = 0;
    }

    // 设置新按钮的选中状态
    m_buttons[m_selectedButtonIndex]->setHovered(true);
}

void MenuScene::activateSelectedButton() {
    if (m_buttons.empty()) return;

    // 直接调用对应的处理函数
    if (m_selectedButtonIndex == 0 && m_btnStart) {
        onStartClicked();
    } else if (m_selectedButtonIndex == 1 && m_btnSettings) {
        onSettingsClicked();
    } else if (m_selectedButtonIndex == 2 && m_btnQuit) {
        onQuitClicked();
    }
}

void MenuScene::updateUILayout() {
    if (!m_rootNode) return;

    // 更新根节点大小
    m_rootNode->setSize((float)getPixelWidth(), (float)getPixelHeight());

    // 重新计算中心位置
    float centerX = getPixelWidth() / 2.0f;
    float startY = getPixelHeight() * 0.5f;
    float buttonWidth = 200.0f * m_uiScale;
    float buttonHeight = 50.0f * m_uiScale;
    float buttonSpacing = 20.0f * m_uiScale;

    // 更新面板位置（第一个子节点）
    if (m_rootNode->getChildCount() > 0) {
        auto* panel = m_rootNode->getChild(0);
        if (panel) {
            panel->setPosition(centerX - 150.0f * m_uiScale, startY - 50.0f * m_uiScale);
            panel->setSize(300.0f * m_uiScale, 250.0f * m_uiScale);
        }
    }

    // 更新按钮位置
    int buttonFontPx = std::max(18, (int)std::lround(32.0f * m_uiScale));
    if (m_btnStart) {
        m_btnStart->setPosition(centerX - buttonWidth/2, startY);
        m_btnStart->setSize(buttonWidth, buttonHeight);
        m_btnStart->setFontPx(buttonFontPx);
    }
    if (m_btnSettings) {
        m_btnSettings->setPosition(centerX - buttonWidth/2, startY + buttonHeight + buttonSpacing);
        m_btnSettings->setSize(buttonWidth, buttonHeight);
        m_btnSettings->setFontPx(buttonFontPx);
    }
    if (m_btnQuit) {
        m_btnQuit->setPosition(centerX - buttonWidth/2, startY + 2 * (buttonHeight + buttonSpacing));
        m_btnQuit->setSize(buttonWidth, buttonHeight);
        m_btnQuit->setFontPx(buttonFontPx);
    }
}

void MenuScene::computeUIScale() {
    const float baseW = 1280.0f;
    const float baseH = 720.0f;
    float sx = (float)getPixelWidth() / baseW;
    float sy = (float)getPixelHeight() / baseH;
    float s = std::min(sx, sy);
    m_uiScale = std::max(0.75f, std::min(s, 2.0f));
}

} // namespace Tina::Game
