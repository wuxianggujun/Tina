//
// MenuScene 实现
//

#include "MenuScene.hpp"
#include "GameScene.hpp"
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"
#include "../core/Log.hpp"
#include "../game/GameConfig.hpp"
#include "../renderer/Primitive2D.hpp"
#include "../renderer/RenderCommandBuilder.hpp"

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

    // 创建背景粒子系统
    m_bgParticles = Memory::MakeUnique<Particles::ParticleSystem2D>();
    if (m_bgParticles->initialize(app()->shaders())) {
        m_bgParticles->setGlobalAcceleration(0.0f, 50.0f);  // 缓慢下落
        m_bgParticles->setDrag(0.0f);
    }

    // 创建 UI
    createUI();
    m_events.setRoot(m_rootNode.get());

    // 设置UI视图
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
}

void MenuScene::update(float dt) {
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
                float x = (float)(std::rand() % m_pixelWidth);
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
    // 设置视图（仅在需要时）
    if (m_viewDirty) {
        setupUIView(uiViewId(), m_pixelWidth, m_pixelHeight);
        m_viewDirty = false;
    }

    // 触摸 view
    bgfx::touch(uiViewId());

    // 开始新的帧
    queue().beginFrame();

    // 使用RenderCommandBuilder构建渲染命令
    using namespace Renderer;

    // 渲染渐变背景（使用RenderQueue）
    {
        // 创建渐变效果：顶部深蓝灰，底部黑色
        float segments = 5;  // 使用多个矩形模拟渐变
        float segmentHeight = m_pixelHeight / segments;

        for (int i = 0; i < segments; ++i) {
            float t = i / (segments - 1.0f);
            // 插值颜色
            float r = 0.1f * (1.0f - t) + 0.05f * t;
            float g = 0.2f * (1.0f - t) + 0.1f * t;
            float b = 0.4f * (1.0f - t) + 0.2f * t;

            RenderCommandBuilder(&queue())
                .view(uiViewId())
                .layer(RenderLayer::Background)
                .depth(i)
                .rect(0, i * segmentHeight, (float)m_pixelWidth, segmentHeight)
                .color(r, g, b, 1.0f)
                .submit();
        }
    }

    // 渲染粒子背景
    if (m_bgParticles) {
        m_bgParticles->render(uiViewId());
    }

    // 使用传统UI渲染器渲染UI元素
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

        // 副标题
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
    }

    // 渲染版本号
    {
        UI::UIRenderer::TextOptions to{};
        to.r = 0.5f; to.g = 0.5f; to.b = 0.5f; to.a = 0.5f;
        to.hAlign = UI::UIRenderer::AlignH::Right; to.vAlign = UI::UIRenderer::AlignV::Bottom;
        ui().drawTextBox(uiViewId(), (float)m_pixelWidth - 10 - 100, (float)m_pixelHeight - 10 - 30,
                                  100, 30, "v1.0.0", to);
    }

    // 刷新RenderQueue（执行所有渲染命令）
    queue().endFrame();
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
        // 更新像素尺寸
        app()->getPixelSize(m_pixelWidth, m_pixelHeight);
        m_viewDirty = true;  // 标记视图需要更新

        // 更新 UI 布局
        updateUILayout();
    }
}

void MenuScene::createUI() {
    // 创建根节点
    m_rootNode = Memory::MakeUnique<UI::UINode>("RootNode");
    m_rootNode->setPosition(0, 0);
    m_rootNode->setSize((float)m_pixelWidth, (float)m_pixelHeight);

    // 计算按钮位置
    float centerX = m_pixelWidth / 2.0f;
    float startY = m_pixelHeight * 0.5f;
    float buttonWidth = 200.0f;
    float buttonHeight = 50.0f;
    float buttonSpacing = 20.0f;

    // 创建按钮面板（半透明背景）
    auto panel = Memory::MakeUnique<UI::UIPanel>("ButtonPanel");
    panel->setPosition(centerX - 150, startY - 50);
    panel->setSize(300, 250);
    panel->setColor(Core::Color{0.1f, 0.1f, 0.15f, 0.8f});
    // panel->setCornerRadius(10.0f);  // UIPanel 不支持圆角

    // 创建开始按钮（直接使用屏幕坐标）
    auto btnStart = Memory::MakeUnique<UI::UIButton>("BtnStart");
    btnStart->setPosition(centerX - buttonWidth/2, startY);  // 使用屏幕绝对坐标
    btnStart->setSize(buttonWidth, buttonHeight);
    btnStart->setText("开始游戏");
    btnStart->setFontPx(32);
    btnStart->setNormalColor(Core::Color{0.2f, 0.3f, 0.5f, 0.9f});
    btnStart->setHoverColor(Core::Color{0.3f, 0.4f, 0.6f, 1.0f});
    btnStart->setPressedColor(Core::Color{0.1f, 0.2f, 0.4f, 1.0f});
    // btnStart->setCornerRadius(5.0f);  // UIButton 不支持圆角
    m_startConnection = btnStart->onClick.connect([this]() { onStartClicked(); });
    m_btnStart = btnStart.get();
    m_buttons.push_back(m_btnStart);

    // 创建设置按钮
    auto btnSettings = Memory::MakeUnique<UI::UIButton>("BtnSettings");
    btnSettings->setPosition(centerX - buttonWidth/2, startY + buttonHeight + buttonSpacing);  // 使用屏幕绝对坐标
    btnSettings->setSize(buttonWidth, buttonHeight);
    btnSettings->setText("游戏设置");
    btnSettings->setFontPx(32);
    btnSettings->setNormalColor(Core::Color{0.2f, 0.3f, 0.5f, 0.9f});
    btnSettings->setHoverColor(Core::Color{0.3f, 0.4f, 0.6f, 1.0f});
    btnSettings->setPressedColor(Core::Color{0.1f, 0.2f, 0.4f, 1.0f});
    // btnSettings->setCornerRadius(5.0f);  // UIButton 不支持圆角
    m_settingsConnection = btnSettings->onClick.connect([this]() { onSettingsClicked(); });
    m_btnSettings = btnSettings.get();
    m_buttons.push_back(m_btnSettings);

    // 创建退出按钮
    auto btnQuit = Memory::MakeUnique<UI::UIButton>("BtnQuit");
    btnQuit->setPosition(centerX - buttonWidth/2, startY + 2 * (buttonHeight + buttonSpacing));  // 使用屏幕绝对坐标
    btnQuit->setSize(buttonWidth, buttonHeight);
    btnQuit->setText("退出游戏");
    btnQuit->setFontPx(32);
    btnQuit->setNormalColor(Core::Color{0.4f, 0.2f, 0.2f, 0.9f});  // 红色调
    btnQuit->setHoverColor(Core::Color{0.5f, 0.3f, 0.3f, 1.0f});
    btnQuit->setPressedColor(Core::Color{0.3f, 0.1f, 0.1f, 1.0f});
    // btnQuit->setCornerRadius(5.0f);  // UIButton 不支持圆角
    m_quitConnection = btnQuit->onClick.connect([this]() { onQuitClicked(); });
    m_btnQuit = btnQuit.get();
    m_buttons.push_back(m_btnQuit);

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
    // 切换到游戏场景
    app()->scenes().replace(Memory::MakeUnique<GameScene>());
}

void MenuScene::onSettingsClicked() {
    TINA_INFO("MenuScene: 设置按钮被点击");
    // TODO: 实现设置场景
}

void MenuScene::onQuitClicked() {
    TINA_INFO("MenuScene: 退出按钮被点击");
    app()->quit();
}

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

    // 触发选中按钮的点击事件
    m_buttons[m_selectedButtonIndex]->onClick.emit();
}

void MenuScene::updateUILayout() {
    if (!m_rootNode) return;

    // 更新根节点大小
    m_rootNode->setSize((float)m_pixelWidth, (float)m_pixelHeight);

    // 重新计算中心位置
    float centerX = m_pixelWidth / 2.0f;
    float startY = m_pixelHeight * 0.5f;
    float buttonWidth = 200.0f;
    float buttonHeight = 50.0f;
    float buttonSpacing = 20.0f;

    // 更新面板位置（第一个子节点）
    if (m_rootNode->getChildCount() > 0) {
        auto* panel = m_rootNode->getChild(0);
        if (panel) {
            panel->setPosition(centerX - 150, startY - 50);
        }
    }

    // 更新按钮位置
    if (m_btnStart) {
        m_btnStart->setPosition(centerX - buttonWidth/2, startY);
    }
    if (m_btnSettings) {
        m_btnSettings->setPosition(centerX - buttonWidth/2, startY + buttonHeight + buttonSpacing);
    }
    if (m_btnQuit) {
        m_btnQuit->setPosition(centerX - buttonWidth/2, startY + 2 * (buttonHeight + buttonSpacing));
    }
}

} // namespace Tina::Game